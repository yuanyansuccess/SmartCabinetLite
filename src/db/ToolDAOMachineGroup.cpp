/**
 * @file ToolDAOMachineGroup.cpp
 * @brief ToolDAO 物理拆分 — 统计与机组
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
 * @brief 统计工具总数与各状态数量（全系统"工具数量"的唯一取数入口，各页面共用同一口径）
 * @param machineGroupId 机组编号：>0 只统计该机组的工具，<=0 统计全库
 * @return 含 inStockCount/borrowedCount/pendingCount/checkedOutCount/maintenanceCount/totalCount 的对象
 * @note  口径统一按位置维度（映射表是位置占用的权威数据源）：
 *        在库=映射表 in_stock 位置数；已借出=映射表 borrowed 位置数；待入库=映射表 pending 位置数；
 *        已出库/维护中取 tool_info 状态（这两类工具不在位置上）；
 *        工具总数=在库+已借出+待入库+已出库（不含维护中）
 */
QJsonObject ToolDAO::getToolStats(int machineGroupId) {
    QJsonObject stats;
    // 位置维度统计统一按机组过滤：不传机组（<=0）时统计全库，与页面"全部机组"的口径一致
    const QString posGroupCond = machineGroupId > 0
        ? QStringLiteral(" JOIN tool_info gti ON gti.tool_id = m.tool_id AND gti.machine_group_id = ?")
        : QString();
    const QString toolGroupCond = machineGroupId > 0
        ? QStringLiteral(" WHERE machine_group_id = ?") : QStringLiteral(" WHERE 1=1");

    QString sql = QStringLiteral(
        "SELECT "
        "  (SELECT COUNT(*) FROM tool_position_mapping m%1 WHERE m.status='in_stock') AS in_stock_count, "
        "  (SELECT COUNT(*) FROM tool_position_mapping m%1 WHERE m.status='borrowed') AS borrowed_count, "
        "  (SELECT COUNT(*) FROM tool_position_mapping m%1 WHERE m.status='pending')  AS pending_count, "
        "  (SELECT COUNT(*) FROM tool_info%2 AND status='checked_out')  AS checked_out_count, "
        "  (SELECT COUNT(*) FROM tool_info%2 AND status='maintenance') AS maintenance_count"
    ).arg(posGroupCond, toolGroupCond);

    // 统一走 BaseDAO 的 query()：内部取带数据库连接的查询对象，并按位置顺序绑定 ? 占位符
    QVariantList params;
    if (machineGroupId > 0) {
        // 位置维度 3 处 + tool_info 2 处，按顺序各绑定一次机组编号
        for (int i = 0; i < 5; ++i) {
            params << machineGroupId;
        }
    }
    QSqlQuery q = query(sql, params);
    if (q.next()) {
        int inStock = q.value("in_stock_count").toInt();
        int borrowed = q.value("borrowed_count").toInt();
        int pending = q.value("pending_count").toInt();
        int checkedOut = q.value("checked_out_count").toInt();
        int maintenance = q.value("maintenance_count").toInt();
        stats["inStockCount"]     = inStock;
        stats["borrowedCount"]    = borrowed;
        stats["pendingCount"]     = pending;
        stats["checkedOutCount"]  = checkedOut;
        stats["maintenanceCount"] = maintenance;
        stats["totalCount"]       = inStock + borrowed + pending + checkedOut;
    }
    return stats;
}

/**
 * @brief 查询工程全部机组列表（含关联部门与工具计数）
 * @return 机组信息对象列表
 */
QList<QJsonObject> ToolDAO::allMachineGroups() {
    QList<QJsonObject> list;
    QSqlQuery q = query(
        "SELECT mg.*, d.dept_name, "
        "(SELECT COUNT(*) FROM tool_info ti WHERE ti.machine_group_id = mg.group_id) AS tool_count "
        "FROM machine_group mg "
        "LEFT JOIN sys_department d ON mg.dept_id = d.dept_id "
        "ORDER BY mg.group_id"
    );
    while (q.next()) {
        QJsonObject obj;
        obj["groupId"]      = q.value("group_id").toInt();
        obj["groupName"]    = q.value("group_name").toString();
        obj["deptId"]       = q.value("dept_id").toInt();
        obj["deptName"]     = q.value("dept_name").toString();
        obj["leaderName"]   = q.value("leader_name").toString();
        obj["leaderPhone"]  = q.value("leader_phone").toString();
        obj["description"]  = q.value("description").toString();
        obj["status"]       = q.value("status").toString();
        obj["toolCount"]    = q.value("tool_count").toInt();
        obj["createdAt"]    = q.value("created_at").toString();
        list.append(obj);
    }
    return list;
}

/**
 * @brief 查询单个机组详情（含关联部门与工具计数）
 * @param groupId 机组ID
 * @return 机组信息对象
 */
QJsonObject ToolDAO::getMachineGroupById(int groupId) {
    QJsonObject obj;
    QSqlQuery q = query(
        "SELECT mg.*, d.dept_name, "
        "(SELECT COUNT(*) FROM tool_info ti WHERE ti.machine_group_id = mg.group_id) AS tool_count "
        "FROM machine_group mg "
        "LEFT JOIN sys_department d ON mg.dept_id = d.dept_id "
        "WHERE mg.group_id = ?", {groupId}
    );
    if (q.next()) {
        obj["groupId"]      = q.value("group_id").toInt();
        obj["groupName"]    = q.value("group_name").toString();
        obj["deptId"]       = q.value("dept_id").toInt();
        obj["deptName"]     = q.value("dept_name").toString();
        obj["leaderName"]   = q.value("leader_name").toString();
        obj["leaderPhone"]  = q.value("leader_phone").toString();
        obj["description"]  = q.value("description").toString();
        obj["status"]       = q.value("status").toString();
        obj["toolCount"]    = q.value("tool_count").toInt();
    }
    return obj;
}

// 查询映射表中in_stock位置，返回 [{mappingId, cabinetId, layer, position, cabinetName}, ...]

} // namespace db
