#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0  任务类型实体
// 日期：2026-06-21  [V1.00.8.4] 修复：映射表task_type，字段对齐数据库schema
#include <QString>
#include <QDateTime>

struct TaskType {
    int     taskTypeId   = 0;   // 对应task_type.type_id
    QString taskName;           // 对应task_type.type_name
    QString taskCode;           // 对应task_type.type_code
    QString description;
    int     defaultDuration = 30; // 分钟（数据库中无此字段，业务层默认值）
    int     sortOrder    = 0;
    int     status       = 1;   // 对应task_type.is_active
    QDateTime createdAt;
};
