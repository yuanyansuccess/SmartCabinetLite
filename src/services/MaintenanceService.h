/**
 * @file MaintenanceService.h
 * @brief 系统维护域服务 - 工具维护与位置对照关系的写操作编排
 * @author 袁燕
 *
 * 分层约定：读查询由页面直调 DAO；写操作必须经本服务，便于统一校验与落库口径。
 * 新增维护类写操作请加到本服务，不要在页面里直接调 DAO。
 */
#pragma once

#include <QJsonObject>
#include <QString>

class MaintenanceService {
public:
    /// 调整任务类型下某工具的关联数量
    bool updateTaskTypeToolQty(int typeId, int toolId, int qty);
    /// 新建工具（含全部字段）
    bool createTool(const QJsonObject& tool);
    /// 更新工具（含全部字段）
    bool updateTool(int toolId, const QJsonObject& updates);
    /// 删除工具及其关联的位置映射记录
    bool deleteTool(int toolId);
    /// 新增位置对照关系（默认 pending 状态）
    bool createPositionMapping(int toolId, int cabinetId, const QString& layer,
                               const QString& position, const QString& status);
    /// 删除位置对照关系（位置已占用时拒绝）
    bool removePositionMapping(int mappingId);
};
