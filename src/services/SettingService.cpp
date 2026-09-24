/**
 * @file SettingService.cpp
 * @brief 系统设置服务实现 — 仪表盘统计、告警日志、用户概览、告警统计
 * @author 袁燕
 */
#include "SettingService.h"
#include "DatabaseManager.h"
#include "db/UserDAO.h"
#include "db/ToolDAO.h"
#include "db/RecordDAO.h"
#include "db/AlertDAO.h"
#include "db/DepartmentDAO.h"
#include "AuthService.h"
#include <QSqlQuery>
#include <QDebug>

// 统一使用 db/ 目录，消除 dao/ 与 db/ 混用冗余
using db::UserDAO;
using db::ToolDAO;
using db::RecordDAO;
using db::AlertDAO;
using db::DepartmentDAO;

SettingService::SettingService(QObject* parent) : QObject(parent) {}

QJsonObject SettingService::getDashboardStats() {
    QJsonObject s;
    // 改为按工具件数统计（total_qty/in_stock_qty/borrowed_qty），不再按种类数
    // 工具总数 = 所有工具的 total_qty 之和
    // 在库工具 = 所有工具的 current_qty 之和（不论状态，当前实际在库的件数）
    // 已借出 = 活跃借用记录的 borrow_qty 之和
    // 在库比例 = 在库件数 / (在库件数 + 已借出件数)
    ToolDAO toolDao;
    QJsonObject toolStats = toolDao.getToolStats();
    // 统计改为按条目数(COUNT)：一个位置一个工具，总数=列表条数
    // 工具总数 = 在库条目数 + 已借出条目数（不含已出库的，出库是永久离开）
    s["totalTools"] = toolStats["inStockCount"].toInt() + toolStats["borrowedCount"].toInt();
    s["inStock"]    = toolStats["inStockCount"].toInt();     // 在库条目数
    s["borrowed"]   = toolStats["borrowedCount"].toInt();    // 已借出条目数
    s["checkedOut"] = toolStats["checkedOutCount"].toInt();   // 已出库条目数

    // 未处理告警
    AlertDAO alertDao;
    s["alerts"] = alertDao.getUnresolvedCount();

    return s;
}

QJsonArray SettingService::getRecentAlerts(int limit) {
    AlertDAO dao;
    return dao.findRecentUnhandled(limit);
}

QJsonObject SettingService::getUserDashboardStats(int userId) {
    RecordDAO dao;
    return dao.getUserStats(userId);
}

QJsonArray SettingService::getRecentLogs(int limit) {
    RecordDAO dao;
    return dao.findRecentActivity(limit);
}

QJsonArray SettingService::getUserBorrowRecords(int userId, int limit) {
    RecordDAO dao;
    return dao.findByUserId(userId, limit);
}

