/**
 * @file MachineGroup.h
 * @brief 工程机组实体模型 - 映射 machine_group 表
 * @author 袁燕  创建: 2026-06-24
 * @修改说明 为工具管理页面改造提供机组数据支持
 */
#pragma once
#include <QString>
#include <QDateTime>

struct MachineGroup {
    int     groupId      = 0;
    QString groupName;           // 机组名称
    int     deptId       = 0;   // 所属部门ID
    QString leaderName;          // 负责人姓名
    QString leaderPhone;         // 负责人电话
    QString description;         // 机组描述
    QString status       = "active";  // active|inactive
    QDateTime createdAt;
    QDateTime updatedAt;

    // 关联展示字段
    QString deptName;            // 部门名称（JOIN查询用）
    int     toolCount    = 0;    // 工具数量（统计用）
};
