/**
 * @file AlertDAO.cpp
 * @brief 告警数据访问对象实现 — 合并QJsonObject API + 实体类API
 * @author 袁燕
 * 合并dao/AlertDAO.cpp的实体类API到此文件，统一namespace db管理
 */
#include "AlertDAO.h"
#include "DatabaseManager.h"
#include "common/PositionFormatter.h"  // common::formatPosition 统一位置格式化
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QCoreApplication>
#include "common/Logger.h"  // 统一日志写入入口（Log::appendLog）
#include "common/Constants.h"  // 告警状态常量 SC::ALERT_*

namespace db {

// 告警排序片段：未处理优先，其次按级别严重度（crit>error>warn>其他），最后按时间倒序
// findAll(QJsonObject API) 与 findAll(实体类API) 共用，避免两处重复维护同一排序规则
namespace {
const char* const ALERT_SORT_ORDER =
    " ORDER BY CASE a.status WHEN 'unhandled' THEN 0 ELSE 1 END, "
    "CASE at.alert_level WHEN 'crit' THEN 0 WHEN 'error' THEN 1 WHEN 'warn' THEN 2 ELSE 3 END, "
    "a.created_at DESC";
}

// 诊断日志写入文件（统一走 Logger 入口）
static void diagLog(const QString& msg) {
    Log::appendLog(QCoreApplication::applicationDirPath() + "/alert_diag.log", msg);
}

// ═══════════════════════════════════════════════
// 实体类转换 JOIN sys_alert_type获取typeName/level
// ═══════════════════════════════════════════════
/**
 * @brief 查询结果行转换为告警实体
 * @param q 已定位到有效行的查询结果
 * @return 填充完成的告警实体
 */
AlertLog AlertDAO::fromQuery(const QSqlQuery& q) {
    AlertLog alert;
    alert.alertId     = q.value("alert_id").toInt();
    alert.typeId      = q.value("type_id").toInt();
    // 类型名/编码/级别从JOIN sys_alert_type获取
    alert.typeCode    = q.value("type_code").toString();
    alert.typeName    = q.value("type_name").toString();
    alert.alertLevel  = q.value("alert_level").toString();
    alert.userId      = q.value("user_id").toInt();
    alert.toolId      = q.value("tool_id").toInt();
    alert.recordId    = q.value("record_id").toInt();
    alert.message     = q.value("content").toString();
    alert.status      = q.value("status").toString();  // 直接映射字符串状态
    alert.handledAt   = q.value("handled_at").toDateTime();
    alert.handledBy   = q.value("handler_id").toString();
    alert.createdAt   = q.value("created_at").toDateTime();
    alert.userName    = q.value("username").toString();
    alert.realName    = q.value("real_name").toString();
    alert.toolCode    = q.value("tool_code").toString();
    alert.toolName    = q.value("tool_name").toString();
    alert.flowNo      = q.value("flow_no").toString();
    return alert;
}

// ═══════════════════════════════════════════════
// QJsonObject API（Service层使用）type_id
// ═══════════════════════════════════════════════
/**
 * @brief 新增告警记录
 * @param alert 告警字段集合，键名使用驼峰形式
 * @return 新记录的ID；写入失败返回 -1
 */
int AlertDAO::insert(const QJsonObject& alert) {
    // 外键字段为0时写NULL，避免触发外键约束
    int typeId = alert["typeId"].toInt(0);
    QVariant userIdVal = alert["userId"].toInt(0) > 0 ? QVariant(alert["userId"].toInt()) : QVariant();
    QVariant toolIdVal = alert["toolId"].toInt(0) > 0 ? QVariant(alert["toolId"].toInt()) : QVariant();

    // 查sys_alert_type获取type_code和alert_level
    QString alertTypeCode;
    QString alertLevelStr;
    QSqlDatabase db = getDb();
    QSqlQuery typeQ(db);
    typeQ.prepare("SELECT type_code, alert_level FROM sys_alert_type WHERE type_id=?");
    typeQ.addBindValue(typeId);
    if (safeExec(typeQ) && typeQ.next()) {
        alertTypeCode = typeQ.value(0).toString();
        alertLevelStr = typeQ.value(1).toString();
    }

    QSqlQuery q(db);
    q.prepare("INSERT INTO sys_alert (type_id, alert_type, alert_level, tool_id, tool_code, content, status, user_id) "
              "VALUES (:tid,:at,:al,:tld,:tc,:ct,:st,:uid)");
    q.bindValue(":tid", typeId);
    q.bindValue(":at", alertTypeCode);
    q.bindValue(":al", alertLevelStr);
    q.bindValue(":tld", toolIdVal);
    q.bindValue(":tc", alert["toolCode"].toString(""));
    q.bindValue(":ct", alert["content"].toString());
    q.bindValue(":st", alert["status"].toString(SC::ALERT_UNHANDLED));
    q.bindValue(":uid", userIdVal);
    if (!safeExec(q)) { qWarning() << "[AlertDAO] insert:" << q.lastError().text(); return -1; }
    return q.lastInsertId().toInt();
}

/**
 * @brief 按告警ID查询详情
 * @param alertId 告警ID
 * @return 含类型名称与级别的详情对象；不存在时返回空对象
 */
QJsonObject AlertDAO::findById(int alertId) {
    QSqlDatabase db = getDb(); QSqlQuery q(db);
    q.prepare("SELECT a.*, at.type_code, at.type_name, at.alert_level, "
              "u.real_name AS user_name, ti.tool_name "
              "FROM sys_alert a "
              "LEFT JOIN sys_alert_type at ON a.type_id=at.type_id "
              "LEFT JOIN sys_user u ON a.user_id=u.user_id "
              "LEFT JOIN tool_info ti ON a.tool_id=ti.tool_id "
              "WHERE a.alert_id=:id");
    q.bindValue(":id", alertId);
    if (!safeExec(q) || !q.next()) return QJsonObject();
    QJsonObject o;
    o["alertId"]=q.value("alert_id").toInt();
    o["typeId"]=q.value("type_id").toInt();
    o["typeCode"]=q.value("type_code").toString();
    o["alertType"]=q.value("type_name").toString();     // [兼容] 前端展示用
    o["alertLevel"]=q.value("alert_level").toString();
    o["content"]=q.value("content").toString();
    o["status"]=q.value("status").toString();
    o["createdAt"]=q.value("created_at").toString();
    return o;
}

/**
 * @brief 分页查询告警列表
 * @param alertType 告警类型编码，为空不过滤
 * @param alertLevel 告警级别，为空不过滤
 * @param status 处理状态，为空不过滤
 * @param startDate 起始日期，为空表示不限
 * @param endDate 截止日期，为空表示不限
 * @param page 页码，从1开始
 * @param pageSize 每页条数
 * @return 含 list 数组与 total 总数的对象
 */
QJsonObject AlertDAO::findAll(const QString& alertType, const QString& alertLevel,
                               const QString& status, const QString& startDate,
                               const QString& endDate, int page, int pageSize) {
    QSqlDatabase db = getDb();
    QStringList conds; QMap<QString, QVariant> binds;
    // 类型筛选为按type_name过滤(JOIN后)
    if (!alertType.isEmpty()) { conds << "at.type_name=:tp"; binds[":tp"] = alertType; }
    if (!alertLevel.isEmpty()) { conds << "at.alert_level=:lv"; binds[":lv"] = alertLevel; }
    if (!status.isEmpty()) { conds << "a.status=:st"; binds[":st"] = status; }
    if (!startDate.isEmpty()) { conds << "a.created_at>=:sd"; binds[":sd"] = startDate; }
    if (!endDate.isEmpty()) { conds << "a.created_at<=:ed"; binds[":ed"] = endDate; }
    QString where = conds.isEmpty() ? "a.status != 'ignored'" : conds.join(" AND ") + " AND a.status != 'ignored'";

    QSqlQuery cq(db);
    cq.prepare("SELECT COUNT(*) FROM sys_alert a "
               "LEFT JOIN sys_alert_type at ON a.type_id=at.type_id WHERE " + where);
    for (auto it = binds.begin(); it != binds.end(); ++it) cq.bindValue(it.key(), it.value());
    safeExec(cq); cq.next(); int total = cq.value(0).toInt();

    QSqlQuery dq(db);
    // JOIN sys_alert_type获取type_name/level, 排序：待处理优先(unhandled在前)
    dq.prepare(
        "SELECT a.alert_id, a.type_id, a.tool_id, a.tool_code, a.content, "
        "a.status, a.user_id, a.created_at, a.handled_at, a.handler_id, a.remark, "
        "at.type_code, at.type_name, at.alert_level, "
        "u.real_name AS user_name, ti.tool_name, "
        "COALESCE(tc.cabinet_name,'') AS cabinet_name, ti.position AS tool_position, ti.layer AS tool_layer "
        "FROM sys_alert a "
        "LEFT JOIN sys_alert_type at ON a.type_id=at.type_id "
        "LEFT JOIN sys_user u ON a.user_id=u.user_id "
        "LEFT JOIN tool_info ti ON a.tool_id=ti.tool_id "
        "LEFT JOIN tool_cabinet tc ON ti.cabinet_id=tc.cabinet_id "
        "WHERE " + where + ALERT_SORT_ORDER + " LIMIT :lim OFFSET :off"
    );
    for (auto it = binds.begin(); it != binds.end(); ++it) dq.bindValue(it.key(), it.value());
    dq.bindValue(":lim", pageSize); dq.bindValue(":off", (page-1)*pageSize);
    safeExec(dq);

    QJsonArray list;
    while (dq.next()) {
        QJsonObject item;
        item["alertId"] = dq.value("alert_id").toInt();
        item["typeId"] = dq.value("type_id").toInt();
        item["typeCode"] = dq.value("type_code").toString();
        item["alertType"] = dq.value("type_name").toString();  // 直接返回中文类型名
        item["alertLevel"] = dq.value("alert_level").toString();
        item["toolId"] = dq.value("tool_id").toInt();
        item["toolCode"] = dq.value("tool_code").toString();
        item["toolName"] = dq.value("tool_name").toString();
        item["content"] = dq.value("content").toString();
        item["status"] = dq.value("status").toString();
        item["userName"] = dq.value("user_name").toString();
        QString cab = dq.value("cabinet_name").toString();
        QString pos = dq.value("tool_position").toString();
        QString lay = dq.value("tool_layer").toString();
        item["position"] = cab.isEmpty() ? pos : common::formatPosition(cab, lay, pos);
        item["createdAt"] = dq.value("created_at").toString();
        item["handledAt"] = dq.value("handled_at").toString();
        item["handlerId"] = dq.value("handler_id").toInt();
        item["remark"] = dq.value("remark").toString();
        list.append(item);
    }
    QJsonObject result; result["list"] = list; result["total"] = total;
    return result;
}

/**
 * @brief 确认告警：状态置为已处理并记录处理人与处理时间
 * @param alertId 告警ID
 * @param handlerId 处理人ID
 * @param remark 处理备注
 * @return true=更新成功
 */
bool AlertDAO::acknowledge(int alertId, int handlerId, const QString& remark) {
    QSqlDatabase db = getDb(); QSqlQuery q(db);
    // SQLite不支持NOW()，datetime('now')
    q.prepare("UPDATE sys_alert SET status='handled', handled_at=CURRENT_TIMESTAMP, handler_id=:hid, remark=:rmk WHERE alert_id=:id");
    q.bindValue(":hid", handlerId); q.bindValue(":rmk", remark); q.bindValue(":id", alertId);
    return safeExec(q);
}

/**
 * @brief 解决告警：语义同 acknowledge
 * @param alertId 告警ID
 * @param handlerId 处理人ID
 * @param remark 处理备注
 * @return true=更新成功
 */
bool AlertDAO::resolve(int alertId, int handlerId, const QString& remark) {
    return acknowledge(alertId, handlerId, remark);
}

/**
 * @brief 统计未处理告警数量
 * @return 状态为待处理的告警条数
 */
int AlertDAO::getUnresolvedCount() {
    QSqlDatabase db = getDb(); QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM sys_alert WHERE status='unhandled'");
    safeExec(q); q.next();
    return q.value(0).toInt();
}

// ═══════════════════════════════════════════════
// 实体类API（Controller层使用）JOIN sys_alert_type
// ═══════════════════════════════════════════════
/**
 * @brief 新增告警记录实体
 * @param a 待写入的告警实体，typeId 为0时按字典表补齐类型与级别
 * @return 新记录的ID；写入失败返回 0
 */
int AlertDAO::insertAlert(const AlertLog& a) {
    // 每次启动hasLegacyData检测alert_type=''→DELETE FROM sys_alert→清空用户产生的告警→"永远是20条"
    // 注意：INSERT补上alert_type和alert_level列，从sys_alert_type表查对应值
    // 外键约束：user_id/tool_id为0时设NULL

    QVariant userIdVal = (a.userId > 0) ? QVariant(a.userId) : QVariant();
    QVariant toolIdVal = (a.toolId > 0) ? QVariant(a.toolId) : QVariant();

    // 查sys_alert_type获取type_code和alert_level
    QString alertTypeCode;
    QString alertLevelStr;
    QSqlDatabase db = getDb();
    QSqlQuery typeQ(db);
    typeQ.prepare("SELECT type_code, alert_level FROM sys_alert_type WHERE type_id=?");
    typeQ.addBindValue(a.typeId);
    if (safeExec(typeQ) && typeQ.next()) {
        alertTypeCode = typeQ.value(0).toString();
        alertLevelStr = typeQ.value(1).toString();
    } else {
        qWarning() << "[AlertDAO] insertAlert: type_id=" << a.typeId << "not found in sys_alert_type, using defaults";
        alertTypeCode = "system";
        alertLevelStr = "warn";
    }

    // sys_alert表完整列：alert_id, type_id, alert_type, alert_level, tool_id, tool_code,
    // content, status, user_id, record_id, created_at, handled_at, handler_id, remark
    int result = insertAndGetId(
        "INSERT INTO sys_alert (type_id, alert_type, alert_level, user_id, tool_id, tool_code, content, status) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        {a.typeId, alertTypeCode, alertLevelStr, userIdVal, toolIdVal, a.toolCode, a.message, SC::ALERT_UNHANDLED});

    if (result <= 0) {
        // 增加诊断日志：INSERT失败时输出完整参数，便于定位
        qWarning() << "[AlertDAO] insertAlert FAILED: result=" << result
                   << "typeId=" << a.typeId << "userId=" << a.userId
                   << "toolId=" << a.toolId << "toolCode=" << a.toolCode
                   << "message=" << a.message.left(50);
    } else {
        qInfo() << "[AlertDAO] insertAlert SUCCESS: alertId=" << result
                << "typeId=" << a.typeId << "message=" << a.message.left(30);
    }
    return result;
}

/**
 * @brief 分页查询告警实体列表
 * @param page 页码，从1开始
 * @param pageSize 每页条数
 * @param type 告警类型编码，为空不过滤
 * @param level 告警级别，为空不过滤
 * @param handled 处理状态：-1 不限，0 待处理，1 已处理
 * @param startDate 起始日期，为空表示不限
 * @param endDate 截止日期，为空表示不限
 * @return 告警实体列表
 */
QList<AlertLog> AlertDAO::findAllAlerts(int page, int pageSize,
                                         const QString& type, const QString& level,
                                         int handled, const QDate& startDate, const QDate& endDate) {
    // JOIN sys_alert_type获取type_name/type_code/alert_level
    QString sql = "SELECT a.*, at.type_code, at.type_name, at.alert_level, "
                  "u.username, u.real_name, ti.tool_code, ti.tool_name, br.flow_no "
                  "FROM sys_alert a "
                  "LEFT JOIN sys_alert_type at ON a.type_id=at.type_id "
                  "LEFT JOIN sys_user u ON a.user_id=u.user_id "
                  "LEFT JOIN tool_info ti ON a.tool_id=ti.tool_id "
                  "LEFT JOIN tool_borrow_record br ON a.tool_id=br.tool_id "
                  "WHERE a.status != 'ignored' ";
    QVariantList params;
    // 类型筛选为按type_code匹配
    if (!type.isEmpty())    { sql += " AND at.type_code = ?";  params << type; }
    if (!level.isEmpty())   { sql += " AND at.alert_level = ?"; params << level; }
    if (handled >= 0)       { sql += " AND a.status = ?";  params << (handled ? SC::ALERT_HANDLED : SC::ALERT_UNHANDLED); }
    if (startDate.isValid()){ sql += " AND a.created_at >= ?";  params << startDate.startOfDay(); }
    if (endDate.isValid())  { sql += " AND a.created_at <= ?";  params << endDate.endOfDay(); }
    // 排序：待处理优先，级别严重优先，时间倒序
    sql += ALERT_SORT_ORDER;

    QList<AlertLog> list;
    QSqlQuery q = query(paginate(sql, page, pageSize), params);
    while (q.next()) list.append(fromQuery(q));
    return list;
}

/**
 * @brief 统计符合条件的告警数量
 * @return 告警条数；筛选条件与 findAllAlerts 保持一致
 */
int AlertDAO::countAlerts(const QString& type, const QString& level,
                           int handled, const QDate& startDate, const QDate& endDate) {
    QString sql = "SELECT a.* FROM sys_alert a "
                  "LEFT JOIN sys_alert_type at ON a.type_id=at.type_id "
                  "WHERE a.status != 'ignored' ";
    QVariantList params;
    if (!type.isEmpty())    { sql += " AND at.type_code = ?";  params << type; }
    if (!level.isEmpty())   { sql += " AND at.alert_level = ?"; params << level; }
    if (handled >= 0)       { sql += " AND a.status = ?";  params << (handled ? SC::ALERT_HANDLED : SC::ALERT_UNHANDLED); }
    if (startDate.isValid()){ sql += " AND a.created_at >= ?";  params << startDate.startOfDay(); }
    if (endDate.isValid())  { sql += " AND a.created_at <= ?";  params << endDate.endOfDay(); }
    return count(sql, params);
}

/**
 * @brief 将告警标记为已处理
 * @param alertId 告警ID
 * @param handledBy 处理人标识；因外键约束限制，此处不写入处理人ID
 * @return true=更新成功
 */
bool AlertDAO::markHandled(int alertId, const QString& handledBy) {
    // handler_id 置 NULL：外键约束 sys_alert.handler_id 引用 sys_user.user_id，
    // 写入不存在的ID会失败，故与其他标记方法保持一致

    QSqlDatabase db = getDb();
    if (!db.isOpen()) return false;
    QSqlQuery q(db);
    q.prepare("UPDATE sys_alert SET status='handled', handled_at=CURRENT_TIMESTAMP, handler_id=NULL WHERE alert_id=?");
    q.addBindValue(alertId);
    return safeExec(q);
}

/**
 * @brief 将全部未处理告警标记为已处理
 * @param handledBy 处理人标识；因外键约束限制，此处不写入处理人ID
 * @return true=更新成功
 */
bool AlertDAO::markAllHandled(const QString& handledBy) {
    // 同markHandled，handler_id=NULL避免外键约束失败
    QSqlDatabase db = getDb();
    if (!db.isOpen()) return false;
    QSqlQuery q(db);
    q.prepare("UPDATE sys_alert SET status='handled', handled_at=CURRENT_TIMESTAMP, handler_id=NULL WHERE status='unhandled'");
    return safeExec(q);
}

// 忽略告警：status设为ignored，DB保留记录供审计，列表查询时排除
// 采用 getDb()+QSqlQuery 直连路径，与其他写入方法保持一致
bool AlertDAO::markIgnored(int alertId, const QString& handlerBy) {
    // 增加完整诊断日志，定位忽略失败根因
    QSqlDatabase db = getDb();
    diagLog(QString("markIgnored START: alertId=%1 driver=%2 isOpen=%3 connName=%4")
            .arg(alertId).arg(db.driverName()).arg(db.isOpen()).arg(db.connectionName()));
    if (!db.isOpen()) {
        diagLog("markIgnored: database not open, attempting reopen");
        if (!db.open()) {
            diagLog(QString("markIgnored: reopen failed: %1").arg(db.lastError().text()));
            return false;
        }
    }
    QSqlQuery q(db);
    // MySQL外键约束fk_alert_handler: handler_id引用sys_user.user_id
    // handler_id 置 NULL：外键约束不允许写入 sys_user 中不存在的处理人ID
    QString sql = "UPDATE sys_alert SET status='ignored', handled_at=CURRENT_TIMESTAMP, handler_id=NULL WHERE alert_id=?";
    q.prepare(sql);
    q.addBindValue(alertId);
    if (!safeExec(q)) {
        diagLog(QString("markIgnored SQL FAILED: error=%1 sql=%2 alertId=%3")
                .arg(q.lastError().text()).arg(sql).arg(alertId));
        return false;
    }
    diagLog(QString("markIgnored SUCCESS: alertId=%1 rowsAffected=%2").arg(alertId).arg(q.numRowsAffected()));
    return true;
}

/**
 * @brief 统计未处理告警数量
 * @return 状态为待处理的告警条数
 */
int AlertDAO::unhandledCount() {
    return scalar("SELECT COUNT(*) FROM sys_alert WHERE status='unhandled'").toInt();
}

/**
 * @brief 统计今日新增告警数
 * @return 今日创建的告警条数
 */
int AlertDAO::todayTotal() {
    return scalar("SELECT COUNT(*) FROM sys_alert WHERE DATE(created_at)=CURDATE()").toInt();
}

// 获取所有启用的告警类型列表，按sort_order排序
QJsonArray AlertDAO::getAlertTypes() {
    QJsonArray arr;
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT type_id, type_code, type_name, alert_level, sort_order "
              "FROM sys_alert_type WHERE is_active=1 ORDER BY sort_order ASC");
    if (!safeExec(q)) { qWarning() << "[AlertDAO] getAlertTypes:" << q.lastError().text(); return arr; }
    while (q.next()) {
        QJsonObject t;
        t["typeId"]     = q.value("type_id").toInt();
        t["typeCode"]   = q.value("type_code").toString();
        t["typeName"]   = q.value("type_name").toString();
        t["alertLevel"] = q.value("alert_level").toString();
        t["sortOrder"]  = q.value("sort_order").toInt();
        arr.append(t);
    }
    return arr;
}

/**
 * @brief 查询最近的未处理告警
 * @param limit 最多返回条数
 * @return 告警数组，按创建时间倒序
 */
QJsonArray AlertDAO::findRecentUnhandled(int limit) {
    QJsonArray arr;
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT a.alert_id, at.type_name AS alert_type, at.alert_level, "
              "a.content, a.created_at, a.tool_code "
              "FROM sys_alert a "
              "LEFT JOIN sys_alert_type at ON a.type_id = at.type_id "
              "WHERE a.status='unhandled' "
              "ORDER BY CASE at.alert_level WHEN 'crit' THEN 0 WHEN 'error' THEN 1 WHEN 'warn' THEN 2 ELSE 3 END, "
              "a.created_at DESC LIMIT :lim");
    q.bindValue(":lim", limit);
    if (!safeExec(q)) return arr;
    while (q.next()) {
        QJsonObject o;
        o["alertId"]    = q.value("alert_id").toInt();
        o["alertType"]  = q.value("alert_type").toString();
        o["alertLevel"] = q.value("alert_level").toString();
        o["content"]    = q.value("content").toString();
        o["createdAt"]  = q.value("created_at").toString();
        o["toolCode"]   = q.value("tool_code").toString();
        arr.append(o);
    }
    return arr;
}

