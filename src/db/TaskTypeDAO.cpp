/**
 * @file TaskTypeDAO.cpp
 * @brief 任务类型数据访问对象实现 — 合并QJsonObject API + 实体类API
 * @author 袁燕
 * 合并dao/TaskTypeDAO.cpp的实体类API到此文件，统一namespace db管理
 */
#include "TaskTypeDAO.h"
#include "DatabaseManager.h"
#include "common/PositionFormatter.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

namespace db {

// ═══════════════════════════════════════════════
// 实体类转换 — 从dao/TaskTypeDAO.cpp合并
// ═══════════════════════════════════════════════
/**
 * @brief 查询结果行转换为任务类型实体
 * @param q 已定位到有效行的查询结果
 * @return 任务类型实体
 */
TaskType TaskTypeDAO::fromQuery(const QSqlQuery& q) {
    TaskType t;
    t.taskTypeId      = q.value("type_id").toInt();
    t.taskName        = q.value("type_name").toString();
    t.taskCode        = q.value("type_code").toString();
    t.description     = q.value("description").toString();
    t.defaultDuration = 30;
    t.sortOrder       = q.value("sort_order").toInt();
    t.status          = q.value("is_active").toInt();
    t.createdAt       = q.value("created_at").toDateTime();
    return t;
}

// ═══════════════════════════════════════════════
// QJsonObject API（Service层使用）
// ═══════════════════════════════════════════════
/**
 * @brief 查询全部启用的任务类型
 * @return 任务类型数组
 */
QJsonArray TaskTypeDAO::findAllActive() {
    QSqlDatabase db = getDb(); QSqlQuery q(db);
    q.prepare("SELECT type_id, type_code, type_name, description FROM task_type WHERE is_active=1 ORDER BY sort_order");
    safeExec(q);
    QJsonArray arr;
    while (q.next()) {
        QJsonObject o;
        o["typeId"] = q.value("type_id").toInt();
        o["typeCode"] = q.value("type_code").toString();
        o["typeName"] = q.value("type_name").toString();
        o["description"] = q.value("description").toString();
        arr.append(o);
    }
    return arr;
}

/**
 * @brief 按任务类型查询推荐工具
 * @param typeIds 任务类型ID列表
 * @param machineGroupId 机组ID，用于限定只返回本机组工具
 * @return 工具数组
 */
QJsonArray TaskTypeDAO::findRecommendedToolsByTypes(const QList<int>& typeIds, int machineGroupId) {
    if (typeIds.isEmpty()) return QJsonArray();
    QSqlDatabase db = getDb();
    QStringList phs; QMap<QString, QVariant> binds;
    for (int i = 0; i < typeIds.size(); ++i) {
        QString ph = ":t" + QString::number(i);
        phs << ph; binds[ph] = typeIds[i];
    }
    // 按工具种类查询（去掉位置JOIN，加availableQty）
    // 借用列表按工具种类显示，推荐工具自动选中，借用时自动分配位置
    // availableQty=映射表in_stock位置数（用户可选的借用数量上限）
    // 位置只从 tool_position_mapping 取（任选一个 in_stock 位置作代表），不再依赖 tool_info 冗余列
    QString sql = "SELECT DISTINCT tt.tool_id, ti.tool_name, ti.tool_code, ti.spec, ti.total_qty, ti.current_qty, tt.recommended_qty, "
                  "ti.unit, "
                  "(SELECT mpm_cb.cabinet_name FROM tool_position_mapping mpm JOIN tool_cabinet mpm_cb ON mpm.cabinet_id=mpm_cb.cabinet_id WHERE mpm.tool_id=ti.tool_id AND mpm.status='in_stock' LIMIT 1) AS cabinet_name, "
                  "(SELECT mpm.layer FROM tool_position_mapping mpm WHERE mpm.tool_id=ti.tool_id AND mpm.status='in_stock' LIMIT 1) AS layer, "
                  "(SELECT mpm.position FROM tool_position_mapping mpm WHERE mpm.tool_id=ti.tool_id AND mpm.status='in_stock' LIMIT 1) AS position, "
                  "(SELECT COUNT(*) FROM tool_position_mapping m WHERE m.tool_id=ti.tool_id AND m.status='in_stock') AS available_qty "
                  "FROM task_type_tool tt JOIN tool_info ti ON tt.tool_id=ti.tool_id "
                  "WHERE tt.type_id IN (" + phs.join(",") + ") "
                  "AND EXISTS(SELECT 1 FROM tool_position_mapping m WHERE m.tool_id=ti.tool_id AND m.status='in_stock') ";
    if (machineGroupId > 0) {
        sql += " AND ti.machine_group_id = :mgid ";
        binds[":mgid"] = machineGroupId;
    }
    sql += " ORDER BY ti.tool_name";
    QSqlQuery q(db);
    q.prepare(sql);
    for (auto it = binds.begin(); it != binds.end(); ++it) q.bindValue(it.key(), it.value());
    safeExec(q);
    QJsonArray arr;
    while (q.next()) {
        QJsonObject o;
        o["toolId"] = q.value("tool_id").toInt();
        o["toolName"] = q.value("tool_name").toString();
        o["toolCode"] = q.value("tool_code").toString();
        o["spec"] = q.value("spec").toString();
        o["totalQty"] = q.value("total_qty").toInt();
        o["currentQty"] = q.value("current_qty").toInt();
        o["recommendedQty"] = q.value("recommended_qty").toInt();
        o["availableQty"] = q.value("available_qty").toInt();  // 可用位置数
        o["unit"] = q.value("unit").toString().isEmpty() ? QStringLiteral("件") : q.value("unit").toString();
        // 位置（tool_info的位置，借用时自动分配具体位置）
        QString cabName = q.value("cabinet_name").toString();
        QString layerStr = q.value("layer").toString();
        QString posStr = q.value("position").toString();
        o["position"] = common::formatPosition(cabName, layerStr, posStr);
        arr.append(o);
    }
    return arr;
}

// ═══════════════════════════════════════════════
// 实体类API（Controller层使用）— 从dao/TaskTypeDAO.cpp合并
// ═══════════════════════════════════════════════
/**
 * @brief 查询全部已启用的任务类型
 * @return 任务类型列表（按 sort_order 升序）
 */
QList<TaskType> TaskTypeDAO::findAll() {
    QList<TaskType> list;
    QSqlQuery q = query("SELECT * FROM task_type WHERE is_active=1 ORDER BY sort_order");
    while (q.next()) list.append(fromQuery(q));
    return list;
}

/**
 * @brief 按任务类型查询本机组在库工具（位置维度，每个在库位置一行）
 * @param typeId 任务类型ID
 * @param machineGroupId 机组ID，大于 0 时启用机组隔离
 * @return 在库工具对象数组，每项含 toolName/toolCode/cabinetName/layer/position
 */
QJsonArray TaskTypeDAO::findInStockToolsByType(int typeId, int machineGroupId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    QString sql =
        "SELECT ti.tool_name, ti.tool_code, COALESCE(cb.cabinet_name,'') AS cabinet_name, "
        "       m.layer, m.position "
        "FROM task_type_tool tt "
        "JOIN tool_info ti ON tt.tool_id = ti.tool_id "
        "JOIN tool_position_mapping m ON m.tool_id = ti.tool_id AND m.status = 'in_stock' "
        "LEFT JOIN tool_cabinet cb ON m.cabinet_id = cb.cabinet_id "
        "WHERE tt.type_id = :tid ";
    if (machineGroupId > 0) sql += " AND ti.machine_group_id = :mgid ";
    sql += " ORDER BY m.cabinet_id, m.layer, m.position";
    q.prepare(sql);
    q.bindValue(":tid", typeId);
    if (machineGroupId > 0) q.bindValue(":mgid", machineGroupId);
    safeExec(q);
    QJsonArray arr;
    while (q.next()) {
        QJsonObject o;
        o["toolName"]    = q.value("tool_name").toString();
        o["toolCode"]    = q.value("tool_code").toString();
        o["cabinetName"] = q.value("cabinet_name").toString();
        o["layer"]       = q.value("layer").toString();
        o["position"]    = q.value("position").toString();
        arr.append(o);
    }
    return arr;
}

} // namespace db
