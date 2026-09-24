/**
 * @file MainWindow.cpp
 * @brief 主窗口实现 - TopBar(68px) + 左侧240px深蓝侧边栏 + 页面栈
 * @author 袁燕
 * @修改说明 2026-06-21 融合版：集成TopBar组件，版本B页面为核心
 */
#include "MainWindow.h"
#include "components/TopBar.h"
#include "pages/LoginPage.h"
#include "pages/DashboardPage.h"
#include "pages/UserManagementPage.h"
#include "pages/ToolManagementPage.h"
#include "pages/ToolBorrowPage.h"
#include "pages/ToolReturnPage.h"
#include "pages/ToolCheckinPage.h"
#include "pages/ToolCheckoutPage.h"
#include "pages/LedgerStatsPage.h"
#include "pages/AlertLogsPage.h"
#include "pages/SystemSettingsPage.h"
#include "pages/SystemMaintenancePage.h"  // [V2.03g] 系统维护页面
#include "pages/CabinetSessionDialog.h"   // [V2.06] 普通用户智能柜会话（开柜提示+关柜清单）
#include "pages/UserEntryDialog.h"        // [V2.07] 普通用户功能选择页（借用/归还、查询）
#include "utils/StyleHelper.h"
#include "common/AppConfig.h"          // [2026-06-27] 读取锁屏时间/备份配置
#include "common/DatabaseManager.h"    // [2026-06-27] 备份检查
#include <QScrollArea>
#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QApplication>  // [2026-06-23] qApp->quit()退出整个系统
#include <QGraphicsOpacityEffect>  // [2026-06-24] 页面切换淡入淡出动画
#include <QPropertyAnimation>
#include <QTimer>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFile>
#include <QDir>
#include <QProcess>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
    , m_topBar(nullptr), m_stack(nullptr), m_sidebar(nullptr), m_contentArea(nullptr)
    , m_loginPage(nullptr), m_dashboardPage(nullptr), m_userMgmtPage(nullptr)
    , m_toolMgmtPage(nullptr), m_borrowPage(nullptr), m_returnPage(nullptr)
    , m_checkinPage(nullptr), m_checkoutPage(nullptr), m_ledgerPage(nullptr)
    , m_alertsPage(nullptr), m_settingsPage(nullptr)
{
    setWindowTitle(QStringLiteral("智能工具柜管理系统"));
    resize(1280, 800);
    setMinimumSize(1024, 680);
    setupUI();
    m_stack->setCurrentIndex(0); // 登录页为首页
    updateSidebarVisibility();
}

MainWindow::~MainWindow() {}

