#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0  工具柜实体
// 日期：2026-06-21  映射表：tool_cabinet
#include <QString>
#include <QDateTime>

struct ToolCabinet {
    int     cabinetId    = 0;
    QString cabinetName;
    QString cabinetCode;
    QString location;
    QString ipAddress;
    QString status      = "online";
    QDateTime createdAt;
};