/**
 * @brief 按告警ID查询详情（含工具与用户关联信息）
 * @param alertId 告警ID
 * @return 详情对象；不存在时返回空对象
 */
QJsonObject AlertDAO::findDetailById(int alertId) {
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare(
        "SELECT a.alert_id, a.type_id, a.content, a.created_at, "
        "  a.status, a.tool_code, a.user_id, a.handled_at, a.handler_id, a.remark, "
        "  at.type_code, at.type_name, at.alert_level, "
        "  COALESCE(t.tool_name, '--') AS tool_name, "
        "  COALESCE(c.cabinet_name, '--') AS cabinet_name, "
        "  COALESCE(t.position, '--') AS position, "
        "  COALESCE(u.real_name, u.username, '--') AS borrower_name, "
        "  COALESCE(h.real_name, h.username, '--') AS handler_name "
        "FROM sys_alert a "
        "LEFT JOIN sys_alert_type at ON a.type_id = at.type_id "
        "LEFT JOIN tool_info t ON a.tool_code = t.tool_code "
        "LEFT JOIN tool_cabinet c ON t.cabinet_id = c.cabinet_id "
        "LEFT JOIN sys_user u ON a.user_id = u.user_id "
        "LEFT JOIN sys_user h ON a.handler_id = h.user_id "
        "WHERE a.alert_id = :id");
    q.bindValue(":id", alertId);
    if (!safeExec(q) || !q.next()) return QJsonObject();
    QJsonObject detail;
    detail["alertId"]      = q.value("alert_id").toInt();
    detail["typeId"]       = q.value("type_id").toInt();
    detail["typeCode"]     = q.value("type_code").toString();
    detail["alertType"]    = q.value("type_name").toString();
    detail["alertLevel"]   = q.value("alert_level").toString();
    detail["content"]      = q.value("content").toString();
    detail["createdAt"]    = q.value("created_at").toString();
    detail["status"]       = q.value("status").toString();
    detail["toolCode"]     = q.value("tool_code").toString();
    detail["toolName"]     = q.value("tool_name").toString();
    detail["cabinetName"]  = q.value("cabinet_name").toString();
    detail["position"]     = q.value("position").toString();
    detail["borrowerName"] = q.value("borrower_name").toString();
    detail["handledAt"]    = q.value("handled_at").toString();
    detail["handlerName"]  = q.value("handler_name").toString();
    detail["remark"]       = q.value("remark").toString();
    return detail;
}

