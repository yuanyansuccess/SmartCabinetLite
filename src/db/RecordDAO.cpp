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
#include "common/Constants.h"

namespace db {

// 借用记录排序片段：进行中/逾期优先，其次按借用时间倒序
// findByToolId 与 findByMappingId 共用，避免两处重复维护同一排序规则
namespace {
const char* const BORROW_RECORD_ORDER =
    "ORDER BY CASE r.status WHEN 'borrowing' THEN 0 WHEN 'overdue' THEN 0 ELSE 1 END, "
    "r.borrow_time DESC ";

// 借用记录查询的公共列清单与表关联：按工具查、按位置查两处共用，
// 改列时只改这一处，避免漏改一处造成字段错位
const char* const BORROW_RECORD_SELECT =
    "SELECT r.borrow_time, r.expected_return_time, r.borrow_qty, r.borrow_reason, "
    "r.status, r.actual_return_time, u.real_name AS user_name, u.work_no AS user_work_no "
    "FROM tool_borrow_record r "
    "LEFT JOIN sys_user u ON r.user_id = u.user_id ";

/**
 * @brief 把借用记录查询的一行组装成结果对象
 * @param query 已执行到当前行的查询，取值列与 BORROW_RECORD_SELECT 的列一致
 * @return 含借用时间/预计归还/数量/原因/状态/实际归还/借用人的对象
 */
QJsonObject borrowRecordRowFromQuery(const QSqlQuery& query)
{
    QJsonObject item;
    item["borrowTime"]        = query.value("borrow_time").toString();
    item["expectedReturnTime"] = query.value("expected_return_time").toString();
    item["borrowQty"]         = query.value("borrow_qty").toInt();
    item["borrowReason"]      = query.value("borrow_reason").toString();
    item["status"]            = query.value("status").toString();
    item["actualReturnTime"]  = query.value("actual_return_time").toString();
    item["userName"]          = query.value("user_name").toString();
    item["userWorkNo"]        = query.value("user_work_no").toString();
    return item;
}
}

// ═══════════════════════════════════════════════
// 实体类转换 — 从dao/RecordDAO.cpp合并
// ═══════════════════════════════════════════════
/**
 * @brief 查询结果行转换为借用记录实体
 * @param q 已定位到有效行的查询结果
 * @return 填充完成的借用记录实体
 */
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

// ═══════════════════════════════════════════════
// QJsonObject API（Service层使用）
// ═══════════════════════════════════════════════
/**
 * @brief 新增借用记录
 * @param record 记录字段集合，键名使用驼峰形式
 * @return 新记录的ID；写入失败返回 -1
 */
int RecordDAO::insert(const QJsonObject& record) {
    QSqlDatabase db = getDb(); QSqlQuery q(db);
    // 增加 mapping_id 字段，记录借用的位置
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
    q.bindValue(":st", record["status"].toString(SC::RECORD_BORROWING));
    q.bindValue(":mid", record["mappingId"].toInt() > 0 ? record["mappingId"].toInt() : QVariant());
    if (!safeExec(q)) { return -1; }
    return q.lastInsertId().toInt();
}

/**
 * @brief 按记录ID查询借用详情
 * @param recordId 借用记录ID
 * @return 含用户名与工具信息的详情对象；不存在时返回空对象
 */
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

/**
 * @brief 分页查询借用记录
 * @param userId 用户ID，0表示不限用户
 * @param toolId 工具ID，0表示不限工具
 * @param status 记录状态，为空不过滤
 * @param startDate 起始日期，为空表示不限
 * @param endDate 截止日期，为空表示不限
 * @param page 页码，从1开始
 * @param pageSize 每页条数
 * @return 含 list 数组与 total 总数的对象
 */
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

/**
 * @brief 按工具ID查询其借用记录（工具详情页用）
 * @param toolId 工具ID
 * @param limit 最多返回条数（默认5）
 * @return 借用记录数组，每项含 borrowTime/borrowQty/borrowReason/status/expectedReturnTime/userName；
 *         借用中/逾期在前、已归还在后，确保两种状态都能显示
 */
QJsonArray RecordDAO::findByToolId(int toolId, int limit) {
    QSqlDatabase db = getDb(); QSqlQuery q(db);
    q.prepare(QString(BORROW_RECORD_SELECT)
              + "WHERE r.tool_id = :tid " + BORROW_RECORD_ORDER + "LIMIT :lim");
    q.bindValue(":tid", toolId);
    q.bindValue(":lim", limit);
    QJsonArray list;
    if (!safeExec(q)) return list;
    while (q.next()) {
        list.append(borrowRecordRowFromQuery(q));
    }
    return list;
}

/**
 * @brief 按位置映射ID查询借用记录（工具详情按位置过滤）
 * @param mappingId 位置映射ID
 * @param limit 最多返回条数
 * @return 借用记录数组，每项含 borrowTime/borrowQty/borrowReason/status/expectedReturnTime/userName
 */
QJsonArray RecordDAO::findByMappingId(int mappingId, int limit) {
    QSqlDatabase db = getDb(); QSqlQuery q(db);
    q.prepare(QString(BORROW_RECORD_SELECT)
              + "WHERE r.mapping_id = :mid " + BORROW_RECORD_ORDER + "LIMIT :lim");
    q.bindValue(":mid", mappingId);
    q.bindValue(":lim", limit);
    QJsonArray list;
    if (!safeExec(q)) return list;
    while (q.next()) {
        list.append(borrowRecordRowFromQuery(q));
    }
    return list;
}

/**
 * @brief 查询工具最新一条操作日志（入库/出库），取操作人与时间
 * @param toolCode 工具编号
 * @param operationType 操作类型（如 checkin/checkout）
 * @return 含 time/operator 的对象；无记录时字段为空
 */
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

/**
 * @brief 查询工具的操作日志列表（入库/出库），支持按位置关键字过滤
 * @param toolCode 工具编号
 * @param operationType 操作类型
 * @param positionKeyword 位置关键字（模糊匹配日志内容）
 * @param limit 最多返回条数
 * @return 操作日志数组，每项含 time/content/operator
 */
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

/**
 * @brief 完成一次归还
 * @param recordId 借用记录ID
 * @param returnTime 归还时间文本
 * @param condition 归还时工具状况，如完好、丢失、损坏
 * @param operatorId 操作人ID
 * @param remark 备注
 * @return true=记录已置为已归还；false=记录不存在或已被归还
 */
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

/**
 * @brief 借出转出库：把该工具仍在借的记录结清（状态置已归还，备注写明是转出库）
 * @param toolId 工具ID
 * @param operatorId 操作人ID
 * @param remark 写入备注的结清说明（如"转出库免开箱"）
 * @return true=执行成功（该工具当前没有在借记录也算成功）；false=写库失败
 * @note  借出的工具被直接转出库后，借用关系同时结束：不清会让台账一直显示"借用中"，
 *        与工具已出库的状态自相矛盾
 */
bool RecordDAO::finishBorrowsByCheckout(int toolId, int operatorId, const QString& remark) {
    if (toolId <= 0) return true;
    QSqlDatabase db = getDb(); QSqlQuery q(db);
    q.prepare("UPDATE tool_borrow_record SET actual_return_time=NOW(), status='returned', "
              "operator_id=:oid, remark=CONCAT(IFNULL(remark,''),' | 转出库:',:rmk) "
              "WHERE tool_id=:tid AND status IN ('borrowing','overdue')");
    q.bindValue(":oid", operatorId);
    q.bindValue(":rmk", remark);
    q.bindValue(":tid", toolId);
    return safeExec(q);
}

/**
 * @brief 查询指定用户的借用记录
 * @param userId 用户ID
 * @param limit 最多返回条数
 * @return 借用记录实体列表，按借用时间倒序
 */
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

/**
 * @brief 通过借用记录ID查询关联工具信息
 * @param recordId 借用记录ID
 * @return 对象含 toolId/toolCode；记录不存在时字段为空
 */
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

/**
 * @brief 写入一条操作日志
 * @param userId 操作人ID（≤0 时回落为 1）
 * @param operationType 操作类型
 * @param targetType 操作对象类型
 * @param targetId 操作对象ID
 * @param content 日志正文
 * @param ipAddress 操作来源IP
 * @return 新写入日志的 log_id；写库失败返回 -1
 */
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
    q.bindValue(":time", QDateTime::currentDateTime().toString(SC::TIMESTAMP_FORMAT));
    if (!safeExec(q)) {
        return -1;
    }
    return q.lastInsertId().toInt();
}

