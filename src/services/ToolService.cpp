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
    bool allSuccess = true;
    for (int i = 0; i < positions.size(); ++i) {
        QJsonObject posObj = positions[i].toObject();
        int cabinetId = posObj["cabinetId"].toInt(0);
        QString layer = posObj["layer"].toString();
        QString position = posObj["position"].toString();

        if (cabinetId <= 0 || layer.isEmpty() || position.isEmpty()) {
            qWarning() << "[ToolService] checkinTool: 位置参数不完整 index=" << i;
            allSuccess = false;
            continue;
        }

        // 先按待入库位置更新；该位置若为已出库位置（checked_out）同样放行入库
        bool updated = dao.updateMappingByPosition(selectedToolId, cabinetId, layer, position,
                                                   SC::TOOL_IN_STOCK, SC::TOOL_PENDING);
        if (!updated) {
            updated = dao.updateMappingByPosition(selectedToolId, cabinetId, layer, position,
                                                  SC::TOOL_IN_STOCK, SC::TOOL_CHECKED_OUT);
        }
        if (updated) {
            qInfo() << "[ToolService] checkinTool: 位置" << cabinetId << layer << position
                    << "入库成功，工具" << toolCode;
        } else {
            qWarning() << "[ToolService] checkinTool: 更新映射表失败 index=" << i;
            allSuccess = false;
        }
    }

    // 入库后同步tool_info状态与数量
    // 有任一在库位置则工具为在库（覆盖待入库新建与已出库回流两种场景）
    // current_qty=映射表中在库位置数
    int inStockCount = dao.countInStockPositions(selectedToolId);
    {
        QJsonObject updates;
        // current_qty 由 tool_position_mapping 触发器自动维护，此处不再手算
        QString oldStatus = existing["status"].toString();
        if (inStockCount > 0 &&
            (oldStatus == SC::TOOL_PENDING || oldStatus == SC::TOOL_CHECKED_OUT)) {
            updates["status"] = SC::TOOL_IN_STOCK;
        }
        dao.update(selectedToolId, updates);
    }

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
            if (spot == spotByPos.constEnd()) {
                cmp.result = RecogItemResult::Empty;
                cmp.detail = QStringLiteral("货位为空，未检测到工具放入");
            } else if (spot->toolId.isEmpty()) {
                cmp.result = RecogItemResult::UnknownItem;
                cmp.detail = QStringLiteral("货位上物品无法识别");
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
