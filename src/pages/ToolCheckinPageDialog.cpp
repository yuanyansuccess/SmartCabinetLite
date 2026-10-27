/**
 * @file ToolCheckinPageDialog.cpp
 * @brief 工具出入库-弹窗流程（开箱环节 / 关柜检查结果对话框）
 * @author 袁燕
 *
 * 本文件实现上述功能，成员函数声明见 ToolCheckinPage.h。
 * 流程：确认单据后弹出统一弹窗（先显示"正在打开柜体"，开柜成功后原地切换为
 *       "柜体已打开+单据摘要"，全程只有一个页面），用户按单据取放工具并关闭
 *       柜门后逐项执行并检查，最后在结果页展示检查结论
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
#include <QTimer>  // 关柜等待超时计时（柜体打开后起算）
#include <QJsonDocument>
// ═══════════ 开箱环节：柜体打开动画 + 单据摘要 ═══════════

/**
 * @brief 构建"开柜+取放工具"统一弹窗（开柜中与柜体已打开共用同一个页面）
 * @param items 本次要展示的出入库单据（首次=全量单据；重新开柜整理=未通过项）
 * @return 新建弹窗指针（显示与关闭由调用方负责）
 * @note 弹窗初始为"正在打开柜体"并禁用下一步按钮；开柜成功后由 applyOpenOpenedState
 *       原地切换标题、提示文案与按钮，全程只有一个页面，避免两个弹窗先后出现造成画面跳闪
 */
DrawerOpeningDialog* ToolCheckinPage::buildOpenExecuteDialog(const QList<OrderRow>& items) {
    auto* dlg = new DrawerOpeningDialog(QStringLiteral("工具出入库"),
        QStringLiteral("正在打开柜体"),
        QStringLiteral("系统正在向边缘计算机发送开箱指令，请稍候…"), 32, 60, this);
    // 应用级模态：弹窗期间屏蔽主页面操作（与原模态等待框的行为一致）
    dlg->setWindowModality(Qt::ApplicationModal);
    dlg->setFixedSize(560, 480);
    // 先按"柜体已打开"状态的最长文案预留按钮宽度，再切回开柜中文案：
    // 这样开柜成功时按钮变成"我已关柜，开始核对 →"不会被裁切，且按钮宽度全程不变（无跳动）
    dlg->setNextButtonText(QStringLiteral("我已关柜，开始核对 →"));
    dlg->setNextButtonText(QStringLiteral("开柜中…"));
    dlg->setNextEnabled(false);

    // 单据摘要表：类型/工具名称/位置（用户照单操作）
    // 布局常量：摘要行高；默认最多铺开6行（超出由表格滚动条承接）；表头近似高度（用于总高估算）
    const int kRowHeight      = 36;
    const int kMaxPreviewRows = 6;
    const int kHeaderHeight   = 38;

    auto* orderTable = new QTableWidget();
    orderTable->setColumnCount(3);
    orderTable->setHorizontalHeaderLabels({QStringLiteral("类型"), QStringLiteral("工具名称"), QStringLiteral("位置")});
    orderTable->verticalHeader()->setVisible(false);
    orderTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    orderTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    // 样式分三层：表格容器（边框/圆角/正文字号）、单元格（内边距无边框）、表头（灰底加粗小字号）
    orderTable->setStyleSheet(QString(
        "QTableWidget { border:1px solid #f0f0f0; background:#fff; border-radius:10px; "
        "  " + StyleHelper::fontSize(StyleHelper::Token::FontBody) + "; outline:none; }"
        "QTableWidget::item { padding:6px 10px; border:none; outline:none; }"
        "QHeaderView::section { background:#f8f9fb; color:#666; font-weight:600; " + StyleHelper::fontSize(StyleHelper::Token::FontSmall) + "; padding:8px; border:none; }"
    ));
    // 列宽策略：类型列固定80px，工具名与位置列按剩余宽度均分拉伸
    orderTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    orderTable->setColumnWidth(0, 80);
    orderTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    orderTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

    orderTable->setRowCount(items.size());
    for (int i = 0; i < items.size(); ++i) {
        bool isIn = (items[i].type != QStringLiteral("out"));
        auto* typeItem = new QTableWidgetItem(isIn ? QStringLiteral("入库") : QStringLiteral("出库"));
        // 类型着色与全站语义一致：绿=入库、橙=出库
        typeItem->setForeground(QColor(isIn ? "#43a047" : "#e67e22"));
        typeItem->setTextAlignment(Qt::AlignCenter);
        orderTable->setItem(i, 0, typeItem);
        orderTable->setItem(i, 1, new QTableWidgetItem(items[i].toolName));
        auto* posItem = new QTableWidgetItem(items[i].position.isEmpty() ? QStringLiteral("--") : items[i].position);
        posItem->setTextAlignment(Qt::AlignCenter);
        orderTable->setItem(i, 2, posItem);
        orderTable->setRowHeight(i, kRowHeight);
    }
    // 表格总高 = 显示行数 × 行高 + 表头高度；超过6行不继续增高，由滚动条承接
    orderTable->setFixedHeight(qMin(items.size(), kMaxPreviewRows) * kRowHeight + kHeaderHeight);
    dlg->bodyLayout()->addWidget(orderTable);
    return dlg;
}

