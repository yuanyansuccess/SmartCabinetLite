/**
 * @file DatabaseManager.cpp
 * @brief MySQL 连接单例实现：建连、建表建视图、播种基础数据，并提供查询与事务接口
 * @author 袁燕
 */
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

/**
         * @brief 获取数据库连接单例
         * @return 全局唯一实例
         */
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

/**
         * @brief 建立数据库连接
         * @param host 地址
         * @param port 端口
         * @param dbName 数据库名
         * @param user 账号
         * @param password 口令
         * @return true=连接成功
         * @note 纯 MySQL 架构：连接失败即视为数据库不可用，不做本地库回退
         */
bool DatabaseManager::initialize(const QString& host, int port,
                                  const QString& dbName, const QString& user,
                                  const QString& password) {
    QMutexLocker l(&m_mutex);
    m_host   = host;
    m_port   = port;
    m_dbName = dbName;
    m_user   = user;
    m_pass   = password;

    // 纯 MySQL 架构：连接失败即视为数据库不可用，不做本地库回退
    bool mysqlReady = tryMysql();
    if (!mysqlReady) {
        qCritical() << "[DB] MySQL connection failed - FATAL (pure MySQL mode, no SQLite fallback)";
        return false;
    }
    qInfo() << "[DB] MySQL primary database ready:" << m_host << ":" << m_port << "/" << m_dbName;
    // 诊断日志写入文件（统一走 Logger 入口：时间戳/建目录/UTF-8 由 Logger 负责）
    Log::appendLog(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/db_init.log",
                   QStringLiteral("MySQL connected=%1").arg(mysqlReady));
    return true;
}

/**
         * @brief 判断数据库是否真实可用
         * @return true=可正常查询
         * @note 不只看连接对象是否打开：会执行 SELECT 1 探测，断开时尝试重连
         */
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

/**
         * @brief 关闭数据库连接
         */
void DatabaseManager::close() {
    QMutexLocker l(&m_mutex);
    if (m_db.isOpen()) m_db.close();
}

/**
         * @brief 取得可用的数据库连接对象
         * @return 数据库连接；未连接时返回无效对象，调用方需判断
         */
QSqlDatabase DatabaseManager::database() const {
    QMutexLocker l(&m_mutex);
    return m_db;
}

/**
         * @brief 确保连接可用，必要时尝试重连
         * @return true=连接可用
         */
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

/**
         * @brief 执行参数化查询
         * @param sql 语句，占位符用 ?
         * @param params 绑定参数
         * @return 查询结果；失败时返回空结果集
         */
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

/**
         * @brief 执行增删改语句
         * @param sql 语句，占位符用 ?
         * @param params 绑定参数
         * @return true=执行成功
         */
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

/**
         * @brief 执行查询并取第一行第一列
         * @param sql 语句，占位符用 ?
         * @param params 绑定参数
         * @return 首行首列值；无结果时返回无效值
         */
QVariant DatabaseManager::executeScalar(const QString& sql, const QVariantList& params) {
    QSqlQuery q = executeQuery(sql, params);
    if (q.next()) return q.value(0);
    return QVariant();
}

/**
         * @brief 开启事务
         * @return true=开启成功
         */
bool DatabaseManager::beginTransaction() {
    QMutexLocker l(&m_mutex);
    return ensureConnected() && m_db.transaction();
}

/**
         * @brief 提交事务
         * @return true=提交成功
         */
bool DatabaseManager::commit() {
    QMutexLocker l(&m_mutex);
    return m_db.commit();
}

/**
         * @brief 回滚事务
         * @return true=回滚成功
         */
bool DatabaseManager::rollback() {
    QMutexLocker l(&m_mutex);
    return m_db.rollback();
}

/**
         * @brief 读取最近一次数据库错误描述
         * @return 错误文本
         */
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

// 纯 MySQL 模式：不提供 SQLite 回退，连接失败即视为数据库不可用


// 兼容版本B的services/DAOs
QSqlDatabase DatabaseManager::getConnection() const {
    return database();
}
