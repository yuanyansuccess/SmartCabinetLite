/**
 * @file Department.h
 * @brief 部门实体，字段对齐 sys_department
 * @author 袁燕
 */
#pragma once
#include <QString>
#include <QDateTime>

struct Department {
    int     deptId    = 0;
    QString deptName;
    int     parentId  = 0;
    int     sortOrder = 0;
    int     status    = 1;
    QDateTime createdAt;
    QDateTime updatedAt;
};
