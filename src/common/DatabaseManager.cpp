// 作者：袁燕  智能柜Qt Widget 2.0  DatabaseManager实现
// 日期：2026-06-21  直连MySQL，使用Qt SQL驱动
// [v4] 新增SQLite回退：MySQL不可用时自动使用本地SQLite，保障开发/测试环境可用
#include "DatabaseManager.h"
#include <QThread>
#include <QUuid>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QDateTime>
#include <QCryptographicHash>  // [v4] SQLite播种用SHA256
#include <QNetworkInterface>   // [V2.15] 物理网络连接检测
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

    // [V2.03 2026-06-27] 纯MySQL策略（+领导确认：本地部署MySQL，不再用SQLite双数据库）
    //   原"SQLite优先→MySQL同步"策略已废弃，避免双库数据不一致问题
    bool mysqlReady = tryMysql();
    if (!mysqlReady) {
        qCritical() << "[DB] MySQL connection failed - FATAL (pure MySQL mode, no SQLite fallback)";
        return false;
    }
    qInfo() << "[DB] MySQL primary database ready:" << m_host << ":" << m_port << "/" << m_dbName;
    // 初始化Schema+播种数据（空表检测不会覆盖已有生产数据）
    bool schemaOk = initSchemaIfNeeded();
    // [V2.03-fix] 诊断日志写入文件
    {
        QFile lf(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/db_init.log");
        if (lf.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream ts(&lf);
            ts << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")
               << " MySQL connected=" << mysqlReady << " schemaOk=" << schemaOk << "\n";
            lf.close();
        }
    }
    return true;
}

