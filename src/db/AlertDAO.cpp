/**
 * @file AlertDAO.cpp
 * @brief 告警数据访问对象实现，提供告警记录的分页查询、新增、类型与统计接口
 * @author 袁燕
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
// 被 findAllPaged 复用，保证列表排序口径一致
namespace {
const char* const ALERT_SORT_ORDER =
    " ORDER BY CASE a.status WHEN 'unhandled' THEN 0 ELSE 1 END, "
    "CASE at.alert_level WHEN 'crit' THEN 0 WHEN 'error' THEN 1 WHEN 'warn' THEN 2 ELSE 3 END, "
    "a.created_at DESC";
}


/**
 * @brief 分页查询告警列表（AlertLogsPage数据源，自SettingService原样迁移）
 *        JOIN sys_alert_type + tool_info + tool_cabinet + sys_user，
 *        与 findAll 的区别：本方法含 tool_cabinet JOIN 与位置格式化字段，
 *        且显示所有状态告警（含已忽略），排序复用 ALERT_SORT_ORDER。
 * @param type 逗号分隔的告警类型编码（type_code），为空不过滤
 * @param level 告警级别，兼容显示值（严重/一般/提示）与库值，为空不过滤
 * @param keyword 关键字（匹配工具名/告警内容），为空不过滤
 * @param page 页码，从1开始
 * @param pageSize 每页条数
 * @return {list: [...], total: n}，字段名 camelCase
 */
QJsonObject AlertDAO::findAllPaged(const QString& type, const QString& level,
                                   const QString& keyword, int page, int pageSize) {
    QSqlDatabase db = getDb();
    if (!db.isValid()) {
        qWarning() << "[AlertDAO] findAllPaged: 数据库连接无效";
        return {};
    }

    // 构建筛选条件（占位符 ? 位置绑定）
    auto buildWhereClause = [&](QVariantList& outBindVals) -> QString {
        QString where = "WHERE 1=1 ";  // 显示所有状态告警（含已忽略），要求忽略后仍可见
        outBindVals.clear();
        if (!type.isEmpty()) {
            QStringList typeList = type.split(",", Qt::SkipEmptyParts);
            if (!typeList.isEmpty()) {
                if (typeList.size() == 1) {
                    where += "AND at.type_code = ? ";
                    outBindVals.append(typeList[0]);
                } else {
                    where += "AND at.type_code IN (";
                    for (int i = 0; i < typeList.size(); ++i) {
                        if (i > 0) where += ", ";
                        where += "?";
                        outBindVals.append(typeList[i]);
                    }
                    where += ") ";
                }
            }
        }
        if (!level.isEmpty()) {
            where += "AND at.alert_level = ? ";
            outBindVals.append(SC::alertLevelCodeFromText(level));
        }
        if (!keyword.isEmpty()) {
            where += "AND (COALESCE(t.tool_name,'') LIKE ? OR a.content LIKE ?) ";
            outBindVals.append(QString("%%1%").arg(keyword));
            outBindVals.append(QString("%%1%").arg(keyword));
        }
        return where;
    };

    QVariantList bindVals;
    QString whereClause = buildWhereClause(bindVals);

    // COUNT查询
    QString countSql = "SELECT COUNT(*) FROM sys_alert a "
                       "LEFT JOIN sys_alert_type at ON a.type_id = at.type_id "
                       "LEFT JOIN tool_info t ON a.tool_id = t.tool_id "
                       "LEFT JOIN tool_cabinet c ON t.cabinet_id = c.cabinet_id "
                       "LEFT JOIN sys_user u ON a.user_id = u.user_id "
                       + whereClause;
    int total = 0;
    {
        QSqlQuery cq(db);
        cq.prepare(countSql);
        for (int i = 0; i < bindVals.size(); ++i) cq.bindValue(i, bindVals[i]);
        if (cq.exec() && cq.next()) {
            total = cq.value(0).toInt();
        } else {
            qWarning() << "[AlertDAO] findAllPaged COUNT查询失败:" << cq.lastError().text();
        }
    }

    // 数据查询：待处理优先 → 级别严重优先 → 时间倒序（复用 ALERT_SORT_ORDER）
    // 位置优先从 tool_position_mapping 取（按 t.tool_id 关联），无映射兜底 tool_info 冗余列
    QString sql = "SELECT a.alert_id, a.type_id, a.content, a.created_at, "
                  "  a.status, t.tool_code, a.user_id, "
                  "  at.type_code, at.type_name, at.alert_level, "
                  "  COALESCE(t.tool_name, '未知工具') AS tool_name, "
                  "  COALESCE((SELECT mpm_cb.cabinet_name FROM tool_position_mapping mpm JOIN tool_cabinet mpm_cb ON mpm.cabinet_id=mpm_cb.cabinet_id WHERE mpm.tool_id=t.tool_id AND mpm.status='in_stock' LIMIT 1), COALESCE(c.cabinet_name,'--')) AS cabinet_name, "
                  "  COALESCE((SELECT mpm.position FROM tool_position_mapping mpm WHERE mpm.tool_id=t.tool_id AND mpm.status='in_stock' LIMIT 1), COALESCE(t.position,'--')) AS position, "
                  "  COALESCE(u.real_name, u.username, '--') AS borrower_name "
                  "FROM sys_alert a "
                  "LEFT JOIN sys_alert_type at ON a.type_id = at.type_id "
                  "LEFT JOIN tool_info t ON a.tool_id = t.tool_id "
                  "LEFT JOIN tool_cabinet c ON t.cabinet_id = c.cabinet_id "
                  "LEFT JOIN sys_user u ON a.user_id = u.user_id "
                  + whereClause + ALERT_SORT_ORDER + " LIMIT ? OFFSET ?";

    QSqlQuery q(db);
    q.prepare(sql);
    for (int i = 0; i < bindVals.size(); ++i) q.bindValue(i, bindVals[i]);
    q.bindValue(bindVals.size(), pageSize);
    q.bindValue(bindVals.size() + 1, (page - 1) * pageSize);

    QJsonObject result;
    QJsonArray arr;
    if (!q.exec()) {
        qWarning() << "[AlertDAO] findAllPaged: 查询失败" << q.lastError().text() << "| SQL:" << sql;
        result["list"] = arr;
        result["total"] = 0;
        return result;
    }
    while (q.next()) {
        QJsonObject o;
        o["alertId"]      = q.value("alert_id").toInt();
        o["typeId"]       = q.value("type_id").toInt();
        o["typeCode"]     = q.value("type_code").toString();
        o["alertType"]    = q.value("type_name").toString();   // 直接返回中文类型名
        o["alertLevel"]   = q.value("alert_level").toString();
        o["content"]      = q.value("content").toString();
        o["createdAt"]    = q.value("created_at").toString();
        o["status"]       = q.value("status").toString();
        o["toolCode"]     = q.value("tool_code").toString();
        o["toolName"]     = q.value("tool_name").toString();
        o["cabinetName"]  = q.value("cabinet_name").toString();
        o["position"]     = q.value("position").toString();
        o["borrowerName"] = q.value("borrower_name").toString();
        arr.append(o);
    }
    result["list"] = arr;
    result["total"] = total;
    qInfo() << "[AlertDAO] findAllPaged: total=" << total << ", returned=" << arr.size()
            << ", type=" << type << ", level=" << level << ", keyword=" << keyword;
    return result;
}


