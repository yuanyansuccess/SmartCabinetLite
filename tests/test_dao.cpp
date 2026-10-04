/**
 * @file test_dao.cpp
 * @brief DAO 层单元测试（BaseDAO 类型转换 + 位置维度/机组隔离等核心查询铁律）
 * @author 袁燕
 *
 * 用例分两组：
 *   1) 纯逻辑组：用 SQLite 内存库验证 BaseDAO 的类型映射与 SQL 拼接，
 *      不依赖任何外部数据库，永远执行。
 *   2) 数据库组：连接 MySQL 测试库，验证位置维度查询、机组隔离、事务与自增ID约定。
 *
 * 安全设计（与 test_services 完全一致的三重闸门）：
 *   1) 连接参数只从环境变量读取：SC_TEST_DB_HOST/PORT/NAME/USER/PASS
 *   2) 库名强制白名单：必须以 "_test" 或 "_unittest" 结尾，否则拒绝连接
 *   3) 未提供环境变量时数据库用例全部 SKIP，纯逻辑组照常执行
 *   4) 数据库组全部为只读查询或零影响断言，不写入任何业务数据
 */

#include <QtTest>
#include <QCoreApplication>
#include <QProcessEnvironment>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QDir>

#include "common/Constants.h"
#include "common/DatabaseManager.h"
#include "db/BaseDAO.h"
#include "db/ToolDAO.h"
#include "db/RecordDAO.h"

using db::ToolDAO;
using db::RecordDAO;

class TestDao : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ── 纯逻辑组（SQLite 内存库，永远执行）──
    void rowToJson_numericTypes();
    void rowToJson_byteArrayToHex();
    void rowToJson_skipsNullFields();
    void paginate_sqlPattern();
    void paginate_clampsIllegalArguments();

    // ── 数据库组（需 MySQL 测试库，缺失则 SKIP）──
    void rowToJson_dateTimeIsoFormat();
    void count_aggregateSqlNotDoubleWrapped();
    void baseDao_insertAndGetIdReturnsZeroOnFailure();
    void baseDao_transactionCommitAndRollback();
    void toolDao_positionQueryRespectsMachineGroup();
    void recordDao_findBorrowsByUserUnknownUser();

private:
    bool    m_dbReady = false;
    QString m_skipReason;
};

// ── 安全闸门：校验测试库名合法性 ────────────────────────────────────────
namespace {
bool isAllowedTestDatabase(const QString& dbName) {
    const QString lower = dbName.toLower();
    return lower.endsWith("_test") || lower.endsWith("_unittest");
}

/// 打开 SQLite 内存库并建一张覆盖多种字段类型的测试表
/// @param[out] db 已打开的内存库连接
/// @return true=可用；false=驱动缺失，调用方应 QSKIP
bool prepareMemoryDb(QSqlDatabase& db) {
    // 多个用例各自建库，先移除同名旧连接，避免 Qt 打印 duplicate connection name 警告
    QSqlDatabase::removeDatabase("dao_mem");
    db = QSqlDatabase::addDatabase("QSQLITE", "dao_mem");
    db.setDatabaseName(":memory:");
    if (!db.open()) return false;
    QSqlQuery q(db);
    bool ok = q.exec("CREATE TABLE probe ("
                     "i INTEGER, d REAL, b INTEGER, s TEXT, blb BLOB, nul TEXT)");
    if (!ok) return false;
    ok = q.exec("INSERT INTO probe VALUES (42, 3.5, 1, 'hello', X'0102', NULL)");
    return ok;
}
}  // namespace

