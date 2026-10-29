/**  
 * @file ToolService.cpp
 * @brief 工具服务类实现 - 工具入库业务逻辑
 * @author 袁燕
 *
 * 所有SQL操作迁移到ToolDAO，Service层纯业务逻辑
 */
#include "ToolService.h"
#include "db/ToolDAO.h"
#include <QJsonDocument>
#include <QDateTime>
#include <QDebug>
#include "common/Constants.h"

using db::ToolDAO;

ToolService::ToolService() {}

/**
 * @brief 工具入库
 * @param data 入库数据，含工具ID、柜体与层位信息
 * @return true=入库成功
 * @note 只更新位置映射表状态并同步工具在库数量，不新建工具记录
 */
bool ToolService::checkinTool(const QJsonObject& data) {
    // 入库逻辑只更新映射表status，不新建tool_info记录
    // 核心设计：一个工具可以在多个位置入库，tool_info只存工具基础信息
    // 入库 = UPDATE tool_position_mapping SET status='in_stock' WHERE mapping_id=?
    // 位置占用状态由映射表status字段管理，不依赖tool_info的cabinet_id/layer/position
    int selectedToolId = data["selectedToolId"].toInt(0);
    int checkinIndex = data["checkinIndex"].toInt(0);
    if (selectedToolId <= 0) {
        qWarning() << "[ToolService] checkinTool: 必须选择已有工具(selectedToolId)";
        return false;
    }

    QJsonArray positions = data["positions"].toArray();
    if (positions.isEmpty()) {
        qWarning() << "[ToolService] checkinTool: positions为空";
        return false;
    }

    ToolDAO dao;
    QJsonObject existing = dao.findById(selectedToolId);
    if (existing.isEmpty()) {
        qWarning() << "[ToolService] checkinTool: 工具不存在 toolId=" << selectedToolId;
        return false;
    }

    QString toolCode = existing["toolCode"].toString();
    qInfo() << "[ToolService] checkinTool: toolCode=" << toolCode
            << "toolId=" << selectedToolId
            << "入库数量=" << positions.size();

    // 遍历所有位置，通过DAO更新映射表status
    // 可入库位置=待入库(pending)或已出库回流(checked_out)，防止在库/借出位置被覆盖
    // 整批单事务：任一位置写失败或状态同步失败就整体回滚，
    // 杜绝"位置已置在库、工具状态没跟上"的半截数据（与借/还/出库同一纪律）
    const bool allSuccess = dao.transaction([&]() -> bool {
        for (int i = 0; i < positions.size(); ++i) {
            const QJsonObject posObj = positions[i].toObject();
            const int cabinetId = posObj["cabinetId"].toInt(0);
            const QString layer = posObj["layer"].toString();
            const QString position = posObj["position"].toString();

            if (cabinetId <= 0 || layer.isEmpty() || position.isEmpty()) {
                qWarning() << "[ToolService] checkinTool: 位置参数不完整 index=" << i;
                return false;
            }

            // 先按待入库位置更新；该位置若为已出库位置（checked_out）同样放行入库
            bool updated = dao.updateMappingByPosition(selectedToolId, cabinetId, layer, position,
                                                       SC::TOOL_IN_STOCK, SC::TOOL_PENDING);
            if (!updated) {
                updated = dao.updateMappingByPosition(selectedToolId, cabinetId, layer, position,
                                                      SC::TOOL_IN_STOCK, SC::TOOL_CHECKED_OUT);
            }
            if (!updated) {
                qWarning() << "[ToolService] checkinTool: 更新映射表失败 index=" << i;
                return false;
            }
            qInfo() << "[ToolService] checkinTool: 位置" << cabinetId << layer << position
                    << "入库成功，工具" << toolCode;
        }

        // 入库后同步 tool_info 状态
        // 有任一在库位置则工具为在库（覆盖待入库新建与已出库回流两种场景）；
        // current_qty 由 tool_position_mapping 触发器自动维护，此处不再手算
        const int inStockCount = dao.countInStockPositions(selectedToolId);
        const QString oldStatus = existing["status"].toString();
        if (inStockCount > 0
            && (oldStatus == SC::TOOL_PENDING || oldStatus == SC::TOOL_CHECKED_OUT)) {
            QJsonObject updates;
            updates["status"] = SC::TOOL_IN_STOCK;
            if (!dao.update(selectedToolId, updates)) {
                qWarning() << "[ToolService] checkinTool: 工具状态同步失败 toolId=" << selectedToolId;
                return false;
            }
        }
        return true;
    });

    return allSuccess;
}

