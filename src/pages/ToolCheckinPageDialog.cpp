/**
 * @file ToolCheckinPageDialog.cpp
 * @brief 工具出入库-弹窗流程（开箱环节 / 关柜检查结果对话框）
 * @author 袁燕
 *
 * 本文件实现上述功能，成员函数声明见 ToolCheckinPage.h。
 * 流程：确认单据后先进入开箱环节（柜体打开动画+单据摘要），用户按单据
 *       取放工具并关闭柜门后逐项执行并检查，最后在结果页展示检查结论
 *       （成功项已写记录，未完成项红字提示且不写记录）。
 */

#include "ToolCheckinPage.h"
#include "ui_ToolCheckinPage.h"
#include <QTableWidget>  // 单据摘要与检查结果表格
#include "utils/StyleHelper.h"
#include "components/DrawerOpeningDialog.h"
#include "components/ResultDialog.h"
#include "services/ToolService.h"       // 入库落库唯一业务入口
#include "services/CheckoutService.h"  // 出库落库唯一业务入口
#include "common/Constants.h"
#include "db/RecordDAO.h"
#include "components/MessageDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
// ═══════════ 开箱环节：柜体打开动画 + 单据摘要 ═══════════

/**
 * @brief 开箱环节：展示柜体打开动画与出入库单摘要，提示用户按单据取放工具
 * @param items 已通过校验的出入库指令列表（入库行已分配好位置）
 */
void ToolCheckinPage::showOrderExecuteDialog(const QList<OrderRow>& items) {
    // 提示文案：按单据放入/取出，完成后关闭柜门
    QString descHtml = QStringLiteral(
        "柜体已自动打开，请按单据放入或取出工具。<br/>"
        "完成后请<b>关闭柜门</b>并点击下一步，系统将逐项检查并记录。"
    );

    DrawerOpeningDialog dlg(QStringLiteral("柜体打开中"), QStringLiteral("柜体已打开"),
        descHtml, 32, 60, this);
    dlg.setFixedSize(560, 480);

    // 单据摘要表：类型/工具名称/位置（用户照单操作）
    auto* orderTable = new QTableWidget();
    orderTable->setColumnCount(3);
    orderTable->setHorizontalHeaderLabels({QStringLiteral("类型"), QStringLiteral("工具名称"), QStringLiteral("位置")});
    orderTable->verticalHeader()->setVisible(false);
    orderTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    orderTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    orderTable->setStyleSheet(QString(
        "QTableWidget { border:1px solid #f0f0f0; background:#fff; border-radius:10px; "
        "  " + StyleHelper::fontSize(StyleHelper::Token::FontBody) + "; outline:none; }"
        "QTableWidget::item { padding:6px 10px; border:none; outline:none; }"
        "QHeaderView::section { background:#f8f9fb; color:#666; font-weight:600; " + StyleHelper::fontSize(StyleHelper::Token::FontSmall) + "; padding:8px; border:none; }"
    ));
    orderTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    orderTable->setColumnWidth(0, 80);
    orderTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    orderTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

    orderTable->setRowCount(items.size());
    for (int i = 0; i < items.size(); ++i) {
        bool isIn = (items[i].type != "out");
        auto* typeItem = new QTableWidgetItem(isIn ? QStringLiteral("入库") : QStringLiteral("出库"));
        typeItem->setForeground(QColor(isIn ? "#43a047" : "#e67e22"));
        typeItem->setTextAlignment(Qt::AlignCenter);
        orderTable->setItem(i, 0, typeItem);
        orderTable->setItem(i, 1, new QTableWidgetItem(items[i].toolName));
        auto* posItem = new QTableWidgetItem(items[i].position.isEmpty() ? QStringLiteral("--") : items[i].position);
        posItem->setTextAlignment(Qt::AlignCenter);
        orderTable->setItem(i, 2, posItem);
        orderTable->setRowHeight(i, 36);
    }
    orderTable->setFixedHeight(qMin(items.size(), 6) * 36 + 38);
    dlg.bodyLayout()->addWidget(orderTable);

    if (dlg.exec() == QDialog::Accepted) {
        // 用户完成取放并关闭柜门：逐项执行并检查
        showCheckResultDialog(items);
    }
}
// ═══════════ 关柜检查：逐项执行 + 结果展示 ═══════════

/**
 * @brief 关柜检查：逐项执行出入库指令并落库，结果逐行展示
 * @param items 已通过校验的出入库指令列表（入库行已分配好位置）
 *
 * 执行约定：各项独立执行互不影响；成功项写操作日志（入库 checkin /
 * 出库 checkout），失败项红字提示原因且不写任何记录，可重新执行。
 */
