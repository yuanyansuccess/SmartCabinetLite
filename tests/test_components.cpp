/**
 * @file test_components.cpp
 * @brief 通用组件与常量映射单元测试（Qt Test）
 * @author 袁燕
 * @说明 覆盖：PaginationBar 分页状态机、SearchBar 桥接、SC 状态显示映射。
 *   纯逻辑/UI断言，不连数据库。新人运行方式见 tests/CMakeLists.txt 注释。
 */
#include <QtTest/QtTest>
#include <QLabel>
#include <QPushButton>
#include "components/PaginationBar.h"
#include "components/SearchBar.h"
#include "common/Constants.h"

class TestComponents : public QObject {
    Q_OBJECT

private slots:
    // ── PaginationBar ──
    void pagination_initialState();
    void pagination_pageInfo();
    void pagination_firstPagePrevDisabled();
    void pagination_lastPageNextDisabled();
    void pagination_totalText();
    void pagination_totalPagesClampedToOne();

    // ── SearchBar ──
    void searchbar_bridgeAccessors();
    void searchbar_placeholderAndReadOnly();
    void searchbar_editPadding();
    void searchbar_keyboardSignal();

    // ── SC 状态映射 ──
    void statusText_allKnown();
    void statusText_unknownPassthrough();
    void statusText_legacyBorrowText();
    void statusColor_allKnown();
    void positionStatus_emptyMeansPending();
};

// ═══════════ PaginationBar ═══════════

void TestComponents::pagination_initialState() {
    PaginationBar bar;
    QCOMPARE(bar.pageText(), QStringLiteral("第 1 页"));
    QCOMPARE(bar.totalText(), QStringLiteral("共 0 条"));
}

void TestComponents::pagination_pageInfo() {
    PaginationBar bar;
    bar.setPageInfo(2, 5);
    QCOMPARE(bar.pageText(), QStringLiteral("第 2/5 页"));
    QVERIFY(bar.prevEnabled());
    QVERIFY(bar.nextEnabled());

    bool prevFired = false, nextFired = false;
    // 组件信号桥接验证（页面 connect 的就是这两个信号）
    connect(&bar, &PaginationBar::prevClicked, [&]() { prevFired = true; });
    connect(&bar, &PaginationBar::nextClicked, [&]() { nextFired = true; });
    bar.findChildren<QPushButton*>().first()->click();  // prev 是第一个创建的按钮
    QVERIFY(prevFired);
    QVERIFY(!nextFired);
}

void TestComponents::pagination_firstPagePrevDisabled() {
    PaginationBar bar;
    bar.setPageInfo(1, 5);
    QVERIFY(!bar.prevEnabled());   // 第1页上一页置灰
    QVERIFY(bar.nextEnabled());
}

void TestComponents::pagination_lastPageNextDisabled() {
    PaginationBar bar;
    bar.setPageInfo(5, 5);
    QVERIFY(bar.prevEnabled());
    QVERIFY(!bar.nextEnabled());   // 末页下一页置灰
}

void TestComponents::pagination_totalText() {
    PaginationBar bar;
    bar.setTotalRecords(108);
    QCOMPARE(bar.totalText(), QStringLiteral("共 108 条"));
    bar.setTotalRecords(0);
    QCOMPARE(bar.totalText(), QStringLiteral("共 0 条"));
}

void TestComponents::pagination_totalPagesClampedToOne() {
    PaginationBar bar;
    bar.setPageInfo(1, 0);   // 总页数0→按1处理（与原页面qMax(1,..)逻辑一致）
    QCOMPARE(bar.pageText(), QStringLiteral("第 1/1 页"));
    bar.setPageInfo(3, -2);  // 非法负值同样钳制
    QCOMPARE(bar.pageText(), QStringLiteral("第 3/1 页"));
    QVERIFY(!bar.nextEnabled());  // 3 > 1 → 下一页置灰
}

// ═══════════ SearchBar ═══════════