/**
 * @brief 统一弹窗切换到"柜体已打开"状态：更新标题、提示文案与按钮，并启动关柜等待计时
 * @param dlg 统一弹窗指针（空指针直接返回，不做处理）
 * @note 关柜等待超时从此刻起算：超时后提示用户确认柜门已关并放弃本次（零写库）
 */
void ToolCheckinPage::applyOpenOpenedState(DrawerOpeningDialog* dlg) {
    if (!dlg) return;
    dlg->setTitleText(QStringLiteral("柜体已打开"));
    dlg->setDescHtml(QStringLiteral(
        "柜体已自动打开，请按单据放入或取出工具。<br/>"
        "完成后请<b>关闭柜门</b>并点击下一步，系统将逐项检查并记录。"));
    // 按钮文案按出入库场景定制：由"关柜"驱动核对（点此按钮顶替关柜信号）
    dlg->setNextButtonText(QStringLiteral("我已关柜，开始核对 →"));
    dlg->setNextEnabled(true);
    m_openDialogOpened = true;
    if (m_closeWaitTimer) {
        m_closeWaitTimer->start(SC::CABINET_CLOSE_WAIT_MS);
    }
}
// ═══════════ 关柜检查：逐项执行 + 结果展示 ═══════════

/**
 * @brief 关柜检查：按边缘机比对结论逐项执行并显示结果
 * @param items         已通过校验的出入库指令列表（入库行已分配好位置）
 * @param compareItems  边缘机比对结论（与 items 同序）：Matched 项才写库，其余红字展示
 * @param misplaced     错放明细（结果页提示行，只提示不告警）
 *
 * 执行约定：比对通过的项写库并写操作日志（入库 checkin / 出库 checkout）；
 * 比对未过的项红字提示原因且不写任何记录。
 */
