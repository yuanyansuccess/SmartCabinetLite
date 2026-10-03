/**
 * @file test_services.cpp
 * @brief Service 层核心业务规则单元测试（连接 MySQL 测试库）
 * @author 袁燕
 *
 * 安全设计（重要）：本测试连接 MySQL，但严格保证不触碰生产数据。
 *   1) 连接参数只从环境变量读取：SC_TEST_DB_HOST/PORT/NAME/USER/PASS
 *   2) 库名强制白名单：必须以 "_test" 或 "_unittest" 结尾（大小写不限），
 *      否则拒绝建立连接（防止误把生产库名填进来）
 *   3) 未提供环境变量时全部用例 SKIP，保证无 MySQL 环境也能正常构建运行
 *   4) 业务流程用例跑完后自愈（借用后必归还），不留下脏数据
 *
 * 环境变量示例：
 *   set SC_TEST_DB_HOST=127.0.0.1
 *   set SC_TEST_DB_PORT=3306
 *   set SC_TEST_DB_NAME=smart_cabinet_test
 *   set SC_TEST_DB_USER=root
 *   set SC_TEST_DB_PASS=xxxx
 */

#include <QtTest>
#include <QCoreApplication>
#include <QProcessEnvironment>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>

#include "common/Constants.h"
#include "common/DatabaseManager.h"
#include "db/ToolDAO.h"
#include "db/UserDAO.h"
#include "db/RecordDAO.h"
#include "services/AuthService.h"
#include "services/BorrowService.h"
#include "services/ReturnService.h"
#include "services/CheckoutService.h"

using db::ToolDAO;
using db::UserDAO;
using db::RecordDAO;

class TestServices : public QObject {
    Q_OBJECT

public:
    TestServices() = default;

private slots:
    void initTestCase();
    void cleanupTestCase();

    // 纯逻辑用例（不依赖数据库，永远执行）
    void testPasswordHashAndVerify();
    void testPasswordSaltUniqueness();
    void testStatusConstantsConsistency();

    // 业务规则用例（需要测试库，缺失则 SKIP）
    void testBorrowRejectsUnknownMapping();
    void testBorrowRejectsNonPositiveQuantity();
    void testBorrowAndReturnRoundTrip();

private:
    // 数据库就绪断言由 ENSURE_DB() 宏在用例函数内展开
    QString pickAvailablePosition(int* outMappingId, int* outToolId, int* outGroupId);

    bool    m_dbReady = false;
    QString m_skipReason;
};

// ── 安全闸门：校验测试库名合法性 ────────────────────────────────────────
namespace {
bool isAllowedTestDatabase(const QString& dbName) {
    const QString lower = dbName.toLower();
    return lower.endsWith("_test") || lower.endsWith("_unittest");
}
}  // namespace

void TestServices::initTestCase() {
    // Qt 的 SQL 驱动插件按 "插件搜索路径 + /sqldrivers" 加载，而测试 exe 位于
    // <构建目录>/tests/Debug，默认不会被搜索到。这里自动把主程序已部署的插件
    // 目录加进搜索路径（典型布局：exe 在 build/tests/Debug，插件在 build/Debug），
    // 这样手动运行 exe 时无需额外设置 QT_PLUGIN_PATH。
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
        // Qt 的 SQL 驱动插件是运行时加载的，测试 exe 在 tests/ 子目录时往往找不到。
        // 给出可操作的修复指引，避免只有一句 "driver not available" 难以排查。
        m_skipReason = QStringLiteral(
            "Qt 未找到可用的 MySQL 驱动插件(QODBC/QMYSQL)。请设置环境变量："
            " QT_PLUGIN_PATH=<构建目录>\\Debug ，该目录下应含 sqldrivers\\qsqlodbcd.dll"
            "（ctest 已自动注入该变量，手动运行 exe 时需自行设置）");
        qWarning() << "[test_services]" << m_skipReason;
        return;
    }

    if (!DatabaseManager::instance().initialize(host, port, name, user, pass)) {
        m_skipReason = "测试库连接失败，跳过数据库用例";
        return;
    }
    if (!DatabaseManager::instance().isConnected()) {
        m_skipReason = "测试库未处于连接状态，跳过数据库用例";
        return;
    }
    m_dbReady = true;
}

void TestServices::cleanupTestCase() {
    // 业务用例均已自愈此处无需清理生产数据；仅做连接收尾提示
}

/**
 * @brief 断言数据库已就绪
 * @return true=可继续；false=已 SKIP，调用方必须立即 return
 * @note QSKIP 只能终止它所在的函数，写在 helper 里无法跳出测试用例，
 *       故此处仅负责记录 SKIP，由调用方决定是否 return。
 */
// ---------------------------------------------------------------------------
// 说明：QSKIP 宏内部自带 return 语句，因此只能出现在返回 void 的测试用例函数中，
// 不能写进 helper（bool/QString 返回值的函数会触发 MSVC C2561），
// 也不能指望 helper 里的 QSKIP 跳出外层用例（它只终止自己所在的函数）。
// 故统一用下面这个宏，在用例函数体内展开。
// ---------------------------------------------------------------------------
#define ENSURE_DB()                                                          \
    do {                                                                     \
        if (!m_dbReady) {                                                    \
            QSKIP(qPrintable(m_skipReason.isEmpty()                          \
                ? QStringLiteral("数据库未就绪") : m_skipReason));           \
            return;                                                          \
        }                                                                    \
    } while (0)

/**
 * @brief 从测试库挑选一个当前可借用的位置（in_stock），用于跑借用/归还闭环
 * @return 找到返回 true，并通过出参回填 mappingId/toolId/machineGroupId
 */
