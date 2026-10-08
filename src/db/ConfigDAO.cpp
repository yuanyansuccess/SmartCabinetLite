/**
 * @file ConfigDAO.cpp
 * @brief 系统配置数据访问对象实现（system_config 表，SQL 自 SettingService 原样迁移）
 * @author 袁燕
 */
#include "ConfigDAO.h"
#include <QSqlQuery>
#include <QDebug>

namespace db {

QJsonObject ConfigDAO::loadAll() {
    QJsonObject obj;
    QSqlDatabase db = getDb();
    if (!db.isValid()) return obj;

    QSqlQuery q(db);
    if (!q.exec("SELECT config_key, config_value FROM system_config")) {
        qWarning() << "[ConfigDAO] loadAll: 查询失败" << q.lastError().text();
        return obj;
    }
    while (q.next()) {
        obj[q.value(0).toString()] = q.value(1).toString();
    }
    return obj;
}

bool ConfigDAO::saveBatch(const QJsonObject& config) {
    if (config.isEmpty()) return true;

    // 历史坑：db.database() 每次返回 QSqlDatabase 副本，事务/查询/提交各取一个
    // 独立副本会导致事务不生效。BaseDAO::transaction 走 DatabaseManager 单例连接，
    // 天然保证 begin/query/commit 复用同一连接。
    return transaction([&]() -> bool {
        QSqlQuery q(getDb());
        for (auto it = config.begin(); it != config.end(); ++it) {
            q.prepare("REPLACE INTO system_config(config_key, config_value, updated_at) "
                      "VALUES(?, ?, NOW())");
            q.addBindValue(it.key());
            q.addBindValue(it.value().toString());
            if (!q.exec()) {
                qWarning() << "[ConfigDAO] saveBatch failed for key:" << it.key()
                           << q.lastError().text();
                return false;  // 触发整体回滚
            }
        }
        return true;
    });
}

QString ConfigDAO::value(const QString& key, const QString& defaultValue) {
    QSqlDatabase db = getDb();
    if (!db.isValid()) return defaultValue;

    QSqlQuery q(db);
    q.prepare("SELECT config_value FROM system_config WHERE config_key = ?");
    q.addBindValue(key);
    if (q.exec() && q.next()) {
        return q.value(0).toString();
    }
    qWarning() << "[ConfigDAO] value失败:" << key << q.lastError().text();
    return defaultValue;
}

} // namespace db