void TestDao::initTestCase() {
    // Qt SQL 驱动插件运行时加载，测试 exe 在 tests/ 子目录需显式补搜索路径
    const QDir appDir(QCoreApplication::applicationDirPath());
    for (const QString& rel : QStringList{ "../../Debug", "../Debug", "Debug" }) {
        const QString cand = QDir::cleanPath(appDir.filePath(rel));
        if (QDir(cand + "/sqldrivers").exists()) {
            QCoreApplication::addLibraryPath(cand);
            break;
        }
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString host = env.value("SC_TEST_DB_HOST");
    const QString name = env.value("SC_TEST_DB_NAME");
    const QString user = env.value("SC_TEST_DB_USER");
    const QString pass = env.value("SC_TEST_DB_PASS");
    const int port = env.value("SC_TEST_DB_PORT", "3306").toInt();

    if (host.isEmpty() || name.isEmpty() || user.isEmpty()) {
        m_skipReason = "未配置 SC_TEST_DB_HOST/NAME/USER 环境变量，跳过数据库用例";
        return;
    }
    if (!isAllowedTestDatabase(name)) {
        m_skipReason = QString("拒绝连接：测试库名必须以 _test 或 _unittest 结尾"
                               "（当前=%1），这是防止误连生产库的保护措施").arg(name);
        return;
    }
    if (!QSqlDatabase::isDriverAvailable("QODBC") &&
        !QSqlDatabase::isDriverAvailable("QMYSQL")) {
        m_skipReason = QStringLiteral(
            "Qt 未找到可用的 MySQL 驱动插件(QODBC/QMYSQL)，请设置 QT_PLUGIN_PATH=<构建目录>\\Debug");
        qWarning() << "[test_dao]" << m_skipReason;
        return;
    }
    if (!DatabaseManager::instance().initialize(host, port, name, user, pass) ||
        !DatabaseManager::instance().isConnected()) {
        m_skipReason = "测试库连接失败，跳过数据库用例";
        return;
    }
    m_dbReady = true;
}

void TestDao::cleanupTestCase() {
    QSqlDatabase::removeDatabase("dao_mem");
}

// ---------------------------------------------------------------------------
// QSKIP 自带 return，只能写在 void 用例函数体内；helper 里用会触发 MSVC C2561。
// 故统一用该宏在用例体内展开，调用后必须立即 return。
// ---------------------------------------------------------------------------
#define ENSURE_DB()                                                          \
    do {                                                                     \
        if (!m_dbReady) {                                                    \
            QSKIP(qPrintable(m_skipReason.isEmpty()                          \
                ? QStringLiteral("数据库未就绪") : m_skipReason));           \
            return;                                                          \
        }                                                                    \
    } while (0)

// ═══════════ 纯逻辑组 ═══════════

void TestDao::rowToJson_numericTypes() {
    QSqlDatabase db;
    if (!prepareMemoryDb(db)) {
        QSKIP("QSQLITE 内存库不可用，跳过类型映射用例");
        return;
    }
    QSqlQuery q(db);
    QVERIFY(q.exec("SELECT i, d, b, s FROM probe"));
    QVERIFY(q.next());
    const QJsonObject o = db::BaseDAO::rowToJson(q);

    QCOMPARE(o["i"].toInt(), 42);
    QCOMPARE(o["d"].toDouble(), 3.5);
    // SQLite 无原生布尔类型，INTEGER 列走 LongLong 分支，故按整数断言
    // （QMetaType::Bool 分支由 MySQL 侧驱动类型映射决定，不在内存库中断言）
    QCOMPARE(o["b"].toInt(), 1);
    QCOMPARE(o["s"].toString(), QStringLiteral("hello"));
}

void TestDao::rowToJson_byteArrayToHex() {
    QSqlDatabase db;
    if (!prepareMemoryDb(db)) {
        QSKIP("QSQLITE 内存库不可用，跳过类型映射用例");
        return;
    }
    QSqlQuery q(db);
    QVERIFY(q.exec("SELECT blb FROM probe"));
    QVERIFY(q.next());
    const QJsonObject o = db::BaseDAO::rowToJson(q);
    // 二进制统一转十六进制文本，避免不可见字符进入 JSON
    QCOMPARE(o["blb"].toString(), QStringLiteral("0102"));
}

void TestDao::rowToJson_skipsNullFields() {
    QSqlDatabase db;
    if (!prepareMemoryDb(db)) {
        QSKIP("QSQLITE 内存库不可用，跳过类型映射用例");
        return;
    }
    QSqlQuery q(db);
    QVERIFY(q.exec("SELECT i, nul FROM probe"));
    QVERIFY(q.next());
    const QJsonObject o = db::BaseDAO::rowToJson(q);
    QVERIFY(o.contains("i"));
    QVERIFY(!o.contains("nul"));  // NULL 字段不写入 JSON
}

void TestDao::paginate_sqlPattern() {
    const QString sql = db::BaseDAO().paginate("SELECT * FROM tool_info", 3, 20, "tool_id ASC");
    QVERIFY(sql.contains("LIMIT 20"));
    QVERIFY(sql.contains("OFFSET 40"));  // 第3页：offset = (3-1)*20
    QVERIFY(sql.contains("ORDER BY tool_id ASC"));
}

void TestDao::paginate_clampsIllegalArguments() {
    db::BaseDAO dao;
    // 页码非法（0/负数）时按第 1 页处理，OFFSET 不能为负否则 SQL 报错
    QVERIFY(dao.paginate("SELECT * FROM t", 0, 20).contains("OFFSET 0"));
    QVERIFY(dao.paginate("SELECT * FROM t", -3, 20).contains("OFFSET 0"));
    // 每页条数非法时回落到默认每页条数
    QVERIFY(dao.paginate("SELECT * FROM t", 1, 0).contains("LIMIT 20"));
    QVERIFY(dao.paginate("SELECT * FROM t", 1, -10).contains("LIMIT 20"));
}

// ═══════════ 数据库组 ═══════════

void TestDao::count_aggregateSqlNotDoubleWrapped() {
    ENSURE_DB();
    db::BaseDAO dao;
    // 明细查询 → 正常包装为 SELECT COUNT(*) FROM (明细) AS _cnt
    const int byDetail = dao.count("SELECT tool_id FROM tool_info");
    QVERIFY(byDetail > 0);
    // 聚合查询 → 必须原样执行；若被二次包装，结果会恒为 1 而与明细计数不等
    const int byAggregate = dao.count("SELECT COUNT(*) FROM tool_info");
    QCOMPARE(byAggregate, byDetail);
}

void TestDao::rowToJson_dateTimeIsoFormat() {
    ENSURE_DB();
    db::BaseDAO dao;
    // 用 NOW() 而非业务表：播种数据的 created_at 可能为空字符串，测不到日期分支
    QSqlQuery q = dao.query("SELECT NOW() AS dt");
    QVERIFY(q.next());
    const QJsonObject o = db::BaseDAO::rowToJson(q);
    const QString value = o["dt"].toString();
    // 必须被归一化成 ISO 8601（带 T），而不是 ODBC 原样的 "yyyy-MM-dd HH:mm:ss"
    const QString msg = QStringLiteral("日期字段非 ISO 格式：%1").arg(value);
    QVERIFY2(QDateTime::fromString(value, Qt::ISODate).isValid(), qPrintable(msg));
    QVERIFY2(value.contains(QLatin1Char('T')),
             qPrintable(QStringLiteral("归一化后仍缺少 ISO 的 T 分隔符：%1").arg(value)));
}

void TestDao::baseDao_insertAndGetIdReturnsZeroOnFailure() {
    ENSURE_DB();
    db::BaseDAO dao;
    // 失败路径统一返回 0（与 Service::Result 的 recordId 约定一致），不再返回 -1
    const int id = dao.insertAndGetId("INSERT INTO no_such_table_for_test VALUES (1)");
    QCOMPARE(id, 0);
}

void TestDao::baseDao_transactionCommitAndRollback() {
    ENSURE_DB();
    db::BaseDAO dao;
    // 提交路径：空事务内无写操作，提交成功
    QVERIFY(dao.transaction([]() { return true; }));
    // 回滚路径：回调返回 false 必须触发 rollback 且函数返回 false
    QVERIFY(!dao.transaction([]() { return false; }));
}

void TestDao::toolDao_positionQueryRespectsMachineGroup() {
    ENSURE_DB();
    // 机组隔离是核心安全设计：不存在的机组不得查出任何在库位置
    const int nonExistGroup = 999999;
    const QJsonObject r = ToolDAO().findAllByPosition(
        "", "", SC::TOOL_IN_STOCK, 0, 1, 20, nonExistGroup);
    QCOMPARE(r["total"].toInt(), 0);
    QCOMPARE(r["list"].toArray().size(), 0);
}

void TestDao::recordDao_findBorrowsByUserUnknownUser() {
    ENSURE_DB();
    const QList<BorrowRecord> list = RecordDAO().findBorrowsByUser(-1, 20);
    QCOMPARE(list.size(), 0);
}

QTEST_MAIN(TestDao)
#include "test_dao.moc"
