/**
 * @file RecordDAO.cpp
 * @brief 借用记录数据访问对象实现 — 合并QJsonObject API + 实体类API + 操作日志查询
 * @author 袁燕
 */
#include "RecordDAO.h"
#include "DatabaseManager.h"
#include "common/PositionFormatter.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

namespace db {

// ═══════════════════════════════════════════════
// 实体类转换 — 从dao/RecordDAO.cpp合并
// ═══════════════════════════════════════════════
BorrowRecord RecordDAO::borrowFromQuery(const QSqlQuery& q) {
    BorrowRecord r;
    r.borrowId        = q.value("record_id").toInt();
    r.flowNo          = q.value("flow_no").toString();
    r.userId          = q.value("user_id").toInt();
    r.toolId          = q.value("tool_id").toInt();
    r.quantity        = q.value("borrow_qty").toInt();
    r.borrowTime      = q.value("borrow_time").toDateTime();
    r.expectReturnTime= q.value("expected_return_time").toDateTime();
    r.actualReturnTime= q.value("actual_return_time").toDateTime();
    r.status          = q.value("status").toString();
    r.borrowReason    = q.value("borrow_reason").toString();
    r.remark          = q.value("remark").toString();
    r.createdAt       = q.value("created_at").toDateTime();
    r.userName        = q.value("username").toString();
    r.realName        = q.value("real_name").toString();
    r.workNo          = q.value("work_no").toString();
    r.department      = q.value("department").toString();
    r.toolCode        = q.value("tool_code").toString();
    r.toolName        = q.value("tool_name").toString();
    r.toolSpec        = q.value("spec").toString();
    r.cabinetName     = q.value("cabinet_name").toString();
    return r;
}

ReturnRecord RecordDAO::returnFromQuery(const QSqlQuery& q) {
    ReturnRecord r;
    r.returnId    = q.value("record_id").toInt();
    r.borrowId    = q.value("record_id").toInt();
    r.userId      = q.value("user_id").toInt();
    r.toolId      = q.value("tool_id").toInt();
    r.quantity    = q.value("borrow_qty").toInt();
    r.returnTime  = q.value("actual_return_time").toDateTime();
    r.condition   = q.value("remark").toString();
    r.remark      = q.value("remark").toString();
    r.createdAt   = q.value("created_at").toDateTime();
    r.userName    = q.value("username").toString();
    r.realName    = q.value("real_name").toString();
    r.toolCode    = q.value("tool_code").toString();
    r.toolName    = q.value("tool_name").toString();
    r.flowNo      = q.value("flow_no").toString();
    r.borrowTime  = q.value("borrow_time").toString();
    return r;
}

// ═══════════════════════════════════════════════
// QJsonObject API（Service层使用）
// ═══════════════════════════════════════════════
int RecordDAO::insert(const QJsonObject& record) {
    QSqlDatabase db = getDb(); QSqlQuery q(db);
    // [V2.11 2026-07-02 袁燕] 增加 mapping_id 字段，记录借用的位置
    q.prepare("INSERT INTO tool_borrow_record (flow_no, tool_id, user_id, borrow_qty, "
              "borrow_reason, expected_return_time, remark, status, mapping_id) "
              "VALUES (:fn,:tid,:uid,:qty,:reason,:ert,:rmk,:st,:mid)");
    q.bindValue(":fn", record["flowNo"].toString());
    q.bindValue(":tid", record["toolId"].toInt());
    q.bindValue(":uid", record["userId"].toInt());
    q.bindValue(":qty", record["borrowQty"].toInt(record["quantity"].toInt(1)));
    q.bindValue(":reason", record["borrowReason"].toString(record["purpose"].toString("")));
    q.bindValue(":ert", record["expectedReturnTime"].toString(""));
    q.bindValue(":rmk", record["remark"].toString(""));
    q.bindValue(":st", record["status"].toString("borrowing"));
    q.bindValue(":mid", record["mappingId"].toInt() > 0 ? record["mappingId"].toInt() : QVariant());
    if (!safeExec(q)) { return -1; }
    return q.lastInsertId().toInt();
}

QJsonObject RecordDAO::findById(int recordId) {
    QSqlDatabase db = getDb(); QSqlQuery q(db);
    q.prepare("SELECT r.*, u.real_name AS user_name, ti.tool_name, ti.tool_code "
              "FROM tool_borrow_record r LEFT JOIN sys_user u ON r.user_id=u.user_id "
              "LEFT JOIN tool_info ti ON r.tool_id=ti.tool_id WHERE r.record_id=:id");
    q.bindValue(":id", recordId);
    if (!safeExec(q) || !q.next()) return QJsonObject();
    QJsonObject r;
    r["recordId"]=q.value("record_id").toInt(); r["flowNo"]=q.value("flow_no").toString();
    r["toolId"]=q.value("tool_id").toInt(); r["toolName"]=q.value("tool_name").toString();
    r["toolCode"]=q.value("tool_code").toString();
    r["userId"]=q.value("user_id").toInt(); r["userName"]=q.value("user_name").toString();
    r["borrowQty"]=q.value("borrow_qty").toInt(); r["borrowReason"]=q.value("borrow_reason").toString();
    r["borrowTime"]=q.value("borrow_time").toString(); r["status"]=q.value("status").toString();
    r["mappingId"]=q.value("mapping_id").toInt();  // 位置映射ID
    return r;
}

QJsonObject RecordDAO::findAll(int userId, int toolId, const QString& status,
                                const QString& startDate, const QString& endDate,
                                int page, int pageSize) {
    QSqlDatabase db = getDb();
    QStringList conditions; QMap<QString, QVariant> binds;
    if (userId > 0) { conditions << "r.user_id=:uid"; binds[":uid"] = userId; }
    if (toolId > 0) { conditions << "r.tool_id=:tid"; binds[":tid"] = toolId; }
    if (!status.isEmpty()) { conditions << "r.status=:st"; binds[":st"] = status; }
    if (!startDate.isEmpty()) { conditions << "r.borrow_time>=:sd"; binds[":sd"] = startDate; }
    if (!endDate.isEmpty()) { conditions << "r.borrow_time<=:ed"; binds[":ed"] = endDate; }
    QString where = conditions.isEmpty() ? "1=1" : conditions.join(" AND ");
    
    QSqlQuery cq(db);
    cq.prepare("SELECT COUNT(*) FROM tool_borrow_record r WHERE " + where);
    for (auto it = binds.begin(); it != binds.end(); ++it) cq.bindValue(it.key(), it.value());
    safeExec(cq); cq.next(); int total = cq.value(0).toInt();

    QSqlQuery dq(db);
    dq.prepare(
        "SELECT r.record_id, r.flow_no, r.tool_id, r.user_id, r.borrow_qty, r.borrow_reason, "
        "r.borrow_time, r.expected_return_time, r.actual_return_time, r.status, r.operator_id, "
        "r.remark, r.created_at, r.mapping_id, u.real_name AS user_name, u.work_no AS user_work_no, "
        "ti.tool_name, ti.tool_code, "
        // 位置优先从映射表取（位置维度借用记录的位置）
        "mpm.layer AS mpm_layer, mpm.position AS mpm_pos, mpm_cb.cabinet_name AS mpm_cab_name, "
        "ti.position AS tool_position, ti.layer AS tool_layer, "
        "COALESCE(tc.cabinet_name,'') AS cabinet_name "
        "FROM tool_borrow_record r "
        "LEFT JOIN sys_user u ON r.user_id=u.user_id "
        "LEFT JOIN tool_info ti ON r.tool_id=ti.tool_id "
        "LEFT JOIN tool_cabinet tc ON ti.cabinet_id=tc.cabinet_id "
        // JOIN映射表获取借用时的位置（通过mapping_id）
        "LEFT JOIN tool_position_mapping mpm ON r.mapping_id=mpm.mapping_id "
        "LEFT JOIN tool_cabinet mpm_cb ON mpm.cabinet_id=mpm_cb.cabinet_id "
        "WHERE " + where + " ORDER BY r.borrow_time DESC LIMIT :lim OFFSET :off"
    );
    for (auto it = binds.begin(); it != binds.end(); ++it) dq.bindValue(it.key(), it.value());
    dq.bindValue(":lim", pageSize); dq.bindValue(":off", (page - 1) * pageSize);
    safeExec(dq);

    QJsonArray list;
    while (dq.next()) {
        QJsonObject item;
        item["recordId"] = dq.value("record_id").toInt();
        item["flowNo"] = dq.value("flow_no").toString();
        item["toolId"] = dq.value("tool_id").toInt();
        item["toolName"] = dq.value("tool_name").toString();
        item["toolCode"] = dq.value("tool_code").toString();
        item["userId"] = dq.value("user_id").toInt();
        item["userName"] = dq.value("user_name").toString();
        item["userWorkNo"] = dq.value("user_work_no").toString();
        item["borrowQty"] = dq.value("borrow_qty").toInt();
        item["borrowReason"] = dq.value("borrow_reason").toString();
        item["borrowTime"] = dq.value("borrow_time").toString();
        item["expectedReturnTime"] = dq.value("expected_return_time").toString();
        item["actualReturnTime"] = dq.value("actual_return_time").toString();
        item["status"] = dq.value("status").toString();
        item["remark"] = dq.value("remark").toString();
        item["createdAt"] = dq.value("created_at").toString();
        item["mappingId"] = dq.value("mapping_id").toInt();  // 位置映射ID
        // 位置优先从映射表取（位置维度借用的记录位置在映射表）
        QString cab = dq.value("mpm_cab_name").toString();
        QString layer = dq.value("mpm_layer").toString();
        QString pos = dq.value("mpm_pos").toString();
        if (cab.isEmpty()) {
            // 旧记录无mapping_id，从tool_info取位置
            cab = dq.value("cabinet_name").toString();
            layer = dq.value("tool_layer").toString();
            pos = dq.value("tool_position").toString();
        }
        // 位置格式化为 柜号-层号-位号（两位补零，如A-01-03）
        item["position"] = common::formatPosition(cab, layer, pos);
        item["cabinetName"] = cab;
        item["layer"] = layer;
        item["rawPosition"] = pos;
        list.append(item);
    }
    QJsonObject result; result["list"] = list; result["total"] = total;
    return result;
}

// 统计某机组下未归还(borrowing/overdue)的借用记录数
//   入参：machineGroupId 机组ID
//   返回：该机组下所有未归还的借用记录数
//   SQL：JOIN tool_info 获取 machine_group_id，筛选 status IN ('borrowing','overdue')
int RecordDAO::countActiveByMachineGroup(int machineGroupId) {
    QSqlDatabase db = getDb(); QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM tool_borrow_record r "
              "LEFT JOIN tool_info t ON r.tool_id = t.tool_id "
              "WHERE t.machine_group_id = :mgid AND r.status IN ('borrowing','overdue')");
    q.bindValue(":mgid", machineGroupId);
    if (!safeExec(q) || !q.next()) return 0;
    return q.value(0).toInt();
}
//   入参：toolId 工具ID，limit 返回记录数(默认5)
//   返回：QJsonArray，每项含 borrowTime/borrowerName/borrowQty/borrowReason/status/expectedReturnTime
// 排序优化：借用中/逾期优先，已归还在后，确保两种状态都能显示
//   作者：袁燕 — 要求详情页借用记录既体现借用中也体现已归还
QJsonArray RecordDAO::findByToolId(int toolId, int limit) {
    QSqlDatabase db = getDb(); QSqlQuery q(db);
    q.prepare("SELECT r.borrow_time, r.expected_return_time, r.borrow_qty, r.borrow_reason, "
              "r.status, r.actual_return_time, u.real_name AS user_name, u.work_no AS user_work_no "
              "FROM tool_borrow_record r "
              "LEFT JOIN sys_user u ON r.user_id = u.user_id "
              "WHERE r.tool_id = :tid "
              "ORDER BY CASE r.status WHEN 'borrowing' THEN 0 WHEN 'overdue' THEN 0 ELSE 1 END, "
              "r.borrow_time DESC LIMIT :lim");
    q.bindValue(":tid", toolId);
    q.bindValue(":lim", limit);
    QJsonArray list;
    if (!safeExec(q)) return list;
    while (q.next()) {
        QJsonObject item;
        item["borrowTime"] = q.value("borrow_time").toString();
        item["expectedReturnTime"] = q.value("expected_return_time").toString();
        item["borrowQty"] = q.value("borrow_qty").toInt();
        item["borrowReason"] = q.value("borrow_reason").toString();
        item["status"] = q.value("status").toString();
        item["actualReturnTime"] = q.value("actual_return_time").toString();
        item["userName"] = q.value("user_name").toString();
        item["userWorkNo"] = q.value("user_work_no").toString();
        list.append(item);
    }
    return list;
}