void MainWindow::setupUI() {
    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    // 垂直布局：TopBar + 内容区(sidebar + stack)
    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ── TopBar (68px固定高度) ──
    setupTopBar();
    m_topBar->setUserAreaVisible(false);  // [2026-06-21] 初始登录页隐藏用户信息
    mainLayout->addWidget(m_topBar);

    // ── 内容区 (侧边栏 + 页面栈) ──
    m_contentArea = new QWidget();
    QHBoxLayout* contentLayout = new QHBoxLayout(m_contentArea);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    // 左侧边栏 [2026-09-23] 8寸屏适配：宽度240→264px配合菜单项加大
    m_sidebar = createSidebar();
    m_sidebar->setFixedWidth(264);
    contentLayout->addWidget(m_sidebar);

    // 右侧页面栈
    m_stack = new QStackedWidget();
    m_stack->setStyleSheet("background:" + StyleHelper::bgColor() + ";");

    m_loginPage     = new LoginPage();
    m_dashboardPage = new DashboardPage();
    m_userMgmtPage  = new UserManagementPage();
    m_toolMgmtPage  = new ToolManagementPage();
    m_borrowPage    = new ToolBorrowPage();
    m_returnPage    = new ToolReturnPage();
    m_checkinPage   = new ToolCheckinPage();
    m_checkoutPage  = new ToolCheckoutPage();
    m_ledgerPage    = new LedgerStatsPage();
    m_alertsPage    = new AlertLogsPage();
    m_settingsPage  = new SystemSettingsPage();
    m_maintenancePage = new SystemMaintenancePage();  // [V2.03g] 系统维护

    // [2026-06-23] 移除人脸录入独立页面，人脸录入功能已集成到人员管理页
    // [V2.03g] 索引：0:登录 1:仪表盘 2:用户管理 3:工具管理 4:借用 5:归还 6:入库 7:出库 8:台账 9:告警 10:系统维护 11:设置
    m_stack->addWidget(m_loginPage);
    m_stack->addWidget(m_dashboardPage);
    m_stack->addWidget(m_userMgmtPage);
    m_stack->addWidget(m_toolMgmtPage);
    m_stack->addWidget(m_borrowPage);
    m_stack->addWidget(m_returnPage);
    m_stack->addWidget(m_checkinPage);
    m_stack->addWidget(m_checkoutPage);
    m_stack->addWidget(m_ledgerPage);
    m_stack->addWidget(m_alertsPage);
    m_stack->addWidget(m_maintenancePage);  // [V2.03g] 系统维护(索引10)
    m_stack->addWidget(m_settingsPage);     // 系统设置(索引11)

    // [2026-09-24] 普通用户流程遮罩页(索引12)：登录后入口页/借用归还会话期间主界面停在本页，
    // 避免两个全屏弹窗切换间隙露出背后的登录页（闪现按键页面问题）
    m_userFlowCover = new QWidget();
    m_userFlowCover->setStyleSheet("background:" + StyleHelper::bgColor() + ";");
    m_stack->addWidget(m_userFlowCover);

    contentLayout->addWidget(m_stack, 1);
    mainLayout->addWidget(m_contentArea, 1);

    // 信号连接
    connect(m_loginPage, &LoginPage::loginSuccess, this, &MainWindow::onLoginSuccess);
    connect(m_dashboardPage, &DashboardPage::navigateRequested, this, &MainWindow::navigateToPage);
    // [2026-06-27] 工具借用页面点击归还按钮 → 跳转工具归还页面并自动选中对应工具
    connect(m_borrowPage, &ToolBorrowPage::returnRequested, this, [this](int recordId) {
        m_returnPage->setPendingReturnRecordId(recordId);
        navigateToPage("return");
    });
    // [2026-06-23] TopBar退出按钮 → 退出整个系统（非注销）
    connect(m_topBar, &TopBar::exitSystemClicked, qApp, &QApplication::quit);
    // 侧边栏"退出登录"按钮 → 注销回登录页（保留）
    connect(m_topBar, &TopBar::logoutClicked, this, &MainWindow::onLogout);

    // [2026-06-27] 自动锁屏定时器：读取AppConfig的lockTime，超时自动退出登录
    int lockMinutes = AppConfig::instance().borrowLockTime();
    m_idleTimer = new QTimer(this);
    m_idleTimer->setInterval(lockMinutes * 60 * 1000);  // 分钟转毫秒
    m_idleTimer->setSingleShot(true);  // 单次触发，超时后需用户活动才重新计时
    m_lastActivity = QDateTime::currentDateTime();
    connect(m_idleTimer, &QTimer::timeout, this, &MainWindow::onAutoLockTimeout);
    m_idleTimer->start();

    // [2026-06-27] 安装全局事件过滤器：监听鼠标/键盘活动，重置空闲计时器
    qApp->installEventFilter(this);

    // [2026-06-27] 数据库备份检查定时器：每小时检查一次是否到了备份时间
    m_backupCheckTimer = new QTimer(this);
    m_backupCheckTimer->setInterval(60 * 60 * 1000);  // 1小时检查一次
    connect(m_backupCheckTimer, &QTimer::timeout, this, &MainWindow::onBackupCheckTimeout);
    m_backupCheckTimer->start();
}

void MainWindow::setupTopBar() {
    m_topBar = new TopBar();
    m_topBar->setFixedHeight(68);
}

