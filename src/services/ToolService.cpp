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