/**
 * @brief 分页查询指定类型的操作日志（含操作人信息）
 * @param operationType 操作类型
 * @param page 页码（从1开始）
 * @param pageSize 每页条数
 * @return 对象含 total（总数）与 list（本页数据）
 */
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

/**
 * @brief 分页查询全部类型的操作日志（含操作人信息）
 * @param page 页码（从1开始）
 * @param pageSize 每页条数
 * @return 对象含 total 与 list（列表项附 operation_type 供展示层中文映射）
 */
QJsonObject RecordDAO::findOperationLogsAll(int page, int pageSize)
{
    QSqlDatabase db = getDb();
    QJsonObject result;
    // 总数
    QSqlQuery countQ(db);
    countQ.prepare("SELECT COUNT(*) FROM sys_operation_log");
    safeExec(countQ);
    int total = 0;
    if (countQ.next()) total = countQ.value(0).toInt();
    result["total"] = total;
    // 分页数据（类型字段一并返回，供展示层做中文映射）
    int offset = (page - 1) * pageSize;
    QSqlQuery q(db);
    q.prepare(
        "SELECT log.created_at, log.operation_type, log.content, "
        "       u.real_name, u.work_no "
        "FROM sys_operation_log log "
        "LEFT JOIN sys_user u ON log.user_id = u.user_id "
        "ORDER BY log.created_at DESC "
        "LIMIT ? OFFSET ?"
    );
    q.addBindValue(pageSize);
    q.addBindValue(offset);
    safeExec(q);
    QJsonArray list;
    while (q.next()) {
        QJsonObject r;
        r["createdAt"] = q.value(0).toString();
        r["type"]      = q.value(1).toString();
        r["content"]   = q.value(2).toString();
        r["realName"]  = q.value(3).toString();
        r["workNo"]    = q.value(4).toString();
        list.append(r);
    }
    result["list"] = list;
    return result;
}