// [V2.11 2026-07-02 袁燕] 按位置映射ID查询借用记录（工具详情按位置过滤）
//   入参：mappingId 位置映射ID，limit 返回记录数
//   返回：QJsonArray，每项含 borrowTime/borrowQty/borrowReason/status/expectedReturnTime/userName
QJsonArray RecordDAO::findByMappingId(int mappingId, int limit) {
    QSqlDatabase db = getDb(); QSqlQuery q(db);
    q.prepare("SELECT r.borrow_time, r.expected_return_time, r.borrow_qty, r.borrow_reason, "
              "r.status, r.actual_return_time, u.real_name AS user_name, u.work_no AS user_work_no "
              "FROM tool_borrow_record r "
              "LEFT JOIN sys_user u ON r.user_id = u.user_id "
              "WHERE r.mapping_id = :mid "
              "ORDER BY CASE r.status WHEN 'borrowing' THEN 0 WHEN 'overdue' THEN 0 ELSE 1 END, "
              "r.borrow_time DESC LIMIT :lim");
    q.bindValue(":mid", mappingId);
    q.bindValue(":lim", limit);
    QJsonArray list;
    if (!safeExec(q)) return list;
    while (q.next()) {
        QJsonObject item;
        item["borrowTime"] = q.value("borrow_time").toString();
        item["expectedReturnTime"] = q.value("expected_return_time").toString();
        item["borrowQty"] = q.value("borrow_qty").toInt();
        item["borrowReason"] = q.value("borrow_reason").toString();
        item["status"] = q.value("status").toString();
        item["actualReturnTime"] = q.value("actual_return_time").toString();
        item["userName"] = q.value("user_name").toString();
        item["userWorkNo"] = q.value("user_work_no").toString();
        list.append(item);
    }
    return list;
}

