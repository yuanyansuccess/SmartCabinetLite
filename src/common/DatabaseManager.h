/**
 * @file DatabaseManager.h
 * @brief MySQL 连接单例：建连、建表建视图、播种基础数据，并提供查询与事务接口
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
    /// 检测真实物理网络连接状态（非数据库连接），跨平台兼容Windows+麒麟Linux
    bool isNetworkConnected() const;
    void close();

    QSqlDatabase database() const;
    QSqlDatabase getConnection() const;  // 兼容版本B的services

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
    // 部署策略为恢复标准生产库备份（db_backup/*.sql，含表/视图/真实账号），
    // 软件不再自动建表/建视图/播种演示数据，避免双数据源漂移与演示数据污染生产库。

    QSqlDatabase m_db;
    QString m_host, m_dbName, m_user, m_pass;
    int m_port = 3306;
    mutable QMutex m_mutex;
    QString m_connectionName;
};
