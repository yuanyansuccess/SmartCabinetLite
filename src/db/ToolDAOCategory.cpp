/**
 * @file ToolDAOCategory.cpp
 * @brief ToolDAO 物理拆分 — 分类与柜体字典
 * @author 袁燕
 *
 * 说明：由 ToolDAO.cpp 按业务域拆分而来，类声明仍在 ToolDAO.h，仅实现分文件。
 * 新增方法请按职责放到对应文件，不要全部堆回 ToolDAO.cpp。
 */
#include "ToolDAO.h"
#include "DatabaseManager.h"
#include "common/PositionFormatter.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include "common/Constants.h"

namespace db {

/**
 * @brief 查询全部工具分类
 * @return 分类实体列表
 */
QList<ToolCategory> ToolDAO::allCategories() {
    QList<ToolCategory> list;
    QSqlQuery q = query("SELECT * FROM tool_category ORDER BY sort_order");
    while (q.next()) {
        ToolCategory category;
        category.categoryId   = q.value("category_id").toInt();
        category.categoryName = q.value("category_name").toString();
        category.parentId     = q.value("parent_id").toInt();
        category.sortOrder    = q.value("sort_order").toInt();
        category.icon         = q.value("icon").toString();
        list.append(category);
    }
    return list;
}

/**
 * @brief 查询全部分类名称
 * @return 分类名称列表
 */
QStringList ToolDAO::allCategoryNames() {
    QStringList names;
    QSqlQuery q = query("SELECT category_name FROM tool_category ORDER BY sort_order");
    while (q.next()) names << q.value(0).toString();
    return names;
}

/**
 * @brief 查询全部柜体
 * @return 柜体实体列表
 */
QList<ToolCabinet> ToolDAO::allCabinets() {
    QList<ToolCabinet> list;
    QSqlQuery q = query("SELECT * FROM tool_cabinet ORDER BY cabinet_id");
    while (q.next()) {
        ToolCabinet cabinet;
        cabinet.cabinetId   = q.value("cabinet_id").toInt();
        cabinet.cabinetName = q.value("cabinet_name").toString();
        cabinet.cabinetCode = q.value("cabinet_code").toString();
        cabinet.location    = q.value("location").toString();
        cabinet.ipAddress   = q.value("ip_address").toString();
        cabinet.status      = q.value("status").toString();
        list.append(cabinet);
    }
    return list;
}

/**
 * @brief 查询全部柜体名称
 * @return 柜体名称列表
 */
QStringList ToolDAO::allCabinetNames() {
    QStringList names;
    QSqlQuery q = query("SELECT cabinet_name FROM tool_cabinet");
    while (q.next()) names << q.value(0).toString();
    return names;
}


// ==============================================================
// 【⑦ 统计与关联查询
//   工具统计、机组查询、在库位置、待入库工具、维护相关查询
// ==============================================================
// 工具统计数据 增加borrowedQty字段，与v_tool_stats新列对齐

} // namespace db