void ToolCheckinPage::showCheckResultDialog(const QList<OrderRow>& items,
                                            const QList<ToolService::RecogCompareItem>& compareItems,
                                            const QList<ToolService::RecogMisplaced>& misplaced,
                                            const ToolService::RecogStockOutcome& stockOutcome,
                                            int round, int maxRounds) {
    int userId = m_user["userId"].toInt();
    if (userId <= 0) userId = 1;

    // 比对收集（先不写库）：Matched 项计入本轮通过集，其余红字展示比对原因不写库
    struct CheckResult {
        bool matched = false;  // 是否通过边缘机核对
        QString typeText;   // 入库 / 出库
        QString toolName;
        QString position;
        QString message;    // 比对原因（写入后会被实际结果覆盖）
    };
    QList<CheckResult> results;
    QSet<int> matchedIdx;   // 本轮通过项的索引（全量单据每轮重新核对，权威反映当前实况）
    for (int i = 0; i < items.size(); ++i) {
        const OrderRow& it = items[i];
        const QString typeText = (it.type != QStringLiteral("out"))
            ? QStringLiteral("入库") : QStringLiteral("出库");
        if (it.borrowedOut) {
            // 借出转出库：工具不在柜内，本来就没有可核对的位置，直接视为通过
            matchedIdx.insert(i);
            results.append({true, typeText, it.toolName, it.position,
                            QStringLiteral("借出转出库（免开箱）")});
            continue;
        }
        bool matched = (i < compareItems.size()
                        && compareItems[i].result == ToolService::RecogItemResult::Matched);
        if (matched) {
            matchedIdx.insert(i);
            results.append({true, typeText, it.toolName, it.position, QStringLiteral("比对通过")});
        } else {
            const QString reason = (i < compareItems.size() && !compareItems[i].detail.isEmpty())
                ? compareItems[i].detail
                : QStringLiteral("未通过边缘机核对");
            results.append({false, typeText, it.toolName, it.position, reason});
        }
    }
    // 全柜核对出现硬差异（库内在库工具柜内已空 / 货位上工具被换 / 柜内多出未登记工具）
    // 说明实际取放与账实对不上：本次不能判"全部成功"，按未全过处理，
    // 走【重新开柜整理】/【按当前实况写库】双出口，并受三轮上限约束。
    // 判定统一走服务层 isRoundFullyPassed，页面与单测共用同一份规则，不会两处漂移。
    const QList<ToolService::RecogStockDiff> hardDiffs = ToolService::hardStockDiffs(stockOutcome);
    const bool hasHardDiff = !hardDiffs.isEmpty();
    const bool allMatched = ToolService::isRoundFullyPassed(matchedIdx.size(), items.size(), stockOutcome);

    // 按指定索引集合写库（全过=全部索引；按实况写库=本轮通过索引）；返回实际写入成功的索引
    auto commitByIndices = [&](const QSet<int>& idxSet) -> QSet<int> {
        ToolService toolSvc;
        CheckoutService checkoutSvc;
        db::RecordDAO recDao;
        QSet<int> okIdx;
        for (int i = 0; i < items.size(); ++i) {
            if (!idxSet.contains(i)) continue;   // 仅写指定项，未过项不写（防脏数据）
            const OrderRow& it = items[i];
            if (it.type != QStringLiteral("out")) {
                QJsonArray positions;
                positions.append(it.posInfo);
                QJsonObject data;
                data["selectedToolId"] = it.toolId;
                data["positions"] = positions;
                data["toolCode"] = it.toolCode;
                if (toolSvc.checkinTool(data)) {
                    QString content = QStringLiteral("入库工具「%1」编号[%2]×1件 位置%3")
                        .arg(it.toolName, it.toolCode, it.position);
                    recDao.insertOperationLog(userId, SC::OP_CHECKIN, "tool", it.toolCode, content);
                    okIdx.insert(i);
                }
            } else {
                CheckoutItem outItem;
                outItem.mappingId = it.mappingId;
                outItem.toolId = it.toolId;
                outItem.toolCode = it.toolCode;
                outItem.toolName = it.toolName;
                outItem.position = it.position;
                outItem.quantity = 1;
                outItem.reason = QStringLiteral("出入库单出库");
                outItem.fromBorrow = it.borrowedOut;   // 借出转出库：写库时一并结清借用记录
                auto out = checkoutSvc.executeCheckout(userId, {outItem});
                if (out.success) okIdx.insert(i);
            }
        }
        return okIdx;
    };

    // 刷新记录并清空单据，同时重置轮次状态供下一单据使用
    auto finishAndRefresh = [&](const QSet<int>& okIdx) {
        // 记录页签始终刷新（本次可能一条未写，但此前操作可能已写入过）
        m_recordCurrentPage = 1;
        loadCheckinRecords();
        m_outRecordCurrentPage = 1;
        loadCheckoutRecords();
        // 单据表只在确有写入时清空：一项未写时保留单据，便于操作人改单据后重新确认
        if (!okIdx.isEmpty()) {
            clearOrderTable();
        }
        m_orderItems.clear();
        m_currentRound = 1;
        m_roundState = RoundState::Running;
    };

    // 全柜核对提示行：库内全量在库货位 vs 边缘机实况清单（本单目标货位不参与）
    // 只提示不阻断：异常货位不影响本单写库，但必须让操作人当场看见；
    // 免开箱等没有实况的场景（checked=false）不显示核对行
    auto buildStockHint = [&stockOutcome]() -> QLabel* {
        auto* label = new QLabel();
        label->setWordWrap(true);
        if (stockOutcome.diffs.isEmpty()) {
            label->setText(QStringLiteral("✓ 全柜核对一致：%1 个在库货位与柜内实况相符")
                .arg(stockOutcome.stockCount));
            label->setStyleSheet(QString(
                "" + StyleHelper::fontSize(StyleHelper::Token::FontSmall) + ";"
                "color:#43a047;background:#f1f8f2;border:1px solid #bfe3c6;"
                "border-radius:10px;padding:8px 12px;"));
            return label;
        }
        // 缺失/换工具/来源不明是硬异常置顶；未覆盖只是没法核对，附在最后
        QStringList hardTips;
        int notCovered = 0;
        for (const auto& d : stockOutcome.diffs) {
            if (d.type == ToolService::RecogStockDiffType::NotCovered) {
                ++notCovered;
            } else {
                hardTips << d.detail;
            }
        }
        QString text = QStringLiteral("⚠ 全柜核对发现 %1 处异常（账实不符，需人工确认后再写库）")
            .arg(stockOutcome.diffs.size());
        if (!hardTips.isEmpty()) {
            text += QStringLiteral("\n") + hardTips.join(QStringLiteral("；"));
        }
        if (notCovered > 0) {
            text += QStringLiteral("\n另有 %1 个货位未被识别覆盖，无法核对").arg(notCovered);
        }
        label->setText(text);
        label->setStyleSheet(QString(
            "" + StyleHelper::fontSize(StyleHelper::Token::FontSmall) + ";"
            "color:#d97a1e;background:#fff7e6;border:1px solid #f0c36d;"
            "border-radius:10px;padding:8px 12px;"));
        return label;
    };

    // 结果表构建（written=true 显示实际写入结果，否则显示比对结论）
    auto buildResultTable = [&](bool written, const QSet<int>& okIdx) {
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
            QString displayMsg;
            bool ok = false;
            if (written) {
                if (okIdx.contains(i)) {
                    displayMsg = (r.typeText == QStringLiteral("入库"))
                        ? QStringLiteral("入库成功") : QStringLiteral("出库成功");
                    ok = true;
                } else if (r.matched) {
                    displayMsg = QStringLiteral("写入失败，请重试");
                } else {
                    displayMsg = r.message;
                }
            } else {
                displayMsg = r.matched ? QStringLiteral("比对通过") : r.message;
                ok = r.matched;
            }
            auto* typeItem = new QTableWidgetItem(r.typeText);
            typeItem->setForeground(QColor(r.typeText == QStringLiteral("入库") ? "#43a047" : "#e67e22"));
            typeItem->setTextAlignment(Qt::AlignCenter);
            resultTable->setItem(i, 0, typeItem);
            resultTable->setItem(i, 1, new QTableWidgetItem(r.toolName));
            auto* posItem = new QTableWidgetItem(r.position.isEmpty() ? QStringLiteral("--") : r.position);
            posItem->setTextAlignment(Qt::AlignCenter);
            resultTable->setItem(i, 2, posItem);
            auto* msgItem = new QTableWidgetItem(displayMsg);
            msgItem->setForeground(QColor(ok ? "#43a047" : "#e53935"));
            resultTable->setItem(i, 3, msgItem);
            resultTable->setRowHeight(i, 40);
        }
        return resultTable;
    };

    if (allMatched) {
        // 三轮内全过：一次性整单写入全部单据
        QSet<int> allIdx; for (int i = 0; i < items.size(); ++i) allIdx.insert(i);
        QSet<int> okIdx = commitByIndices(allIdx);
        bool allWriteOk = (okIdx.size() == items.size());
        // 每轮识别写一条操作日志：记录结果与逐项结论
        writeRoundLog(round, items, compareItems, misplaced, allWriteOk);
        m_roundState = allWriteOk ? RoundState::Passed : RoundState::Partial;

        ResultDialog dlg(allWriteOk ? QStringLiteral("出入库完成") : QStringLiteral("出入库检查结果"),
            allWriteOk ? QStringLiteral("✅") : QStringLiteral("⚠️"),
            allWriteOk ? QStringLiteral("全部指令执行成功！")
                       : QStringLiteral("共 %1 项，成功 %2 项，未完成 %3 项")
                             .arg(items.size()).arg(okIdx.size()).arg(items.size() - okIdx.size()),
            allWriteOk ? QStringLiteral("#43a047") : QStringLiteral("#e67e22"), 14, this);
        dlg.setFixedSize(620, 480);

        if (!misplaced.isEmpty()) {
            QStringList tips;
            for (const auto& m : misplaced) {
                tips << QStringLiteral("%1 货位有异常工具（%2 应放 %3）")
                    .arg(m.actualPosition,
                         m.toolName.isEmpty() ? m.toolCode : m.toolName,
                         m.expectedPosition);
            }
            auto* misplacedLabel = new QLabel(QStringLiteral("⚠ 检测到错放：") + tips.join(QStringLiteral("；")));
            misplacedLabel->setWordWrap(true);
            misplacedLabel->setStyleSheet(QString(
                "" + StyleHelper::fontSize(StyleHelper::Token::FontSmall) + ";"
                "color:#d97a1e;background:#fff7e6;border:1px solid #f0c36d;"
                "border-radius:10px;padding:8px 12px;"));
            dlg.bodyLayout()->addWidget(misplacedLabel);
        }
        if (stockOutcome.checked) dlg.bodyLayout()->addWidget(buildStockHint());
        dlg.bodyLayout()->addWidget(buildResultTable(true, okIdx));
        dlg.addFinishButtonRow(QStringLiteral("完成"), StyleHelper::buttonPrimary(), 140);
        dlg.exec();
        finishAndRefresh(okIdx);
        return;
    }

    // 未全过：本轮不写库，按轮次给出出口（文案带当前轮次，操作人一眼知道是第几轮）
    const bool reachedLimit = (round >= maxRounds);
    QString summaryText = reachedLimit
        ? QStringLiteral("共 %1 项，比对通过 %2 项，未完成 %3 项。第 %4 轮仍未全过，按当前实况写库（通过项写入，未过项保持原状）")
              .arg(items.size()).arg(matchedIdx.size()).arg(items.size() - matchedIdx.size()).arg(round)
        : QStringLiteral("共 %1 项，比对通过 %2 项，未完成 %3 项，请选择后续操作（第 %4/%5 轮）")
              .arg(items.size()).arg(matchedIdx.size()).arg(items.size() - matchedIdx.size())
              .arg(round).arg(maxRounds);
    if (hasHardDiff) {
        summaryText += QStringLiteral("\n另有 %1 处全柜差异（库里有工具但柜内没有，或柜内多出未登记工具）：实际取放与单据不一致，请确认后再写库")
                          .arg(hardDiffs.size());
    }
    ResultDialog dlg(QStringLiteral("出入库检查结果"), QStringLiteral("⚠️"), summaryText,
        QStringLiteral("#e67e22"), 14, this);
    dlg.setFixedSize(620, 480);

    if (!misplaced.isEmpty()) {
        QStringList tips;
        for (const auto& m : misplaced) {
            tips << QStringLiteral("「%1」实际在 %2，应放 %3")
                .arg(m.toolName.isEmpty() ? m.toolCode : m.toolName,
                     m.actualPosition, m.expectedPosition);
        }
        auto* misplacedLabel = new QLabel(QStringLiteral("⚠ 检测到错放：") + tips.join(QStringLiteral("；")));
        misplacedLabel->setWordWrap(true);
        misplacedLabel->setStyleSheet(QString(
            "" + StyleHelper::fontSize(StyleHelper::Token::FontSmall) + ";"
            "color:#d97a1e;background:#fff7e6;border:1px solid #f0c36d;"
            "border-radius:10px;padding:8px 12px;"));
        dlg.bodyLayout()->addWidget(misplacedLabel);
        }
        if (stockOutcome.checked) dlg.bodyLayout()->addWidget(buildStockHint());
        dlg.bodyLayout()->addWidget(buildResultTable(false, QSet<int>()));

    auto* tipLabel = new QLabel(QStringLiteral("未完成项未写入记录，可重新开柜整理或按当前实况写库"));
    tipLabel->setStyleSheet("" + StyleHelper::fontSize(StyleHelper::Token::FontSmall) + ";color:#999;background:transparent;");
    tipLabel->setAlignment(Qt::AlignCenter);
    dlg.bodyLayout()->addWidget(tipLabel);

    QString action;
    // 单据项全过、只是全柜有差异：没有可整理的单据项，重开柜无从下手，
    // 改为让操作人核查柜内实物后确认写库
    const bool onlyStockDiff = (matchedIdx.size() == items.size()) && hasHardDiff;
    if (round < maxRounds && m_roundState != RoundState::GivenUp && !onlyStockDiff) {
        // 未到上限：双出口（重新开柜整理 / 按当前实况写库），按钮带下一轮轮次
        m_roundState = RoundState::Partial;
        dlg.addTwoButtonsRow(QStringLiteral("重新开柜整理（第 %1/%2 轮）").arg(round + 1).arg(maxRounds),
                             StyleHelper::buttonDefault(),
                             QStringLiteral("按当前实况写库"), StyleHelper::buttonPrimary(), 160);
    } else if (onlyStockDiff) {
        m_roundState = RoundState::Partial;
        dlg.setFinishAction(QStringLiteral("writeNow"));
        dlg.addFinishButtonRow(QStringLiteral("已核查，按当前实况写库"), StyleHelper::buttonPrimary(), 180);
    } else {
        // 已达上限：只给按实况写库（保底，不零写库也不死循环）
        m_roundState = RoundState::GivenUp;
        dlg.setFinishAction(QStringLiteral("writeNow"));
        dlg.addFinishButtonRow(QStringLiteral("按当前实况写库"), StyleHelper::buttonPrimary(), 160);
    }
    dlg.exec();
    action = dlg.clickedAction();

    if (action == QStringLiteral("reopen")) {
        // 本轮整理记录（操作人 / 第几轮 / 待修正项清单），内容按约定写 JSON
        writeRoundLog(m_currentRound, items, compareItems, misplaced, false);
        ++m_currentRound;
        m_roundState = RoundState::Running;
        // 摘要只列失败项（引导用户修正），识别仍按全量单据 m_orderItems 核对
        QList<OrderRow> failedItems;
        for (int i = 0; i < items.size(); ++i) {
            if (!matchedIdx.contains(i)) failedItems.append(items[i]);
        }
        // 再次下发开锁指令（流程图 ⑤→⑥）：开箱成功后再弹摘要弹窗
        m_pendingOpenItems = failedItems;
        m_reopenPending = true;
        emit openRequested();
        return;
    }

    if (action != QStringLiteral("writeNow")) {
        // 用户未明确选择（按 ESC 或直接关窗）：视为放弃本次，不写库
        finishAndRefresh(QSet<int>());
        return;
    }

    // 按当前实况写库（本轮通过项写入，未过项保持原状）
    QSet<int> okIdx = commitByIndices(matchedIdx);
    // 保底轮次同样写日志：结果记 fail，便于事后追溯为何按实况写入
    writeRoundLog(round, items, compareItems, misplaced, false);
    // 保底结果如实呈现：一项未写也必须明确告知，避免操作人误以为已入库
    const int expectWrite = matchedIdx.size();
    if (expectWrite == 0) {
        MessageDialog::showWarning(this, QStringLiteral("本次未写入任何记录"),
            QStringLiteral("本轮没有任何一项通过核对，未写入任何记录。\n工具状态保持原样，请重新发起本次出入库。"));
    } else if (okIdx.size() < expectWrite) {
        MessageDialog::showWarning(this, QStringLiteral("本次未全部写入"),
            QStringLiteral("按当前实况写库：计划写入 %1 项，实际写入 %2 项。\n未写入项保持原状，可重新发起本次出入库。")
                .arg(expectWrite).arg(okIdx.size()));
    }
    finishAndRefresh(okIdx);
}