// 查询工具的最新一条操作日志（入库/出库），返回操作人和时间
QJsonObject RecordDAO::findLastOperationLog(const QString& toolCode, const QString& operationType) {
    QJsonObject result;
    QSqlDatabase db = getDb(); QSqlQuery q(db);
    q.prepare("SELECT log.created_at, u.real_name "
              "FROM sys_operation_log log "
              "LEFT JOIN sys_user u ON log.user_id = u.user_id "
              "WHERE log.operation_type = :opType AND log.target_id = :code "
              "ORDER BY log.created_at DESC LIMIT 1");
    q.bindValue(":opType", operationType);
    q.bindValue(":code", toolCode);
    if (safeExec(q) && q.next()) {
        result["time"] = q.value(0).toString();
        result["operator"] = q.value(1).toString();
    }
    return result;
}

// 查询工具的操作日志列表（入库/出库），支持按位置关键字过滤
QJsonArray RecordDAO::findOperationLogs(const QString& toolCode, const QString& operationType,
                                         const QString& positionKeyword, int limit) {
    QJsonArray list;
    QSqlDatabase db = getDb(); QSqlQuery q(db);
    q.prepare("SELECT log.created_at, log.content, u.real_name "
              "FROM sys_operation_log log "
              "LEFT JOIN sys_user u ON log.user_id = u.user_id "
              "WHERE log.operation_type = :opType AND log.target_id = :code "
              "AND log.content LIKE :pos "
              "ORDER BY log.created_at DESC LIMIT :lim");
    q.bindValue(":opType", operationType);
    q.bindValue(":code", toolCode);
    q.bindValue(":pos", "%" + positionKeyword + "%");
    q.bindValue(":lim", limit);
    if (safeExec(q)) {
        while (q.next()) {
            QJsonObject obj;
            obj["time"] = q.value(0).toString();
            obj["content"] = q.value(1).toString();
            obj["operator"] = q.value(2).toString();
            list.append(obj);
        }
    }
    return list;
}