/**
 * @brief 单据与边缘机实况比对（静态纯函数：不读库不写库不碰界面）
 * @param items   待比对的单据指令列表（入库项已分配好目标货位）
 * @param result  边缘机返回的全柜实况清单（recognize_ack 解析结果）
 * @return 逐行结论（与 items 同序）+ 错放明细
 * @note  入库项：目标货位须识别出正是本工具且置信度达 SC::RECOG_MIN_CONFIDENCE 门槛；
 *        出库项：目标货位上没有识别出工具即通过（已取走）；
 *        错放明细：本单入库工具出现在非目标货位（出库工具无"应放"目标，不参与错放扫描）
 */
ToolService::RecogCompareOutcome ToolService::compareOrderWithRecognition(
    const QList<RecogOrderItem>& items, const RecognizeResult& result)
{
    RecogCompareOutcome outcome;

    // 识别清单为空（边缘机未就绪/摄像头未覆盖）：全部判"未检测到"并走完整理流程。
    // 绝不能当成"全部通过"——否则纯出库单会在工具还在柜里时整单扣库存
    if (result.items.isEmpty()) {
        for (const RecogOrderItem& it : items) {
            RecogCompareItem cmp;
            cmp.toolId   = it.toolId;
            cmp.toolCode = it.toolCode;
            cmp.toolName = it.toolName;
            cmp.position = it.position;
            cmp.result   = RecogItemResult::Empty;
            cmp.detail   = QStringLiteral("未检测到该货位（识别清单为空）");
            outcome.items.append(cmp);
        }
        return outcome;
    }

    // 实况清单按货位建索引：协议约定一个货位一条记录，直接覆盖不会丢数据
    QHash<QString, RecognizeItem> spotByPos;
    for (const RecognizeItem& s : result.items) {
        spotByPos.insert(s.position, s);
    }

    // 入库工具的编号->名称、编号->目标货位（错放扫描与提示用）
    QHash<QString, QString> nameByCode;
    QHash<QString, QString> expectedPosByCode;
    for (const RecogOrderItem& it : items) {
        if (!it.toolName.isEmpty()) nameByCode.insert(it.toolCode, it.toolName);
        if (it.type == QStringLiteral("in")) {
            expectedPosByCode.insert(it.toolCode, it.position);
        }
    }

    for (const RecogOrderItem& it : items) {
        RecogCompareItem cmp;
        cmp.toolId   = it.toolId;
        cmp.toolCode = it.toolCode;
        cmp.toolName = it.toolName;
        cmp.position = it.position;

        if (it.type == QStringLiteral("in")) {
            auto spot = spotByPos.constFind(it.position);
            if (spot != spotByPos.constEnd()) {
                cmp.confidence = spot->confidence;   // 该货位识别置信度（写入轮次操作日志）
            }
            if (spot == spotByPos.constEnd()) {
                cmp.result = RecogItemResult::Empty;
                cmp.detail = QStringLiteral("货位为空，未检测到工具放入");
            } else if (spot->toolId.isEmpty()) {
                cmp.result = RecogItemResult::UnknownItem;
                cmp.detail = QStringLiteral("%1 货位有无法识别物品").arg(it.position);
            } else if (spot->toolId != it.toolCode) {
                cmp.result = RecogItemResult::WrongTool;
                cmp.detail = QStringLiteral("货位上为%1").arg(spot->toolId);
            } else if (spot->confidence < SC::RECOG_MIN_CONFIDENCE) {
                cmp.result = RecogItemResult::LowConfidence;
                cmp.detail = QStringLiteral("置信度%1%，未达%2%门槛")
                    .arg(spot->confidence * 100, 0, 'f', 0)
                    .arg(SC::RECOG_MIN_CONFIDENCE * 100, 0, 'f', 0);
            } else {
                cmp.result = RecogItemResult::Matched;
            }
        } else {
            // 出库项：货位上没有识别出工具即通过（工具已取走）
            auto spot = spotByPos.constFind(it.position);
            if (spot != spotByPos.constEnd()) {
                cmp.confidence = spot->confidence;
            }
            if (spot == spotByPos.constEnd() || spot->toolId.isEmpty()) {
                cmp.result = RecogItemResult::Matched;
            } else {
                cmp.result = RecogItemResult::StillThere;
                cmp.detail = QStringLiteral("货位上仍有%1").arg(spot->toolId);
            }
        }
        outcome.items.append(cmp);
    }

    // 错放扫描：本单入库工具出现在非目标货位
    // 错放是一次识别事件而非货位状态：只单独记明细（结果页提示行），
    // 不改写逐项判定（那一条仍按"目标货位为空/放着别的工具"如实呈现）
    for (const RecognizeItem& s : result.items) {
        if (s.toolId.isEmpty()) continue;
        auto expected = expectedPosByCode.constFind(s.toolId);
        if (expected != expectedPosByCode.constEnd() && expected.value() != s.position) {
            RecogMisplaced misplaced;
            misplaced.toolCode         = s.toolId;
            misplaced.toolName         = nameByCode.value(s.toolId);
            misplaced.actualPosition   = s.position;
            misplaced.expectedPosition = expected.value();
            outcome.misplaced.append(misplaced);
        }
    }

    return outcome;
}