bool DatabaseManager::isConnected() const {
    // [V2.03l 2026-06-30] 修复：不再只用isOpen()（TCP断开后可能仍返回true）
    //   改为执行SELECT 1测试真实连接，失败则尝试重连
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

/// [V2.16 2026-07-07 袁燕] 检测真实物理网络链路连接状态
///   区别于isConnected()（数据库连接检测），本方法检测网卡物理链路（网线/WiFi）是否真实连通
///   跨平台兼容：Windows + 麒麟Linux，纯Qt网络接口，无平台私有API
///   判断标准（全部满足才判定该网卡联网）：
///     1. 非回环、非虚拟隧道网卡
///     2. 网卡标记：IsUp(启用) + IsRunning(驱动正常) + CanBroadcast(物理链路存在，拔网线失效)
///     3. 拥有有效局域网/公网IPv4地址，排除169.254.x.x断网自动私有地址
///   返回：true=存在至少一张网卡物理链路连通且有可用IP；false=全部网卡断开/无有效网络
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
        // [V2.03 2026-06-27] 纯MySQL模式，直接用MySQL参数重连
        m_db.setHostName(m_host);
        m_db.setPort(m_port);
        m_db.setDatabaseName(m_dbName);
        m_db.setUserName(m_user);
        m_db.setPassword(m_pass);
        if (!m_db.open()) {
            qWarning() << "[DB] Reconnect failed (MySQL):" << m_db.lastError().text();
            emit connectionLost();
            return false;
        }
        qInfo() << "[DB] Reconnected: MySQL";
        emit connectionRestored();
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

// [v12] 优先直连MySQL（127.0.0.1:3306/smart_cabinet），使用QODBC驱动
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

// [V8.0 2026-06-28] trySqlite已删除——纯MySQL模式，不再支持SQLite回退
//   作者：袁燕 - 要求彻底删除SQLite，只保留MySQL

// [v4] 自动建表+播种默认管理员 (CF001/123456)
bool DatabaseManager::initSchemaIfNeeded() {
    QSqlQuery q(m_db);

    // [V2.03-fix] 诊断lambda：记录建表/播种失败信息到文件
    auto logFail = [](const char* step, const QString& err) {
        QFile lf(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/db_schema.log");
        if (lf.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream ts(&lf);
            ts << QDateTime::currentDateTime().toString("HH:mm:ss") << " FAIL[" << step << "] " << err << "\n";
            lf.close();
        }
    };

    // [v4.6修复] 不再整体跳过schema初始化（早期return会阻止新增表）
    //  每个CREATE TABLE用的都是IF NOT EXISTS，安全幂等
    //  种子数据使用INSERT OR IGNORE，也安全幂等
    bool hasExistingSchema = false;
    {
        QSqlQuery check(m_db);
        // [V8.0 2026-06-28] 删除sqlite_master分支——纯MySQL模式
        check.exec("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='sys_user'");
        hasExistingSchema = (check.next() && check.value(0).toInt() > 0);
    }

    // [V2.03-fix] MySQL中表已存在时跳过建表（AUTOINCREMENT是SQLite专用语法，MySQL用AUTO_INCREMENT）
    //   MySQL在解析SQL时就检查语法，即使表已存在CREATE TABLE IF NOT EXISTS也会因AUTOINCREMENT语法错误而失败
    //   MySQL表由schema.sql预先创建，这里只需播种数据
    // [V8.0 2026-06-28] 删除m_usingSqlite判断——纯MySQL模式
    bool skipCreateTables = hasExistingSchema;

    qInfo() << "[DB] First run: initializing schema...";


    // [V2.03-fix] MySQL中表已存在时跳过建表（AUTOINCREMENT语法不兼容MySQL）
    if (!skipCreateTables) {
    // ── 建表：sys_user ──
    bool ok = q.exec(
        "CREATE TABLE IF NOT EXISTS sys_user ("
        "  user_id       INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  username      TEXT    NOT NULL UNIQUE,"
        "  password_hash TEXT    NOT NULL,"
        "  password_salt TEXT    NOT NULL,"
        "  real_name     TEXT    NOT NULL,"
        "  work_no       TEXT    NOT NULL UNIQUE,"
        "  dept_id       INTEGER DEFAULT 0,"
        "  department    TEXT    DEFAULT '',"
        "  role          TEXT    DEFAULT 'user',"
        "  face_feature  TEXT    DEFAULT NULL,"
        "  phone         TEXT    DEFAULT '',"
        "  email         TEXT    DEFAULT '',"
        "  status        TEXT    DEFAULT 'active',"
        "  last_login_at TEXT    DEFAULT NULL,"
        "  created_at    TEXT    DEFAULT (datetime('now','localtime')),"
        "  updated_at    TEXT    DEFAULT (datetime('now','localtime'))"
        ")"
    );
    if (!ok) { logFail("sys_user", q.lastError().text()); qWarning() << "[DB] Create sys_user failed:" << q.lastError().text(); return false; }

    // [v4.6新增] 建表：sys_department（部门表，dao/UserDAO::findAll LEFT JOIN需要）
    ok = q.exec(
        "CREATE TABLE IF NOT EXISTS sys_department ("
        "  dept_id       INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  dept_name     TEXT    NOT NULL,"
        "  parent_id     INTEGER DEFAULT 0,"
        "  sort_order    INTEGER DEFAULT 0,"
        "  created_at    TEXT    DEFAULT (datetime('now','localtime')),"
        "  updated_at    TEXT    DEFAULT (datetime('now','localtime'))"
        ")"
    );
    if (!ok) { logFail("sys_department", q.lastError().text()); qWarning() << "[DB] Create sys_department failed:" << q.lastError().text(); return false; }

    // ── 建表：tool_category（工具分类） ──
    ok = q.exec(
        "CREATE TABLE IF NOT EXISTS tool_category ("
        "  category_id   INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  category_name TEXT    NOT NULL,"
        "  parent_id     INTEGER DEFAULT 0,"
        "  sort_order    INTEGER DEFAULT 0,"
        "  icon          TEXT    DEFAULT '',"
        "  created_at    TEXT    DEFAULT (datetime('now','localtime')),"
        "  updated_at    TEXT    DEFAULT (datetime('now','localtime'))"
        ")"
    );
    if (!ok) { logFail("tool_category", q.lastError().text()); qWarning() << "[DB] Create tool_category failed:" << q.lastError().text(); return false; }

    // ── 建表：tool_cabinet（工具柜） ──
    ok = q.exec(
        "CREATE TABLE IF NOT EXISTS tool_cabinet ("
        "  cabinet_id    INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  cabinet_name  TEXT    NOT NULL,"
        "  location      TEXT    DEFAULT '',"
        "  capacity      INTEGER DEFAULT 0,"
        "  status        TEXT    DEFAULT 'active',"
        "  created_at    TEXT    DEFAULT (datetime('now','localtime')),"
        "  updated_at    TEXT    DEFAULT (datetime('now','localtime'))"
        ")"
    );
    if (!ok) { logFail("tool_cabinet", q.lastError().text()); qWarning() << "[DB] Create tool_cabinet failed:" << q.lastError().text(); return false; }

    // ── 建表：tool_info（工具信息） [V7.0] 列名对齐MySQL schema.sql
    // [V2.01 2026-06-27] 新增 recognition_method/document_path 列
    ok = q.exec(
        "CREATE TABLE IF NOT EXISTS tool_info ("
        "  tool_id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  tool_name        TEXT    NOT NULL,"
        "  tool_code        TEXT    NOT NULL UNIQUE,"
        "  spec             TEXT    DEFAULT '',"
        "  category_id      INTEGER DEFAULT 0,"
        "  cabinet_id       INTEGER DEFAULT 0,"
        "  machine_group_id INTEGER DEFAULT NULL,"
        "  layer            TEXT    DEFAULT '',"
        "  position         TEXT    DEFAULT '',"
        "  total_qty        INTEGER DEFAULT 0,"
        "  current_qty      INTEGER DEFAULT 0,"
        "  rfid_tag         TEXT    DEFAULT '',"
        "  status           TEXT    DEFAULT 'in_stock',"
        "  checkout_reason  TEXT    DEFAULT '',"
        "  is_recommended   INTEGER DEFAULT 0,"
        "  recognition_method TEXT  DEFAULT 'rfid',"
        "  document_path    TEXT    DEFAULT '',"
        "  created_at       TEXT    DEFAULT (datetime('now','localtime')),"
        "  updated_at       TEXT    DEFAULT (datetime('now','localtime'))"
        ")"
    );
    if (!ok) { logFail("tool_info", q.lastError().text()); qWarning() << "[DB] Create tool_info failed:" << q.lastError().text(); return false; }

    // [V7.0] 为已有SQLite数据库迁移：添加缺失列（IF NOT EXISTS）
    // SQLite不支持 ADD COLUMN IF NOT EXISTS，用try-catch忽略"duplicate column"错误
    const char* alterCols[] = {
        "ALTER TABLE tool_info ADD COLUMN spec TEXT DEFAULT ''",
        "ALTER TABLE tool_info ADD COLUMN machine_group_id INTEGER DEFAULT NULL",
        "ALTER TABLE tool_info ADD COLUMN layer TEXT DEFAULT ''",
        "ALTER TABLE tool_info ADD COLUMN current_qty INTEGER DEFAULT 0",
        "ALTER TABLE tool_info ADD COLUMN rfid_tag TEXT DEFAULT ''",
        "ALTER TABLE tool_info ADD COLUMN checkout_reason TEXT DEFAULT ''",
        "ALTER TABLE tool_info ADD COLUMN is_recommended INTEGER DEFAULT 0",
        "ALTER TABLE tool_info ADD COLUMN recognition_method TEXT DEFAULT 'rfid'",   // [V2.01]
        "ALTER TABLE tool_info ADD COLUMN document_path TEXT DEFAULT ''",            // [V2.01]
    };
    for (const char* alterSql : alterCols) {
        q.exec(alterSql);  // 忽略"duplicate column"错误
    }
    // 迁移：将available_qty数据复制到current_qty
    q.exec("UPDATE tool_info SET current_qty = available_qty WHERE current_qty = 0 AND available_qty > 0");

    // [V7.0] 建表：machine_group（工程机组） ──
    ok = q.exec(
        "CREATE TABLE IF NOT EXISTS machine_group ("
        "  group_id      INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  group_name    TEXT    NOT NULL UNIQUE,"
        "  dept_id       INTEGER DEFAULT NULL,"
        "  leader_name   TEXT    DEFAULT '',"
        "  leader_phone  TEXT    DEFAULT '',"
        "  description   TEXT    DEFAULT '',"
        "  status        TEXT    DEFAULT 'active',"
        "  created_at    TEXT    DEFAULT (datetime('now','localtime')),"
        "  updated_at    TEXT    DEFAULT (datetime('now','localtime'))"
        ")"
    );
    if (!ok) { logFail("machine_group", q.lastError().text()); qWarning() << "[DB] Create machine_group failed:" << q.lastError().text(); return false; }

    // [2026-06-26v8] 建表：system_config（系统配置持久化）
    // [V2.03 2026-06-27] TIMESTAMP类型+CURRENT_TIMESTAMP兼容MySQL5.7+SQLite（TEXT类型在MySQL不支持DEFAULT CURRENT_TIMESTAMP）
    ok = q.exec(
        "CREATE TABLE IF NOT EXISTS system_config ("
        "  config_key   VARCHAR(64) PRIMARY KEY,"
        "  config_value TEXT,"
        "  updated_at   TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ")"
    );
    if (!ok) { logFail("system_config", q.lastError().text()); qWarning() << "[DB] Create system_config failed:" << q.lastError().text(); return false; }

    // ── 建表：tool_borrow_record（借用记录） ──
    // [V2.11 2026-07-02 袁燕] 增加 mapping_id 列（位置维度借用记录）
    ok = q.exec(
        "CREATE TABLE IF NOT EXISTS tool_borrow_record ("
        "  record_id            INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  flow_no              TEXT    NOT NULL,"
        "  tool_id              INTEGER NOT NULL,"
        "  user_id              INTEGER NOT NULL,"
        "  borrow_qty           INTEGER DEFAULT 1,"
        "  borrow_reason        TEXT    DEFAULT '',"
        "  borrow_time          TEXT    DEFAULT NULL,"
        "  expected_return_time TEXT    DEFAULT NULL,"
        "  actual_return_time   TEXT    DEFAULT NULL,"
        "  status               TEXT    DEFAULT 'borrowing',"
        "  operator_id          INTEGER DEFAULT NULL,"
        "  remark               TEXT    DEFAULT '',"
        "  mapping_id           INTEGER DEFAULT NULL,"  // [V2.11] 位置映射ID
        "  created_at           TEXT    DEFAULT (datetime('now','localtime')),"
        "  updated_at           TEXT    DEFAULT (datetime('now','localtime'))"
        ")"
    );
    if (!ok) { logFail("tool_borrow_record", q.lastError().text()); qWarning() << "[DB] Create tool_borrow_record failed:" << q.lastError().text(); return false; }

    // [V2.11 2026-07-02 袁燕] 已有数据库迁移：tool_borrow_record增加mapping_id列
    q.exec("ALTER TABLE tool_borrow_record ADD COLUMN mapping_id INTEGER DEFAULT NULL");

    // [v4.6新增] 播种部门数据（人员管理页面需要）
    // [V2.03-fix] REPLACE INTO兼容MySQL+SQLite
    q.exec("REPLACE INTO sys_department(dept_id, dept_name, sort_order) "
           "VALUES (1, '技术部', 1)");
    q.exec("REPLACE INTO sys_department(dept_id, dept_name, sort_order) "
           "VALUES (2, '生产部', 2)");
    q.exec("REPLACE INTO sys_department(dept_id, dept_name, sort_order) "
           "VALUES (3, '质控部', 3)");
    q.exec("REPLACE INTO sys_department(dept_id, dept_name, sort_order) "
           "VALUES (4, '仓储部', 4)");
    q.exec("REPLACE INTO sys_department(dept_id, dept_name, sort_order) "
           "VALUES (5, '行政部', 5)");

    // [V7.0] 播种工程机组数据（8个机组） ──
    struct MgSeed { int id; const char* name; int did; const char* leader; const char* phone; const char* desc; } mgs[] = {
        {1,"发动机维护机组",1,"张三","138****6789","负责发动机拆装、检查、更换部件等核心维护作业"},
        {2,"航电检修机组",2,"李四","139****8901","负责航空电子设备、仪表、通信导航系统检测维修"},
        {3,"液压系统机组",3,"王五","137****2345","负责液压管路、泵阀、作动筒检查与更换"},
        {4,"结构修理机组",4,"赵六","136****7890","负责机身蒙皮、框架、紧固件损伤修复"},
        {5,"起落架维护机组",1,"周八","133****9012","负责起落架减震、刹车系统、轮胎更换"},
        {6,"电气线路机组",2,"孙七","135****3456","负责线路故障排查、线束修复、接插件更换"},
        {7,"焊接作业机组",3,"李四","139****8901","负责金属结构焊接、修补、热处理"},
        {8,"精密测量机组",4,"赵六","136****7890","负责三坐标测量、形位公差检测、校准"},
    };
    for (auto& mg : mgs) {
        // [V2.03-fix] REPLACE INTO兼容MySQL+SQLite
        q.prepare("REPLACE INTO machine_group(group_id,group_name,dept_id,leader_name,leader_phone,description,status) "
                  "VALUES(?,?,?,?,?,?,'active')");
        q.addBindValue(mg.id); q.addBindValue(mg.name); q.addBindValue(mg.did);
        q.addBindValue(mg.leader); q.addBindValue(mg.phone); q.addBindValue(mg.desc);
        q.exec();
    }

    // ── 播种默认管理员：CF001 / 123456 ──
    QString salt = "K7mP2xQ9vL5nR3";  // 固定盐值，与schema.sql一致
    QString pwdHash = QString(QCryptographicHash::hash(
        (salt + QStringLiteral("123456")).toUtf8(),
        QCryptographicHash::Sha256).toHex());

    // [V2.03-fix] 先查后插：避免REPLACE INTO触发外键约束（tool_borrow_record引用sys_user.user_id）
    //   MySQL和SQLite都兼容，已有记录时跳过不覆盖
    {
        QSqlQuery adminChk(m_db);
        adminChk.exec("SELECT COUNT(*) FROM sys_user WHERE user_id=1");
        int adminCnt = (adminChk.next()) ? adminChk.value(0).toInt() : 0;
        if (adminCnt == 0) {
            q.prepare("INSERT INTO sys_user "
                      "(user_id, username, password_hash, password_salt, real_name, "
                      " work_no, dept_id, department, role, phone, status) "
                      "VALUES (1, 'CF001', :hash, :salt, '张三', "
                      " 'CF001', 1, '技术部', 'admin', '138****6789', 'active')");
            q.bindValue(":hash", pwdHash);
            q.bindValue(":salt", salt);
            if (!q.exec()) { logFail("admin_user", q.lastError().text()); qWarning() << "[DB] Seed admin user failed:" << q.lastError().text(); return false; }
        }
    }

    // [v4.6新增] 播种测试用户数据（人员管理页面有数据可查）
    struct TestUser { int id; QString uname; QString rname; QString wno; int did; QString dept; QString role; QString phone; QString status; };
    QList<TestUser> testUsers = {
        {2, "LI004",  "李四",   "CF002", 2, "生产部", "user", "139****1234", "active"},
        {3, "WANG5",  "王五",   "CF003", 3, "质控部", "user", "137****5678", "active"},
        {4, "ZHAO6",  "赵六",   "CF004", 4, "仓储部", "user", "136****9012", "active"},
        {5, "SUN7",   "孙七",   "CF005", 1, "技术部", "user", "135****3456", "active"},
        {6, "ZHOU8",  "周八",   "CF006", 2, "生产部", "user", "134****7890", "inactive"},
    };
    for (const auto& u : testUsers) {
        // [V2.03-fix] 先查后插：避免REPLACE INTO触发外键约束
        QSqlQuery uChk(m_db);
        uChk.prepare("SELECT COUNT(*) FROM sys_user WHERE user_id=?");
        uChk.addBindValue(u.id);
        uChk.exec();
        int uCnt = (uChk.next()) ? uChk.value(0).toInt() : 0;
        if (uCnt > 0) continue;  // 已存在则跳过

        QSqlQuery ins(m_db);
        ins.prepare("INSERT INTO sys_user "
                    "(user_id, username, password_hash, password_salt, real_name, "
                    " work_no, dept_id, department, role, phone, status) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
        ins.addBindValue(u.id);
        ins.addBindValue(u.uname);
        ins.addBindValue(pwdHash);
        ins.addBindValue(salt);
        ins.addBindValue(u.rname);
        ins.addBindValue(u.wno);
        ins.addBindValue(u.did);
        ins.addBindValue(u.dept);
        ins.addBindValue(u.role);
        ins.addBindValue(u.phone);
        ins.addBindValue(u.status);
        if (!ins.exec()) { qWarning() << "[DB] Seed test user failed:" << u.rname << ins.lastError().text(); }
    }

    // [V1.00.10 2026-06-24] 建表：task_type（任务类型）
    ok = q.exec(
        "CREATE TABLE IF NOT EXISTS task_type ("
        "  type_id       INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  type_code     TEXT    NOT NULL UNIQUE,"
        "  type_name     TEXT    NOT NULL,"
        "  description   TEXT    DEFAULT '',"
        "  default_duration INTEGER DEFAULT 30,"
        "  sort_order    INTEGER DEFAULT 0,"
        "  is_active     INTEGER DEFAULT 1,"
        "  created_at    TEXT    DEFAULT (datetime('now','localtime')),"
        "  updated_at    TEXT    DEFAULT (datetime('now','localtime'))"
        ")"
    );
    if (!ok) { logFail("task_type", q.lastError().text()); qWarning() << "[DB] Create task_type failed:" << q.lastError().text(); return false; }

    // [V1.00.10] 建表：task_type_tool（任务类型-推荐工具关联表）
    ok = q.exec(
        "CREATE TABLE IF NOT EXISTS task_type_tool ("
        "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  type_id       INTEGER NOT NULL,"
        "  tool_id       INTEGER NOT NULL,"
        "  sort_order    INTEGER DEFAULT 0,"
        "  created_at    TEXT    DEFAULT (datetime('now','localtime')),"
        "  FOREIGN KEY (type_id) REFERENCES task_type(type_id),"
        "  FOREIGN KEY (tool_id) REFERENCES tool_info(tool_id),"
        "  UNIQUE(type_id, tool_id)"
        ")"
    );
    if (!ok) { logFail("task_type_tool", q.lastError().text()); qWarning() << "[DB] Create task_type_tool failed:" << q.lastError().text(); return false; }

    // [V2.03q 2026-06-30 袁燕] 建表：tool_position_mapping（工具-位置对照关系）
    //   设计理念：一个工具可对应多个位置（一对多），一个位置只对应一个工具（位置唯一）
    //   对照关系维护=配置层，tool_info.cabinet_id/layer/position=实际入库层
    //   入库时从映射表查该工具的可用位置（未被其他在库工具占用）
    // [V2.08-fix 2026-06-30 袁燕] status字段移入建表语句，修复MySQL端建表漏字段的致命Bug
    //   根因：原MySQL兼容建表（建表块外）未包含status字段，导致入库UPDATE SET status失败
    //   修复：SQLite建表和MySQL建表都显式包含status字段，DEFAULT 'pending'
    ok = q.exec(
        "CREATE TABLE IF NOT EXISTS tool_position_mapping ("
        "  mapping_id    INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  tool_id       INTEGER NOT NULL,"
        "  cabinet_id    INTEGER NOT NULL,"
        "  layer         TEXT    NOT NULL,"
        "  position      TEXT    NOT NULL,"
        "  status        TEXT    NOT NULL DEFAULT 'pending',"
        "  created_at    TEXT    DEFAULT (datetime('now','localtime')),"
        "  FOREIGN KEY (tool_id) REFERENCES tool_info(tool_id),"
        "  FOREIGN KEY (cabinet_id) REFERENCES tool_cabinet(cabinet_id),"
        "  UNIQUE(cabinet_id, layer, position)"
        ")"
    );
    if (!ok) { logFail("tool_position_mapping", q.lastError().text()); qWarning() << "[DB] Create tool_position_mapping failed:" << q.lastError().text(); return false; }
    } // [V2.03-fix] 关闭 if (!skipCreateTables) 建表块

    // [V1.00.10] 为已有SQLite数据库迁移：添加缺失列（tool_info.cabinet_name兼容）
    q.exec("ALTER TABLE tool_info ADD COLUMN cabinet_name TEXT DEFAULT ''");
    q.exec("ALTER TABLE tool_info ADD COLUMN unit TEXT DEFAULT '件'");
    // [V7.9 2026-06-24] tool_cabinet增加cabinet_code和ip_address列（对齐ToolCabinet模型）
    q.exec("ALTER TABLE tool_cabinet ADD COLUMN cabinet_code TEXT DEFAULT ''");
    q.exec("ALTER TABLE tool_cabinet ADD COLUMN ip_address TEXT DEFAULT ''");
    // [V2.08 2026-06-30 袁燕] 映射表增加status字段
    //   入库只更新映射表status，不新建tool_info记录
    //   pending=待入库（配置了对照关系但未实际放入工具）
    //   in_stock=在库，borrowed=已借出
    q.exec("ALTER TABLE tool_position_mapping ADD COLUMN status VARCHAR(16) DEFAULT 'pending'");
    // [V2.12-fix5 2026-07-03 袁燕] 删除启动时用tool_info重置映射表status的脏数据修复代码
    //   根因：这段代码每次启动都用tool_info的cabinet_id/layer/position匹配映射表重置status，
    //         但V2.08+已改为位置维度管理，tool_info的位置不代表实际占用状态，
    //         导致已borrowed的位置被强制改回in_stock→数据不一致
    //   规则：代码层面不处理脏数据，所有数据修复直接在数据库操作
    // [V2.02-fix 2026-06-27] MySQL的task_type表缺少default_duration列，seedBusinessData的INSERT需要此列
    q.exec("ALTER TABLE task_type ADD COLUMN default_duration INTEGER DEFAULT 30");
    // [V2.03-fix 2026-06-27] MySQL的tool_info表缺少recognition_method/document_path列
    //   MySQL 5.7不允许TEXT类型设DEFAULT值，改用VARCHAR兼容MySQL+SQLite
    q.exec("ALTER TABLE tool_info ADD COLUMN recognition_method VARCHAR(16) DEFAULT 'rfid'");
    q.exec("ALTER TABLE tool_info ADD COLUMN document_path VARCHAR(512) DEFAULT ''");

    // [V2.03q 2026-06-30 袁燕] MySQL兼容建表+旧数据迁移（在建表块外，不受skipCreateTables控制）
    //   根因：skipCreateTables=true时跳过建表，导致MySQL端无tool_position_mapping表
    //   ODBC驱动报"Unable to execute statement"就是因为表不存在
    //   用MySQL兼容语法（AUTO_INCREMENT而非AUTOINCREMENT，CURRENT_TIMESTAMP而非datetime()）
    q.exec("CREATE TABLE IF NOT EXISTS tool_position_mapping ("
           "  mapping_id    INT AUTO_INCREMENT PRIMARY KEY,"
           "  tool_id       INT NOT NULL,"
           "  cabinet_id    INT NOT NULL,"
           "  layer         VARCHAR(16) NOT NULL,"
           "  position      VARCHAR(16) NOT NULL,"
           "  status        VARCHAR(16) NOT NULL DEFAULT 'pending',"
           "  created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
           "  FOREIGN KEY (tool_id) REFERENCES tool_info(tool_id),"
           "  FOREIGN KEY (cabinet_id) REFERENCES tool_cabinet(cabinet_id),"
           "  UNIQUE(cabinet_id, layer, position)"
           ")");
    if (q.lastError().isValid()) {
        qWarning() << "[DB] Create tool_position_mapping (MySQL compat) error:" << q.lastError().text();
    }
    // 迁移旧数据：从tool_info中已有位置的工具导入到映射表
    //   INSERT IGNORE避免重复（UNIQUE约束兜底）
    q.exec("INSERT IGNORE INTO tool_position_mapping (tool_id, cabinet_id, layer, position) "
           "SELECT tool_id, cabinet_id, layer, position FROM tool_info "
           "WHERE cabinet_id > 0 AND layer IS NOT NULL AND layer != '' AND position IS NOT NULL AND position != ''");
    if (q.lastError().isValid()) {
        qWarning() << "[DB] Migrate tool_position_mapping data error:" << q.lastError().text();
    }

    // [V7.1 2026-06-24] 在播种数据前先确保视图存在（修复已有数据库视图缺失导致表格无数据的问题）
    createViewsIfNeeded();

    // [2026-06-23] 丰富SQLite种子数据：工具+借用记录+告警，确保仪表盘和各页面有数据可看
    seedBusinessData();

    if (hasExistingSchema) {
        qInfo() << "[DB] Schema updated (v4.6: added sys_department table). Default admin: CF001 / 123456";
    } else {
        qInfo() << "[DB] Schema initialized (6 tables + 5 departments + 6 users). Default admin: CF001 / 123456";
    }
    return true;
}

// [V7.1 2026-06-24] 每次启动确保视图存在（解决已有数据库缺失v_tool_latest_operation等视图导致表格无数据的问题）
//   使用 CREATE OR REPLACE VIEW 兼容 MySQL 和 SQLite
bool DatabaseManager::createViewsIfNeeded() {
    QSqlQuery q(m_db);

    // v_tool_stats 工具统计视图 [2026-06-26v15] 借用统计从tool_borrow_record获取，确保与记录一致
    // [2026-06-27] in_stock_qty 改为所有工具的 current_qty 之和（不论状态），反映实际在库件数
    q.exec("CREATE OR REPLACE VIEW v_tool_stats AS "
           "SELECT (SELECT COUNT(*) FROM tool_info) AS total_tools,"
           "(SELECT COUNT(*) FROM tool_info WHERE status='in_stock') AS in_stock_count,"
           "(SELECT COUNT(DISTINCT tool_id) FROM tool_borrow_record WHERE status IN ('borrowing','overdue')) AS borrowed_count,"
           "(SELECT COUNT(*) FROM tool_info WHERE status='maintenance') AS maintenance_count,"
           "(SELECT COALESCE(SUM(total_qty),0) FROM tool_info) AS total_qty,"
           "(SELECT COALESCE(SUM(current_qty),0) FROM tool_info) AS in_stock_qty,"
           "(SELECT COALESCE(SUM(borrow_qty),0) FROM tool_borrow_record WHERE status IN ('borrowing','overdue')) AS borrowed_qty");
    if (q.lastError().isValid())
        qWarning() << "[DB] Create v_tool_stats failed:" << q.lastError().text();

    // v_tool_latest_operation 最近操作视图
    // [2026-06-27] 机组隔离：只统计活跃借用(borrowing/overdue)，已归还记录不作为最近操作
    // [V2.12-fix 2026-07-03 袁燕] 修复视图产生重复行导致工具管理列表每位置多一行的致命Bug
    //   根因：原LEFT JOIN tool_borrow_record tbr ON borrow_time=latest_borrow_time，
    //         同一秒借用多条记录时tbr匹配多行→视图对同一tool_id返回多行→findAllTools行翻倍
    //   修复：latest_op_user改用子查询LIMIT 1，去掉tbr的JOIN，确保每个tool_id只返回1行
    q.exec("CREATE OR REPLACE VIEW v_tool_latest_operation AS "
           "SELECT t.tool_id,"
           "COALESCE(br.latest_borrow_time,'') AS latest_op_time,"
           "CASE "
           "  WHEN br.latest_borrow_time IS NOT NULL THEN 'borrow'"
           "  ELSE 'checkin'"
           "END AS latest_op_type,"
           "COALESCE((SELECT u.real_name FROM tool_borrow_record tbr2 "
           "          LEFT JOIN sys_user u ON u.user_id=tbr2.user_id "
           "          WHERE tbr2.tool_id=t.tool_id AND tbr2.status IN ('borrowing','overdue') "
           "          ORDER BY tbr2.borrow_time DESC LIMIT 1),'') AS latest_op_user "
           "FROM tool_info t "
           "LEFT JOIN ("
           "  SELECT tool_id, MAX(borrow_time) AS latest_borrow_time "
           "  FROM tool_borrow_record WHERE status IN ('borrowing','overdue') GROUP BY tool_id"
           ") br ON t.tool_id=br.tool_id");
    if (q.lastError().isValid())
        qWarning() << "[DB] Create v_tool_latest_operation failed:" << q.lastError().text();

    qInfo() << "[DB] Views ensured: v_tool_stats + v_tool_latest_operation";
    return true;
}


// [2026-06-23] SQLite回退模式播种完整业务数据（工具/记录/告警/操作日志）
//   确保仪表盘4个卡片 + 各列表页面都有数据展示
bool DatabaseManager::seedBusinessData() {
    QSqlQuery q(m_db);

    // ── 工具分类（7个）─ [V2.02-fix] 空表检测，避免覆盖已有生产数据 ──
    {
        QSqlQuery chk(m_db);
        chk.exec("SELECT COUNT(*) FROM tool_category");
        if (!chk.next() || chk.value(0).toInt() == 0) {
            struct { int id; const char* name; int pid; int sort; const char* icon; } cats[] = {
                {1,"电动工具",0,1,"🔌"}, {2,"手动工具",0,2,"🔧"}, {3,"测量工具",0,3,"📏"},
                {4,"焊接工具",0,4,"🔥"}, {5,"照明工具",0,5,"🔦"}, {6,"紧固工具",1,1,"🔩"},
                {7,"切割工具",2,1,"✂️"},
            };
            for (auto& c : cats) {
                q.prepare("INSERT INTO tool_category(category_id,category_name,parent_id,sort_order,icon) VALUES(?,?,?,?,?)");
                q.addBindValue(c.id); q.addBindValue(c.name); q.addBindValue(c.pid); q.addBindValue(c.sort); q.addBindValue(c.icon);
                q.exec();
            }
        }
    }

    // ── 工具柜（3个）[V2.02-fix] 空表检测 ──
    {
        QSqlQuery chk(m_db);
        chk.exec("SELECT COUNT(*) FROM tool_cabinet");
        if (!chk.next() || chk.value(0).toInt() == 0) {
            q.exec("INSERT INTO tool_cabinet(cabinet_id,cabinet_name,cabinet_code,location,ip_address,status,created_at,updated_at) "
                   "VALUES(1,'A柜','CAB-A','维修车间东侧','192.168.1.101','active',datetime('now','localtime'),datetime('now','localtime'))");
            q.exec("INSERT INTO tool_cabinet(cabinet_id,cabinet_name,cabinet_code,location,ip_address,status,created_at,updated_at) "
                   "VALUES(2,'B柜','CAB-B','维修车间西侧','192.168.1.102','active',datetime('now','localtime'),datetime('now','localtime'))");
            q.exec("INSERT INTO tool_cabinet(cabinet_id,cabinet_name,cabinet_code,location,ip_address,status,created_at,updated_at) "
                   "VALUES(3,'C柜','CAB-C','备件仓库','192.168.1.103','active',datetime('now','localtime'),datetime('now','localtime'))");
        }
    }

    // ── 工具信息（26个）[V2.02-fix] 空表检测 ──
    {
        QSqlQuery chk(m_db);
        chk.exec("SELECT COUNT(*) FROM tool_info");
        if (!chk.next() || chk.value(0).toInt() == 0) {
    struct { int id; const char* code; const char* name; const char* spec; int cat; int cab; int mg; const char* pos; int total; int avail; const char* status; int recommended; const char* rfid; } tools[] = {
        {1,"JZ01-CDQ","充电式电动解锥","12V锂电池",1,1,1,"03-04位",4,4,"in_stock",1,"E28011005200000001A"},
        {2,"JZ01-KB","9# 开口扳手","9mm",2,1,1,"01-05位",6,6,"in_stock",1,"E28011005200000002B"},
        {3,"JZ01-BX","保险丝钳","6寸",2,1,1,"01-15位",3,3,"in_stock",1,"E28011005200000003C"},
        {4,"JZ01-PH2","十字解锥头 2#","PH2",2,1,2,"03-12位",10,10,"in_stock",0,""},
        {5,"JZ01-NLJ","内六角扳手","1.5-10mm",2,1,3,"01-06位",5,4,"in_stock",1,"E28011005200000005D"},
        {6,"JZ01-SB","塞尺","0.02-1.0mm",3,1,3,"02-01位",4,4,"in_stock",0,"E28011005200000006E"},
        {7,"JZ01-DB","电工刀","折叠式",7,1,2,"01-09位",5,4,"in_stock",0,"E28011005200000007F"},
        {8,"JZ01-YQ","压线钳","0.25-10mm²",2,1,2,"02-07位",3,3,"in_stock",0,"E28011005200000008A"},
        {9,"JZ01-CZ","锤子","1.5磅",2,1,3,"03-14位",4,4,"in_stock",0,"E28011005200000009B"},
        {10,"JZ01-TH","套筒扳手组","8-32mm",6,1,1,"02-13位",2,2,"in_stock",1,"E2801100520000000AC"},
        {11,"JZ01-WY","万用表","数字式",3,1,2,"02-01位",3,3,"in_stock",0,"E2801100520000000BD"},
        {12,"JZ01-CRV","开口扳手(20×22)","20×22mm",2,1,3,"01-05位",6,6,"in_stock",0,"E2801100520000000CE"},
        {13,"JZ02-ZD","十字螺丝刀","PH2×150mm",2,2,5,"01-03位",8,8,"in_stock",0,"E2801100520000000DF"},
        {14,"JZ02-YZ","一字解锥头","6mm",2,2,4,"02-12位",5,4,"in_stock",0,""},
        {15,"JZ02-JL","棘轮扳手","3/8寸",2,2,4,"02-03位",12,11,"in_stock",0,"E2801100520000000F1"},
        {16,"JZ02-HQ","焊枪","100W",4,2,5,"03-07位",3,3,"maintenance",0,"E280110052000000102"},
        {17,"JZ02-BZ","剥线钳","0.5-6mm²",2,2,5,"01-14位",4,4,"in_stock",0,"E280110052000000113"},
        {18,"JZ02-SG","手锯","300mm",7,2,4,"01-11位",3,3,"in_stock",0,"E280110052000000124"},
        {19,"JZ02-CL","游标卡尺","0-150mm",3,2,7,"02-06位",3,2,"in_stock",0,"E280110052000000135"},
        {20,"JZ02-DS","电刷","标准型",2,2,6,"01-08位",5,5,"in_stock",0,""},
        {21,"JZ03-QG","强光手电","LED 1000lm",5,2,6,"03-10位",7,7,"in_stock",0,"E280110052000000157"},
        {22,"JZ03-RF","热风枪","2000W",4,3,6,"01-11位",2,1,"in_stock",1,"E280110052000000168"},
        {23,"JZ03-JQ","剪刀","8寸",7,3,8,"03-02位",8,8,"maintenance",0,"E280110052000000179"},
        {24,"JZ03-DJ","电烙铁","60W",4,3,6,"02-09位",5,5,"in_stock",0,"E28011005200000018A"},
        {25,"JZ03-YG","验电笔","数字式",3,3,2,"01-02位",6,6,"in_stock",0,"E28011005200000019B"},
        {26,"JZ03-XY","吸锡器","手动式",4,3,7,"03-05位",4,4,"in_stock",0,"E2801100520000001AC"},
    };
    for (auto& t : tools) {
        q.prepare("INSERT INTO tool_info(tool_id,tool_code,tool_name,spec,category_id,cabinet_id,machine_group_id,position,total_qty,current_qty,rfid_tag,status,is_recommended) "
                  "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)");
        q.addBindValue(t.id); q.addBindValue(t.code); q.addBindValue(t.name); q.addBindValue(t.spec);
        q.addBindValue(t.cat); q.addBindValue(t.cab); q.addBindValue(t.mg); q.addBindValue(t.pos);
        q.addBindValue(t.total); q.addBindValue(t.avail); q.addBindValue(t.rfid); q.addBindValue(t.status);
        q.addBindValue(t.recommended);
        q.exec();
    }
        } // [V2.02-fix] 关闭 tool_info 空表检测 if 块
    }

    // ── 借用记录（20条）[V2.02-fix] 空表检测 ──
    {
        QSqlQuery chk(m_db);
        chk.exec("SELECT COUNT(*) FROM tool_borrow_record");
        if (!chk.next() || chk.value(0).toInt() == 0) {
    struct { int id; const char* flow; int uid; int tid; int qty; const char* reason; const char* btime; const char* etime; const char* atime; const char* status; } recs[] = {
        {1,"BR20260301001",2,1,1,"维修电动设备","2026-03-01 08:30","2026-03-01 17:00","2026-03-01 16:45","returned"},
        {2,"BR20260305001",3,2,2,"更换开口扳手组","2026-03-05 09:15","2026-03-05 18:00","2026-03-05 17:30","returned"},
        {3,"BR20260310001",4,5,1,"设备检修","2026-03-10 10:00","2026-03-10 17:00","2026-03-10 16:20","returned"},
        {4,"BR20260315001",2,10,1,"大修发动机","2026-03-15 08:45","2026-03-16 17:00","2026-03-16 15:30","returned"},
        {5,"BR20260402001",6,13,2,"日常维护","2026-04-02 11:00","2026-04-02 17:00","2026-04-02 16:50","returned"},
        {6,"BR20260408001",3,1,1,"紧急维修","2026-04-08 14:20","2026-04-08 18:00","2026-04-08 17:15","returned"},
        {7,"BR20260415001",5,4,3,"批量更换螺丝","2026-04-15 09:30","2026-04-15 18:00","2026-04-15 17:40","returned"},
        {8,"BR20260422001",2,11,1,"电气检测","2026-04-22 10:10","2026-04-22 17:00","2026-04-22 16:30","returned"},
        {9,"BR20260501001",4,7,1,"线路维修","2026-05-01 08:00","2026-05-01 12:00","2026-05-01 11:45","returned"},
        {10,"BR20260506002",1,6,1,"精密测量","2026-05-06 13:30","2026-05-06 18:00","2026-05-06 17:20","returned"},
        {11,"BR20260512001",6,22,1,"设备加热","2026-05-12 09:50","2026-05-12 17:00","2026-05-12 16:55","returned"},
        {12,"BR20260601001",3,9,1,"基础维修","2026-06-01 10:30","2026-06-01 17:00","2026-06-01 17:10","returned"},
        {13,"BR20260615001",5,19,1,"精密测量","2026-06-15 08:15","2026-06-15 17:00","2026-06-15 16:40","returned"},
        {14,"BR20260603002",4,3,1,"电器维修","2026-06-03 10:00","2026-06-03 17:00","2026-06-03 16:30","returned"},
        {15,"BR20260605003",2,14,2,"改锥头更换","2026-06-05 11:15","2026-06-05 18:00","2026-06-05 17:30","returned"},
        {16,"BR20260608001",5,22,1,"热风作业","2026-06-08 08:50","2026-06-08 17:00","","borrowing"},
        {17,"BR20260610001",4,7,1,"电工刀维修","2026-06-10 09:10","2026-06-10 17:00","","borrowing"},
        {18,"BR20260612001",2,14,1,"解锥头更换","2026-06-12 10:30","2026-06-12 18:00","","overdue"},
        {19,"BR20260609002",3,10,1,"发动机检修","2026-06-09 08:15","2026-06-09 17:00","2026-06-09 16:40","returned"},
        {20,"BR20260611003",6,5,1,"内六角保养","2026-06-11 13:00","2026-06-11 17:00","","borrowing"},
    };
    for (auto& r : recs) {
        q.prepare("INSERT INTO tool_borrow_record(record_id,flow_no,user_id,tool_id,borrow_qty,borrow_reason,"
                  "borrow_time,expected_return_time,actual_return_time,status) VALUES(?,?,?,?,?,?,?,?,?,?)");
        q.addBindValue(r.id); q.addBindValue(r.flow); q.addBindValue(r.uid); q.addBindValue(r.tid);
        q.addBindValue(r.qty); q.addBindValue(r.reason); q.addBindValue(r.btime);
        q.addBindValue(r.etime);
        q.addBindValue(strlen(r.atime) > 0 ? r.atime : QVariant());
        q.addBindValue(r.status);
        q.exec();
    }
        } // [V2.02-fix] 关闭 tool_borrow_record 空表检测 if 块
    }

    // ── 告警类型字典表 [2026-06-25] 动态告警类型+级别管理 ──
    q.exec("CREATE TABLE IF NOT EXISTS sys_alert_type ("
           "  type_id     INTEGER PRIMARY KEY AUTOINCREMENT,"
           "  type_code   TEXT    NOT NULL UNIQUE,"
           "  type_name   TEXT    NOT NULL,"
           "  alert_level TEXT    DEFAULT 'warn',"
           "  sort_order  INTEGER DEFAULT 0,"
           "  is_active   INTEGER DEFAULT 1,"
           "  created_at  TEXT    DEFAULT (datetime('now','localtime'))"
           ")");
    // [2026-06-26v13] 告警类型对齐MySQL现有映射（type_id必须一致）
    //   MySQL当前映射：1-overdue,2-mismatch,3-missing,4-offline,5-unauthorized,
    //                 6-low_stock,7-system,8-power,9-network_error,10-door_open,
    //                 11-stranger,12-rack_mismatch,13-temp_high,14-power_low,
    //                 15-sensor_fail,16-login_fail
    //   注意：先清空旧数据确保type_id从1开始自增
    q.exec("DELETE FROM sys_alert_type");
    struct { const char* code; const char* name; const char* level; int order; } types[] = {
        {"overdue","逾期未还","warn",1},       {"mismatch","工具错放","warn",2},
        {"missing","工具缺失","error",3},      {"offline","柜门异常","error",4},
        {"unauthorized","未授权操作","error",5},{"low_stock","库存不足","info",6},
        {"system","系统异常","info",7},         {"power","电源异常","error",8},
        {"network_error","网络故障","error",9}, {"door_open","柜门未关","warn",10},
        {"stranger","陌生人告警","warn",11},    {"rack_mismatch","货架错放","warn",12},
        {"temp_high","温度过高","warn",13},     {"power_low","电量不足","warn",14},
        {"sensor_fail","传感器故障","error",15},{"login_fail","登录失败","warn",16},
    };
    int typeIdx = 1;
    for (auto& t : types) {
        q.prepare("INSERT INTO sys_alert_type(type_id,type_code,type_name,alert_level,sort_order) VALUES(?,?,?,?,?)");
        q.addBindValue(typeIdx);
        q.addBindValue(t.code); q.addBindValue(t.name); q.addBindValue(t.level); q.addBindValue(t.order);
        q.exec();
        typeIdx++;
    }
    qInfo() << "[DB] Alert types seeded (cleared+reinserted with explicit IDs):" << (sizeof(types)/sizeof(types[0])) << "types";

    // ── 告警记录表 [2026-06-26v12] 字段对齐MySQL：保留alert_type/alert_level兼容AlertDAO ──
    // [2026-06-26v2] 新增record_id列，关联tool_borrow_record用于借款人回退查询
    q.exec("CREATE TABLE IF NOT EXISTS sys_alert ("
           "  alert_id    INTEGER PRIMARY KEY AUTOINCREMENT,"
           "  type_id     INTEGER NOT NULL DEFAULT 1,"
           "  alert_type  TEXT    DEFAULT '',"
           "  alert_level TEXT    DEFAULT 'warn',"
           "  tool_id     INTEGER DEFAULT NULL,"
           "  tool_code   TEXT    DEFAULT '',"
           "  content     TEXT    NOT NULL DEFAULT '',"
           "  status      TEXT    DEFAULT 'unhandled',"
           "  user_id     INTEGER DEFAULT NULL,"
           "  record_id   INTEGER DEFAULT 0,"
           "  created_at  TEXT    DEFAULT (datetime('now','localtime')),"
           "  handled_at  TEXT    DEFAULT NULL,"
           "  handler_id  INTEGER DEFAULT NULL,"
           "  remark      TEXT    DEFAULT ''"
           ")");
    // [2026-06-26v12] 兼容旧表迁移：添加可能缺失的列（SQLite ALTER不支持NOT NULL，用DEFAULT代替）
    q.exec("ALTER TABLE sys_alert ADD COLUMN type_id INTEGER DEFAULT 1");
    q.exec("ALTER TABLE sys_alert ADD COLUMN alert_type TEXT DEFAULT ''");
    q.exec("ALTER TABLE sys_alert ADD COLUMN alert_level TEXT DEFAULT 'warn'");
    q.exec("ALTER TABLE sys_alert ADD COLUMN tool_id INTEGER DEFAULT NULL");
    q.exec("ALTER TABLE sys_alert ADD COLUMN tool_code TEXT DEFAULT ''");
    q.exec("ALTER TABLE sys_alert ADD COLUMN user_id INTEGER DEFAULT NULL");
    q.exec("ALTER TABLE sys_alert ADD COLUMN handled_at TEXT DEFAULT NULL");
    q.exec("ALTER TABLE sys_alert ADD COLUMN handler_id INTEGER DEFAULT NULL");
    q.exec("ALTER TABLE sys_alert ADD COLUMN remark TEXT DEFAULT ''");
    q.exec("ALTER TABLE sys_alert ADD COLUMN record_id INTEGER DEFAULT 0");  // [2026-06-26v2] 关联借用记录
    // [2026-06-26v7] 检查sys_alert_type表是否存在type_code列，如缺失则重建
    {
        QSqlQuery colCheck(m_db);
        colCheck.exec("SELECT type_code FROM sys_alert_type LIMIT 1");
        if (colCheck.lastError().isValid()) {
            qWarning() << "[DB] sys_alert_type missing type_code column, recreating...";
            q.exec("DROP TABLE IF EXISTS sys_alert_type");
            q.exec("CREATE TABLE IF NOT EXISTS sys_alert_type ("
                   "  type_id     INTEGER PRIMARY KEY AUTOINCREMENT,"
                   "  type_code   TEXT    NOT NULL UNIQUE,"
                   "  type_name   TEXT    NOT NULL,"
                   "  alert_level TEXT    DEFAULT 'warn',"
                   "  sort_order  INTEGER DEFAULT 0,"
                   "  is_active   INTEGER DEFAULT 1,"
                   "  created_at  TEXT    DEFAULT (datetime('now','localtime'))"
                   ")");
            int ti = 1;
            for (auto& t : types) {
                q.prepare("INSERT INTO sys_alert_type(type_id,type_code,type_name,alert_level,sort_order) VALUES(?,?,?,?,?)");
                q.addBindValue(ti); q.addBindValue(t.code); q.addBindValue(t.name); q.addBindValue(t.level); q.addBindValue(t.order);
                q.exec(); ti++;
            }
        }
    }

    // [V2.13 2026-07-04 袁燕] 种子告警数据修复根因：
    //   原逻辑检测hasLegacyData时包含"alert_type=''"条件，
    //   但insertAlert不填alert_type列→新告警alert_type为空→每次启动判定hasLegacyData=true
    //   → DELETE FROM sys_alert清空全部告警→重插20条种子→"永远是20条"
    //   修复：只在sys_alert为空表时播种种子数据，有数据就不再清空
    {
        QSqlQuery check(m_db);
        check.exec("SELECT COUNT(*) FROM sys_alert");
        int existingCount = (check.next()) ? check.value(0).toInt() : 0;
        qInfo() << "[DB] Existing alert count:" << existingCount;
        if (existingCount == 0) {
            qInfo() << "[DB] Seeding 20 initial alert records...";
            // 结构: type_id, alert_type, alert_level, tool_code, user_id, status, content, created_at
            struct { int tid; const char* atype; const char* alevel; const char* tcode; int uid;
                     const char* st; const char* ct; const char* ctime; } alerts[] = {
                // ══════ 逾期未还 type_id=1 warn ══════
                {1,"overdue","warn","TOOL-001",4,"unhandled",
                 "工具[热风枪]已逾期未归还，借用时长超出规定期限3天","2026-06-25 08:00"},
                {1,"overdue","warn","TOOL-003",5,"unhandled",
                 "工具[充电式电动解锥]已逾期未归还，借用时长超出规定期限2天","2026-06-24 10:30"},
                {1,"overdue","warn","TOOL-005",6,"unhandled",
                 "工具[游标卡尺]已逾期未归还，借用时长超出规定期限5天","2026-06-23 09:15"},
                {1,"overdue","warn","TOOL-010",7,"handled",
                 "工具[9# 开口扳手]已逾期未归还，借用时长超出规定期限1天","2026-06-20 08:00"},
                // ══════ 工具错放 type_id=2(mismatch) warn ══════
                {2,"mismatch","warn","TOOL-002",0,"unhandled",
                 "工具[保险丝钳]检测到放置在错误货位，当前货位：A-03，正确货位：B-02","2026-06-25 14:00"},
                {2,"mismatch","warn","TOOL-006",0,"unhandled",
                 "工具[十字解锥头 2#]检测到放置在错误货位，当前货位：C-01，正确货位：C-05","2026-06-24 16:30"},
                {2,"mismatch","warn","TOOL-008",7,"handled",
                 "工具[内六角扳手]检测到放置在错误货位，已由管理员复位","2026-06-19 10:00"},
                // ══════ 工具缺失 type_id=3(missing) error ══════
                {3,"missing","error","TOOL-004",0,"unhandled",
                 "工具[棘轮扳手]在货位D-02检测缺失，上次盘点时间：2026-06-25 08:00","2026-06-26 08:00"},
                // ══════ 柜门异常 type_id=4(offline) error ══════
                {4,"offline","error","",0,"unhandled",
                 "1号柜柜门超过60秒未关闭，请检查柜门状态","2026-06-26 10:15"},
                {4,"offline","error","",0,"handled",
                 "2号柜柜门异常开启，已由管理员确认关闭","2026-06-18 14:30"},
                // ══════ 未授权操作 type_id=5(unauthorized) error ══════
                {5,"unauthorized","error","",0,"unhandled",
                 "检测到未授权人员尝试打开3号柜，时间：2026-06-26 14:30","2026-06-26 14:30"},
                // ══════ 库存不足 type_id=6(low_stock) info ══════
                {6,"low_stock","info","TOOL-007",0,"unhandled",
                 "工具[一字解锥头]库存低于安全阈值，当前库存：2件，安全阈值：5件","2026-06-25 11:00"},
                {6,"low_stock","info","TOOL-001",0,"unhandled",
                 "工具[热风枪]库存低于安全阈值，当前库存：1件，安全阈值：3件","2026-06-24 09:00"},
                // ══════ 系统异常 type_id=7(system) info ══════
                {7,"system","info","",0,"unhandled",
                 "系统内存使用率超过85%，请检查系统资源","2026-06-26 12:00"},
                {7,"system","info","",0,"handled",
                 "数据库连接池耗尽告警，已自动恢复","2026-06-17 08:00"},
                // ══════ 陌生人告警 type_id=11(stranger) warn ══════
                {11,"stranger","warn","",0,"unhandled",
                 "摄像头检测到未注册人员靠近智能柜，时间：2026-06-26 10:15","2026-06-26 10:15"},
                {11,"stranger","warn","",0,"handled",
                 "摄像头检测到未注册人员靠近智能柜，时间：2026-06-25 18:00，已确认","2026-06-25 18:00"},
                // ══════ 温度过高 type_id=13(temp_high) warn ══════
                {13,"temp_high","warn","",0,"unhandled",
                 "2号柜内部温度达到38°C，超出安全范围(25-35°C)","2026-06-26 13:00"},
                // ══════ 登录失败 type_id=16(login_fail) warn ══════
                {16,"login_fail","warn","",0,"unhandled",
                 "用户[admin]连续3次登录失败，IP：192.168.1.100","2026-06-26 07:00"},
                {16,"login_fail","warn","",7,"handled",
                 "用户[孙七]登录失败，已重置密码","2026-06-16 15:30"},
            };
            for (auto& a : alerts) {
                q.prepare("INSERT INTO sys_alert(type_id,alert_type,alert_level,tool_code,user_id,status,content,created_at)"
                          " VALUES(?,?,?,?,?,?,?,?)");
                q.addBindValue(a.tid);
                q.addBindValue(QString::fromUtf8(a.atype));
                q.addBindValue(QString::fromUtf8(a.alevel));
                q.addBindValue(QString::fromUtf8(a.tcode));
                q.addBindValue(a.uid);
                q.addBindValue(QString::fromUtf8(a.st));
                q.addBindValue(QString::fromUtf8(a.ct));
                q.addBindValue(QString::fromUtf8(a.ctime));
                if (!q.exec()) {
                    qWarning() << "[DB] Seed alert failed:" << q.lastError().text();
                }
            }
            qInfo() << "[DB] Alert seed data inserted: 20 records (type_id aligned with MySQL)";
        }
    }

    // ── 操作日志（15条） ──
    q.exec("CREATE TABLE IF NOT EXISTS sys_operation_log ("
           "  log_id         INTEGER PRIMARY KEY AUTOINCREMENT,"
           "  user_id        INTEGER DEFAULT NULL,"
           "  username       TEXT    DEFAULT '',"
           "  operation_type TEXT    NOT NULL,"
           "  target_type    TEXT    DEFAULT '',"
           "  target_id      TEXT    DEFAULT '',"
           "  content        TEXT,"
           "  ip_address     TEXT    DEFAULT '',"
           "  created_at     TEXT    DEFAULT (datetime('now','localtime'))"
           ")");
    // [V8.0 2026-06-28] 致命Bug修复：原代码无条件DELETE清空所有操作日志
    //   导致test1等真实出库/入库记录每次启动都被删除
    //   改为空表检测：仅在表为空时插入模拟数据，保留运行时产生的真实记录
    //   作者：袁燕 - 多次反馈test1记录丢失，根因在此
    QSqlQuery logChk(m_db);
    logChk.exec("SELECT COUNT(*) FROM sys_operation_log");
    int logCnt = (logChk.next()) ? logChk.value(0).toInt() : 0;
    if (logCnt == 0) {
    struct { int uid; const char* uname; const char* op; const char* content; const char* ctime; } logs[] = {
        {1,"张三","login","管理员张三登录系统","2026-06-13 08:30"},
        {2,"李四","login","用户李四通过人脸识别登录","2026-06-13 08:35"},
        {3,"王五","borrow","王五借用工具:充电式电动解锥","2026-06-12 09:00"},
        {4,"赵六","return","赵六归还工具:内六角扳手","2026-06-12 09:30"},
        {5,"孙七","borrow","孙七借用工具:游标卡尺","2026-06-12 11:00"},
        {6,"周八","login","用户周八登录系统","2026-06-12 13:00"},
        {1,"张三","add_user","管理员新增测试用户","2026-06-12 10:00"},
        {1,"张三","edit_user","管理员编辑用户:赵六信息","2026-06-12 10:15"},
        {2,"李四","borrow","李四借用工具:热风枪","2026-06-12 14:00"},
        {3,"王五","return","王五归还工具:9#开口扳手","2026-06-11 09:00"},
        {1,"张三","disable_user","管理员禁用用户:孙七","2026-06-11 10:00"},
        {2,"李四","return","李四归还工具:万用表","2026-06-11 10:30"},
        {3,"王五","borrow","王五借用工具:锤子","2026-06-11 11:00"},
        {6,"周八","borrow","周八借用工具:焊枪","2026-06-11 14:00"},
        {1,"张三","export","管理员导出本月借用记录","2026-06-09 16:00"},
    };
    for (auto& l : logs) {
        q.prepare("INSERT INTO sys_operation_log(user_id,username,operation_type,content,created_at) VALUES(?,?,?,?,?)");
        q.addBindValue(l.uid); q.addBindValue(l.uname); q.addBindValue(l.op); q.addBindValue(l.content); q.addBindValue(l.ctime);
        q.exec();
    }
    }  // [V8.0] 闭合 if (logCnt == 0)

    // [2026-08-24 袁燕] 人脸识别识别统计日志表（专利实测数据通道）
    //   用途：记录每次人脸识别尝试，用于统计误识率(FAR)/拒识率(FRR)/识别延迟，支撑专利交底书实测数据
    //   字段说明：
    //     result       识别结果：success(通过)/stranger(判陌生人)/rejected(双验证失败)/error(异常)
    //     best_sim     最佳候选余弦相似度（0~1，陌生人场景记为0）
    //     best_dist    最佳候选欧氏距离（第二防线，陌生人场景记为1）
    //     threshold    本次生效判定阈值（单人脸/多人脸动态取值）
    //     mode         判定模式：single(单人脸)/multi(多人脸)
    //     candidate_cnt 库内已录入人脸数（库容）
    //     elapsed_ms   单次识别耗时（毫秒，含特征提取+比对）
    //     matched_user 命中用户工号（stranger/error 为空）
    q.exec("CREATE TABLE IF NOT EXISTS face_recog_log ("
           "  log_id         INTEGER PRIMARY KEY AUTOINCREMENT,"
           "  result         TEXT    NOT NULL,"
           "  best_sim       REAL    DEFAULT 0,"
           "  best_dist      REAL    DEFAULT 1,"
           "  threshold      REAL    DEFAULT 0,"
           "  mode           TEXT    DEFAULT '',"
           "  candidate_cnt  INTEGER DEFAULT 0,"
           "  elapsed_ms     INTEGER DEFAULT 0,"
           "  matched_user   TEXT    DEFAULT '',"
           "  created_at     TEXT    DEFAULT (datetime('now','localtime'))"
           ")");

    // [V8.0 2026-06-28] 出库操作历史记录（10条），content加工具编号，INSERT加target_id
    //   作者：袁燕 - 修复出库记录工具编号缺失问题
    //   1. 清理旧格式数据（content不含"编号["），保留真实出库记录
    //   2. 仅在新格式数据为0时插入模拟数据（避免重复）
    q.exec("DELETE FROM sys_operation_log WHERE operation_type='checkout' AND content NOT LIKE '%编号[%'");
    {
        QSqlQuery chkCheckout(m_db);
        chkCheckout.exec("SELECT COUNT(*) FROM sys_operation_log WHERE operation_type='checkout'");
        int checkoutCnt = (chkCheckout.next()) ? chkCheckout.value(0).toInt() : 0;
        if (checkoutCnt == 0) {
            // tool_id映射: 充电式电动解锥=1, 内六角扳手=5, 游标卡尺=19, 热风枪=22,
            //              9# 开口扳手=2, 万用表=11, 锤子=9, 焊枪=16
            struct { int uid; int targetId; const char* op; const char* content; const char* ctime; } checkoutLogs[] = {
                {1,1, "checkout","出库工具「充电式电动解锥」编号[JZ01-CDQ]×2，原因：报废更换","2026-06-20 09:15"},
                {2,5, "checkout","出库工具「内六角扳手」编号[JZ01-NLJ]×1，原因：损坏退役","2026-06-19 14:30"},
                {3,19,"checkout","出库工具「游标卡尺」编号[JZ02-CL]×1，原因：调拨其他机组","2026-06-18 10:00"},
                {1,22,"checkout","出库工具「热风枪」编号[JZ03-RF]×3，原因：升级替换","2026-06-17 15:45"},
                {2,2, "checkout","出库工具「9# 开口扳手」编号[JZ01-KB]×2，原因：超期淘汰","2026-06-16 11:20"},
                {3,11,"checkout","出库工具「万用表」编号[JZ01-WY]×1，原因：其他原因","2026-06-15 16:00"},
                {1,9, "checkout","出库工具「锤子」编号[JZ01-CZ]×1，原因：损坏退役","2026-06-14 09:30"},
                {2,16,"checkout","出库工具「焊枪」编号[JZ02-HQ]×1，原因：报废更换","2026-06-13 13:45"},
                {3,1, "checkout","出库工具「充电式电动解锥」编号[JZ01-CDQ]×1，原因：调拨其他机组","2026-06-12 10:15"},
                {1,5, "checkout","出库工具「内六角扳手」编号[JZ01-NLJ]×2，原因：升级替换","2026-06-11 14:30"},
            };
            for (auto& cl : checkoutLogs) {
                q.prepare("INSERT INTO sys_operation_log(user_id,operation_type,target_type,target_id,content,ip_address,created_at) VALUES(?,?,?,?,?,?,?)");
                q.addBindValue(cl.uid); q.addBindValue(cl.op); q.addBindValue("tool");
                q.addBindValue(cl.targetId); q.addBindValue(cl.content); q.addBindValue("127.0.0.1"); q.addBindValue(cl.ctime);
                q.exec();
            }
            qInfo() << "[DB] V8.0 checkout logs inserted (10 records with tool_code)";
        }
    }

    // [V2.03 2026-06-27] 入库操作历史记录（10条），供入库记录Tab展示
    //   content格式与出库统一：入库工具「名」编号[编号]×数量，供应商：xxx
    //   注意：只在sys_operation_log中没有checkin记录时插入（空表检测）
    {
        QSqlQuery chkCheckin(m_db);
        chkCheckin.exec("SELECT COUNT(*) FROM sys_operation_log WHERE operation_type='checkin'");
        int checkinCnt = (chkCheckin.next()) ? chkCheckin.value(0).toInt() : 0;
        if (checkinCnt == 0) {
            struct { int uid; const char* op; const char* content; const char* ctime; } checkinLogs[] = {
                {1,"checkin","入库工具「充电式电动解锥」编号[JZ01-CDQ]×2，供应商：史丹利","2026-06-20 10:30"},
                {2,"checkin","入库工具「内六角扳手」编号[JZ01-NLJ]×3，供应商：世达工具","2026-06-19 15:45"},
                {3,"checkin","入库工具「游标卡尺」编号[JZ02-CL]×1，供应商：上海量具","2026-06-18 11:15"},
                {1,"checkin","入库工具「热风枪」编号[JZ03-RF]×2，供应商：博世电动","2026-06-17 16:30"},
                {2,"checkin","入库工具「9#开口扳手」编号[JZ01-KB]×4，供应商：世达工具","2026-06-16 12:00"},
                {3,"checkin","入库工具「万用表」编号[JZ01-WY]×1，供应商：福禄克","2026-06-15 17:15"},
                {1,"checkin","入库工具「锤子」编号[JZ01-CZ]×2，供应商：史丹利","2026-06-14 10:45"},
                {2,"checkin","入库工具「焊枪」编号[JZ02-HQ]×1，供应商：博世电动","2026-06-13 14:15"},
                {3,"checkin","入库工具「充电式电动解锥」编号[JZ01-CDQ]×1，供应商：史丹利","2026-06-12 11:30"},
                {1,"checkin","入库工具「内六角扳手」编号[JZ01-NLJ]×2，供应商：世达工具","2026-06-11 15:00"},
            };
            for (auto& ci : checkinLogs) {
                q.prepare("INSERT INTO sys_operation_log(user_id,operation_type,target_type,content,ip_address,created_at) VALUES(?,?,?,?,?,?)");
                q.addBindValue(ci.uid); q.addBindValue(ci.op); q.addBindValue("tool");
                q.addBindValue(ci.content); q.addBindValue("127.0.0.1"); q.addBindValue(ci.ctime);
                q.exec();
            }
            qInfo() << "[DB] Checkin seed data inserted: 10 records";
        }
    }

    qInfo() << "[DB] Business seed data complete: 7 categories, 3 cabinets, 26 tools, 20 records, 16 alert_types, 8 alerts, 35 logs(含10出库+10入库)";

    // [v13] 验证种子数据完整性（写入文件供确认）
    {
        QSqlQuery vfy(m_db);
        vfy.exec("SELECT COUNT(*) FROM sys_alert");
        int alertCnt = (vfy.next()) ? vfy.value(0).toInt() : 0;
        vfy.exec("SELECT COUNT(*) FROM sys_alert_type");
        int typeCnt = (vfy.next()) ? vfy.value(0).toInt() : 0;
        vfy.exec("SELECT COUNT(*) FROM tool_info");
        int toolCnt = (vfy.next()) ? vfy.value(0).toInt() : 0;
        vfy.exec("SELECT COUNT(*) FROM sys_user");
        int userCnt = (vfy.next()) ? vfy.value(0).toInt() : 0;
        vfy.exec("SELECT COUNT(*) FROM tool_cabinet");
        int cabinetCnt = (vfy.next()) ? vfy.value(0).toInt() : 0;
        qInfo() << "[DB] VERIFY: alerts=" << alertCnt << "types=" << typeCnt << "tools=" << toolCnt << "users=" << userCnt << "cabinets=" << cabinetCnt;
        // 写入验证文件
        QFile vf(m_db.databaseName() + ".verify.txt");
        if (vf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QTextStream ts(&vf);
            ts << "SQLite DB Verify: " << m_db.databaseName() << "\n";
            ts << "alerts=" << alertCnt << " (expect 20)\n";
            ts << "alert_types=" << typeCnt << " (expect 16)\n";
            ts << "tools=" << toolCnt << " (expect 26)\n";
            ts << "users=" << userCnt << " (expect 7)\n";
            ts << "cabinets=" << cabinetCnt << " (expect 3)\n";
            // 额外验证：告警是否都有type_name
            vfy.exec("SELECT COUNT(*) FROM sys_alert a LEFT JOIN sys_alert_type t ON a.type_id=t.type_id WHERE t.type_name IS NULL");
            int nullTypes = (vfy.next()) ? vfy.value(0).toInt() : -1;
            ts << "alerts_with_null_type=" << nullTypes << " (expect 0)\n";
            vf.close();
        }
    }

    // [V7.0] 创建视图（SQLite版本，兼容MySQL语法）
    // v_tool_stats 工具统计视图 [2026-06-26v15] 借用统计从tool_borrow_record获取
    // [2026-06-27] in_stock_qty 改为所有工具的 current_qty 之和（不论状态），反映实际在库件数
    q.exec("DROP VIEW IF EXISTS v_tool_stats");
    q.exec("CREATE VIEW v_tool_stats AS "
           "SELECT (SELECT COUNT(*) FROM tool_info) AS total_tools,"
           "(SELECT COUNT(*) FROM tool_info WHERE status='in_stock') AS in_stock_count,"
           "(SELECT COUNT(DISTINCT tool_id) FROM tool_borrow_record WHERE status IN ('borrowing','overdue')) AS borrowed_count,"
           "(SELECT COUNT(*) FROM tool_info WHERE status='maintenance') AS maintenance_count,"
           "(SELECT COALESCE(SUM(total_qty),0) FROM tool_info) AS total_qty,"
           "(SELECT COALESCE(SUM(current_qty),0) FROM tool_info) AS in_stock_qty,"
           "(SELECT COALESCE(SUM(borrow_qty),0) FROM tool_borrow_record WHERE status IN ('borrowing','overdue')) AS borrowed_qty");

    // v_tool_latest_operation 最近操作视图
    // [2026-06-27] 机组隔离：只统计活跃借用(borrowing/overdue)
    // [V2.12-fix 2026-07-03 袁燕] 修复视图重复行Bug（latest_op_user改子查询LIMIT 1）
    q.exec("DROP VIEW IF EXISTS v_tool_latest_operation");
    q.exec("CREATE VIEW v_tool_latest_operation AS "
           "SELECT t.tool_id,"
           "COALESCE(br.latest_borrow_time,'') AS latest_op_time,"
           "CASE "
           "  WHEN br.latest_borrow_time IS NOT NULL THEN 'borrow'"
           "  ELSE 'checkin'"
           "END AS latest_op_type,"
           "COALESCE((SELECT u.real_name FROM tool_borrow_record tbr2 "
           "          LEFT JOIN sys_user u ON u.user_id=tbr2.user_id "
           "          WHERE tbr2.tool_id=t.tool_id AND tbr2.status IN ('borrowing','overdue') "
           "          ORDER BY tbr2.borrow_time DESC LIMIT 1),'') AS latest_op_user "
           "FROM tool_info t "
           "LEFT JOIN ("
           "  SELECT tool_id, MAX(borrow_time) AS latest_borrow_time "
           "  FROM tool_borrow_record WHERE status IN ('borrowing','overdue') GROUP BY tool_id"
           ") br ON t.tool_id=br.tool_id");

    qInfo() << "[DB] V7.0 views created: v_tool_stats + v_tool_latest_operation";

    // [V2.03u 2026-06-30 袁燕] 修复：删除DELETE+INSERT改为INSERT IGNORE
    //   根因：每次启动DELETE FROM task_type/task_type_tool清空了用户数据！
    //   用户在系统维护中添加的任务工具，重启后被种子数据覆盖。
    //   修复：INSERT IGNORE只插入不存在的主键，不覆盖已有数据。
    //   举一反三：对照关系(tool_position_mapping)种子数据不受影响（无DELETE）
    // 播种任务类型数据 — 只插入不存在的记录（INSERT IGNORE）
    // [2026-06-27] 默认任务类型：航前检查/航后维护等10种
    struct { int id; const char* code; const char* name; const char* desc; int dur; int sort; } taskTypes[] = {
        {1, "PRECHECK",   "航前检查",     "航班起飞前对工具柜工具进行全面检查与准备", 60, 1},
        {2, "POSTCHECK",  "航后维护",     "航班降落后对工具进行归位、清洁与维护", 60, 2},
        {3, "ENG_MAINT",  "发动机维护",   "发动机拆装、检查、更换部件等核心维护作业", 120, 3},
        {4, "AVIONICS",   "航电检修",     "航空电子设备、仪表、通信导航系统检测维修", 90, 4},
        {5, "HYDRAULIC",  "液压系统维护", "液压管路、泵阀、作动筒检查与更换", 60, 5},
        {6, "STRUCTURE",  "结构修理",     "机身蒙皮、框架、紧固件损伤修复", 180, 6},
        {7, "LANDING",    "起落架维护",   "起落架减震、刹车系统、轮胎更换", 90, 7},
        {8, "ELECTRICAL", "电气线路检修", "线路故障排查、线束修复、接插件更换", 60, 8},
        {9, "WELDING",    "焊接作业",     "金属结构焊接、修补、热处理", 120, 9},
        {10, "MEASURE",   "精密测量",     "三坐标测量、形位公差检测、校准", 60, 10},
    };
    for (auto& t : taskTypes) {
        q.prepare("INSERT IGNORE INTO task_type(type_id,type_code,type_name,description,default_duration,sort_order,is_active) "
                  "VALUES(?,?,?,?,?,?,1)");
        q.addBindValue(t.id); q.addBindValue(t.code); q.addBindValue(t.name);
        q.addBindValue(t.desc); q.addBindValue(t.dur); q.addBindValue(t.sort);
        q.exec();
    }

    // 播种任务类型-推荐工具关联数据 — 只插入不存在的记录
    // [V2.03u] 不再DELETE FROM task_type_tool！用户添加的工具关联必须保留
    struct { int typeId; int toolId; int sort; } ttRel[] = {
        // 航前检查(1) → 本机组所有工具（10个，覆盖全面检查场景）
        {1,1,1}, {1,2,2}, {1,3,3}, {1,5,4}, {1,6,5}, {1,9,6}, {1,10,7}, {1,11,8}, {1,13,9}, {1,19,10},
        // 航后维护(2) → 清洁维护工具（6个：清洁刷、抹布、扳手、螺丝刀、万用表、塞尺）
        {2,9,1}, {2,13,2}, {2,2,3}, {2,11,4}, {2,6,5}, {2,19,6},
        // 发动机维护(3) → 电动解锥、开口扳手、套筒扳手、内六角扳手、万用表、塞尺
        {3,1,1}, {3,2,2}, {3,10,3}, {3,5,4}, {3,11,5}, {3,6,6},
        // 航电检修(4) → 万用表、验电笔、剥线钳、压线钳、电烙铁、保险丝钳
        {4,11,1}, {4,25,2}, {4,17,3}, {4,8,4}, {4,24,5}, {4,3,6},
        // 液压系统维护(5) → 开口扳手、棘轮扳手、内六角扳手、锤子、十字螺丝刀
        {5,2,1}, {5,15,2}, {5,5,3}, {5,9,4}, {5,13,5},
        // 结构修理(6) → 锤子、手锯、电刷、十字螺丝刀、一字解锥头、剪刀
        {6,9,1}, {6,18,2}, {6,20,3}, {6,13,4}, {6,14,5}, {6,23,6},
        // 起落架维护(7) → 套筒扳手、棘轮扳手、开口扳手、强光手电、游标卡尺
        {7,10,1}, {7,15,2}, {7,12,3}, {7,21,4}, {7,19,5},
        // 电气线路检修(8) → 万用表、验电笔、剥线钳、电工刀、电烙铁、保险丝钳
        {8,11,1}, {8,25,2}, {8,17,3}, {8,7,4}, {8,24,5}, {8,3,6},
        // 焊接作业(9) → 焊枪、热风枪、电烙铁、吸锡器、锤子
        {9,16,1}, {9,22,2}, {9,24,3}, {9,26,4}, {9,9,5},
        // 精密测量(10) → 游标卡尺、塞尺、万用表、验电笔、内六角扳手
        {10,19,1}, {10,6,2}, {10,11,3}, {10,25,4}, {10,5,5},
    };
    for (auto& r : ttRel) {
        // [V2.03u] INSERT IGNORE：如果(type_id,tool_id)组合已存在则跳过，不覆盖用户数据
        q.prepare("INSERT IGNORE INTO task_type_tool(type_id,tool_id,sort_order,recommended_qty) VALUES(?,?,?,1)");
        q.addBindValue(r.typeId); q.addBindValue(r.toolId); q.addBindValue(r.sort);
        q.exec();
    }

    qInfo() << "[DB] V1.00.10 task_type seed data complete: 10 types + 60 tool associations";
    return true;
}

// 兼容版本B的services/DAOs
QSqlDatabase DatabaseManager::getConnection() const {
    return database();
}