QWidget* MainWindow::createSidebar() {
    QWidget* sb = new QWidget();
    sb->setStyleSheet("background:#1a1a2e;");
    QVBoxLayout* layout = new QVBoxLayout(sb);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 标题区 [2026-09-23] 8寸屏适配：标题22→24px，高度72→76px
    QLabel* title = new QLabel(QStringLiteral("  智能工具柜"));
    title->setStyleSheet("color:white; font-size:24px; font-weight:bold; padding:20px 12px; background:#151528;");
    title->setFixedHeight(76);
    layout->addWidget(title);

    // 分隔线
    QFrame* line = new QFrame(); line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("border:1px solid #2a2a4e;");
    layout->addWidget(line);

    // 导航区(可滚动)
    QScrollArea* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("background:transparent; border:none;");
    QWidget* navWidget = new QWidget();
    navWidget->setStyleSheet("background:transparent;");
    m_sidebarNav = new QVBoxLayout(navWidget);
    m_sidebarNav->setContentsMargins(8, 8, 8, 8);
    m_sidebarNav->setSpacing(6);  // [2026-09-23] 4→6，菜单项间距加大防误触

    struct NavItem { QString id; QString label; };
    QList<NavItem> items = {
        {"dashboard",  QStringLiteral("📊 系统概览")},
        {"users",      QStringLiteral("👥 人员管理")},
        {"tools",      QStringLiteral("🔧 工具管理")},
        // [2026-09-23] 管理员借用/归还入口合并为一项，点击进入模拟数据智能柜会话
        {"borrowreturn", QStringLiteral("📤 工具借用/归还")},
        {"checkin",    QStringLiteral("📦 工具入库")},
        {"checkout",   QStringLiteral("📋 工具出库")},
        {"ledger",     QStringLiteral("📈 台账统计")},
        {"alerts",     QStringLiteral("🔔 告警日志")},
        {"maintenance",QStringLiteral("🛠️ 系统维护")},  // [V2.03g] 系统维护
        {"settings",   QStringLiteral("⚙️ 系统设置")},
    };

    for (const auto& item : items) {
        QPushButton* btn = new QPushButton("  " + item.label);
        btn->setObjectName(item.id);
        btn->setCursor(Qt::PointingHandCursor);
        // [2026-09-23] 8寸屏适配：菜单项高度52→60px，字体16→18px，触控目标加大防误触
        btn->setMinimumHeight(60);
        btn->setStyleSheet(
            "QPushButton { color:#c0c0d0; font-size:18px; text-align:left; padding-left:18px; "
            "border:none; border-radius:10px; background:transparent; }"
            "QPushButton:hover { background:#2a2a5e; color:white; }"
            "QPushButton[active=\"true\"] { background:#4da3ff; color:white; font-weight:bold; }"
        );
        connect(btn, &QPushButton::clicked, this, [this, id = item.id] {
            // [2026-09-23] 借用/归并入口不进页面栈，直接启动模拟会话
            if (id == "borrowreturn") { openBorrowReturnSession(); return; }
            navigateToPage(id);
        });
        m_sidebarNav->addWidget(btn);
        m_navButtons.append(btn);
    }
    m_sidebarNav->addStretch();
    scroll->setWidget(navWidget);
    layout->addWidget(scroll, 1);

    return sb;
}

