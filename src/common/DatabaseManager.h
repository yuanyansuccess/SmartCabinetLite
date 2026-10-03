#pragma once
// 智能柜Qt Widget 2.0  数据库连接管理（单例）
// 功能：管理MySQL连接、执行查询、事务控制
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
    // 删除trySqlite——纯MySQL模式，不支持SQLite回退
    bool initSchemaIfNeeded();  // 自动建表+播种默认管理员
    bool createViewsIfNeeded();  // 每次启动确保视图存在（修复已有数据库视图缺失）
    bool seedBusinessData();    // 播种完整业务数据（工具/记录/告警/日志）

    QSqlDatabase m_db;
    QString m_host, m_dbName, m_user, m_pass;
    int m_port = 3306;
    mutable QMutex m_mutex;
    QString m_connectionName;
    // 删除m_usingSqlite——纯MySQL模式
};
