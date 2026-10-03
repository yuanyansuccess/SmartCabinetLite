/**
 * @file MaintenanceService.cpp
 * @brief 系统维护域服务实现
 * @author 袁燕
 */
#include "MaintenanceService.h"

#include "db/ToolDAO.h"
#include "common/Constants.h"

bool MaintenanceService::updateTaskTypeToolQty(int typeId, int toolId, int qty) {
    if (typeId <= 0 || toolId <= 0) return false;
    db::ToolDAO dao;
    return dao.updateTaskTypeToolQty(typeId, toolId, qty);}

bool MaintenanceService::createTool(const QJsonObject& tool) {
    if (tool.isEmpty()) return false;
    db::ToolDAO dao;
    return dao.insertToolFull(tool);
}

bool MaintenanceService::updateTool(int toolId, const QJsonObject& updates) {
    if (toolId <= 0 || updates.isEmpty()) return false;
    db::ToolDAO dao;
    return dao.updateToolFull(toolId, updates);
}

bool MaintenanceService::deleteTool(int toolId) {
    if (toolId <= 0) return false;
    db::ToolDAO dao;
    return dao.deleteToolFully(toolId);
}

bool MaintenanceService::createPositionMapping(int toolId, int cabinetId, const QString& layer,
                                               const QString& position, const QString& status) {
    if (toolId <= 0 || cabinetId <= 0 || layer.isEmpty() || position.isEmpty()) return false;
    db::ToolDAO dao;
    return dao.insertPositionMapping(toolId, cabinetId, layer, position,
                                     status.isEmpty() ? QString(SC::TOOL_PENDING) : status);
}

bool MaintenanceService::removePositionMapping(int mappingId) {
    if (mappingId <= 0) return false;
    db::ToolDAO dao;
    // 位置已被占用（在库/借用中）时不允许删除，由 DAO 判定
    if (dao.isPositionMappingOccupied(mappingId)) return false;
    return dao.deletePositionMapping(mappingId);
}
