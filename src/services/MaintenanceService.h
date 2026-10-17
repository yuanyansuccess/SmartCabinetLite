/**
 * @file MaintenanceService.h
 * @brief 维护域服务 - 任务配置/工具维护/位置对照关系的写操作编排
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
};
