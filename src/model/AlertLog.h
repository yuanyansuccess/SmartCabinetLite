#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0  告警日志实体
// 日期：2026-06-21  [V1.00.8.4] 修复：映射表sys_alert，字段对齐数据库schema
// [2026-06-25] 重构：新增typeId/typeName/status字段，移除alertType/alertLevel(改为JOIN获取)
#include <QString>
#include <QDateTime>

struct AlertLog {
    int     alertId      = 0;
    int     typeId       = 0;    // [2026-06-25] 引用sys_alert_type.type_id
    QString typeCode;           // [2026-06-25] 类型编码(来自JOIN)
    QString typeName;           // [2026-06-25] 类型显示名(来自JOIN)
    QString alertLevel;         // [2026-06-25] 告警级别(来自JOIN sys_alert_type.alert_level)
    int     userId       = 0;
    int     toolId       = 0;
    int     recordId     = 0;
    QString message;           // 对应sys_alert.content
    QString status;            // [2026-06-25] unhandled|handled|ignored (替代isHandled)
    QDateTime handledAt;
    QString handledBy;         // 对应sys_alert.handler_id（显示时转为用户名）
    QDateTime createdAt;

    // 关联展示字段
    QString userName;
    QString realName;
    QString toolCode;
    QString toolName;
    QString flowNo;
};