/**
 * @brief 统计告警数量
 * @param types 逗号分隔的类型编码列表，为空表示不限
 * @param level 告警级别，为空表示不限
 * @param keyword 关键字，为空表示不限
 * @return 含各级别计数的统计对象
 */
QJsonObject AlertDAO::getStats(const QString& types, const QString& level, const QString& keyword) {
    QSqlDatabase db = getDb();
    QString where = "WHERE 1=1 ";
    QVariantList binds;
    if (!types.isEmpty()) {
        QStringList typeList = types.split(",", Qt::SkipEmptyParts);
        if (typeList.size() == 1) {
            where += "AND at.type_code = ? ";
            binds.append(typeList[0]);
        } else if (typeList.size() > 1) {
            where += "AND at.type_code IN (" + QString("?,").repeated(typeList.size()-1) + "?) ";
            for (const QString& t : typeList) binds.append(t);
        }
    }
    if (!level.isEmpty()) {
        QString dbLevel = level;
        if (level == QStringLiteral("严重")) dbLevel = "error";
        else if (level == QStringLiteral("一般")) dbLevel = "warn";
        else if (level == QStringLiteral("提示")) dbLevel = "info";
        where += "AND at.alert_level = ? ";
        binds.append(dbLevel);
    }
    if (!keyword.isEmpty()) {
        where += "AND (COALESCE(t.tool_name,'') LIKE ? OR a.content LIKE ?) ";
        binds.append(QString("%%1%").arg(keyword));
        binds.append(QString("%%1%").arg(keyword));
    }
    QSqlQuery q(db);
    q.prepare(
        "SELECT "
        "  COUNT(*) AS total, "
        "  SUM(CASE WHEN at.alert_level IN ('error','crit') THEN 1 ELSE 0 END) AS crit, "
        "  SUM(CASE WHEN at.alert_level = 'warn' THEN 1 ELSE 0 END) AS warn, "
        "  SUM(CASE WHEN at.alert_level = 'info' THEN 1 ELSE 0 END) AS info, "
        "  SUM(CASE WHEN a.status = 'handled' THEN 1 ELSE 0 END) AS resolved "
        "FROM sys_alert a "
        "LEFT JOIN sys_alert_type at ON a.type_id = at.type_id "
        "LEFT JOIN tool_info t ON a.tool_code = t.tool_code "
        + where);
    for (int i = 0; i < binds.size(); ++i) q.bindValue(i, binds[i]);
    QJsonObject stats;
    if (safeExec(q) && q.next()) {
        stats["total"]    = q.value("total").toInt();
        stats["crit"]     = q.value("crit").toInt();
        stats["warn"]     = q.value("warn").toInt();
        stats["info"]     = q.value("info").toInt();
        stats["resolved"] = q.value("resolved").toInt();
    }
    return stats;
}

// 按type_code查询type_id，找不到返回-1
int AlertDAO::findTypeIdByCode(const QString& typeCode)
{
    if (typeCode.isEmpty()) return -1;
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT type_id FROM sys_alert_type WHERE type_code=? AND is_active=1");
    q.addBindValue(typeCode);
    if (safeExec(q) && q.next()) return q.value(0).toInt();
    return -1;
}

// 按type_id查询alert_level，找不到返回空字符串
QString AlertDAO::findTypeLevelById(int typeId)
{
    if (typeId <= 0) return QString();
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT alert_level FROM sys_alert_type WHERE type_id=?");
    q.addBindValue(typeId);
    if (safeExec(q) && q.next()) return q.value(0).toString();
    return QString();
}

} // namespace db