/**
 * @brief 页面中立实况结构版比对：转成通信层结果后复用核心逻辑
 * @param items   待比对的单据指令列表
 * @param spots   全柜实况清单（页面中立结构，由 MainWindow 从通信层转换）
 * @return 逐行结论与错放明细
 * @note  页面经此重载调用，无需包含通信层头文件（分层铁律）
 */
ToolService::RecogCompareOutcome ToolService::compareOrderWithRecognition(
    const QList<RecogOrderItem>& items, const QList<RecogSpot>& spots)
{
    RecognizeResult result;
    for (const RecogSpot& s : spots) {
        RecognizeItem item;
        item.position   = s.position;
        item.toolId     = s.toolCode;
        item.confidence = s.confidence;
        item.present    = s.present;
        result.items.append(item);
    }
    return compareOrderWithRecognition(items, result);
}

/**
 * @brief 库内全量在库货位 与 边缘机实况清单 的全柜比对（静态纯函数）
 * @param stocks          库内在库货位列表（页面经 ToolDAO 取后传入）
 * @param spots           边缘机回报的全柜实况清单
 * @param ignorePositions 本单据涉及的目标货位（入库/出库项），不参与全柜核对
 * @return 异常明细与两侧规模；diffs 为空表示全柜一致
 * @note  单据比对只管本单几十项，本方法补上"其他货位有没有被动过"这一路：
 *        库里在库却空了=Missing；库里是A却识别出B=Mismatch；
 *        清单没回报该货位=NotCovered（未覆盖，只提示不算异常丢失）；
 *        库里无在库记录却识别出工具=Extra（未登记入库的工具出现在柜内）
 */
ToolService::RecogStockOutcome ToolService::compareStockWithRecognition(
    const QList<RecogStockItem>& stocks, const QList<RecogSpot>& spots,
    const QSet<QString>& ignorePositions)
{
    RecogStockOutcome outcome;
    outcome.checked     = true;
    outcome.stockCount  = stocks.size();
    outcome.scannedCount = spots.size();

    // 两侧都按货位建索引：协议约定一项一货位，直接覆盖不会丢数据
    QHash<QString, RecogSpot> spotByPos;
    for (const RecogSpot& s : spots) {
        spotByPos.insert(s.position, s);
    }
    QHash<QString, RecogStockItem> stockByPos;
    for (const RecogStockItem& st : stocks) {
        stockByPos.insert(st.position, st);
    }

    // ① 库内在库货位逐个核对：空了 / 被换 / 没扫到
    for (const RecogStockItem& st : stocks) {
        if (ignorePositions.contains(st.position)) {
            continue;   // 本单正在处理的货位，写库前后状态会变，不参与
        }
        const QString nameText = st.toolName.isEmpty() ? st.toolCode : st.toolName;
        const auto spot = spotByPos.constFind(st.position);
        if (spot == spotByPos.constEnd()) {
            RecogStockDiff diff;
            diff.type      = RecogStockDiffType::NotCovered;
            diff.position  = st.position;
            diff.toolCode  = st.toolCode;
            diff.toolName  = st.toolName;
            diff.detail    = QStringLiteral("%1 货位未被识别覆盖，无法核对（库内为%2）")
                .arg(st.position, nameText);
            outcome.diffs.append(diff);
            continue;
        }
        if (spot->toolCode.isEmpty()) {
            RecogStockDiff diff;
            diff.type      = RecogStockDiffType::Missing;
            diff.position  = st.position;
            diff.toolCode  = st.toolCode;
            diff.toolName  = st.toolName;
            diff.detail    = QStringLiteral("%1 货位已空，库内为在库工具%2")
                .arg(st.position, nameText);
            outcome.diffs.append(diff);
            continue;
        }
        if (spot->toolCode != st.toolCode) {
            RecogStockDiff diff;
            diff.type      = RecogStockDiffType::Mismatch;
            diff.position  = st.position;
            diff.toolCode  = st.toolCode;
            diff.toolName  = st.toolName;
            diff.detail    = QStringLiteral("%1 货位库内为%2，实际识别为%3")
                .arg(st.position, nameText, spot->toolCode);
            outcome.diffs.append(diff);
        }
    }

    // ② 识别清单里多出来的：库内该货位没有在库记录（未登记入库的工具出现在柜内）
    for (const RecogSpot& s : spots) {
        if (s.toolCode.isEmpty()) {
            continue;   // 认不出的货位由单据比对的"未知物品"分支负责提示
        }
        if (ignorePositions.contains(s.position)) {
            continue;
        }
        if (stockByPos.contains(s.position)) {
            continue;   // 库内有的已在①核对过（一致或被换），不重复报
        }
        RecogStockDiff diff;
        diff.type      = RecogStockDiffType::Extra;
        diff.position  = s.position;
        diff.toolCode  = s.toolCode;
        diff.detail    = QStringLiteral("%1 货位库内无在库记录，实际识别为%2")
            .arg(s.position, s.toolCode);
        outcome.diffs.append(diff);
    }

    return outcome;
}