bool RecordDAO::completeReturn(int recordId, const QString& returnTime,
                                const QString& condition, int operatorId, const QString& remark) {
    QSqlDatabase db = getDb(); QSqlQuery q(db);
    q.prepare("UPDATE tool_borrow_record SET actual_return_time=:rt, status='returned', "
              "operator_id=:oid, remark=CONCAT(IFNULL(remark,''),' | 归还:',:rmk) "
              "WHERE record_id=:id AND status IN ('borrowing','overdue')");
    q.bindValue(":rt", returnTime); q.bindValue(":oid", operatorId);
    q.bindValue(":rmk", remark.isEmpty() ? condition : remark + "(" + condition + ")");
    q.bindValue(":id", recordId);
    return safeExec(q) && q.numRowsAffected() > 0;
}

// ═══════════════════════════════════════════════
// 实体类API（Controller层使用）— 从dao/RecordDAO.cpp合并
// ═══════════════════════════════════════════════
int RecordDAO::insertBorrow(const BorrowRecord& r) {
    return insertAndGetId("INSERT INTO tool_borrow_record (flow_no,user_id,tool_id,borrow_qty,"
                          "borrow_time,expected_return_time,status,borrow_reason,remark) "
                          "VALUES (?,?,?,?,?,?,?,?,?)",
                          {r.flowNo, r.userId, r.toolId, r.quantity,
                           r.borrowTime, r.expectReturnTime, r.status,
                           r.borrowReason, r.remark});
}

