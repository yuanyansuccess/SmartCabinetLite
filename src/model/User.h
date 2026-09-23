#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0  用户实体
// 日期：2026-06-21  映射表：sys_user
#include <QString>
#include <QDateTime>

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
    QString status       = "active";
    QDateTime lastLoginAt;
    QDateTime createdAt;
    QDateTime updatedAt;
};
