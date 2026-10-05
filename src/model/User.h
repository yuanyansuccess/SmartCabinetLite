/**
 * @file User.h
 * @brief 用户实体，字段对齐 sys_user，含角色、状态与人脸特征
 * @author 袁燕
 */
#pragma once
#include <QString>
#include <QDateTime>
#include "common/Constants.h"

struct User {
    int     userId       = 0;
    QString username;
    QString passwordHash;
    QString passwordSalt;
    QString realName;
    QString workNo;
    int     deptId       = 0;
    QString department;
    QString role         = "user";
    QString faceFeature;       // 人脸特征(base64)
    QString phone;
    QString email;
    QString status       = SC::USER_ACTIVE;  // 与Constants.h口径一致
    QDateTime lastLoginAt;
    QDateTime createdAt;
    QDateTime updatedAt;
};