QStringList ToolService::detectReturnMisplaced(
    const QList<RecogStockDiff>& diffs,
    const QHash<QString, QString>& regPositionByCode)
{
    QStringList alerts;
    for (const RecogStockDiff& d : diffs) {
        if (d.type != RecogStockDiffType::Extra)
            continue;   // 只判归还分支：借出工具柜内复现
        const QString regPos = regPositionByCode.value(d.toolCode);
        // 实际货位非空、登记货位非空、且两者不一致 = 错放（与在库工具错放同一口径）
        if (!d.position.isEmpty() && !regPos.isEmpty() && d.position != regPos)
            alerts.append(QStringLiteral("工具 %1 应位于 %2，实际位于 %3（错放）")
                              .arg(d.toolCode, regPos, d.position));
    }
    return alerts;
}

/**
 * @brief 取出全柜核对结果里的硬差异明细（排除未覆盖）
 * @param outcome 全柜核对结果
 * @return 硬差异明细列表：库内在库工具柜内已空、货位上工具被换、柜内出现未登记工具
 * @note  未覆盖（摄像头没扫到该货位）不算硬差异：那是识别覆盖问题，不是账实不符
 */
QList<ToolService::RecogStockDiff> ToolService::hardStockDiffs(const RecogStockOutcome& outcome)
{
    QList<RecogStockDiff> hard;
    for (const RecogStockDiff& d : outcome.diffs) {
        if (d.type != RecogStockDiffType::NotCovered) {
            hard.append(d);
        }
    }
    return hard;
}

/**
 * @brief 判断全柜核对结果里有没有硬差异（账实不符，需要人工确认）
 * @param outcome 全柜核对结果
 * @return true=有硬差异；false=只有未覆盖或没有差异
 * @note  有硬差异说明实际取放与账实对不上，本次出入库不能判"全部成功"，
 *        要让操作人确认（重新开柜整理或按当前实况写库）后再落库
 */
bool ToolService::hasHardStockDiff(const RecogStockOutcome& outcome)
{
    return !hardStockDiffs(outcome).isEmpty();
}

/**
 * @brief 判断本轮识别是否全过（单据项全过 + 全柜无账实不符）
 * @param matchedCount 本轮比对通过的单据项数
 * @param itemCount    本单单据总项数
 * @param outcome      全柜核对结果
 * @return true=本轮全过（可整单写库）；false=本轮未全过（走双出口确认）
 */
bool ToolService::isRoundFullyPassed(int matchedCount, int itemCount,
                                     const RecogStockOutcome& outcome)
{
    if (matchedCount != itemCount) {
        return false;   // 单据项有未通过的
    }
    if (hasHardStockDiff(outcome)) {
        return false;   // 全柜有账实不符：实际取放与单据不一致，要人工确认
    }
    return true;
}