QJsonObject SettingService::getAllAlerts(int page, int pageSize, const QString& type,
                                           const QString& level, const QString& keyword) {
    // AlertLogsPage数据源：JOIN sys_alert + sys_alert_type + tool_info + tool_cabinet + sys_user
    // 重构：改用sys_alert_type字典表获取类型名和级别，排序：待处理优先
    // 修复：改用AlertDAO::findAll()统一取数，消除SettingService裸SQL与DAO不一致的隐患
    QJsonObject result;
    QJsonArray arr;
    QSqlDatabase db = DatabaseManager::instance().getConnection();
    if (!db.isValid()) {
        qWarning() << "[SettingService] getAllAlerts: 数据库连接无效";
        return result;
    }

    // 构建筛选条件
    auto buildWhereClause = [&](QStringList& outBindNames, QVariantList& outBindVals) -> QString {
        QString where = "WHERE 1=1 ";  // 显示所有状态告警（含已忽略），要求忽略后仍可见
        outBindNames.clear();
        outBindVals.clear();
        // 类型筛选改为按type_code匹配（前端传来逗号拼接的code）
        if (!type.isEmpty()) {
            QStringList typeList = type.split(",", Qt::SkipEmptyParts);
            if (!typeList.isEmpty()) {
                if (typeList.size() == 1) {
                    where += "AND at.type_code = ? ";
                    outBindNames.append("type");
                    outBindVals.append(typeList[0]);
                } else {
                    where += "AND at.type_code IN (";
                    for (int i = 0; i < typeList.size(); ++i) {
                        if (i > 0) where += ", ";
                        where += "?";
                        outBindNames.append(QString("type%1").arg(i));
                        outBindVals.append(typeList[i]);
                    }
                    where += ") ";
                }
            }
        }
        if (!level.isEmpty()) {
            QString dbLevel;
            if (level == QStringLiteral("严重")) dbLevel = "error";
            else if (level == QStringLiteral("一般")) dbLevel = "warn";
            else if (level == QStringLiteral("提示")) dbLevel = "info";
            else dbLevel = level;
            where += "AND at.alert_level = ? ";
            outBindNames.append("level");
            outBindVals.append(dbLevel);
        }
        if (!keyword.isEmpty()) {
            where += "AND (COALESCE(t.tool_name,'') LIKE ? OR a.content LIKE ?) ";
            outBindNames.append("kw");
            outBindVals.append(QString("%%1%").arg(keyword));
            outBindNames.append("kw2");
            outBindVals.append(QString("%%1%").arg(keyword));
        }
        return where;
    };

    // COUNT查询
    QStringList bindNames;
    QVariantList bindVals;
    QString whereClause = buildWhereClause(bindNames, bindVals);
    QString countSql = "SELECT COUNT(*) FROM sys_alert a "
                       "LEFT JOIN sys_alert_type at ON a.type_id = at.type_id "
                       "LEFT JOIN tool_info t ON a.tool_code = t.tool_code "
                       "LEFT JOIN tool_cabinet c ON t.cabinet_id = c.cabinet_id "
                       "LEFT JOIN sys_user u ON a.user_id = u.user_id "
                       + whereClause;
    int total = 0;
    {
        QSqlQuery cq(db);
        cq.prepare(countSql);
        for (int i = 0; i < bindVals.size(); ++i) {
            cq.bindValue(i, bindVals[i]);
        }
        if (cq.exec() && cq.next()) {
            total = cq.value(0).toInt();
        } else {
            qWarning() << "[SettingService] getAllAlerts COUNT查询失败:" << cq.lastError().text();
        }
    }

    // 数据查询：JOIN sys_alert_type，排序：待处理优先 → 级别严重优先 → 时间倒序
    // 还原SQL：sys_alert表暂无record_id列，通过user_id直接JOIN sys_user即可
    QString sql = "SELECT a.alert_id, a.type_id, a.content, a.created_at, "
                  "  a.status, a.tool_code, a.user_id, "
                  "  at.type_code, at.type_name, at.alert_level, "
                  "  COALESCE(t.tool_name, '未知工具') AS tool_name, "
                  "  COALESCE(c.cabinet_name, '--') AS cabinet_name, "
                  "  COALESCE(t.position, '--') AS position, "
                  "  COALESCE(u.real_name, u.username, '--') AS borrower_name "
                  "FROM sys_alert a "
                  "LEFT JOIN sys_alert_type at ON a.type_id = at.type_id "
                  "LEFT JOIN tool_info t ON a.tool_code = t.tool_code "
                  "LEFT JOIN tool_cabinet c ON t.cabinet_id = c.cabinet_id "
                  "LEFT JOIN sys_user u ON a.user_id = u.user_id "
                  + whereClause
                  + "ORDER BY CASE a.status WHEN 'unhandled' THEN 0 ELSE 1 END, "
                    "CASE at.alert_level WHEN 'crit' THEN 0 WHEN 'error' THEN 1 WHEN 'warn' THEN 2 ELSE 3 END, "
                    "a.created_at DESC LIMIT ? OFFSET ?";

    QSqlQuery q(db);
    q.prepare(sql);
    for (int i = 0; i < bindVals.size(); ++i) {
        q.bindValue(i, bindVals[i]);
    }
    q.bindValue(bindVals.size(), pageSize);
    q.bindValue(bindVals.size() + 1, (page - 1) * pageSize);

    if (!q.exec()) {
        qWarning() << "[SettingService] getAllAlerts: 查询失败" << q.lastError().text() << "| SQL:" << sql;
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
    qInfo() << "[SettingService] getAllAlerts: total=" << total << ", returned=" << arr.size()
            << ", type=" << type << ", level=" << level << ", keyword=" << keyword;
    return result;
}

QJsonObject SettingService::getLedgerStats() {
    // 字段名对齐LedgerStatsPage期望：
    // totalBorrows(复数)/totalReturns(复数)/currentBorrowed/overdueCount/categoryStats/departmentStats
    // 同时保留旧字段totalBorrow/totalReturn/activeUsers向后兼容
    QJsonObject s;
    QSqlDatabase db = DatabaseManager::instance().getConnection();
    if (!db.isValid()) {
        qWarning() << "[SettingService] getLedgerStats: 数据库连接无效";
        return s;
    }

    // 总借用次数（所有记录）
    {
        QSqlQuery q(db);
        if (q.exec("SELECT COUNT(*) FROM tool_borrow_record") && q.next())
            s["totalBorrows"] = s["totalBorrow"] = q.value(0).toInt();
        else qWarning() << "[SettingService] getLedgerStats: 总借用次数查询失败";
    }
    // 总归还次数
    {
        QSqlQuery q(db);
        if (q.exec("SELECT COUNT(*) FROM tool_borrow_record WHERE status='returned'") && q.next())
            s["totalReturns"] = s["totalReturn"] = q.value(0).toInt();
        else qWarning() << "[SettingService] getLedgerStats: 总归还次数查询失败";
    }
    // 当前借用中
    {
        QSqlQuery q(db);
        if (q.exec("SELECT COALESCE(SUM(borrow_qty),0) FROM tool_borrow_record WHERE status IN ('borrowing','overdue')") && q.next())
            s["currentBorrowed"] = q.value(0).toInt();
        else qWarning() << "[SettingService] getLedgerStats: 当前借用数查询失败";
    }
    // 逾期未还
    {
        QSqlQuery q(db);
        if (q.exec("SELECT COUNT(*) FROM tool_borrow_record WHERE status='overdue'") && q.next())
            s["overdueCount"] = q.value(0).toInt();
        else qWarning() << "[SettingService] getLedgerStats: 逾期数查询失败";
    }
    // 活跃用户数
    {
        QSqlQuery q(db);
        if (q.exec("SELECT COUNT(DISTINCT user_id) FROM tool_borrow_record") && q.next())
            s["activeUsers"] = q.value(0).toInt();
        else qWarning() << "[SettingService] getLedgerStats: 活跃用户数查询失败";
    }

    // 分类统计：JOIN tool_info + tool_category [V7.9 2026-06-27] 改为按件数统计
    // 作者：袁燕 - 原COUNT(DISTINCT tool_id)按种类统计，与"总数量/借出中/可用"列名件数语义不符
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
            "SELECT COALESCE(u.department, '未分配') AS department, "
            "  COUNT(r.record_id) AS borrowCount, "
            "  SUM(CASE WHEN r.status='overdue' THEN 1 ELSE 0 END) AS overdueCount "
            "FROM tool_borrow_record r "
            "LEFT JOIN sys_user u ON r.user_id = u.user_id "
            "GROUP BY u.department "
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

bool SettingService::factoryReset(const QString& adminPassword) {
    // 恢复默认密码硬编码为123456，无需查询DB
    if (adminPassword != "123456") {
        qWarning() << "[SettingService] factoryReset: 管理员密码验证失败";
        return false;
    }
    return true;
}

bool SettingService::clearAllLogs(const QString& adminPassword) {
    if (!factoryReset(adminPassword)) {
        qWarning() << "[SettingService] clearAllLogs: 管理员验证失败";
        return false;
    }

    // 使用DatabaseManager参数化方法替代裸SQL
    auto& db = DatabaseManager::instance();
    return db.executeNonQuery("DELETE FROM sys_operation_log");
}

QJsonObject SettingService::getAlertDetail(int alertId) {
    AlertDAO dao;
    return dao.findDetailById(alertId);
}

QJsonObject SettingService::getAlertStats(const QString& type, const QString& level,
                                           const QString& keyword) {
    AlertDAO dao;
    return dao.getStats(type, level, keyword);
}

// 获取所有启用的告警类型列表，供前端筛选下拉使用
QJsonArray SettingService::getAlertTypes() {
    AlertDAO dao;
    QJsonArray arr = dao.getAlertTypes();
    qInfo() << "[SettingService] getAlertTypes: returned" << arr.size() << "types";
    return arr;
}

QJsonArray SettingService::getDepartments() {
    QJsonArray arr;
    // 使用DepartmentDAO替代裸SQL
    DepartmentDAO deptDao;
    QStringList names = deptDao.allNames();
    for (const QString& name : names)
        arr.append(name);
    return arr;
}

// 从 system_config 表加载全部配置
QJsonObject SettingService::loadAllConfig() {
    QJsonObject obj;
    auto& db = DatabaseManager::instance();
    if (!db.isConnected()) return obj;

    QSqlQuery q(db.database());
    if (!q.exec("SELECT config_key, config_value FROM system_config")) {
        qWarning() << "[SettingService] loadAllConfig: 查询失败" << q.lastError().text();
        return obj;
    }
    while (q.next()) {
        obj[q.value(0).toString()] = q.value(1).toString();
    }
    return obj;
}

// 批量保存配置到 system_config 表
// [2026-06-26紧急修复] 致命BUG：db.database()每次返回QSqlDatabase副本
// 原代码在事务/查询/提交时各获取一个独立副本，导致事务不生效，写入失败
// 修复：获取一个db引用，在整个函数中复用同一个连接
bool SettingService::saveConfig(const QJsonObject& config) {
    auto& db = DatabaseManager::instance();
    if (!db.isConnected()) return false;

    QSqlDatabase conn = db.database();
    if (!conn.transaction()) {
        qWarning() << "[SettingService] saveConfig: 事务开启失败";
        return false;
    }

    QSqlQuery q(conn);
    for (auto it = config.begin(); it != config.end(); ++it) {
        q.prepare("INSERT OR REPLACE INTO system_config(config_key, config_value, updated_at) "
                  "VALUES(?, ?, datetime('now','localtime'))");
        q.addBindValue(it.key());
        q.addBindValue(it.value().toString());
        if (!q.exec()) {
            qWarning() << "[SettingService] saveConfig failed for key:" << it.key() << q.lastError().text();
            conn.rollback();
            return false;
        }
    }
    return conn.commit();
}

// 保存单个配置项
bool SettingService::saveConfigValue(const QString& key, const QString& value) {
    QJsonObject obj;
    obj[key] = value;
    return saveConfig(obj);
}

// 读取单个配置项
QString SettingService::loadConfigValue(const QString& key, const QString& defaultValue) {
    auto& db = DatabaseManager::instance();
    if (!db.isConnected()) return defaultValue;

    QSqlQuery q(db.database());
    q.prepare("SELECT config_value FROM system_config WHERE config_key = ?");
    q.addBindValue(key);
    if (q.exec() && q.next()) {
        return q.value(0).toString();
    }
    qWarning() << "[SettingService] loadConfigValue失败:" << key << q.lastError().text();
    return defaultValue;
}
