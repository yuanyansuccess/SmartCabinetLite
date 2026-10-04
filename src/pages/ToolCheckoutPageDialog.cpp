/**
 * @file ToolCheckoutPageDialog.cpp
 * @brief 工具出库-弹窗流程（待出库清单/开柜引导/告警/成功结果对话框）
 * @author 袁燕
 *
 * 本文件实现上述功能，成员函数声明见 ToolCheckoutPage.h。
 */

#include "ToolCheckoutPage.h"
#include "ui_ToolCheckoutPage.h"
#include <QTableWidget>  // 待出库/出库记录表格
#include "components/PaginationBar.h"
#include "utils/StyleHelper.h"
#include "components/DrawerOpeningDialog.h"
#include "components/VerifyAlertDialog.h"
#include "components/ResultDialog.h"
#include "services/ToolService.h"
#include "services/BorrowService.h"
#include "components/SoftKeyboard.h"
#include "components/MultiSelectFilter.h"
#include "db/RecordDAO.h"             // 出库记录分页查询
#include "services/CheckoutService.h" // 出库落库（映射表/库存/日志）
#include "common/AppConfig.h"         // 获取当前用户
#include "common/Constants.h"         // 分页常量
#include "services/AlertService.h"    // 告警闭环写入
#include "model/AlertLog.h"           // AlertLog实体
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QGroupBox>
#include <QTabWidget>
#include <QDateTime>
#include <QRegularExpression>
#include <QHBoxLayout>
#include <QHeaderView>
#include "components/MessageDialog.h"
#include <QCheckBox>
#include <QGroupBox>
#include <QDialog>
#include <QFrame>
#include <QTimer>  // 抽屉打开动画
#include <algorithm>

// ═══════════ 步骤1: 待出库清单对话框 ═══════════
void ToolCheckoutPage::showCheckoutListDialog() {
    QList<CheckoutItem> items = collectSelectedItems();
    if (items.isEmpty()) return;

    QDialog* dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("出库清单"));
    dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dlg->setStyleSheet(StyleHelper::dialogStyle());
    int dlgHeight = qMax(440, 320 + items.size() * 40);
    dlg->setFixedSize(560, dlgHeight);

    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(32, 28, 32, 24);
    layout->setSpacing(16);

    // 标题
    auto* titleRow = new QHBoxLayout();
    auto* iconLabel = new QLabel(QStringLiteral("📦"));
    iconLabel->setStyleSheet("font-size:28px;background:transparent;");
    auto* titleLabel = new QLabel(QStringLiteral("待出库清单"));
    titleLabel->setStyleSheet(StyleHelper::dialogTitleText());
    titleRow->addWidget(iconLabel);
    titleRow->addWidget(titleLabel);
    titleRow->addStretch();
    layout->addLayout(titleRow);

    // 提示条：抽屉已打开
    auto* tipFrame = new QFrame();
    tipFrame->setObjectName("tipFrame");
    tipFrame->setStyleSheet(
        "QFrame#tipFrame{background:#e6f7ff;border:1px solid #91d5ff;"
        "border-radius:10px;padding:10px 14px;}"
    );
    auto* tipLayout = new QHBoxLayout(tipFrame);
    tipLayout->setContentsMargins(12, 8, 12, 8);
    auto* tipIcon = new QLabel(QStringLiteral("🔓"));
    tipIcon->setStyleSheet("font-size:20px;background:transparent;");
    auto* tipText = new QLabel(QStringLiteral("对应抽屉/箱子已打开，请进行出库操作"));
    tipText->setStyleSheet("font-size:15px;color:#1890ff;font-weight:600;background:transparent;");
    tipLayout->addWidget(tipIcon);
    tipLayout->addWidget(tipText, 1);
    layout->addWidget(tipFrame);

    // 清单表格
    auto* table = new QTableWidget();
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({
        QStringLiteral("工具名称"), QStringLiteral("出库数量"),
        QStringLiteral("存放地点"), QStringLiteral("出库原因")
    });
    table->setRowCount(items.size());
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setStyleSheet(StyleHelper::listTableStyle());
    for (int col = 0; col < 4; ++col) {
        table->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Stretch);
    }
    for (int i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        table->setItem(i, 0, new QTableWidgetItem(it.toolName));
        table->setItem(i, 1, new QTableWidgetItem(QStringLiteral("%1 件").arg(it.quantity)));
        table->setItem(i, 2, new QTableWidgetItem(it.position.isEmpty() ? QStringLiteral("--") : it.position));
        table->setItem(i, 3, new QTableWidgetItem(it.reason));
        table->setRowHeight(i, 38);
    }
    table->setFixedHeight(qMax(160, items.size() * 38 + 38));
    layout->addWidget(table);

    layout->addStretch();

    // 按钮区
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    cancelBtn->setStyleSheet(StyleHelper::buttonDefault());
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setMinimumHeight(StyleHelper::Token::ControlHeight);
    cancelBtn->setMinimumWidth(110);
    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);

    auto* nextBtn = new QPushButton(QStringLiteral("下一步 →"));
    nextBtn->setStyleSheet(StyleHelper::buttonPrimary());
    nextBtn->setCursor(Qt::PointingHandCursor);
    nextBtn->setMinimumHeight(StyleHelper::Token::ControlHeight);
    nextBtn->setMinimumWidth(140);
    connect(nextBtn, &QPushButton::clicked, this, [dlg]() { dlg->accept(); });
    btnRow->addWidget(cancelBtn);
    btnRow->addSpacing(12);
    btnRow->addWidget(nextBtn);
    layout->addLayout(btnRow);

    // 模态执行
    if (dlg->exec() == QDialog::Accepted) {
        delete dlg;
        showCheckoutDrawerOpeningDialog();  // 进入步骤2：抽屉打开中
    } else {
        delete dlg;
    }
}

