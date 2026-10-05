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
 * @brief 统计工具总数与各状态数量
 * @return 含 total、inStock、borrowed、checkedOut 等指标的对象
 */
QJsonObject ToolDAO::getToolStats() {
    QJsonObject stats;
    // 统计按映射表status计算
    // 在库 = 映射表status='in_stock'
    // 已借出 = 映射表status='borrowed'
    // 待入库 = 映射表status='pending'
    // 已出库 = tool_info中checked_out状态（不在位置上）
    QSqlQuery q = query(
        "SELECT "
        "  (SELECT COUNT(*) FROM tool_position_mapping WHERE status='in_stock') AS in_stock_count, "
        "  (SELECT COUNT(*) FROM tool_position_mapping WHERE status='borrowed') AS borrowed_count, "
        "  (SELECT COUNT(*) FROM tool_position_mapping WHERE status='pending') AS pending_count, "
        "  (SELECT COUNT(*) FROM tool_info WHERE status='checked_out') AS checked_out_count, "
        "  (SELECT COUNT(*) FROM tool_info WHERE status='maintenance') AS maintenance_count"
    );
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

// 工程机组列表
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

// 机组详情
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