/**
 * @brief 入库记录分页查询（含位置信息，入库记录Tab专用）
 * @param page 页码（从1开始）
 * @param pageSize 每页条数
 * @return 对象含 total 与 list
 */
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
    // 位置一律从 tool_position_mapping 取（按 tool_id 关联，避免用 ti 冗余列反查 mpm 产生漂移）
    // 列顺序/别名保持与 C++ 读取一致（ti_* 与 mpm_* 现在都填映射表的值）
    q.prepare(
        "SELECT log.created_at, log.content, log.target_id, "
        "       u.real_name, u.work_no, "
        "       (SELECT mpm.cabinet_id FROM tool_position_mapping mpm WHERE mpm.tool_id = ti.tool_id AND mpm.status='in_stock' LIMIT 1) AS ti_cab_id, "
        "       (SELECT mpm_cb.cabinet_name FROM tool_position_mapping mpm JOIN tool_cabinet mpm_cb ON mpm.cabinet_id=mpm_cb.cabinet_id WHERE mpm.tool_id = ti.tool_id AND mpm.status='in_stock' LIMIT 1) AS ti_cab_name, "
        "       (SELECT mpm.layer FROM tool_position_mapping mpm WHERE mpm.tool_id = ti.tool_id AND mpm.status='in_stock' LIMIT 1) AS ti_layer, "
        "       (SELECT mpm.position FROM tool_position_mapping mpm WHERE mpm.tool_id = ti.tool_id AND mpm.status='in_stock' LIMIT 1) AS ti_pos, "
        "       (SELECT mpm.cabinet_id FROM tool_position_mapping mpm WHERE mpm.tool_id = ti.tool_id AND mpm.status='in_stock' LIMIT 1) AS mpm_cab_id, "
        "       (SELECT mpm_cb.cabinet_name FROM tool_position_mapping mpm JOIN tool_cabinet mpm_cb ON mpm.cabinet_id=mpm_cb.cabinet_id WHERE mpm.tool_id = ti.tool_id AND mpm.status='in_stock' LIMIT 1) AS mpm_cab_name, "
        "       (SELECT mpm.layer FROM tool_position_mapping mpm WHERE mpm.tool_id = ti.tool_id AND mpm.status='in_stock' LIMIT 1) AS mpm_layer, "
        "       (SELECT mpm.position FROM tool_position_mapping mpm WHERE mpm.tool_id = ti.tool_id AND mpm.status='in_stock' LIMIT 1) AS mpm_pos "
        "FROM sys_operation_log log "
        "LEFT JOIN sys_user u ON log.user_id = u.user_id "
        "LEFT JOIN tool_info ti ON ti.tool_code = log.target_id "
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

// ═══════════════════════════════════════════════
// 台账统计（自SettingService原样迁移，SQL一字未改）
// ═══════════════════════════════════════════════
/**
 * @brief 汇总台账统计数据（LedgerStatsPage数据源）
 * @return totalBorrows/totalReturns/currentBorrowed/overdueCount/activeUsers
 *         + categoryStats（按品类件数）+ departmentStats（按部门）
 */
QJsonObject RecordDAO::getLedgerStats() {
    // 字段名与LedgerStatsPage期望一致：
    // totalBorrows(复数)/totalReturns(复数)/currentBorrowed/overdueCount/categoryStats/departmentStats
    // 同时保留旧字段totalBorrow/totalReturn/activeUsers向后兼容
    QJsonObject s;
    QSqlDatabase db = getDb();
    if (!db.isValid()) {
        qWarning() << "[RecordDAO] getLedgerStats: 数据库连接无效";
        return s;
    }

    // 总借用次数（所有记录）
    {
        QSqlQuery q(db);
        if (q.exec("SELECT COUNT(*) FROM tool_borrow_record") && q.next())
            s["totalBorrows"] = s["totalBorrow"] = q.value(0).toInt();
        else qWarning() << "[RecordDAO] getLedgerStats: 总借用次数查询失败";
    }
    // 总归还次数
    {
        QSqlQuery q(db);
        if (q.exec("SELECT COUNT(*) FROM tool_borrow_record WHERE status='returned'") && q.next())
            s["totalReturns"] = s["totalReturn"] = q.value(0).toInt();
        else qWarning() << "[RecordDAO] getLedgerStats: 总归还次数查询失败";
    }
    // 当前借用中
    {
        QSqlQuery q(db);
        if (q.exec("SELECT COALESCE(SUM(borrow_qty),0) FROM tool_borrow_record WHERE status IN ('borrowing','overdue')") && q.next())
            s["currentBorrowed"] = q.value(0).toInt();
        else qWarning() << "[RecordDAO] getLedgerStats: 当前借用数查询失败";
    }
    // 逾期未还
    {
        QSqlQuery q(db);
        if (q.exec("SELECT COUNT(*) FROM tool_borrow_record WHERE status='overdue'") && q.next())
            s["overdueCount"] = q.value(0).toInt();
        else qWarning() << "[RecordDAO] getLedgerStats: 逾期数查询失败";
    }
    // 活跃用户数
    {
        QSqlQuery q(db);
        if (q.exec("SELECT COUNT(DISTINCT user_id) FROM tool_borrow_record") && q.next())
            s["activeUsers"] = q.value(0).toInt();
        else qWarning() << "[RecordDAO] getLedgerStats: 活跃用户数查询失败";
    }

    // 分类统计：JOIN tool_info + tool_category 按件数统计
    // 原COUNT(DISTINCT tool_id)按种类统计，与"总数量/借出中/可用"列名件数语义不符
    // 用子查询先按tool_id聚合borrow_qty，避免LEFT JOIN多借用记录导致SUM(total_qty)重复计算
    QJsonArray categoryStats;
    {
        QSqlQuery q(db);
        if (q.exec(
            "SELECT COALESCE(tc.category_name, '未分类') AS category, "
            "  COALESCE(SUM(ti.total_qty),0) AS totalCount, "
            "  COALESCE(SUM(ti.current_qty),0) AS availableCount, "
            "  COALESCE(SUM(bq.borrowed_qty),0) AS borrowedCount "
            "FROM tool_info ti "
            "LEFT JOIN tool_category tc ON ti.category_id = tc.category_id "
            "LEFT JOIN (SELECT tool_id, SUM(borrow_qty) AS borrowed_qty "
            "           FROM tool_borrow_record WHERE status IN ('borrowing','overdue') "
            "           GROUP BY tool_id) bq ON bq.tool_id = ti.tool_id "
            "GROUP BY tc.category_name "
            "ORDER BY totalCount DESC")) {
            while (q.next()) {
                QJsonObject cs;
                cs["category"]       = q.value("category").toString();
                cs["totalCount"]     = q.value("totalCount").toInt();
                cs["borrowedCount"]  = q.value("borrowedCount").toInt();
                cs["availableCount"] = q.value("availableCount").toInt();
                categoryStats.append(cs);
            }
        }
    }
    s["categoryStats"] = categoryStats;

    // 部门统计：JOIN tool_borrow_record + sys_user
    QJsonArray deptStats;
    {
        QSqlQuery q(db);
        if (q.exec(
            "SELECT COALESCE(d.dept_name, '未分配') AS department, "
            "  COUNT(r.record_id) AS borrowCount, "
            "  SUM(CASE WHEN r.status='overdue' THEN 1 ELSE 0 END) AS overdueCount "
            "FROM tool_borrow_record r "
            "LEFT JOIN sys_user u ON r.user_id = u.user_id "
            "LEFT JOIN sys_department d ON u.dept_id = d.dept_id "
            "GROUP BY d.dept_name "
            "ORDER BY borrowCount DESC")) {
            while (q.next()) {
                QJsonObject ds;
                int borrowC  = q.value("borrowCount").toInt();
                int overdueC = q.value("overdueCount").toInt();
                ds["department"]  = q.value("department").toString();
                ds["borrowCount"] = borrowC;
                ds["overdueCount"] = overdueC;
                ds["overdueRate"]  = (borrowC > 0) ? (double)overdueC / borrowC : 0.0;
                deptStats.append(ds);
            }
        }
    }
    s["departmentStats"] = deptStats;

    return s;
}

} // namespace db
