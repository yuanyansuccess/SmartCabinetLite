#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0  部门实体
// 日期：2026-06-21  映射表：sys_department
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