bool RecordDAO::completeBorrowReturn(int borrowId, const QDateTime& returnTime,
                                     const QString& condition, const QString& remark) {
    return execute("UPDATE tool_borrow_record SET actual_return_time=?,status='returned',remark=? WHERE record_id=?",
                   {returnTime, remark, borrowId});
}

QList<BorrowRecord> RecordDAO::findBorrows(int page, int pageSize,
                                            const QString& keyword, const QString& status,
                                            int userId, const QDate& startDate, const QDate& endDate) {
    QString sql = "SELECT br.*, u.username, u.real_name, u.work_no, u.department, "
                  "ti.tool_code, ti.tool_name, ti.spec, tc.cabinet_name "
                  "FROM tool_borrow_record br "
                  "LEFT JOIN sys_user u ON br.user_id=u.user_id "
                  "LEFT JOIN tool_info ti ON br.tool_id=ti.tool_id "
                  "LEFT JOIN tool_cabinet tc ON ti.cabinet_id=tc.cabinet_id WHERE 1=1";
    QVariantList params;
    if (!keyword.isEmpty()) {
        sql += " AND (u.real_name LIKE ? OR ti.tool_name LIKE ? OR br.flow_no LIKE ?)";
        QString kw = "%" + keyword + "%";
        params << kw << kw << kw;
    }
    if (!status.isEmpty()) { sql += " AND br.status = ?"; params << status; }
    if (userId > 0)        { sql += " AND br.user_id = ?"; params << userId; }
    if (startDate.isValid()) { sql += " AND br.borrow_time >= ?"; params << startDate.startOfDay(); }
    if (endDate.isValid())   { sql += " AND br.borrow_time <= ?"; params << endDate.endOfDay(); }

    QList<BorrowRecord> list;
    QSqlQuery q = query(paginate(sql, page, pageSize, "br.borrow_time DESC"), params);
    while (q.next()) list.append(borrowFromQuery(q));
    return list;
}

int RecordDAO::borrowCount(const QString& keyword, const QString& status, int userId,
                            const QDate& startDate, const QDate& endDate) {
    // [2026-06-26 修复] keyword筛选条件必须与findBorrows完全一致（含flow_no），否则COUNT与数据不匹配
    QString sql = "SELECT br.* FROM tool_borrow_record br "
                  "LEFT JOIN sys_user u ON br.user_id=u.user_id "
                  "LEFT JOIN tool_info ti ON br.tool_id=ti.tool_id WHERE 1=1";
    QVariantList params;
    if (!keyword.isEmpty()) {
        sql += " AND (u.real_name LIKE ? OR ti.tool_name LIKE ? OR br.flow_no LIKE ?)";
        QString kw = "%" + keyword + "%";
        params << kw << kw << kw;
    }
    if (!status.isEmpty()) { sql += " AND br.status = ?"; params << status; }
    if (userId > 0)        { sql += " AND br.user_id = ?"; params << userId; }
    if (startDate.isValid()) { sql += " AND br.borrow_time >= ?"; params << startDate.startOfDay(); }
    if (endDate.isValid())   { sql += " AND br.borrow_time <= ?"; params << endDate.endOfDay(); }
    return count(sql, params);
}

BorrowRecord RecordDAO::findBorrowById(int borrowId) {
    QSqlQuery q = query("SELECT br.*, u.username, u.real_name, ti.tool_code, ti.tool_name "
                         "FROM tool_borrow_record br "
                         "LEFT JOIN sys_user u ON br.user_id=u.user_id "
                         "LEFT JOIN tool_info ti ON br.tool_id=ti.tool_id "
                         "WHERE br.record_id = ?", {borrowId});
    if (q.next()) return borrowFromQuery(q);
    return BorrowRecord();
}

