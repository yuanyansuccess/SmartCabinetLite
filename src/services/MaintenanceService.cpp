/**
 * @file MaintenanceService.cpp
 * @brief 维护域服务实现
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
