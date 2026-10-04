/**
 * @file ToolDAOPosition.cpp
 * @brief ToolDAO 物理拆分 — 位置映射查询（位置维度铁律）
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

QJsonArray ToolDAO::findInStockPositions(int toolId, int limit)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT mpm.mapping_id, mpm.cabinet_id, mpm.layer, mpm.position, "
              "cb.cabinet_name "
              "FROM tool_position_mapping mpm "
              "JOIN tool_cabinet cb ON mpm.cabinet_id=cb.cabinet_id "
              "WHERE mpm.tool_id=? AND mpm.status='in_stock' "
              "ORDER BY cb.cabinet_name, mpm.layer, mpm.position LIMIT ?");
    q.addBindValue(toolId);
    q.addBindValue(limit);
    safeExec(q);
    QJsonArray arr;
    while (q.next()) {
        QJsonObject obj;
        obj["mappingId"]   = q.value(0).toInt();
        obj["cabinetId"]   = q.value(1).toInt();
        obj["layer"]       = q.value(2).toString();
        obj["position"]    = q.value(3).toString();
        obj["cabinetName"] = q.value(4).toString();
        arr.append(obj);
    }
    return arr;
}

// 更新映射表status（checked_out/pending/in_stock等）
bool ToolDAO::updateMappingStatus(int mappingId, const QString& status)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("UPDATE tool_position_mapping SET status=? WHERE mapping_id=?");
    q.addBindValue(status);
    q.addBindValue(mappingId);
    if (!safeExec(q)) {
        return false;
    }
    return true;
}

// 查找待入库工具（映射表中有status='pending'空闲位置）
QJsonArray ToolDAO::findPendingTools(int categoryId, int machineGroupId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    if (categoryId <= 0) {
        q.prepare(
            "SELECT ti.tool_id, ti.tool_code, ti.tool_name, ti.status, "
            "(SELECT COUNT(*) FROM tool_position_mapping mpm "
            " WHERE mpm.tool_id=ti.tool_id AND mpm.status='pending') AS free_pos_count "
            "FROM tool_info ti "
            "WHERE ti.machine_group_id=? "
            "  AND ti.status IN ('pending','in_stock','checked_out') "
            "  AND EXISTS (SELECT 1 FROM tool_position_mapping m WHERE m.tool_id=ti.tool_id AND m.status='pending') "
            "ORDER BY ti.tool_name");
        q.addBindValue(machineGroupId);
    } else {
        q.prepare(
            "SELECT ti.tool_id, ti.tool_code, ti.tool_name, ti.status, "
            "(SELECT COUNT(*) FROM tool_position_mapping mpm "
            " WHERE mpm.tool_id=ti.tool_id AND mpm.status='pending') AS free_pos_count "
            "FROM tool_info ti "
            "WHERE ti.category_id=? AND ti.machine_group_id=? "
            "  AND ti.status IN ('pending','in_stock','checked_out') "
            "  AND EXISTS (SELECT 1 FROM tool_position_mapping m WHERE m.tool_id=ti.tool_id AND m.status='pending') "
            "ORDER BY ti.tool_name");
        q.addBindValue(categoryId);
        q.addBindValue(machineGroupId);
    }
    safeExec(q);
    QJsonArray arr;
    while (q.next()) {
        QJsonObject obj;
        obj["toolId"]        = q.value(0).toInt();
        obj["toolCode"]      = q.value(1).toString();
        obj["toolName"]      = q.value(2).toString();
        obj["status"]        = q.value(3).toString();
        obj["freePosCount"]  = q.value(4).toInt();
        arr.append(obj);
    }
    return arr;
}

// 查找工具基本信息
QJsonObject ToolDAO::findToolBasicInfo(int toolId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT tool_name, tool_code, spec FROM tool_info WHERE tool_id=?");
    q.addBindValue(toolId);
    safeExec(q);
    QJsonObject obj;
    if (q.next()) {
        obj["toolName"] = q.value(0).toString();
        obj["toolCode"] = q.value(1).toString();
        obj["spec"]     = q.value(2).toString();
    }
    return obj;
}

// 查找工具的待入库位置（映射表status='pending'）
QJsonArray ToolDAO::findPendingPositions(int toolId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare(
        "SELECT mpm.cabinet_id, mpm.layer, mpm.position, "
        "       cb.cabinet_name, cb.cabinet_code "
        "FROM tool_position_mapping mpm "
        "JOIN tool_cabinet cb ON mpm.cabinet_id = cb.cabinet_id "
        "WHERE mpm.tool_id=? AND mpm.status='pending' "
        "ORDER BY cb.cabinet_name, mpm.layer, mpm.position");
    q.addBindValue(toolId);
    safeExec(q);
    QJsonArray arr;
    while (q.next()) {
        QJsonObject obj;
        obj["cabinetId"]   = q.value(0).toInt();
        obj["layer"]       = q.value(1).toString();
        obj["position"]    = q.value(2).toString();
        obj["cabinetName"] = q.value(3).toString();
        obj["cabinetCode"] = q.value(4).toString();
        arr.append(obj);
    }
    return arr;
}

// ═══════════════════════════════════════════════
// 系统维护页：任务配置
// ═══════════════════════════════════════════════

} // namespace db