QList<BorrowRecord> RecordDAO::findBorrowsByUser(int userId, int limit) {
    QList<BorrowRecord> list;
    QSqlQuery q = query("SELECT br.*, u.username, u.real_name, ti.tool_code, ti.tool_name, ti.spec "
                         "FROM tool_borrow_record br "
                         "LEFT JOIN sys_user u ON br.user_id=u.user_id "
                         "LEFT JOIN tool_info ti ON br.tool_id=ti.tool_id "
                         "WHERE br.user_id = ? ORDER BY br.borrow_time DESC LIMIT ?",
                         {userId, limit});
    while (q.next()) list.append(borrowFromQuery(q));
    return list;
}

bool RecordDAO::hasOverdue(int userId) {
    // CURRENT_TIMESTAMP兼容MySQL和SQLite
    return scalar("SELECT COUNT(*) FROM tool_borrow_record WHERE user_id=? AND status IN ('borrowing','overdue') "
                   "AND expected_return_time < CURRENT_TIMESTAMP", {userId}).toInt() > 0;
}

QList<ReturnRecord> RecordDAO::findReturns(int page, int pageSize,
                                            const QString& keyword, int userId,
                                            const QDate& startDate, const QDate& endDate) {
    QString sql = "SELECT br.*, u.username, u.real_name, ti.tool_code, ti.tool_name, "
                  "br.flow_no, br.borrow_time "
                  "FROM tool_borrow_record br "
                  "LEFT JOIN sys_user u ON br.user_id=u.user_id "
                  "LEFT JOIN tool_info ti ON br.tool_id=ti.tool_id "
                  "WHERE br.status='returned'";
    QVariantList params;
    if (!keyword.isEmpty()) {
        sql += " AND (ti.tool_name LIKE ? OR u.real_name LIKE ?)";
        QString kw = "%" + keyword + "%";
        params << kw << kw;
    }
    if (userId > 0)        { sql += " AND br.user_id = ?"; params << userId; }
    if (startDate.isValid()){ sql += " AND br.actual_return_time >= ?"; params << startDate.startOfDay(); }
    if (endDate.isValid())  { sql += " AND br.actual_return_time <= ?"; params << endDate.endOfDay(); }

    QList<ReturnRecord> list;
    QSqlQuery q = query(paginate(sql, page, pageSize, "br.actual_return_time DESC"), params);
    while (q.next()) list.append(returnFromQuery(q));
    return list;
}

// 返回去重工具数（统计逻辑与v_tool_stats.borrowed_count一致，
//                  便于Dashboard与工具管理页数值对齐）
int RecordDAO::activeBorrowCount() {
    return scalar("SELECT COUNT(DISTINCT tool_id) FROM tool_borrow_record WHERE status IN ('borrowing','overdue')").toInt();
}

int RecordDAO::overdueCount() {
    // CURRENT_TIMESTAMP兼容MySQL和SQLite
    return scalar("SELECT COUNT(*) FROM tool_borrow_record WHERE status='overdue' "
                   "AND expected_return_time < CURRENT_TIMESTAMP").toInt();
}

// 通过借用记录ID查询关联工具信息，返回 {toolId, toolCode}
QJsonObject RecordDAO::findToolInfoByRecordId(int recordId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT tool_id, tool_code FROM tool_borrow_record WHERE record_id=?");
    q.addBindValue(recordId);
    safeExec(q);
    QJsonObject obj;
    if (q.next()) {
        obj["toolId"] = q.value(0).toInt();
        obj["toolCode"] = q.value(1).toString();
    }
    return obj;
}

// 写入操作日志，返回log_id
int RecordDAO::insertOperationLog(int userId, const QString& operationType,
                                   const QString& targetType, const QString& targetId,
                                   const QString& content, const QString& ipAddress)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("INSERT INTO sys_operation_log "
              "(user_id, operation_type, target_type, target_id, content, ip_address, created_at) "
              "VALUES (:uid, :opType, :tgtType, :tgtId, :content, :ip, :time)");
    int uid = (userId > 0) ? userId : 1;
    q.bindValue(":uid", uid);
    q.bindValue(":opType", operationType);
    q.bindValue(":tgtType", targetType);
    q.bindValue(":tgtId", targetId);
    q.bindValue(":content", content);
    q.bindValue(":ip", ipAddress);
    q.bindValue(":time", QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    if (!safeExec(q)) {
        return -1;
    }
    return q.lastInsertId().toInt();
}

