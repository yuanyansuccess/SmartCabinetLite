/**
 * @file ToolCabinet.h
 * @brief 柜体实体，字段与 tool_cabinet 表对应
 * @author 袁燕
 */
#pragma once
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