/**
 * @brief 写本轮识别的操作日志（每轮一条，用于追溯"第几轮、哪些项没过"）
 * @param round        当前轮次（从 1 开始）
 * @param items        本轮核对的全量单据
 * @param compareItems 本轮逐项比对结论
 * @param misplaced    本轮错放明细
 * @param allPassed    本轮是否全部通过
 * @return 无。写库失败不影响界面流程
 * @note  内容按约定写 JSON：{round, order[], misplaced[], unknown[], result}；
 *        operation_type 用 checkin_round / checkout_round，与普通出入库记录区分开
 */
void ToolCheckinPage::writeRoundLog(int round, const QList<OrderRow>& items,
                                    const QList<ToolService::RecogCompareItem>& compareItems,
                                    const QList<ToolService::RecogMisplaced>& misplaced,
                                    bool allPassed)
{
    int userId = m_user["userId"].toInt();
    if (userId <= 0) userId = 1;

    // 本单是入库还是出库：出现任一出行即按出库轮次记
    bool isCheckout = false;
    for (const OrderRow& it : items) {
        if (it.type == QStringLiteral("out")) {
            isCheckout = true;
            break;
        }
    }

    QJsonArray orderArr;
    for (int i = 0; i < items.size(); ++i) {
        const bool matched = (i < compareItems.size()
                              && compareItems[i].result == ToolService::RecogItemResult::Matched);
        QJsonObject o;
        o["toolCode"] = items[i].toolCode;
        o["toolName"] = items[i].toolName;
        o["position"] = items[i].position;
        o["type"]     = items[i].type;
        o["result"]   = matched ? QStringLiteral("success") : QStringLiteral("fail");
        o["confidence"] = (i < compareItems.size()) ? compareItems[i].confidence : 0.0;  // 识别置信度（可复核门槛判定）
        if (i < compareItems.size() && !compareItems[i].detail.isEmpty()) {
            o["detail"] = compareItems[i].detail;
        }
        orderArr.append(o);
    }

    QJsonArray misplacedArr;
    for (const auto& m : misplaced) {
        QJsonObject o;
        o["toolCode"]         = m.toolCode;
        o["toolName"]         = m.toolName;
        o["actualPosition"]   = m.actualPosition;
        o["expectedPosition"] = m.expectedPosition;
        misplacedArr.append(o);
    }

    // 未知物品：目标货位上的东西认不出（货位号取单据对应位置）
    QJsonArray unknownArr;
    for (int i = 0; i < compareItems.size(); ++i) {
        if (compareItems[i].result != ToolService::RecogItemResult::UnknownItem) {
            continue;
        }
        QJsonObject o;
        o["position"] = (i < items.size()) ? items[i].position : compareItems[i].position;
        unknownArr.append(o);
    }

    QJsonObject content;
    content["round"]     = round;
    content["order"]     = orderArr;
    content["misplaced"] = misplacedArr;
    content["unknown"]   = unknownArr;
    content["result"]    = allPassed ? QStringLiteral("success") : QStringLiteral("fail");

    db::RecordDAO recDao;
    const QString opType = isCheckout ? SC::ORDER_ROUND_OP_CHECKOUT : SC::ORDER_ROUND_OP_CHECKIN;
    recDao.insertOperationLog(userId, opType, "tool", QString(),
                               QString::fromUtf8(QJsonDocument(content).toJson(QJsonDocument::Compact)));
}
