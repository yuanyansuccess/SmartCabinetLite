/**
 * @file  ProtocolAlertTypes.h
 * @brief 与边缘计算机通信接口的 alert_type 协议枚举，上位机校验与模拟器下拉共用
 * @author 袁燕
 */
#pragma once
#include <QString>
#include <QStringList>

namespace Proto {

inline const QString ALERT_TYPE_NONE           = QStringLiteral("none");
inline const QString ALERT_TYPE_MISPLACED      = QStringLiteral("misplaced");
inline const QString ALERT_TYPE_UNKNOWN        = QStringLiteral("unknown");
inline const QString ALERT_TYPE_MISSING        = QStringLiteral("missing");
inline const QString ALERT_TYPE_LOW_CONFIDENCE = QStringLiteral("low_confidence");

/// 协议 3.3 认可的告警类型取值，顺序与界面下拉一致
inline const QStringList ALERT_TYPE_VALUES = {
    ALERT_TYPE_NONE, ALERT_TYPE_MISPLACED, ALERT_TYPE_UNKNOWN,
    ALERT_TYPE_MISSING, ALERT_TYPE_LOW_CONFIDENCE
};

/// 与 ALERT_TYPE_VALUES 一一对应的中文说明，供界面下拉展示
inline const QStringList ALERT_TYPE_LABELS = {
    QStringLiteral("无告警 (none)"),
    QStringLiteral("错放 (misplaced)"),
    QStringLiteral("未知物 (unknown)"),
    QStringLiteral("缺失 (missing)"),
    QStringLiteral("置信度不足 (low_confidence)")
};

}  // namespace Proto