// ═══════════ 步骤2: 抽屉打开中（三点跳动动画） ═══════════
void ToolCheckoutPage::showCheckoutDrawerOpeningDialog() {
    QList<CheckoutItem> items = collectSelectedItems();
    if (items.isEmpty()) return;

    DrawerOpeningDialog dlg(QStringLiteral("抽屉打开中"), QStringLiteral("柜子已打开"),
        QStringLiteral("对应工具抽屉已自动打开，<br/>"
                       "请照下方清单从指定位置取用工具并手动关闭抽屉后点击下一步。"), 32, 60, this);
    dlg.setFixedSize(480, qMax(460, 320 + items.size() * 32));

    // 出库清单（精简版表格：工具名/位置/数量）
    auto* table = new QTableWidget();
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({
        QStringLiteral("工具名称"), QStringLiteral("存放位置"), QStringLiteral("出库数量")
    });
    table->setRowCount(items.size());
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setStyleSheet(QString(
        "QTableWidget { border:1px solid #f0f0f0; background:#fff; border-radius:10px; "
        "  font-family:\"Microsoft YaHei\",sans-serif; font-size:13px; }"
        "QTableWidget::item { padding:8px 12px; color:#333; border:none; "
        "  border-bottom:1px solid #f3f3f3; }"
        "QHeaderView::section { background:#f8f9fb; color:#666; font-weight:600; "
        "  font-size:12px; padding:8px 12px; border:none; border-bottom:1px solid #f0f0f0; }"
    ));
    for (int col = 0; col < 3; ++col) {
        table->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Stretch);
    }
    for (int i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        table->setItem(i, 0, new QTableWidgetItem(it.toolName));
        table->setItem(i, 1, new QTableWidgetItem(it.position.isEmpty() ? QStringLiteral("--") : it.position));
        table->setItem(i, 2, new QTableWidgetItem(QStringLiteral("%1 件").arg(it.quantity)));
        table->setRowHeight(i, 32);
    }
    table->setFixedHeight(qMax(120, items.size() * 32 + 32));
    dlg.bodyLayout()->addWidget(table);

    if (dlg.exec() == QDialog::Accepted) {
        showCheckoutWarningDialog();  // 进入步骤3：异常校验
    }
}