void MainWindow::showPage(const QString& name) {
    // [V2.07 权限拦截] 普通用户仅允许访问 系统概览/工具机组查询，
    // 防止其他入口（如告警跳转）绕过侧边栏限制
    bool isAdmin = (m_user["role"].toString() == "admin");
    if (!m_user.isEmpty() && !isAdmin && name != "dashboard" && name != "tools") {
        qInfo() << "[MainWindow] 普通用户无权访问页面:" << name << "，重定向到 dashboard";
        showPage("dashboard");
        return;
    }
    // [2026-06-24v4] 覆盖层遮罩切换：overlay盖住页面→切换→overlay淡出→透出新页
    static QMap<QString, int> map = {
        {"login", 0}, {"dashboard", 1}, {"users", 2}, {"tools", 3},
        {"borrow", 4}, {"return", 5}, {"checkin", 6}, {"checkout", 7},
        {"ledger", 8}, {"alerts", 9}, {"maintenance", 10}, {"settings", 11}
    };
    int idx = map.value(name, 1);
    // 去掉"同页面不刷新"限制，菜单点击时即使同页面也强制refresh
    // 反馈：系统维护页面切换时不刷新，根因是索引相同直接return
    // 举一反三：所有页面都需要在菜单切换时刷新数据（可能后台数据已变化）
    bool isSamePage = (idx == m_stack->currentIndex());

    // [V2.03t] 同页面只做refresh，不重复切换动画
    if (isSamePage) {
        // 同页面：直接refresh，不需要遮罩动画
        if (name == "dashboard" && m_dashboardPage)      m_dashboardPage->refresh();
        else if (name == "users" && m_userMgmtPage)      m_userMgmtPage->refresh();
        else if (name == "tools" && m_toolMgmtPage)      m_toolMgmtPage->refresh();
        else if (name == "borrow" && m_borrowPage)        m_borrowPage->refresh();
        else if (name == "return" && m_returnPage)        m_returnPage->refresh();
        else if (name == "checkin" && m_checkinPage)      m_checkinPage->refresh();
        else if (name == "checkout" && m_checkoutPage)    m_checkoutPage->refresh();
        else if (name == "ledger" && m_ledgerPage)        m_ledgerPage->refresh();
        else if (name == "alerts" && m_alertsPage)        m_alertsPage->refresh();
        else if (name == "maintenance" && m_maintenancePage) m_maintenancePage->refresh();
        else if (name == "settings" && m_settingsPage)    m_settingsPage->refresh();
        else qWarning() << "[MainWindow] Unknown page name for refresh:" << name;
        return;
    }

    // 1. 创建白色遮罩覆盖在QStackedWidget上方
    auto* overlay = new QWidget(m_stack->parentWidget());
    overlay->setGeometry(m_stack->geometry());
    overlay->setStyleSheet("background:#f0f2f5;");
    overlay->raise();
    overlay->show();

    // 2. 切换页面（被遮罩挡住，用户看不到跳变）
    m_stack->setCurrentIndex(idx);
    updateSidebarActive(name);

    // 3. 遮罩淡出→透出新页面 [2026-06-27] 350ms→150ms，提升菜单切换响应速度
    auto* overlayEffect = new QGraphicsOpacityEffect(overlay);
    overlayEffect->setOpacity(1.0);
    overlay->setGraphicsEffect(overlayEffect);
    auto* fadeOut = new QPropertyAnimation(overlayEffect, "opacity");
    fadeOut->setDuration(150);
    fadeOut->setStartValue(1.0);
    fadeOut->setEndValue(0.0);
    fadeOut->setEasingCurve(QEasingCurve::OutCubic);
    connect(fadeOut, &QPropertyAnimation::finished, this, [overlay]() {
        overlay->deleteLater();
    });
    fadeOut->start(QAbstractAnimation::DeleteWhenStopped);

    // 更新TopBar页面标题
    static QMap<QString, QString> titles = {
        {"dashboard", QStringLiteral("系统概览")},
        {"users", QStringLiteral("人员管理")},
        {"tools", QStringLiteral("工具管理")},
        {"borrow", QStringLiteral("工具借用")},
        {"return", QStringLiteral("工具归还")},
        {"checkin", QStringLiteral("工具入库")},
        {"checkout", QStringLiteral("工具出库")},
        {"ledger", QStringLiteral("台账统计")},
        {"alerts", QStringLiteral("告警日志")},
        {"maintenance", QStringLiteral("系统维护")},  // [V2.03t] 补充缺失的系统维护标题
        {"settings", QStringLiteral("系统设置")},
    };
    if (name == "login") {
        m_topBar->setPageTitle("");
        m_topBar->setUserAreaVisible(false);  // [2026-06-21] 登录页隐藏时间/用户名/退出
    } else {
        m_topBar->setPageTitle(titles.value(name, ""));
        m_topBar->setUserAreaVisible(true);
    }

    // 菜单切换性能优化：refresh异步执行
    // 原问题：refresh同步执行DB查询阻塞UI线程，导致150ms淡出动画卡顿
    // 优化：用QTimer::singleShot(0)将refresh延迟到下一轮事件循环
    // 让setCurrentIndex+遮罩动画先渲染，再执行DB查询
    // 效果：用户立即看到页面切换动画，DB查询在后台进行不卡顿
    QMetaObject::invokeMethod(this, [this, name]() {
        if (name == "dashboard" && m_dashboardPage)      m_dashboardPage->refresh();
        else if (name == "users" && m_userMgmtPage)      m_userMgmtPage->refresh();
        else if (name == "tools" && m_toolMgmtPage)      m_toolMgmtPage->refresh();
        else if (name == "borrow" && m_borrowPage)        m_borrowPage->refresh();
        else if (name == "return" && m_returnPage)        m_returnPage->refresh();
        else if (name == "checkin" && m_checkinPage)      m_checkinPage->refresh();
        else if (name == "checkout" && m_checkoutPage)    m_checkoutPage->refresh();
        else if (name == "ledger" && m_ledgerPage)        m_ledgerPage->refresh();
        else if (name == "alerts" && m_alertsPage)        m_alertsPage->refresh();
        else if (name == "maintenance" && m_maintenancePage) m_maintenancePage->refresh();
        else if (name == "settings" && m_settingsPage)    m_settingsPage->refresh();
        else qWarning() << "[MainWindow] Unknown page name for async refresh:" << name;
    }, Qt::QueuedConnection);
}

