/**
 * @file DepartmentDAO.cpp
 * @brief 部门数据访问对象实现
 * @author 袁燕
 * [2026-06-23] 从 dao/ 迁移到 db/，namespace db 包裹
 */
#include "DepartmentDAO.h"

namespace db {

QStringList DepartmentDAO::allNames() {
    QStringList names;
    QSqlQuery q = query("SELECT dept_name FROM sys_department WHERE status=1 ORDER BY sort_order");
    while (q.next()) names << q.value(0).toString();
    return names;
}

} // namespace db
