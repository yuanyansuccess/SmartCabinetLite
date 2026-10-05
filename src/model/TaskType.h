/**
 * @file TaskType.h
 * @brief 任务类型实体，字段对齐 task_type
 * @author 袁燕
 */
#pragma once
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
