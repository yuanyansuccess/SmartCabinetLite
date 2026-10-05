/**
 * @file MaintenanceService.cpp
 * @brief 系统维护域服务实现
 * @author 袁燕
 */
#include "MaintenanceService.h"

#include "db/ToolDAO.h"
#include "common/Constants.h"

/**
 * @brief 更新任务类型下工具的数量
 * @param typeId 任务类型ID
 * @param toolId 工具ID
 * @param qty 新数量
 * @return true=更新成功
 */
bool MaintenanceService::updateTaskTypeToolQty(int typeId, int toolId, int qty) {
    if (typeId <= 0 || toolId <= 0) return false;
    db::ToolDAO dao;
    return dao.updateTaskTypeToolQty(typeId, toolId, qty);}

/**
 * @brief 新增工具
 * @param tool 工具字段集合
 * @return true=新增成功
 */
bool MaintenanceService::createTool(const QJsonObject& tool) {
    if (tool.isEmpty()) return false;
    db::ToolDAO dao;
    return dao.insertToolFull(tool);
}

/**
 * @brief 更新工具信息
 * @param toolId 工具ID
 * @param updates 待更新字段集合
 * @return true=更新成功
 */
bool MaintenanceService::updateTool(int toolId, const QJsonObject& updates) {
    if (toolId <= 0 || updates.isEmpty()) return false;
    db::ToolDAO dao;
    return dao.updateToolFull(toolId, updates);
}

/**
 * @brief 删除工具
 * @param toolId 工具ID
 * @return true=删除成功
 */
bool MaintenanceService::deleteTool(int toolId) {
    if (toolId <= 0) return false;
    db::ToolDAO dao;
    return dao.deleteToolFully(toolId);
}

/**
 * @brief 新增位置对照关系
 * @param toolId 工具ID
 * @param cabinetId 柜体ID
 * @param layer 层号
 * @param position 位号
 * @param status 初始状态，默认待入库
 * @return true=新增成功
 */
bool MaintenanceService::createPositionMapping(int toolId, int cabinetId, const QString& layer,
                                               const QString& position, const QString& status) {
    if (toolId <= 0 || cabinetId <= 0 || layer.isEmpty() || position.isEmpty()) return false;
    db::ToolDAO dao;
    return dao.insertPositionMapping(toolId, cabinetId, layer, position,
                                     status.isEmpty() ? QString(SC::TOOL_PENDING) : status);
}

/**
 * @brief 删除位置对照关系
 * @param mappingId 位置映射ID
 * @return true=删除成功
 */
bool MaintenanceService::removePositionMapping(int mappingId) {
    if (mappingId <= 0) return false;
    db::ToolDAO dao;
    // 位置已被占用（在库/借用中）时不允许删除，由 DAO 判定
    if (dao.isPositionMappingOccupied(mappingId)) return false;
    return dao.deletePositionMapping(mappingId);
}