void MainWindow::updateSidebarActive(const QString& name) {
    for (auto* btn : m_navButtons) {
        // [2026-09-24] 防御：跳过空指针（防异常构建状态下脏指针导致崩溃）
        if (!btn) continue;
        bool active = (btn->objectName() == name);
        btn->setProperty("active", active ? "true" : "false");
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
    }
}

void MainWindow::updateSidebarVisibility() {
    bool loggedIn = !m_user.isEmpty();
    m_sidebar->setVisible(loggedIn);
    for (auto* btn : m_navButtons) {
        QString id = btn->objectName();
        bool isAdmin = m_user["role"].toString() == "admin";
        bool show;
        if (isAdmin) {
            // [2026-09-23] 借用/归还合并为borrowreturn入口（模拟会话）
            show = (id == "dashboard" || id == "tools" || id == "borrowreturn" || id == "alerts")
                || (id == "users" || id == "checkin" || id == "checkout" || id == "ledger"
                     || id == "maintenance" || id == "settings");  // [V2.03g] 管理员可见系统维护
        } else {
            // [V2.07 权限] 普通用户仅开放 系统概览/工具机组查询
            show = (id == "dashboard" || id == "tools");
        }
        btn->setVisible(show);
    }
}

void MainWindow::setCurrentUser(const QJsonObject& user) {
    m_user = user;
    // [2026-06-23] 用户信息仅在TopBar显示，侧边栏不再重复展示
    m_topBar->setUserName(user["realName"].toString());
    m_topBar->setDepartment(user["department"].toString());

    updateSidebarVisibility();
}

void MainWindow::onLoginSuccess(const QJsonObject& user) {
    // [2026-06-23] 登录成功后确保摄像头已关闭（兜底保险）
    m_loginPage->stopFaceRecognitionPublic();
    setCurrentUser(user);
    m_dashboardPage->setUser(user);
    m_borrowPage->setUser(user);
    m_returnPage->setUser(user);
    // [2026-06-27] 出库页面设置当前用户（出库记录写入正确操作人）
    m_checkoutPage->setUser(user);
    // [V2.01 2026-06-27] 入库页面设置当前用户（入库记录写入正确操作人）
    m_checkinPage->setUser(user);
    // [2026-06-27] 告警日志页面设置当前用户（忽略按钮权限控制）
    m_alertsPage->setUser(user);
    // [2026-06-27] 登录成功后刷新TopBar版本号显示（从INI读取最新版本）
    m_topBar->refreshVersionLabel();

    // [V2.07] 普通用户登录成功后进入功能选择页（普通用户首页）：
    // [2026-09-23] 借用/归还流程结束后回到功能选择页（首页），退出登录才回登录页
    // "借用/归还"→进入智能柜会话，会话结束→循环回本页
    // "查询/告警日志"→弹出明细对话框（不关闭本页）
    // "退出登录"→回登录页
    bool isAdmin = (user["role"].toString() == "admin");
    if (!isAdmin) {
        // [2026-09-24] 整个普通用户流程期间：主界面停在中性遮罩页+隐藏侧边栏，
        // 防止入口页与借用归还会话两个全屏弹窗切换间隙闪现登录页
        if (m_userFlowCover) m_stack->setCurrentIndex(m_stack->indexOf(m_userFlowCover));
        m_sidebar->setVisible(false);
        while (true) {
            UserEntryDialog entry(user, this);
            UserEntryDialog::Choice choice = entry.execChoice();
            if (choice == UserEntryDialog::Choice::Logout) break;   // 退出登录→登录页
            if (choice == UserEntryDialog::Choice::BorrowReturn) {
                CabinetSessionDialog session(user, this);
                session.startSession();                             // 流程结束→循环回功能选择页
                continue;
            }
            // Query兜底（查询按钮已改为弹出对话框不再accept，理论不可达）
            updateSidebarVisibility();  // 恢复侧边栏后再进首页
            showPage("dashboard");
            emit userLoggedIn(user);
            return;
        }
        onLogout();  // 退出登录 → 登录页
        return;
    }

    showPage("dashboard");
    emit userLoggedIn(user);
}

