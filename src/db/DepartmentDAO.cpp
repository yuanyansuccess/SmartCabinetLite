/**
 * @file DepartmentDAO.cpp
 * @brief 部门数据访问对象实现
 * @author 袁燕
 * 从 dao/ 迁移到 db/，namespace db 包裹
 */
#include "DepartmentDAO.h"

namespace db {

/**
 * @brief 查询全部启用部门（含部门ID与名称）
 * @return 部门列表，按 sort_order 升序；无数据时返回空列表
 */
QList<DepartmentDAO::Department> DepartmentDAO::allDepartments() {
    QList<Department> list;
    QSqlQuery q = query("SELECT dept_id, dept_name FROM sys_department WHERE status=1 ORDER BY sort_order");
    while (q.next()) {
        Department dept;
        dept.deptId   = q.value(0).toInt();
        dept.deptName = q.value(1).toString();
        list.append(dept);
    }
    return list;
}

/**
 * @brief 查询全部部门名称
 * @return 部门名称列表，按 sort_order 升序；无数据时返回空列表
 */
QStringList DepartmentDAO::allNames() {
    QStringList names;
    for (const Department& dept : allDepartments())
        names << dept.deptName;
    return names;
}

} // namespace db
