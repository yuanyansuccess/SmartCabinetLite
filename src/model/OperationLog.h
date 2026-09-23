#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0  操作日志实体
// 日期：2026-06-21  映射表：operation_log
#include <QString>
#include <QDateTime>

struct OperationLog {
    int     logId        = 0;
    int     userId       = 0;
    QString operation;         // login/logout/borrow/return/checkin/checkout/user_manage/system_config
    QString targetType;        // user/tool/record/cabinet/system
    int     targetId     = 0;
    QString detail;
    QString ipAddress;
    QDateTime createdAt;

    QString userName;
    QString realName;
};
