/**
 * @file User.h
 * @brief 用户实体，字段与 sys_user 表对应，含角色、状态与人脸特征
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
    /// 部门名称：只读展示字段，由 dept_id 关联 sys_department 得到，不写入用户表
    QString department;
    QString role         = SC::ROLE_USER;  // 角色走 SC:: 常量，与 Constants.h 口径一致
    int     mustChangePwd = 0;    // 1=下次登录强制修改初始密码（新建/重置时置1，改密成功后清0）
    QString faceFeature;       // 人脸特征(base64)
    QString phone;
    QString email;
    QString status       = SC::USER_ACTIVE;  // 与Constants.h口径一致
    QDateTime lastLoginAt;
    QDateTime createdAt;
    QDateTime updatedAt;
};