void MainWindow::onLogout() {
    m_user = QJsonObject();
    // [2026-06-23] 侧边栏已移除用户区域，仅重置TopBar
    m_topBar->setUserName(QStringLiteral("未登录"));
    m_topBar->setDepartment("");
    updateSidebarVisibility();
    // [V6.3致命修复] 退出登录必须重置LoginPage所有状态
    // 否则残留"身份验证通过"、张三识别信息、m_autoJumpTimer未停等问题
    m_loginPage->resetPageState();
    // [2026-09-24fix] 注销必须可靠回到登录刷脸页：直接切索引，
    // 绕过showPage的遮罩/动画/异步刷新链路（此前出现过未切换停留原页的情况）
    m_stack->setCurrentIndex(0);
    m_topBar->setPageTitle(QStringLiteral(""));
    m_topBar->setUserAreaVisible(false);
    updateSidebarActive(QStringLiteral("login"));
    // [2026-09-24fix] 注销后抑制自动刷脸登录：人未离开摄像头画面时不立即自动回登，
    // 离开画面后恢复（onFaceLost清除），保证注销后稳定停在刷脸页面
    m_loginPage->suppressAutoLoginAfterLogout();
    emit userLoggedOut();
}

// 快捷操作/侧边栏的"工具借用/归还"入口统一在转交处拦截：
// borrowreturn 不属于页面栈，直接启动智能柜借用/归还会话，避免落入未知页面兜底到系统概览
void MainWindow::navigateToPage(const QString& name) {
    if (name == QStringLiteral("borrowreturn")) {
        openBorrowReturnSession();
        return;
    }
    showPage(name);
}

// [2026-09-23] 管理员"工具借用/归还"入口：与普通用户相同的智能柜会话流程
// CabinetSessionDialog为全模拟数据演示（开柜提示→演示面板模拟拿取/放回→关柜差异清单）
// [2026-09-23fix] 功能选择页(普通用户首页)仅普通用户可见，管理员流程结束回系统概览
void MainWindow::openBorrowReturnSession() {
    if (m_user.isEmpty()) return;
    CabinetSessionDialog session(m_user, this);
    session.startSession();
    showPage("dashboard");  // 管理员流程结束 → 回系统概览（管理页面）
}

// [2026-06-27] 自动锁屏超时：退出登录回到登录页
void MainWindow::onAutoLockTimeout() {
    // 仅在已登录状态下触发锁屏
    if (m_user.isEmpty()) return;

    qInfo() << "[AutoLock] Idle timeout, auto logout";
    onLogout();
}

