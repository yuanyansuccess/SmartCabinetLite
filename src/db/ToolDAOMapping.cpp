/**
 * @file ToolDAOMapping.cpp
 * @brief ToolDAO 物理拆分 — 位置映射写操作与按名查ID辅助
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

QJsonArray ToolDAO::allToolsSimple()
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT tool_id, tool_code, tool_name FROM tool_info ORDER BY tool_id");
    safeExec(q);
    QJsonArray arr;
    while (q.next()) {
        QJsonObject obj;
        obj["toolId"]   = q.value(0).toInt();
        obj["toolCode"] = q.value(1).toString();
        obj["toolName"] = q.value(2).toString();
        arr.append(obj);
    }
    return arr;
}

QJsonObject ToolDAO::checkPositionMappingExists(int cabinetId, const QString& layer, const QString& position)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT m.mapping_id, t.tool_name FROM tool_position_mapping m "
              "JOIN tool_info t ON m.tool_id=t.tool_id "
              "WHERE m.cabinet_id=? AND m.layer=? AND m.position=?");
    q.addBindValue(cabinetId);
    q.addBindValue(layer);
    q.addBindValue(position);
    safeExec(q);
    QJsonObject obj;
    if (q.next()) {
        obj["mappingId"] = q.value(0).toInt();
        obj["occupier"]  = q.value(1).toString();
    }
    return obj;
}

bool ToolDAO::insertPositionMapping(int toolId, int cabinetId, const QString& layer, const QString& position, const QString& status)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("INSERT INTO tool_position_mapping (tool_id, cabinet_id, layer, position, status) VALUES (?, ?, ?, ?, ?)");
    q.addBindValue(toolId);
    q.addBindValue(cabinetId);
    q.addBindValue(layer);
    q.addBindValue(position);
    q.addBindValue(status);
    return safeExec(q);
}

QJsonObject ToolDAO::findPositionMappingDetail(int mappingId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT m.cabinet_id, m.layer, m.position, t.tool_name, t.status "
              "FROM tool_position_mapping m JOIN tool_info t ON m.tool_id=t.tool_id "
              "WHERE m.mapping_id=?");
    q.addBindValue(mappingId);
    safeExec(q);
    QJsonObject obj;
    if (q.next()) {
        obj["cabinetId"] = q.value(0).toInt();
        obj["layer"]     = q.value(1).toString();
        obj["position"]  = q.value(2).toString();
        obj["toolName"]  = q.value(3).toString();
        obj["toolStatus"] = q.value(4).toString();
    }
    return obj;
}

bool ToolDAO::isPositionMappingOccupied(int mappingId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT m.status FROM tool_position_mapping m WHERE m.mapping_id=? AND m.status IN ('in_stock','borrowed')");
    q.addBindValue(mappingId);
    safeExec(q);
    return q.next();
}

bool ToolDAO::deletePositionMapping(int mappingId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("DELETE FROM tool_position_mapping WHERE mapping_id=?");
    q.addBindValue(mappingId);
    return safeExec(q);
}

QJsonArray ToolDAO::findAllPositionMappings()
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT m.mapping_id, t.tool_name, t.tool_code, "
           "c.cabinet_name, m.layer, m.position, "
           "m.status AS position_status "
           "FROM tool_position_mapping m "
           "JOIN tool_info t ON m.tool_id=t.tool_id "
           "JOIN tool_cabinet c ON m.cabinet_id=c.cabinet_id "
           "ORDER BY c.cabinet_name, m.layer, m.position");
    safeExec(q);
    QJsonArray arr;
    while (q.next()) {
        QJsonObject obj;
        obj["mappingId"]      = q.value(0).toInt();
        obj["toolName"]       = q.value(1).toString();
        obj["toolCode"]       = q.value(2).toString();
        obj["cabinetName"]    = q.value(3).toString();
        obj["layer"]          = q.value(4).toString();
        obj["position"]       = q.value(5).toString();
        obj["positionStatus"] = q.value(6).toString();
        arr.append(obj);
    }
    return arr;
}

// 按分类名查询category_id，找不到返回-1
int ToolDAO::findCategoryIdByName(const QString& name)
{
    if (name.isEmpty()) return -1;
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT category_id FROM tool_category WHERE category_name=? LIMIT 1");
    q.addBindValue(name);
    if (safeExec(q) && q.next()) return q.value(0).toInt();
    return -1;
}

// 按柜体名查询cabinet_id，找不到返回-1
int ToolDAO::findCabinetIdByName(const QString& name)
{
    if (name.isEmpty()) return -1;
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT cabinet_id FROM tool_cabinet WHERE cabinet_name=? LIMIT 1");
    q.addBindValue(name);
    if (safeExec(q) && q.next()) return q.value(0).toInt();
    return -1;
}

// 统计工具in_stock位置数（迁移自ToolService::checkinTool COUNT查询）
int ToolDAO::countInStockPositions(int toolId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM tool_position_mapping WHERE tool_id=? AND status='in_stock'");
    q.addBindValue(toolId);
    if (safeExec(q) && q.next()) return q.value(0).toInt();
    return 0;
}

// 查询映射表状态（迁移自BorrowService::borrowTool状态校验）
QString ToolDAO::findMappingStatus(int mappingId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT status FROM tool_position_mapping WHERE mapping_id=?");
    q.addBindValue(mappingId);
    if (safeExec(q) && q.next()) return q.value(0).toString();
    return QString();
}

// 按位置条件更新映射表状态（原子CAS，防止并发覆盖）
// 入参：toolId工具ID, cabinetId柜体ID, layer层, position位, newStatus目标状态, expectedStatus期望当前状态
// 返回：true=更新成功（匹配到1行），false=更新失败（无匹配行或状态已变更）
bool ToolDAO::updateMappingByPosition(int toolId, int cabinetId, const QString& layer,
                                       const QString& position, const QString& newStatus,
                                       const QString& expectedStatus)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("UPDATE tool_position_mapping SET status=? "
              "WHERE tool_id=? AND cabinet_id=? AND layer=? AND position=? AND status=?");
    q.addBindValue(newStatus);
    q.addBindValue(toolId);
    q.addBindValue(cabinetId);
    q.addBindValue(layer);
    q.addBindValue(position);
    q.addBindValue(expectedStatus);
    return safeExec(q) && q.numRowsAffected() > 0;
}

} // namespace db
