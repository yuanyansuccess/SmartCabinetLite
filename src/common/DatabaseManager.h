/**
 * @file DatabaseManager.h
 * @brief MySQL 连接单例：建连、查询与事务接口
 * @author 袁燕
 */
#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QMutex>

class DatabaseManager : public QObject {
    Q_OBJECT
public:
    static DatabaseManager& instance();

    bool initialize(const QString& host, int port,
                    const QString& dbName, const QString& user, const QString& password);
    bool isConnected() const;
    void close();

    QSqlDatabase database() const;
    QSqlDatabase getConnection() const;  // 供服务层获取数据库连接

    // 便捷查询
    QSqlQuery executeQuery(const QString& sql, const QVariantList& params = {});
    bool      executeNonQuery(const QString& sql, const QVariantList& params = {});
    QVariant  executeScalar(const QString& sql, const QVariantList& params = {});

    // 事务控制
    bool beginTransaction();
    bool commit();
    bool rollback();

    // 错误信息
    QString lastError() const;

private:
    DatabaseManager(QObject* parent = nullptr);
    ~DatabaseManager();
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    bool ensureConnected();
    bool tryMysql();  // 直连MySQL（127.0.0.1:3306/smart_cabinet）
    // 表结构、视图与基础账号由部署时恢复的标准生产库备份提供，
    // 本类只负责连接、查询与事务，不在运行时创建业务数据。

    QSqlDatabase m_db;
    QString m_host, m_dbName, m_user, m_pass;
    int m_port = 3306;
    mutable QMutex m_mutex;
    QString m_connectionName;
};
