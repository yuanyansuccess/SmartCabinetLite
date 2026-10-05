/**
 * @file BaseDAO.h
 * @brief DAO基类模板 — db/目录统一namespace db
 * @author 袁燕
 */
#pragma once
#include <QList>
#include <QVariant>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QJsonObject>
#include "DatabaseManager.h"
#include "common/Constants.h"

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

    /// 插入并返回自增ID；失败返回0（与 Service::Result 的 recordId 约定一致，调用方统一用 >0 判定成功）
    int insertAndGetId(const QString& sql, const QVariantList& params = {}) {
        QSqlQuery q = query(sql, params);
        if (q.numRowsAffected() > 0) {
            QVariant id = q.lastInsertId();
            if (id.isValid()) return id.toInt();
            // 兜底：部分 ODBC 驱动对 MySQL 不返回 lastInsertId，改查会话级自增ID
            QSqlQuery q2 = query("SELECT LAST_INSERT_ID()");
            if (q2.next()) return q2.value(0).toInt();
        }
        return 0;
    }

    /// 构建分页查询（自动钳制非法入参）
    /// 页码从 1 起：传入 0 或负数时按第 1 页处理，避免生成 OFFSET 负数导致 SQL 报错
    /// 每页条数：为 0 或负数时回落到 SC::PAGE_SIZE_DEFAULT
    /// 注意：不设上限，SC::PAGE_SIZE_UNLIMITED 这类"取全量"语义需保持原样
    QString paginate(const QString& baseSql, int page, int pageSize,
                     const QString& orderBy = "created_at DESC") {
        const int safePage  = (page < 1) ? 1 : page;
        const int safeSize  = (pageSize < 1) ? SC::PAGE_SIZE_DEFAULT : pageSize;
        const qint64 offset = static_cast<qint64>(safePage - 1) * safeSize;
        return QString("%1 ORDER BY %2 LIMIT %3 OFFSET %4")
            .arg(baseSql, orderBy).arg(safeSize).arg(offset);
    }

    /// 计数查询
    /// baseSql 传明细查询即可；若传入的本身已是聚合语句（如 SELECT COUNT(*) ...），
    /// 再包一层会变成 SELECT COUNT(*) FROM (SELECT COUNT(*) ...) AS _cnt，结果恒为 1，
    /// 因此这里识别出聚合语句后直接执行原 SQL。
    int count(const QString& baseSql, const QVariantList& params = {}) {
        if (baseSql.contains(QLatin1String("COUNT("), Qt::CaseInsensitive)) {
            return scalar(baseSql, params).toInt();
        }
        const QString countSql = QString("SELECT COUNT(*) FROM (%1) AS _cnt").arg(baseSql);
        return scalar(countSql, params).toInt();
    }

    /// 事务封装
    bool transaction(std::function<bool()> fn) {
        if (!DatabaseManager::instance().beginTransaction()) return false;
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
            case QMetaType::QDateTime:
                // 统一 ISO 8601，避免走默认分支拿到本地化文本（如"周一 10月5日"）导致解析失败
                obj[rec.fieldName(i)] = v.toDateTime().toString(Qt::ISODate); break;
            case QMetaType::QByteArray:
                // 二进制统一转十六进制文本，避免默认 toString 产生不可见字符
                obj[rec.fieldName(i)] = QString::fromUtf8(v.toByteArray().toHex()); break;
            default: {
                // MySQL ODBC 驱动会把 DATETIME/TIMESTAMP 作为字符串返回（形如
                // "yyyy-MM-dd HH:mm:ss"），到不了 QDateTime 分支，这里统一转成 ISO 8601，
                // 保证本函数输出只有一种日期格式，调用方解析方式固定。
                const QString text = v.toString();
                const QDateTime dt = QDateTime::fromString(text, "yyyy-MM-dd HH:mm:ss");
                obj[rec.fieldName(i)] = dt.isValid() ? dt.toString(Qt::ISODate) : text;
                break;
            }
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
