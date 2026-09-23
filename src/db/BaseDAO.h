/**
 * @file BaseDAO.h
 * @brief DAO基类模板 — db/目录统一namespace db
 * @author 袁燕
 * @修改说明 V6.9 2026-06-24 从dao/移入db/并包裹namespace db，统一DAO层架构
 */
#pragma once
#include <QList>
#include <QVariant>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QJsonObject>
#include "DatabaseManager.h"

namespace db {

/**
 * BaseDAO — 通用数据访问基类
 * 封装常用SQL操作，子类复用，避免SQL散布
 */
class BaseDAO {
public:
    BaseDAO() = default;
    virtual ~BaseDAO() = default;

    /// 获取数据库连接（所有DAO子类复用，无需各自定义）
    QSqlDatabase getDb() { return DatabaseManager::instance().getConnection(); }

    /// 执行查询返回QSqlQuery
    QSqlQuery query(const QString& sql, const QVariantList& params = {}) {
        return DatabaseManager::instance().executeQuery(sql, params);
    }

    /// 执行非查询（INSERT/UPDATE/DELETE）
    bool execute(const QString& sql, const QVariantList& params = {}) {
        return DatabaseManager::instance().executeNonQuery(sql, params);
    }

    /// 查询单值（如COUNT、MAX）
    QVariant scalar(const QString& sql, const QVariantList& params = {}) {
        return DatabaseManager::instance().executeScalar(sql, params);
    }

    /// 插入并返回自增ID
    int insertAndGetId(const QString& sql, const QVariantList& params = {}) {
        QSqlQuery q = query(sql, params);
        if (q.numRowsAffected() > 0) {
            QVariant id = q.lastInsertId();
            if (id.isValid()) return id.toInt();
        }
        return -1;
    }

    /// 构建分页查询
    QString paginate(const QString& baseSql, int page, int pageSize,
                     const QString& orderBy = "created_at DESC") {
        int offset = (page - 1) * pageSize;
        return QString("%1 ORDER BY %2 LIMIT %3 OFFSET %4")
            .arg(baseSql, orderBy).arg(pageSize).arg(offset);
    }

    /// 计数查询
    int count(const QString& baseSql, const QVariantList& params = {}) {
        QString countSql = QString("SELECT COUNT(*) FROM (%1) AS _cnt").arg(baseSql);
        return scalar(countSql, params).toInt();
    }

    /// 事务封装
    bool transaction(std::function<bool()> fn) {
        DatabaseManager::instance().beginTransaction();
        if (fn()) {
            return DatabaseManager::instance().commit();
        }
        DatabaseManager::instance().rollback();
        return false;
    }

    /// QSqlQuery → QJsonObject 辅助转换
    static QJsonObject rowToJson(const QSqlQuery& q) {
        QJsonObject obj;
        QSqlRecord rec = q.record();
        for (int i = 0; i < rec.count(); ++i) {
            QVariant v = q.value(i);
            if (v.isNull()) continue;
            switch (v.typeId()) {
            case QMetaType::Int: case QMetaType::LongLong:
                obj[rec.fieldName(i)] = v.toLongLong(); break;
            case QMetaType::Double:
                obj[rec.fieldName(i)] = v.toDouble(); break;
            case QMetaType::Bool:
                obj[rec.fieldName(i)] = v.toBool(); break;
            default:
                obj[rec.fieldName(i)] = v.toString(); break;
            }
        }
        return obj;
    }

    /// 执行QSqlQuery并自动记录错误日志
    static bool safeExec(QSqlQuery& q) {
        if (!q.exec()) {
            qWarning() << "[DAO] SQL error:" << q.lastError().text()
                        << "| Query:" << q.lastQuery();
            return false;
        }
        return true;
    }
};

} // namespace db