void TestComponents::searchbar_bridgeAccessors() {
    SearchBar bar;
    QVERIFY(bar.lineEdit() != nullptr);
    QVERIFY(bar.keyboardButton() != nullptr);
    QCOMPARE(bar.lineEdit()->text(), QString());  // 初始为空
}

void TestComponents::searchbar_placeholderAndReadOnly() {
    SearchBar bar;
    bar.setPlaceholderText(QStringLiteral("搜索工具名称..."));
    QCOMPARE(bar.lineEdit()->placeholderText(), QStringLiteral("搜索工具名称..."));
    QVERIFY(!bar.lineEdit()->isReadOnly());
    bar.setReadOnly(true);
    QVERIFY(bar.lineEdit()->isReadOnly());  // UserManagementPage 场景
}

void TestComponents::searchbar_editPadding() {
    SearchBar bar;
    bar.setEditPadding(14);  // AlertLogsPage 历史值
    // 内边距14px：样式串应包含 padding:0 14px
    QVERIFY(bar.lineEdit()->styleSheet().contains("padding:0 14px"));
}

void TestComponents::searchbar_keyboardSignal() {
    SearchBar bar;
    bool fired = false;
    connect(&bar, &SearchBar::keyboardRequested, [&]() { fired = true; });
    bar.keyboardButton()->click();
    QVERIFY(fired);
}

// ═══════════ SC 状态映射 ═══════════

void TestComponents::statusText_allKnown() {
    QCOMPARE(SC::toolStatusText(SC::TOOL_IN_STOCK),    QStringLiteral("在库"));
    QCOMPARE(SC::toolStatusText(SC::TOOL_BORROWED),    QStringLiteral("已借出"));
    QCOMPARE(SC::toolStatusText(SC::TOOL_CHECKED_OUT), QStringLiteral("已出库"));
    QCOMPARE(SC::toolStatusText(SC::TOOL_MAINTENANCE), QStringLiteral("维护中"));
    QCOMPARE(SC::toolStatusText(SC::TOOL_PENDING),     QStringLiteral("待入库"));
}

void TestComponents::statusText_unknownPassthrough() {
    QCOMPARE(SC::toolStatusText(QStringLiteral("weird_state")), QStringLiteral("weird_state"));
}

void TestComponents::statusText_legacyBorrowText() {
    // 工具管理页历史文案"已借用"（borrowed不一致问题待袁总定夺，现状保留）
    QCOMPARE(SC::toolStatusText(SC::TOOL_BORROWED, true), QStringLiteral("已借用"));
    QCOMPARE(SC::toolStatusText(SC::TOOL_BORROWED, false), QStringLiteral("已借出"));
}

void TestComponents::statusColor_allKnown() {
    QCOMPARE(SC::toolStatusColor(SC::TOOL_IN_STOCK),    QStringLiteral("#43a047"));
    QCOMPARE(SC::toolStatusColor(SC::TOOL_BORROWED),    QStringLiteral("#f57c00"));
    QCOMPARE(SC::toolStatusColor(SC::TOOL_CHECKED_OUT), QStringLiteral("#e53935"));
    QCOMPARE(SC::toolStatusColor(SC::TOOL_PENDING),     QStringLiteral("#1890ff"));
    QCOMPARE(SC::toolStatusColor(SC::TOOL_MAINTENANCE), QStringLiteral("#999999"));
    QCOMPARE(SC::toolStatusColor(QStringLiteral("weird")), QStringLiteral("#999999"));
}

void TestComponents::positionStatus_emptyMeansPending() {
    QCOMPARE(SC::positionStatusText(QString()),                 QStringLiteral("待入库"));
    QCOMPARE(SC::positionStatusText(SC::TOOL_PENDING),          QStringLiteral("待入库"));
    QCOMPARE(SC::positionStatusText(SC::TOOL_IN_STOCK),         QStringLiteral("在库"));
    QCOMPARE(SC::positionStatusText(SC::TOOL_CHECKED_OUT),      QStringLiteral("已出库"));
}

QTEST_MAIN(TestComponents)
#include "test_components.moc"
