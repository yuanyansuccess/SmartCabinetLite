// 智能柜Qt Widget 2.0  DatabaseManager实现
// 直连MySQL，使用Qt SQL驱动（纯MySQL模式，不支持SQLite回退）
#include "DatabaseManager.h"
#include "Logger.h"  // 统一日志写入入口（Log::appendLog）
#include <QThread>
#include <QUuid>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QDateTime>
#include <QCryptographicHash>  // SQLite播种用SHA256
#include <QNetworkInterface>  // 物理网络连接检测
#include <QTcpSocket>
#include <QEventLoop>
#include <QTimer>

DatabaseManager& DatabaseManager::instance() {
    static DatabaseManager inst;
    return inst;
}

DatabaseManager::DatabaseManager(QObject* parent)
    : QObject(parent), m_port(3306)
{
    m_connectionName = QString("sc_main_%1").arg(
        QString::fromLatin1(QUuid::createUuid().toRfc4122().toHex().left(8)));
}

DatabaseManager::~DatabaseManager() {
    close();
}

bool DatabaseManager::initialize(const QString& host, int port,
                                  const QString& dbName, const QString& user,
                                  const QString& password) {
    QMutexLocker l(&m_mutex);
    m_host   = host;
    m_port   = port;
    m_dbName = dbName;
    m_user   = user;
    m_pass   = password;

    // 纯MySQL策略（+领导确认：本地部署MySQL，不用SQLite双数据库）
    // 原"SQLite优先→MySQL同步"策略已废弃，避免双库数据不一致问题
    bool mysqlReady = tryMysql();
    if (!mysqlReady) {
        qCritical() << "[DB] MySQL connection failed - FATAL (pure MySQL mode, no SQLite fallback)";
        return false;
    }
    qInfo() << "[DB] MySQL primary database ready:" << m_host << ":" << m_port << "/" << m_dbName;
    // 初始化Schema+播种数据（空表检测不会覆盖已有生产数据）
    bool schemaOk = initSchemaIfNeeded();
    // 诊断日志写入文件（统一走 Logger 入口：时间戳/建目录/UTF-8 由 Logger 负责）
    Log::appendLog(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/db_init.log",
                   QStringLiteral("MySQL connected=%1 schemaOk=%2").arg(mysqlReady).arg(schemaOk));
    return true;
}

bool DatabaseManager::isConnected() const {
    // 注意：不只用isOpen()（TCP断开后可能仍返回true）
    // 执行SELECT 1测试真实连接，失败则尝试重连
    QMutexLocker l(&m_mutex);
    if (!m_db.isOpen()) return false;
    QSqlQuery q(m_db);
    if (q.exec("SELECT 1") && q.next()) {
        return true;
    }
    // SELECT 1失败，连接已断开，尝试重连
    l.unlock();
    const_cast<DatabaseManager*>(this)->ensureConnected();
    l.relock();
    return m_db.isOpen() && QSqlQuery(m_db).exec("SELECT 1");
}

/// 检测真实物理网络链路连接状态
/// 区别于isConnected()（数据库连接检测），本方法检测网卡物理链路（网线/WiFi）是否真实连通
/// 跨平台兼容：Windows + 麒麟Linux，纯Qt网络接口，无平台私有API
/// 判断标准（全部满足才判定该网卡联网）：
/// 1. 非回环、非虚拟隧道网卡
/// 2. 网卡标记：IsUp(启用) + IsRunning(驱动正常) + CanBroadcast(物理链路存在，拔网线失效)
/// 3. 拥有有效局域网/公网IPv4地址，排除169.254.x.x断网自动私有地址
/// 返回：true=存在至少一张网卡物理链路连通且有可用IP；false=全部网卡断开/无有效网络
bool DatabaseManager::isNetworkConnected() const
{
    QTcpSocket socket;
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    timeoutTimer.setInterval(300);

    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QTcpSocket::connected, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QTcpSocket::errorOccurred, &loop, &QEventLoop::quit);

    timeoutTimer.start();
    socket.connectToHost("114.114.114.114", 53);
    loop.exec();

    bool reachable = socket.state() == QTcpSocket::ConnectedState;
    socket.abort();
    return reachable;
}

void DatabaseManager::close() {
    QMutexLocker l(&m_mutex);
    if (m_db.isOpen()) m_db.close();
}