void ToolCheckinPage::showCheckResultDialog(const QList<OrderRow>& items) {
    int userId = m_user["userId"].toInt();
    if (userId <= 0) userId = 1;

    // 记录每项执行结果：行结构 = 类型/工具名称/位置/结果描述
    struct CheckResult {
        bool success;
        QString typeText;   // 入库 / 出库
        QString toolName;
        QString position;
        QString message;    // 成功描述或失败原因
    };
    QList<CheckResult> results;
    int successCount = 0;

    ToolService toolSvc;
    CheckoutService checkoutSvc;
    db::RecordDAO recDao;

    for (const OrderRow& it : items) {
        if (it.type != "out") {
            // ── 入库项：更新映射表位置为在库（服务内部校验位置状态） ──
            QJsonArray positions;
            positions.append(it.posInfo);
            QJsonObject data;
            data["selectedToolId"] = it.toolId;
            data["positions"] = positions;
            data["toolCode"] = it.toolCode;

            bool ok = toolSvc.checkinTool(data);
            if (ok) {
                // 入库成功写操作日志（与记录Tab展示格式一致）
                QString content = QStringLiteral("入库工具「%1」编号[%2]×1件 位置%3")
                    .arg(it.toolName, it.toolCode, it.position);
                recDao.insertOperationLog(userId, SC::OP_CHECKIN, "tool", it.toolCode, content);
                results.append({true, QStringLiteral("入库"), it.toolName, it.position,
                    QStringLiteral("入库成功")});
                ++successCount;
            } else {
                results.append({false, QStringLiteral("入库"), it.toolName, it.position,
                    QStringLiteral("未检测到入库，请确认工具已放入指定位置")});
            }
        } else {
            // ── 出库项：按位置出库（服务内部单事务：状态/库存/日志） ──
            CheckoutItem outItem;
            outItem.mappingId = it.mappingId;
            outItem.toolId = it.toolId;
            outItem.toolCode = it.toolCode;
            outItem.toolName = it.toolName;
            outItem.position = it.position;
            outItem.quantity = 1;
            outItem.reason = QStringLiteral("出入库单出库");

            auto out = checkoutSvc.executeCheckout(userId, {outItem});
            if (out.success) {
                results.append({true, QStringLiteral("出库"), it.toolName, it.position,
                    QStringLiteral("出库成功")});
                ++successCount;
            } else {
                results.append({false, QStringLiteral("出库"), it.toolName, it.position,
                    out.message.isEmpty() ? QStringLiteral("未检测到出库，请确认工具已从位置取走")
                                          : out.message});
            }
        }
    }

    // 结果对话框：标题按总体结果区分
    bool allSuccess = (successCount == items.size());
    ResultDialog dlg(allSuccess ? QStringLiteral("出入库完成") : QStringLiteral("出入库检查结果"),
        allSuccess ? QStringLiteral("✅") : QStringLiteral("⚠️"),
        allSuccess ? QStringLiteral("全部指令执行成功！")
                   : QStringLiteral("共 %1 项，成功 %2 项，未完成 %3 项")
                         .arg(items.size()).arg(successCount).arg(items.size() - successCount),
        allSuccess ? QStringLiteral("#43a047") : QStringLiteral("#e67e22"), 14, this);
    dlg.setFixedSize(620, 480);

    // 检查结果表：类型/工具名称/位置/结果
    auto* resultTable = new QTableWidget();
    resultTable->setColumnCount(4);
    resultTable->setHorizontalHeaderLabels({QStringLiteral("类型"), QStringLiteral("工具名称"),
                                            QStringLiteral("位置"), QStringLiteral("检查结果")});
    resultTable->verticalHeader()->setVisible(false);
    resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultTable->setStyleSheet(QString(
        "QTableWidget { border:1px solid #f0f0f0; background:#fff; border-radius:10px; "
        "  " + StyleHelper::fontSize(StyleHelper::Token::FontBody) + "; outline:none; }"
        "QTableWidget::item { padding:6px 10px; border:none; outline:none; }"
        "QHeaderView::section { background:#f8f9fb; color:#666; font-weight:600; " + StyleHelper::fontSize(StyleHelper::Token::FontSmall) + "; padding:8px; border:none; }"
    ));
    resultTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    resultTable->setColumnWidth(0, 70);
    resultTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    resultTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    resultTable->setColumnWidth(2, 110);
    resultTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);

    resultTable->setRowCount(results.size());
    for (int i = 0; i < results.size(); ++i) {
        const CheckResult& r = results[i];
        auto* typeItem = new QTableWidgetItem(r.typeText);
        typeItem->setForeground(QColor(r.typeText == QStringLiteral("入库") ? "#43a047" : "#e67e22"));
        typeItem->setTextAlignment(Qt::AlignCenter);
        resultTable->setItem(i, 0, typeItem);
        resultTable->setItem(i, 1, new QTableWidgetItem(r.toolName));
        auto* posItem = new QTableWidgetItem(r.position.isEmpty() ? QStringLiteral("--") : r.position);
        posItem->setTextAlignment(Qt::AlignCenter);
        resultTable->setItem(i, 2, posItem);
        auto* msgItem = new QTableWidgetItem(r.message);
        msgItem->setForeground(QColor(r.success ? "#43a047" : "#e53935"));
        resultTable->setItem(i, 3, msgItem);
        resultTable->setRowHeight(i, 40);
    }
    dlg.bodyLayout()->addWidget(resultTable);

    if (!allSuccess) {
        auto* tipLabel = new QLabel(QStringLiteral("未完成项未写入记录，可重新添加指令后再次执行"));
        tipLabel->setStyleSheet("" + StyleHelper::fontSize(StyleHelper::Token::FontSmall) + ";color:#999;background:transparent;");
        tipLabel->setAlignment(Qt::AlignCenter);
        dlg.bodyLayout()->addWidget(tipLabel);
    }

    dlg.addFinishButtonRow(QStringLiteral("完成"), StyleHelper::buttonPrimary(), 140);
    dlg.exec();

    // 有成功项：刷新记录并清空单据（未完成项需用户重新添加指令）
    if (successCount > 0) {
        m_recordCurrentPage = 1;
        loadCheckinRecords();
        m_outRecordCurrentPage = 1;
        loadCheckoutRecords();
        clearOrderTable();
    }
}
