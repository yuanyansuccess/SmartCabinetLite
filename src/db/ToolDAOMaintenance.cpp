/**
 * @file ToolDAOMaintenance.cpp
 * @brief ToolDAO 物理拆分 — 维护页专用（任务类型/工具对照/完整增删改）
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

QJsonArray ToolDAO::allTaskTypes()
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT type_id, type_name FROM task_type WHERE is_active=1 ORDER BY sort_order");
    safeExec(q);
    QJsonArray arr;
    while (q.next()) {
        QJsonObject obj;
        obj["typeId"]   = q.value(0).toInt();
        obj["typeName"] = q.value(1).toString();
        arr.append(obj);
    }
    return arr;
}

QJsonArray ToolDAO::findTaskTypeTools(int typeId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT ttt.tool_id, ti.tool_name, ti.tool_code, ti.spec, ttt.recommended_qty "
              "FROM task_type_tool ttt JOIN tool_info ti ON ttt.tool_id=ti.tool_id "
              "WHERE ttt.type_id=? ORDER BY ti.tool_name");
    q.addBindValue(typeId);
    safeExec(q);
    QJsonArray arr;
    while (q.next()) {
        QJsonObject obj;
        obj["toolId"]         = q.value(0).toInt();
        obj["toolName"]       = q.value(1).toString();
        obj["toolCode"]       = q.value(2).toString();
        obj["spec"]           = q.value(3).toString();
        obj["recommendedQty"] = q.value(4).toInt();
        arr.append(obj);
    }
    return arr;
}

bool ToolDAO::updateTaskTypeToolQty(int typeId, int toolId, int qty)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("UPDATE task_type_tool SET recommended_qty=? WHERE type_id=? AND tool_id=?");
    q.addBindValue(qty);
    q.addBindValue(typeId);
    q.addBindValue(toolId);
    return safeExec(q);
}

bool ToolDAO::checkTaskTypeToolExists(int typeId, int toolId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT 1 FROM task_type_tool WHERE type_id=? AND tool_id=?");
    q.addBindValue(typeId);
    q.addBindValue(toolId);
    safeExec(q);
    return q.next();
}

bool ToolDAO::addTaskTypeTool(int typeId, int toolId, int qty)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("INSERT INTO task_type_tool (type_id, tool_id, recommended_qty) VALUES (?, ?, ?)");
    q.addBindValue(typeId);
    q.addBindValue(toolId);
    q.addBindValue(qty);
    return safeExec(q);
}

bool ToolDAO::deleteTaskTypeTool(int typeId, int toolId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("DELETE FROM task_type_tool WHERE type_id=? AND tool_id=?");
    q.addBindValue(typeId);
    q.addBindValue(toolId);
    return safeExec(q);
}

// ═══════════════════════════════════════════════
// 系统维护页：工具维护
// ═══════════════════════════════════════════════

QJsonArray ToolDAO::allToolsForMaintenance()
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT t.tool_id, t.tool_code, t.tool_name, c.category_name, t.spec, t.unit, t.status "
           "FROM tool_info t LEFT JOIN tool_category c ON t.category_id=c.category_id "
           "ORDER BY t.tool_id");
    safeExec(q);
    QJsonArray arr;
    while (q.next()) {
        QJsonObject obj;
        obj["toolId"]       = q.value(0).toInt();
        obj["toolCode"]     = q.value(1).toString();
        obj["toolName"]     = q.value(2).toString();
        obj["categoryName"] = q.value(3).toString();
        obj["spec"]         = q.value(4).toString();
        obj["unit"]         = q.value(5).toString();
        obj["status"]       = q.value(6).toString();
        arr.append(obj);
    }
    return arr;
}

QJsonObject ToolDAO::findToolForEdit(int toolId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT tool_name, tool_code, category_id, spec, unit, supplier, recognition_method, document_path "
              "FROM tool_info WHERE tool_id=?");
    q.addBindValue(toolId);
    safeExec(q);
    QJsonObject obj;
    if (q.next()) {
        obj["toolName"]          = q.value(0).toString();
        obj["toolCode"]          = q.value(1).toString();
        obj["categoryId"]        = q.value(2).toInt();
        obj["spec"]              = q.value(3).toString();
        obj["unit"]              = q.value(4).toString();
        obj["supplier"]          = q.value(5).toString();
        obj["recognitionMethod"] = q.value(6).toString();
        obj["documentPath"]      = q.value(7).toString();
    }
    return obj;
}

bool ToolDAO::insertToolFull(const QJsonObject& tool)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("INSERT INTO tool_info (tool_name, tool_code, category_id, spec, unit, supplier, "
              "recognition_method, document_path, status, total_qty, current_qty, machine_group_id) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'pending', 1, 1, ?)");
    q.addBindValue(tool["toolName"].toString());
    q.addBindValue(tool["toolCode"].toString());
    q.addBindValue(tool["categoryId"].toInt());
    q.addBindValue(tool["spec"].toString());
    q.addBindValue(tool["unit"].toString());
    q.addBindValue(tool["supplier"].toString());
    q.addBindValue(tool["recognitionMethod"].toString());
    q.addBindValue(tool["documentPath"].toString());
    q.addBindValue(tool["machineGroupId"].toInt());
    return safeExec(q);
}

bool ToolDAO::updateToolFull(int toolId, const QJsonObject& updates)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("UPDATE tool_info SET tool_name=?, tool_code=?, category_id=?, spec=?, unit=?, "
              "supplier=?, recognition_method=?, document_path=? WHERE tool_id=?");
    q.addBindValue(updates["toolName"].toString());
    q.addBindValue(updates["toolCode"].toString());
    q.addBindValue(updates["categoryId"].toInt());
    q.addBindValue(updates["spec"].toString());
    q.addBindValue(updates["unit"].toString());
    q.addBindValue(updates["supplier"].toString());
    q.addBindValue(updates["recognitionMethod"].toString());
    q.addBindValue(updates["documentPath"].toString());
    q.addBindValue(toolId);
    return safeExec(q);
}

bool ToolDAO::deleteToolFully(int toolId)
{
    QSqlDatabase db = getDb();
    // 先删除映射表记录
    QSqlQuery delMapping(db);
    delMapping.prepare("DELETE FROM tool_position_mapping WHERE tool_id=?");
    delMapping.addBindValue(toolId);
    safeExec(delMapping);
    // 再删除工具记录
    QSqlQuery q(db);
    q.prepare("DELETE FROM tool_info WHERE tool_id=?");
    q.addBindValue(toolId);
    return safeExec(q);
}

// ═══════════════════════════════════════════════
// 系统维护页：位置对照
// ═══════════════════════════════════════════════

} // namespace db