// ═══════════ 步骤2: 抽屉异常警告对话框 ═══════════
void ToolCheckoutPage::showCheckoutWarningDialog() {
    // 增加倒计时+忽略+告警入库+重置页面（不退出系统）
    QList<CheckoutItem> items = collectSelectedItems();
    if (items.isEmpty()) return;

    // 警告详情
    QString firstPosition = items.first().position.isEmpty() ? QStringLiteral("A-01") : items.first().position;

    VerifyAlertDialog dlg(QStringLiteral("出库校验"), QStringLiteral("出库校验异常"),
        QStringLiteral(
        "检测到 <b>%1</b> 抽屉未关好，或工具未正确放入回收位。<br/>"
        "请检查抽屉状态后重新校验。").arg(firstPosition),
        "font-size:15px;color:#555;line-height:1.6;background:transparent;",
        QStringLiteral("取消出库"), QStringLiteral("🔄 重新校验"), 150,
        AppConfig::instance().borrowReturnBuffer(), this);
    dlg.setFixedSize(520, 380);

    int result = dlg.exec();
    if (result == QDialog::Accepted) {
        showCheckoutSuccessDialog();
    } else if (result == 2) {
        // 忽略或倒计时结束：写告警日志（闭环）
        int alertToolId = 0;
        QString alertToolCode;
        if (!m_selectedSet.isEmpty()) {
            int firstMappingId = *m_selectedSet.constBegin();
            for (const QJsonValue& v : m_tools) {
                QJsonObject t = v.toObject();
                if (t["mappingId"].toInt() == firstMappingId) {
                    alertToolId = t["toolId"].toInt();
                    alertToolCode = t["toolCode"].toString();
                    break;
                }
            }
        }
        AlertService alertSvc;
        alertSvc.recordVerifyAlert(m_user["userId"].toInt(), alertToolId, alertToolCode,
            QStringLiteral("出库校验异常：%1抽屉未关好或工具未正确放入回收位，用户忽略告警或倒计时超时").arg(firstPosition));

        MessageDialog::showWarning(nullptr, QStringLiteral("告警已记录"),
            QStringLiteral("出库校验异常告警已记录到系统告警，页面已重置。"));
        m_selectedSet.clear();
        if (m_table) {
            for (int i = 0; i < m_table->rowCount(); ++i) {
                auto* cb = qobject_cast<QCheckBox*>(m_table->cellWidget(i, 0));
                if (cb) cb->setChecked(false);
            }
        }
    }
}

// ═══════════ 步骤3: 出库成功对话框 ═══════════
void ToolCheckoutPage::showCheckoutSuccessDialog() {
    QList<CheckoutItem> items = collectSelectedItems();
    int totalQty = 0;
    for (const auto& it : items) totalQty += it.quantity;

    ResultDialog dlg(QStringLiteral("出库成功"), QStringLiteral("✅"),
        QStringLiteral("出库成功！"), QStringLiteral("#43a047"), 14, this);
    dlg.setFixedSize(480, qMax(380, 280 + items.size() * 30));

    // 汇总信息
    auto* summaryLabel = new QLabel(QStringLiteral(
        "共出库 <b style='color:#43a047;font-size:18px;'>%1</b> 件工具，"
        "涉及 <b style='color:#43a047;font-size:18px;'>%2</b> 种工具"
    ).arg(totalQty).arg(items.size()));
    summaryLabel->setAlignment(Qt::AlignCenter);
    summaryLabel->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontLabel, StyleHelper::textField()));
    summaryLabel->setTextFormat(Qt::RichText);
    dlg.bodyLayout()->addWidget(summaryLabel);

    // 工具清单
    auto* detailFrame = new QFrame();
    detailFrame->setStyleSheet("background:transparent;border:none;");
    auto* detailLayout = new QVBoxLayout(detailFrame);
    detailLayout->setSpacing(6);
    detailLayout->setContentsMargins(20, 8, 20, 8);
    for (const auto& it : items) {
        auto* toolInfo = new QLabel(QStringLiteral("✓ %1  ×%2件").arg(it.toolName).arg(it.quantity));
        toolInfo->setStyleSheet("font-size:14px;font-weight:600;color:#333;");
        detailLayout->addWidget(toolInfo);
    }
    dlg.bodyLayout()->addWidget(detailFrame);

    dlg.addFinishButtonRow(QStringLiteral("完成"), StyleHelper::buttonPrimary(), 140);
    dlg.exec();

    // 出库成功后落库：映射表状态 + 扣减库存 + 操作日志（业务下沉CheckoutService）
    // 整批单事务，失败不刷新列表，让用户看到失败明细而非"假装成功"
    CheckoutService checkoutSvc;
    CheckoutService::CheckoutResult coRet =
        checkoutSvc.executeCheckout(m_user["userId"].toInt(), items);
    if (!coRet.success) {
        MessageDialog::showError(this, QStringLiteral("出库失败"), coRet.message);
        return;
    }

    // 成功出库后清空选中状态并刷新
    m_selectedSet.clear();
    m_currentPage = 1;
    loadTools();
}