// [2026-06-27] 定时检查是否到了数据库备份时间
// 每小时检查一次，根据备份周期（每日/每周一/每周日）判断是否需要备份
// 同一天只备份一次（通过 m_lastBackupDate 去重）
void MainWindow::onBackupCheckTimeout() {
    auto& cfg = AppConfig::instance();
    if (!cfg.backupAutoEnabled()) return;  // 自动备份未启用

    QDate today = QDate::currentDate();
    // 同一天不重复备份
    if (m_lastBackupDate == today) return;

    int period = cfg.backupPeriod();
    int dayOfWeek = today.dayOfWeek();  // 1=周一, 7=周日
    bool shouldBackup = false;
    if (period == 0) {
        shouldBackup = true;  // 每日
    } else if (period == 1) {
        shouldBackup = (dayOfWeek == 1);  // 每周一
    } else if (period == 2) {
        shouldBackup = (dayOfWeek == 7);  // 每周日
    }

    if (!shouldBackup) return;

    // 到了备份时间，执行数据库备份
    // 备份逻辑复用 SystemSettingsPage::performDatabaseBackup 的核心代码
    QString backupPath = cfg.backupPath();
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");

    // 确保备份目录存在
    QDir dir;
    if (!dir.exists(backupPath)) {
        if (!dir.mkpath(backupPath)) {
            qWarning() << "[BackupCheck] Cannot create backup dir:" << backupPath;
            return;
        }
    }

#ifdef Q_OS_WIN
    QString dbPath = QStringLiteral("d:/CFDZ/smartCabinet/trunk/QtSmartCabinet/build/smartcabinet.db");
#else
    QString dbPath = QStringLiteral("/var/lib/smartcabinet/smartcabinet.db");
#endif

    if (QFile::exists(dbPath)) {
        // SQLite模式：拷贝数据库文件
        QString backupFile = backupPath + QDir::separator() +
                             QStringLiteral("smartcabinet_backup_%1.db").arg(timestamp);
        if (QFile::copy(dbPath, backupFile)) {
            m_lastBackupDate = today;
            qInfo() << "[BackupCheck] Auto backup success:" << backupFile;

            // 清理超过7天的旧备份
            QDir backupDir(backupPath);
            QStringList filters;
            filters << "smartcabinet_backup_*.db";
            QFileInfoList oldFiles = backupDir.entryInfoList(filters, QDir::Files, QDir::Time);
            for (const QFileInfo& fi : oldFiles) {
                if (fi.lastModified().daysTo(QDateTime::currentDateTime()) > 7) {
                    QFile::remove(fi.absoluteFilePath());
                }
            }
        } else {
            qWarning() << "[BackupCheck] Auto backup failed:" << backupFile;
        }
    } else {
        // MySQL模式：用mysqldump导出
        QString backupFile = backupPath + QDir::separator() +
                             QStringLiteral("smartcabinet_backup_%1.sql").arg(timestamp);
        auto* proc = new QProcess(this);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, today, backupFile](int exitCode, QProcess::ExitStatus) {
            auto* p = qobject_cast<QProcess*>(sender());
            if (!p) return;
            p->deleteLater();
            if (exitCode == 0) {
                m_lastBackupDate = today;
                qInfo() << "[BackupCheck] MySQL auto backup success:" << backupFile;
            } else {
                qWarning() << "[BackupCheck] MySQL auto backup failed, exitCode:" << exitCode;
            }
        });
        proc->setStandardOutputFile(backupFile);
        proc->start("mysqldump", QStringList()
                    << QStringLiteral("-h%1").arg(cfg.dbHost())
                    << QStringLiteral("-u%1").arg(cfg.dbUser())
                    << QStringLiteral("-p%1").arg(cfg.dbPass())
                    << cfg.dbName());
        if (!proc->waitForStarted(3000)) {
            qWarning() << "[BackupCheck] Cannot start mysqldump";
            proc->deleteLater();
        }
    }
}

// [2026-06-27] 重置空闲计时器（用户有操作时调用）
void MainWindow::resetIdleTimer() {
    m_lastActivity = QDateTime::currentDateTime();
    if (m_idleTimer && m_idleTimer->isActive()) {
        m_idleTimer->start();  // 重新计时
    }
}

// [2026-06-27] 全局事件过滤器：监听鼠标点击/移动、键盘按键，重置空闲计时器
bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::MouseMove ||
        event->type() == QEvent::KeyPress ||
        event->type() == QEvent::Wheel) {
        resetIdleTimer();
    }
    return QMainWindow::eventFilter(watched, event);
}