// ═══════════════════════════════════════════════
// 告警写入与字典查询
// ═══════════════════════════════════════════════
/**
 * @brief 新增告警记录实体
 * @param alert 待写入的告警实体，typeId 为0表示未分类告警
 * @return 新记录的ID；写入失败返回 0
 */
int AlertDAO::insertAlert(const AlertLog& alert) {
    // 类型编码与级别由 type_id 关联字典表 sys_alert_type 取得，本表不再重复存放
    // 外键约束：user_id/tool_id为0时设NULL
    QVariant userIdVal = (alert.userId > 0) ? QVariant(alert.userId) : QVariant();
    QVariant toolIdVal = (alert.toolId > 0) ? QVariant(alert.toolId) : QVariant();

    // created_at 列无默认值，写入方不传会落 NULL（告警列表无时间可显示），统一补当前时间
    int result = insertAndGetId(
        "INSERT INTO sys_alert (type_id, user_id, tool_id, content, status, created_at) "
        "VALUES (?, ?, ?, ?, ?, NOW())",
        {alert.typeId, userIdVal, toolIdVal, alert.message, SC::ALERT_UNHANDLED});

    if (result <= 0) {
        // 增加诊断日志：INSERT失败时输出完整参数，便于定位
        qWarning() << "[AlertDAO] insertAlert FAILED: result=" << result
                   << "typeId=" << alert.typeId << "userId=" << alert.userId
                   << "toolId=" << alert.toolId
                   << "message=" << alert.message.left(50);
    } else {
        qInfo() << "[AlertDAO] insertAlert SUCCESS: alertId=" << result
                << "typeId=" << alert.typeId << "message=" << alert.message.left(30);
    }
    return result;
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


} // namespace db