// 分页查询操作日志（含操作人信息）
QJsonObject RecordDAO::findOperationLogsByType(const QString& operationType, int page, int pageSize)
{
    QSqlDatabase db = getDb();
    QJsonObject result;
    // 总数
    QSqlQuery countQ(db);
    countQ.prepare("SELECT COUNT(*) FROM sys_operation_log WHERE operation_type=?");
    countQ.addBindValue(operationType);
    safeExec(countQ);
    int total = 0;
    if (countQ.next()) total = countQ.value(0).toInt();
    result["total"] = total;
    // 分页数据
    int offset = (page - 1) * pageSize;
    QSqlQuery q(db);
    q.prepare(
        "SELECT log.created_at, log.content, log.target_id, "
        "       t.tool_code, u.real_name, u.work_no "
        "FROM sys_operation_log log "
        "LEFT JOIN sys_user u ON log.user_id = u.user_id "
        "LEFT JOIN tool_info t ON log.target_id = CAST(t.tool_id AS CHAR) "
        "WHERE log.operation_type = ? "
        "ORDER BY log.created_at DESC "
        "LIMIT ? OFFSET ?"
    );
    q.addBindValue(operationType);
    q.addBindValue(pageSize);
    q.addBindValue(offset);
    safeExec(q);
    QJsonArray list;
    while (q.next()) {
        QJsonObject r;
        r["createdAt"] = q.value(0).toString();
        r["content"] = q.value(1).toString();
        r["toolId"] = q.value(2).toInt();
        r["toolCode"] = q.value(3).toString();
        r["realName"] = q.value(4).toString();
        r["workNo"] = q.value(5).toString();
        list.append(r);
    }
    result["list"] = list;
    return result;
}

// 统计操作日志数量
int RecordDAO::countOperationLogsByType(const QString& operationType)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM sys_operation_log WHERE operation_type=?");
    q.addBindValue(operationType);
    safeExec(q);
    if (q.next()) return q.value(0).toInt();
    return 0;
}

// 入库记录查询（含位置JOIN）— 入库记录Tab专用
QJsonObject RecordDAO::findCheckinLogs(int page, int pageSize)
{
    QSqlDatabase db = getDb();
    QJsonObject result;
    // 总数
    QSqlQuery countQ(db);
    countQ.prepare("SELECT COUNT(*) FROM sys_operation_log WHERE operation_type='checkin'");
    safeExec(countQ);
    int total = 0;
    if (countQ.next()) total = countQ.value(0).toInt();
    result["total"] = total;
    // 分页数据
    int offset = (page - 1) * pageSize;
    QSqlQuery q(db);
    q.prepare(
        "SELECT log.created_at, log.content, log.target_id, "
        "       u.real_name, u.work_no, "
        "       ti.cabinet_id AS ti_cab_id, ti_cb.cabinet_name AS ti_cab_name, "
        "       ti.layer AS ti_layer, ti.position AS ti_pos, "
        "       mpm.cabinet_id AS mpm_cab_id, mpm_cb.cabinet_name AS mpm_cab_name, "
        "       mpm.layer AS mpm_layer, mpm.position AS mpm_pos "
        "FROM sys_operation_log log "
        "LEFT JOIN sys_user u ON log.user_id = u.user_id "
        "LEFT JOIN tool_info ti ON ti.tool_code = log.target_id "
        "LEFT JOIN tool_cabinet ti_cb ON ti.cabinet_id = ti_cb.cabinet_id "
        "LEFT JOIN tool_position_mapping mpm ON mpm.cabinet_id = ti.cabinet_id "
        "  AND mpm.layer = ti.layer AND mpm.position = ti.position "
        "LEFT JOIN tool_cabinet mpm_cb ON mpm.cabinet_id = mpm_cb.cabinet_id "
        "WHERE log.operation_type = 'checkin' "
        "ORDER BY log.created_at DESC "
        "LIMIT ? OFFSET ?"
    );
    q.addBindValue(pageSize);
    q.addBindValue(offset);
    safeExec(q);
    QJsonArray list;
    while (q.next()) {
        QJsonObject r;
        r["createdAt"]  = q.value(0).toString();
        r["content"]    = q.value(1).toString();
        r["targetId"]   = q.value(2).toString();
        r["realName"]   = q.value(3).toString();
        r["workNo"]     = q.value(4).toString();
        r["tiCabId"]    = q.value(5).toInt();
        r["tiCabName"]  = q.value(6).toString();
        r["tiLayer"]    = q.value(7).toString();
        r["tiPos"]      = q.value(8).toString();
        r["mpmCabId"]   = q.value(9).toInt();
        r["mpmCabName"] = q.value(10).toString();
        r["mpmLayer"]   = q.value(11).toString();
        r["mpmPos"]     = q.value(12).toString();
        list.append(r);
    }
    result["list"] = list;
    return result;
}

