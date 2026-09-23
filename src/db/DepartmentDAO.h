/**
 * @file DepartmentDAO.h
 * @brief 部门数据访问对象 — db/层统一namespace
 * @author 袁燕
 * [2026-06-23] 从 dao/ 迁移到 db/，添加 namespace db 消除与 dao/ 的冗余
 */
#pragma once
#include <QStringList>
#include <QSqlQuery>
#include "DatabaseManager.h"

namespace db {

class DepartmentDAO {
public:
    DepartmentDAO() = default;

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
