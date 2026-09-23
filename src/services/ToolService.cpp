/**  
 * @file ToolService.cpp
 * @brief 工具服务类实现 - 工具入库业务逻辑
 * @author 袁燕
 *
 * [V2.15 2026-07-05] 所有SQL操作迁移到ToolDAO，Service层纯业务逻辑
 */
#include "ToolService.h"
#include "db/ToolDAO.h"
#include <QJsonDocument>
#include <QDateTime>
#include <QDebug>

using db::ToolDAO;

ToolService::ToolService() {}

bool ToolService::checkinTool(const QJsonObject& data) {
    // [V2.08 2026-06-30 袁燕] 入库逻辑重构：只更新映射表status，不新建tool_info记录
    //   核心设计：一个工具可以在多个位置入库，tool_info只存工具基础信息
    //   入库 = UPDATE tool_position_mapping SET status='in_stock' WHERE mapping_id=?
    //   位置占用状态由映射表status字段管理，不再依赖tool_info的cabinet_id/layer/position
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
    // [V2.12-fix5 2026-07-03 袁燕] 只允许pending位置入库，防止已borrowed位置被覆盖
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

        if (dao.updateMappingByPosition(selectedToolId, cabinetId, layer, position,
                                         "in_stock", "pending")) {
            qInfo() << "[ToolService] checkinTool: 位置" << cabinetId << layer << position
                    << "入库成功，工具" << toolCode;
        } else {
            qWarning() << "[ToolService] checkinTool: 更新映射表失败 index=" << i;
            allSuccess = false;
        }
    }

    // [V2.11 2026-07-02 袁燕] 入库后更新tool_info状态
    //   如果工具原来是pending，入库后至少有1个in_stock位置，tool_info改为in_stock
    //   current_qty=映射表中in_stock位置数
    if (existing["status"].toString() == "pending") {
        QJsonObject updates;
        updates["status"] = "in_stock";
        dao.update(selectedToolId, updates);
    }
    // [V2.15 2026-07-05] 迁移COUNT查询到ToolDAO::countInStockPositions
    int inStockCount = dao.countInStockPositions(selectedToolId);
    if (inStockCount >= 0) {
        QJsonObject updates;
        updates["currentQty"] = inStockCount;
        dao.update(selectedToolId, updates);
    }

    return allSuccess;
}