QJsonObject RecordDAO::getUserStats(int userId) {
    QJsonObject stats;
    stats["todayBorrow"] = 0;
    stats["pendingReturn"] = 0;
    stats["monthBorrow"] = 0;
    QSqlDatabase db = getDb();
    // 今日借用数
    {
        QSqlQuery q(db);
        q.prepare("SELECT COUNT(*) FROM tool_borrow_record "
                  "WHERE user_id=:uid AND DATE(borrow_time)=CURDATE()");
        q.bindValue(":uid", userId);
        if (safeExec(q) && q.next()) stats["todayBorrow"] = q.value(0).toInt();
    }
    // 待归还数
    {
        QSqlQuery q(db);
        q.prepare("SELECT COUNT(*) FROM tool_borrow_record "
                  "WHERE user_id=:uid AND status IN ('borrowing','overdue')");
        q.bindValue(":uid", userId);
        if (safeExec(q) && q.next()) stats["pendingReturn"] = q.value(0).toInt();
    }
    // 本月借用数
    {
        QSqlQuery q(db);
        q.prepare("SELECT COUNT(*) FROM tool_borrow_record "
                  "WHERE user_id=:uid AND DATE_FORMAT(borrow_time,'%Y-%m')=DATE_FORMAT(CURDATE(),'%Y-%m')");
        q.bindValue(":uid", userId);
        if (safeExec(q) && q.next()) stats["monthBorrow"] = q.value(0).toInt();
    }
    return stats;
}

QJsonArray RecordDAO::findRecentActivity(int limit) {
    QJsonArray arr;
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare(
        "SELECT r.record_id, r.borrow_time, r.borrow_qty, r.status, "
        "  COALESCE(u.real_name, u.username) AS user_name, "
        "  COALESCE(t.tool_name, '未知工具') AS tool_name "
        "FROM tool_borrow_record r "
        "LEFT JOIN sys_user u ON r.user_id = u.user_id "
        "LEFT JOIN tool_info t ON r.tool_id = t.tool_id "
        "ORDER BY r.borrow_time DESC LIMIT :lim");
    q.bindValue(":lim", limit);
    if (!safeExec(q)) return arr;
    while (q.next()) {
        QJsonObject o;
        QString status = q.value("status").toString();
        o["time"]   = q.value("borrow_time").toString();
        o["user"]   = q.value("user_name").toString();
        o["type"]   = (status == "returned") ? QStringLiteral("归还") : QStringLiteral("借用");
        o["tool"]   = q.value("tool_name").toString();
        o["qty"]    = q.value("borrow_qty").toInt();
        o["status"] = (status == "returned") ? QStringLiteral("已归还") : QStringLiteral("已借出");
        arr.append(o);
    }
    return arr;
}

QJsonArray RecordDAO::findByUserId(int userId, int limit) {
    QJsonArray arr;
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare(
        "SELECT r.borrow_time, r.borrow_qty, r.status, "
        "  COALESCE(t.tool_name, '未知工具') AS tool_name "
        "FROM tool_borrow_record r "
        "LEFT JOIN tool_info t ON r.tool_id = t.tool_id "
        "WHERE r.user_id = :uid "
        "ORDER BY r.borrow_time DESC LIMIT :lim");
    q.bindValue(":uid", userId);
    q.bindValue(":lim", limit);
    if (!safeExec(q)) return arr;
    while (q.next()) {
        QJsonObject o;
        o["borrowTime"] = q.value("borrow_time").toString();
        o["toolName"]   = q.value("tool_name").toString();
        o["quantity"]   = q.value("borrow_qty").toInt();
        o["status"]     = q.value("status").toString();
        arr.append(o);
    }
    return arr;
}

} // namespace db
