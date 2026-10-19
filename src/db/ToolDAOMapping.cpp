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

/**
 * @brief 查询工具简要列表
 * @return 元素含 toolId、toolCode、toolName 的数组，供下拉框使用
 */
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

/**
 * @brief 查询指定柜体层位是否已存在对照关系
 * @param cabinetId 柜体ID
 * @param layer 层号
 * @param position 位号
 * @return 含 mappingId 与 status 的对象；该位置未配置时返回空对象
 */
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

/**
 * @brief 新增位置对照关系
 * @param toolId 工具ID
 * @param cabinetId 柜体ID
 * @param layer 层号
 * @param position 位号
 * @param status 初始状态，默认待入库
 * @return true=新增成功
 */
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

int ToolDAO::findCabinetIdByPrefix(const QString& prefix)
{
    QSqlQuery q = query("SELECT cabinet_id FROM tool_cabinet "
                        "WHERE cabinet_name LIKE ? ORDER BY cabinet_id LIMIT 1",
                        { prefix + "%" });
    return q.next() ? q.value(0).toInt() : -1;
}

/**
 * @brief 在库位置明细（位置维度，出库下拉用）：一条映射记录=一个在库位置
 * @param machineGroupId 机组编号，用于按机组隔离数据
 * @return 查询结果数组，每个元素为一个对象
 */
QJsonArray ToolDAO::findAllInStockPositions(int machineGroupId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    if (machineGroupId > 0) {
        q.prepare("SELECT m.mapping_id, t.tool_id, t.tool_code, t.tool_name, t.spec, "
               "c.cabinet_name, m.layer, m.position "
               "FROM tool_position_mapping m "
               "JOIN tool_info t ON m.tool_id=t.tool_id "
               "JOIN tool_cabinet c ON m.cabinet_id=c.cabinet_id "
               "WHERE m.status='in_stock' AND t.machine_group_id=? "
               "ORDER BY c.cabinet_name, m.layer, m.position");
        q.addBindValue(machineGroupId);
    } else {
        q.prepare("SELECT m.mapping_id, t.tool_id, t.tool_code, t.tool_name, t.spec, "
               "c.cabinet_name, m.layer, m.position "
               "FROM tool_position_mapping m "
               "JOIN tool_info t ON m.tool_id=t.tool_id "
               "JOIN tool_cabinet c ON m.cabinet_id=c.cabinet_id "
               "WHERE m.status='in_stock' "
               "ORDER BY c.cabinet_name, m.layer, m.position");
    }
    safeExec(q);
    QJsonArray arr;
    while (q.next()) {
        QJsonObject obj;
        obj["mappingId"]   = q.value(0).toInt();
        obj["toolId"]      = q.value(1).toInt();
        obj["toolCode"]    = q.value(2).toString();
        obj["toolName"]    = q.value(3).toString();
        obj["spec"]        = q.value(4).toString();
        obj["cabinetName"] = q.value(5).toString();
        obj["layer"]       = q.value(6).toString();
        obj["position"]    = q.value(7).toString();
        arr.append(obj);
    }
    return arr;
}

/**
 * @brief 统计工具in_stock位置数（迁移自ToolService::checkinTool COUNT查询）
 * @param toolId 工具唯一标识 tool_id
 * @return 返回查询得到的编号或计数（找不到返回 -1 或 0）
 */
int ToolDAO::countInStockPositions(int toolId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM tool_position_mapping WHERE tool_id=? AND status='in_stock'");
    q.addBindValue(toolId);
    if (safeExec(q) && q.next()) return q.value(0).toInt();
    return 0;
}

/**
 * @brief 查询映射表状态（迁移自BorrowService::borrowTool状态校验）
 * @param mappingId 位置映射编号
 * @return 返回生成的字符串（失败时可能为空串）
 */
QString ToolDAO::findMappingStatus(int mappingId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT status FROM tool_position_mapping WHERE mapping_id=?");
    q.addBindValue(mappingId);
    if (safeExec(q) && q.next()) return q.value(0).toString();
    return QString();
}

/**
 * @brief 按位置条件更新映射表状态（原子CAS，防止并发覆盖）
 * @param toolId 工具唯一标识 tool_id
 * @param cabinetId 柜体编号
 * @param layer 层号
 * @param position 位号
 * @param newStatus 要写入的目标状态值
 * @param expectedStatus 期望的当前状态值，与库内不一致则更新失败（乐观锁）
 * @return 操作是否成功（true=成功，false=失败）
 */
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
