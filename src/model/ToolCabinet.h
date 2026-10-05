/**
 * @file ToolCabinet.h
 * @brief 柜体实体，字段对齐 tool_cabinet
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
