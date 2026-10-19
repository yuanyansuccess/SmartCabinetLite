/**
 * @file DepartmentDAO.h
 * @brief 部门数据访问对象 — db/层统一namespace
 * @author 袁燕
 * 从 dao/ 迁移到 db/，添加 namespace db 消除与 dao/ 的冗余
 */
#pragma once
#include <QStringList>
#include <QList>
#include <QSqlQuery>
#include "DatabaseManager.h"

namespace db {

class DepartmentDAO {
public:
    DepartmentDAO() = default;

    /// 部门条目：ID 与名称，供界面下拉框同时展示名称、提交 ID
    struct Department {
        int     deptId = 0;
        QString deptName;
    };

    /// 获取所有活跃部门（含 ID，按排序字段排序）
    QList<Department> allDepartments();

    /// 获取所有活跃部门名称（按排序字段排序）
    QStringList allNames();

private:
    QSqlQuery query(const QString& sql) {
        QSqlDatabase d = DatabaseManager::instance().getConnection();
        QSqlQuery q(d);
        q.exec(sql);
        return q;
    }
};

} // namespace db