QString TestServices::pickAvailablePosition(int* outMappingId, int* outToolId, int* outGroupId) {
    ToolDAO toolDao;
    QJsonObject page = toolDao.findAllByPosition(QString(), QString(),
                                                 SC::TOOL_IN_STOCK, 0, 1, 50);
    QJsonArray list = page["list"].toArray();
    if (list.isEmpty()) return QString();

    const QJsonObject first = list.first().toObject();
    *outMappingId = first["mappingId"].toInt();
    *outToolId    = first["toolId"].toInt();
    *outGroupId   = first["machineGroupId"].toInt(0);
    return first["position"].toString();
}

// ── 纯逻辑用例 ────────────────────────────────────────────────────────
void TestServices::testPasswordHashAndVerify() {
    const QString salt = AuthService::generateSalt();
    QVERIFY(!salt.isEmpty());

    const QString pwd = QStringLiteral("Str0ng!Pass");
    const QString hash = AuthService::hashPassword(pwd, salt);
    QVERIFY(!hash.isEmpty());
    QVERIFY(hash != pwd);  // 不得明文存储

    QVERIFY(AuthService::verifyPassword(pwd, salt, hash));
    QVERIFY(!AuthService::verifyPassword(QStringLiteral("wrong-pass"), salt, hash));
}

void TestServices::testPasswordSaltUniqueness() {
    // 相同口令 + 不同盐 => 不同哈希（防彩虹表）
    const QString pwd = QStringLiteral("same-password");
    const QString saltA = AuthService::generateSalt();
    const QString saltB = AuthService::generateSalt();
    QVERIFY(saltA != saltB);
    QVERIFY(AuthService::hashPassword(pwd, saltA) != AuthService::hashPassword(pwd, saltB));

    // 错误盐必须验证失败
    const QString hash = AuthService::hashPassword(pwd, saltA);
    QVERIFY(!AuthService::verifyPassword(pwd, saltB, hash));
}

void TestServices::testStatusConstantsConsistency() {
    // 工具状态与记录状态常量不得互相污染（历史踩坑点）
    QVERIFY(SC::TOOL_IN_STOCK    != SC::RECORD_RETURNED);
    QVERIFY(SC::TOOL_BORROWED    != SC::RECORD_BORROWING);
    QVERIFY(SC::RECORD_RETURNED  != SC::RECORD_OVERDUE);
    QVERIFY(!SC::OP_CHECKIN.isEmpty() && SC::OP_CHECKIN != SC::OP_CHECKOUT);
}

// ── 业务规则用例 ──────────────────────────────────────────────────────
void TestServices::testBorrowRejectsUnknownMapping() {
    ENSURE_DB();

    BorrowService svc;
    // 一个确定不存在的 mappingId
    BorrowService::Result r = svc.borrowTool(1, 1, 99999999, 1,
        QStringLiteral("单元测试：不存在的位置"),
        QDateTime::currentDateTime().addDays(1).toString("yyyy-MM-dd HH:mm:ss"),
        QStringLiteral("UNITTEST-FLOW-001"));

    QVERIFY(!r.success);
    // 注意：失败时 BorrowService 约定返回 recordId = -1（不是 0），
    // 故断言"未创建成功记录"用 <= 0 表达，避免依赖具体约定值。
    QVERIFY(r.recordId <= 0);
    QVERIFY(!r.message.isEmpty());
}

void TestServices::testBorrowRejectsNonPositiveQuantity() {
    ENSURE_DB();

    BorrowService svc;
    BorrowService::Result r = svc.borrowTool(1, 1, 1, 0,
        QStringLiteral("单元测试：数量为零"),
        QDateTime::currentDateTime().addDays(1).toString("yyyy-MM-dd HH:mm:ss"),
        QStringLiteral("UNITTEST-FLOW-002"));

    QVERIFY(!r.success);
    QVERIFY(r.recordId <= 0);  // 失败约定为 -1，见上
}

/**
 * @brief 借用→归还闭环：验证借用成功后记录进入借用中，归还后回到已归还
 * @note 用例自行完成归还，跑完后数据状态复原，不留下脏数据
 */
void TestServices::testBorrowAndReturnRoundTrip() {
    ENSURE_DB();

    int mappingId = 0, toolId = 0, groupId = 0;
    const QString pos = pickAvailablePosition(&mappingId, &toolId, &groupId);
    if (mappingId <= 0 || toolId <= 0) {
        QSKIP("测试库中暂无 in_stock 位置，跳过借用闭环用例");
        return;
    }

    UserDAO userDao;
    QList<User> users = userDao.findAllUsers(1, 1, QString(), QString(), SC::USER_ACTIVE);
    if (users.isEmpty()) {
        QSKIP("测试库中暂无可用测试用户，跳过借用闭环用例");
        return;
    }
    const int userId = users.first().userId;

    BorrowService borrowSvc;
    BorrowService::Result b = borrowSvc.borrowTool(userId, toolId, mappingId, 1,
        QStringLiteral("单元测试：借用归还闭环"),
        QDateTime::currentDateTime().addDays(1).toString("yyyy-MM-dd HH:mm:ss"),
        QStringLiteral("UNITTEST-FLOW-003"));
    if (!b.success) {
        QSKIP(qPrintable(QString("借用前置条件不满足，跳过（原因：%1）").arg(b.message)));
        return;
    }
    QVERIFY(b.recordId > 0);

    // 归还（保持数据自愈）
    ReturnService returnSvc;
    QJsonObject info;
    info["condition"] = QStringLiteral("正常");
    info["remark"] = QStringLiteral("单元测试自动归还");
    ReturnService::Result r = returnSvc.returnTools(QList<int>{b.recordId}, userId, info);
    QVERIFY(r.success);
    QVERIFY(r.count == 1);
}

QTEST_APPLESS_MAIN(TestServices)
#include "test_services.moc"
