/**
 * @file DepartmentDAO.cpp
 * @brief 部门数据访问对象实现
 * @author 袁燕
 * 从 dao/ 迁移到 db/，namespace db 包裹
 */
#include "DepartmentDAO.h"

namespace db {

/**
 * @brief 查询全部部门名称
 * @return 部门名称列表
 */
QStringList DepartmentDAO::allNames() {
    QStringList names;
    QSqlQuery q = query("SELECT dept_name FROM sys_department WHERE status=1 ORDER BY sort_order");
    while (q.next()) names << q.value(0).toString();
    return names;
}

} // namespace db