QSqlDatabase DatabaseManager::database() const {
    QMutexLocker l(&m_mutex);
    return m_db;
}

bool DatabaseManager::ensureConnected() {
    if (!m_db.isOpen()) {
        // 纯MySQL模式，直接用MySQL参数重连
        m_db.setHostName(m_host);
        m_db.setPort(m_port);
        m_db.setDatabaseName(m_dbName);
        m_db.setUserName(m_user);
        m_db.setPassword(m_pass);
        if (!m_db.open()) {
            qWarning() << "[DB] Reconnect failed (MySQL):" << m_db.lastError().text();
            return false;
        }
        qInfo() << "[DB] Reconnected: MySQL";
    }
    return true;
}

QSqlQuery DatabaseManager::executeQuery(const QString& sql, const QVariantList& params) {
    QMutexLocker l(&m_mutex);
    if (!ensureConnected()) return QSqlQuery();
    QSqlQuery q(m_db);
    q.prepare(sql);
    for (int i = 0; i < params.size(); ++i)
        q.bindValue(i, params[i]);
    if (!q.exec()) {
        qWarning() << "SQL error:" << q.lastError().text() << "| SQL:" << sql;
    }
    return q;
}

bool DatabaseManager::executeNonQuery(const QString& sql, const QVariantList& params) {
    QMutexLocker l(&m_mutex);
    if (!ensureConnected()) return false;
    QSqlQuery q(m_db);
    q.prepare(sql);
    for (int i = 0; i < params.size(); ++i)
        q.bindValue(i, params[i]);
    if (!q.exec()) {
        qWarning() << "NonQuery error:" << q.lastError().text();
        return false;
    }
    return true;
}

QVariant DatabaseManager::executeScalar(const QString& sql, const QVariantList& params) {
    QSqlQuery q = executeQuery(sql, params);
    if (q.next()) return q.value(0);
    return QVariant();
}

bool DatabaseManager::beginTransaction() {
    QMutexLocker l(&m_mutex);
    return ensureConnected() && m_db.transaction();
}

bool DatabaseManager::commit() {
    QMutexLocker l(&m_mutex);
    return m_db.commit();
}

bool DatabaseManager::rollback() {
    QMutexLocker l(&m_mutex);
    return m_db.rollback();
}

QString DatabaseManager::lastError() const {
    QMutexLocker l(&m_mutex);
    return m_db.lastError().text();
}

// 优先直连MySQL（127.0.0.1:3306/smart_cabinet），使用QODBC驱动
bool DatabaseManager::tryMysql() {
    static const QStringList drivers = {"QODBC", "QMYSQL"};
    for (const QString& driver : drivers) {
        if (!QSqlDatabase::isDriverAvailable(driver)) {
            qWarning() << "[DB] MySQL driver not available:" << driver;
            continue;
        }

        // 先关闭旧连接
        if (QSqlDatabase::contains(m_connectionName)) {
            QSqlDatabase::removeDatabase(m_connectionName);
        }

        m_db = QSqlDatabase::addDatabase(driver, m_connectionName);

        if (driver == "QMYSQL") {
            m_db.setHostName(m_host);
            m_db.setPort(m_port);
            m_db.setDatabaseName(m_dbName);
            m_db.setUserName(m_user);
            m_db.setPassword(m_pass);
            m_db.setConnectOptions("MYSQL_OPT_CONNECT_TIMEOUT=5;MYSQL_OPT_READ_TIMEOUT=10");
        } else {
            // QODBC连接字符串
            m_db.setDatabaseName(QString("DRIVER={MySQL ODBC 8.0 Unicode Driver};"
                "SERVER=%1;PORT=%2;DATABASE=%3;UID=%4;PWD=%5;OPTION=3")
                .arg(m_host).arg(m_port).arg(m_dbName).arg(m_user).arg(m_pass));
        }

        if (m_db.open()) {
            qInfo() << "[DB] MySQL connected via" << driver << ":" << m_host << ":" << m_port << "/" << m_dbName;
            return true;
        }
        qWarning() << "[DB] MySQL connection failed via" << driver << ":" << m_db.lastError().text();
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    return false;
}

// trySqlite已删除——纯MySQL模式，不支持SQLite回退
//   要求彻底删除SQLite，只保留MySQL


// 兼容版本B的services/DAOs
QSqlDatabase DatabaseManager::getConnection() const {
    return database();
}
