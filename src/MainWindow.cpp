/**
 * @file MainWindow.cpp
 * @brief 主窗口实现 - TopBar(68px) + 左侧240px深蓝侧边栏 + 页面栈
 * @author 袁燕
 */
#include "MainWindow.h"
#include "components/TopBar.h"
#include "pages/LoginPage.h"
#include "pages/UserManagementPage.h"
#include "pages/ToolManagementPage.h"
#include "pages/ToolBorrowPage.h"
#include "pages/ToolReturnPage.h"
#include "pages/ToolCheckinPage.h"
#include "pages/ToolCheckoutPage.h"
#include "pages/LedgerStatsPage.h"
#include "pages/AlertLogsPage.h"
#include "pages/SystemSettingsPage.h"
#include "pages/SystemMaintenancePage.h"  // 系统维护页面
#include "pages/CabinetSessionDialog.h"  // 普通用户智能柜会话（开柜提示+关柜清单）
#include "pages/UserEntryDialog.h"  // 普通用户功能选择页（借用/归还、查询）
#include "pages/ForceChangePasswordDialog.h"  // 首次登录强制修改初始密码
#include "pages/RoleSelectDialog.h"  // 管理用户登录身份选择
#include "utils/StyleHelper.h"
#include "common/AppConfig.h"  // 读取锁屏时间/备份配置
#include "common/DatabaseManager.h"  // 备份检查
#include <QScrollArea>
#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QApplication>  // qApp->quit()退出整个系统
#include <QGraphicsOpacityEffect>  // 页面切换淡入淡出动画
#include <QPropertyAnimation>
#include <QTimer>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFile>
#include <QDir>
#include <QProcess>
#include "common/Constants.h"

// ── 页面注册表（唯一真相源：页面名/栈索引/标题） ──
// 新人新增页面只需：1)本表加一行 2)setupUI()里addWidget 3)refreshPage()加一分支。
// 顺序必须与 setupUI() 中 m_stack->addWidget 的添加顺序一致（索引即栈位置）。
namespace {
struct PageDef { QString name; int index; QString title; };
static const QVector<PageDef>& pageRegistry() {
    static const QVector<PageDef> reg = {
        {"login",       0,  QString()},
        {"users",       1,  QStringLiteral("人员管理")},
        {"tools",       2,  QStringLiteral("工具管理")},
        {"borrow",      3,  QStringLiteral("工具借用")},
        {"return",      4,  QStringLiteral("工具归还")},
        {"checkin",     5,  QStringLiteral("工具入库")},
        {"checkout",    6,  QStringLiteral("工具出库")},
        {"ledger",      7,  QStringLiteral("台账统计")},
        {"alerts",      8,  QStringLiteral("告警日志")},
        {SC::TOOL_MAINTENANCE, 9, QStringLiteral("系统维护")},
        {"settings",    10, QStringLiteral("系统设置")},
    };
    return reg;
}
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
    , m_topBar(nullptr), m_stack(nullptr), m_sidebar(nullptr), m_contentArea(nullptr)
    , m_loginPage(nullptr), m_userMgmtPage(nullptr)
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

/**
 * @brief 构建主窗口：侧边栏、页面堆栈与顶栏
 */
void MainWindow::setupUI() {
    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    // 垂直布局：TopBar + 内容区(sidebar + stack)
    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ── TopBar (68px固定高度) ──
    setupTopBar();
    m_topBar->setUserAreaVisible(false);  // 初始登录页隐藏用户信息
    mainLayout->addWidget(m_topBar);

    // ── 内容区 (侧边栏 + 页面栈) ──
    m_contentArea = new QWidget();
    QHBoxLayout* contentLayout = new QHBoxLayout(m_contentArea);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    // 左侧边栏 8寸屏适配：宽度264px配合菜单项加大
    m_sidebar = createSidebar();
    m_sidebar->setFixedWidth(264);
    contentLayout->addWidget(m_sidebar);

    // 右侧页面栈
    m_stack = new QStackedWidget();
    m_stack->setStyleSheet("background:" + StyleHelper::bgColor() + ";");

    m_loginPage     = new LoginPage();
    m_userMgmtPage  = new UserManagementPage();
    m_toolMgmtPage  = new ToolManagementPage();
    m_borrowPage    = new ToolBorrowPage();
    m_returnPage    = new ToolReturnPage();
    m_checkinPage   = new ToolCheckinPage();
    m_checkoutPage  = new ToolCheckoutPage();
    m_ledgerPage    = new LedgerStatsPage();
    m_alertsPage    = new AlertLogsPage();
    m_settingsPage  = new SystemSettingsPage();
    m_maintenancePage = new SystemMaintenancePage();  // 系统维护

    // 移除人脸录入独立页面，人脸录入功能已集成到人员管理页
    // 索引：0:登录 1:用户管理 2:工具管理 3:借用 4:归还 5:入库 6:出库 7:台账 8:告警 9:系统维护 10:设置
    m_stack->addWidget(m_loginPage);
    m_stack->addWidget(m_userMgmtPage);
    m_stack->addWidget(m_toolMgmtPage);
    m_stack->addWidget(m_borrowPage);
    m_stack->addWidget(m_returnPage);
    m_stack->addWidget(m_checkinPage);
    m_stack->addWidget(m_checkoutPage);
    m_stack->addWidget(m_ledgerPage);
    m_stack->addWidget(m_alertsPage);
    m_stack->addWidget(m_maintenancePage);  // 系统维护(索引9)
    m_stack->addWidget(m_settingsPage);     // 系统设置(索引10)

    // 普通用户流程遮罩页(索引11)：登录后入口页/借用归还会话期间主界面停在本页，
    // 避免两个全屏弹窗切换间隙露出背后的登录页（闪现按键页面问题）
    m_userFlowCover = new QWidget();
    m_userFlowCover->setStyleSheet("background:" + StyleHelper::bgColor() + ";");
    m_stack->addWidget(m_userFlowCover);

    contentLayout->addWidget(m_stack, 1);
    mainLayout->addWidget(m_contentArea, 1);

    // 信号连接
    connect(m_loginPage, &LoginPage::loginSuccess, this, &MainWindow::onLoginSuccess);
    // 工具借用页面点击归还按钮 → 跳转工具归还页面并自动选中对应工具
    connect(m_borrowPage, &ToolBorrowPage::returnRequested, this, [this](int recordId) {
        m_returnPage->setPendingReturnRecordId(recordId);
        navigateToPage("return");
    });
    // TopBar退出按钮 → 退出整个系统（非注销）
    connect(m_topBar, &TopBar::exitSystemClicked, qApp, &QApplication::quit);
    // 侧边栏"退出登录"按钮 → 注销回登录页（保留）
    connect(m_topBar, &TopBar::logoutClicked, this, &MainWindow::onLogout);

    // 自动锁屏定时器：读取AppConfig的lockTime，超时自动退出登录
    int lockMinutes = AppConfig::instance().borrowLockTime();
    m_idleTimer = new QTimer(this);
    m_idleTimer->setInterval(lockMinutes * 60 * 1000);  // 分钟转毫秒
    m_idleTimer->setSingleShot(true);  // 单次触发，超时后需用户活动才重新计时
    m_lastActivity = QDateTime::currentDateTime();
    connect(m_idleTimer, &QTimer::timeout, this, &MainWindow::onAutoLockTimeout);
    m_idleTimer->start();

    // 安装全局事件过滤器：监听鼠标/键盘活动，重置空闲计时器
    qApp->installEventFilter(this);

    // 数据库备份检查定时器：每小时检查一次是否到了备份时间
    m_backupCheckTimer = new QTimer(this);
    m_backupCheckTimer->setInterval(60 * 60 * 1000);  // 1小时检查一次
    connect(m_backupCheckTimer, &QTimer::timeout, this, &MainWindow::onBackupCheckTimeout);
    m_backupCheckTimer->start();
}

/**
 * @brief 初始化顶部栏
 */
void MainWindow::setupTopBar() {
    m_topBar = new TopBar();
    m_topBar->setFixedHeight(68);
    // 版本号由页面层读配置后注入顶栏（组件层不读 AppConfig）
    m_topBar->setVersionText(AppConfig::instance().appVersion());
}

/**
 * @brief 创建侧边栏
 */
QWidget* MainWindow::createSidebar() {
    QWidget* sb = new QWidget();
    sb->setStyleSheet("background:#1a1a2e;");
    QVBoxLayout* layout = new QVBoxLayout(sb);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 标题区 8寸屏适配：标题24px，高度76px
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
    m_sidebarNav->setSpacing(6);  // 6，菜单项间距加大防误触

    struct NavItem { QString id; QString label; };
    QList<NavItem> items = {
        {"users",      QStringLiteral("👥 人员管理")},
        {"tools",      QStringLiteral("🔧 工具管理")},
        // 管理员借用/归还入口合并为一项，点击进入模拟数据智能柜会话
        {"borrowreturn", QStringLiteral("📤 工具借用/归还")},
        {"checkin",    QStringLiteral("📦 工具入库")},
        {"checkout",   QStringLiteral("📋 工具出库")},
        {"ledger",     QStringLiteral("📈 台账统计")},
        {"alerts",     QStringLiteral("🔔 告警日志")},
        {SC::TOOL_MAINTENANCE,QStringLiteral("🛠️ 系统维护")},  // 系统维护
        {"settings",   QStringLiteral("⚙️ 系统设置")},
    };

    for (const auto& item : items) {
        QPushButton* btn = new QPushButton("  " + item.label);
        btn->setObjectName(item.id);
        btn->setCursor(Qt::PointingHandCursor);
        // 8寸屏适配：菜单项高度60px，字体18px，触控目标加大防误触
        btn->setMinimumHeight(60);
        btn->setStyleSheet(
            "QPushButton { color:#c0c0d0; font-size:18px; text-align:left; padding-left:18px; "
            "border:none; border-radius:10px; background:transparent; }"
            "QPushButton:hover { background:#2a2a5e; color:white; }"
            "QPushButton[active=\"true\"] { background:#4da3ff; color:white; font-weight:bold; }"
        );
        connect(btn, &QPushButton::clicked, this, [this, id = item.id] {
            // 借用/归并入口不进页面栈，直接启动模拟会话
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

/**
 * @brief 显示页面
 */
void MainWindow::showPage(const QString& name) {
  // 普通用户仅允许访问 工具管理，
    // 防止其他入口（如告警跳转）绕过侧边栏限制
    bool canUseAdminPages = (m_user["role"].toString() == SC::ROLE_ADMIN
                          || m_user["role"].toString() == SC::ROLE_MANAGER)
                          && !m_userModeAsNormalUser;
    if (!m_user.isEmpty() && !canUseAdminPages && name != "tools") {
        qInfo() << "[MainWindow] 普通用户无权访问页面:" << name << "，重定向到 tools";
        showPage("tools");
        return;
    }
    // 覆盖层遮罩切换：overlay盖住页面→切换→overlay淡出→透出新页
    int idx = 2;  // 未知名默认tools（工具管理）
    for (const auto& pd : pageRegistry()) {
        if (pd.name == name) { idx = pd.index; break; }
    }
    // 去掉"同页面不刷新"限制，菜单点击时即使同页面也强制refresh
    // 反馈：系统维护页面切换时不刷新，根因是索引相同直接return
    // 举一反三：所有页面都需要在菜单切换时刷新数据（可能后台数据已变化）
    bool isSamePage = (idx == m_stack->currentIndex());

    // 页面刷新统一入口（同页点击与跨页切换共用）
    auto refreshPage = [this](const QString& n) {
        if (n == "users" && m_userMgmtPage)      m_userMgmtPage->refresh();
        else if (n == "tools" && m_toolMgmtPage)      m_toolMgmtPage->refresh();
        else if (n == "borrow" && m_borrowPage)        m_borrowPage->refresh();
        else if (n == "return" && m_returnPage)        m_returnPage->refresh();
        else if (n == "checkin" && m_checkinPage)      m_checkinPage->refresh();
        else if (n == "checkout" && m_checkoutPage)    m_checkoutPage->refresh();
        else if (n == "ledger" && m_ledgerPage)        m_ledgerPage->refresh();
        else if (n == "alerts" && m_alertsPage)        m_alertsPage->refresh();
        else if (n == SC::TOOL_MAINTENANCE && m_maintenancePage) m_maintenancePage->refresh();
        else if (n == "settings" && m_settingsPage)    m_settingsPage->refresh();
        else qWarning() << "[MainWindow] Unknown page name for refresh:" << n;
    };

    // 同页面只做refresh，不重复切换动画
    if (isSamePage) {
        // 同页面：直接refresh，不需要遮罩动画
        refreshPage(name);
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

    // 3. 遮罩淡出→透出新页面 350ms→150ms，提升菜单切换响应速度
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

    // 更新TopBar页面标题（标题来自页面注册表）
    QString pageTitle;
    for (const auto& pd : pageRegistry()) {
        if (pd.name == name) { pageTitle = pd.title; break; }
    }
    if (name == "login") {
        m_topBar->setPageTitle("");
        m_topBar->setUserAreaVisible(false);  // 登录页隐藏时间/用户名/退出
    } else {
        m_topBar->setPageTitle(pageTitle);
        m_topBar->setUserAreaVisible(true);
    }

    // 菜单切换性能优化：refresh异步执行
    // 原问题：refresh同步执行DB查询阻塞UI线程，导致150ms淡出动画卡顿
    // 优化：用QTimer::singleShot(0)将refresh延迟到下一轮事件循环
    // 让setCurrentIndex+遮罩动画先渲染，再执行DB查询
    // 效果：用户立即看到页面切换动画，DB查询在后台进行不卡顿
    QMetaObject::invokeMethod(this, [this, name, refreshPage]() { refreshPage(name); }, Qt::QueuedConnection);
}

/**
         * @brief 高亮侧边栏中当前所在的页面
         * @param pageKey 页面标识
         */
void MainWindow::updateSidebarActive(const QString& name) {
    for (auto* btn : m_navButtons) {
        // 防御：跳过空指针（防异常构建状态下脏指针导致崩溃）
        if (!btn) continue;
        bool active = (btn->objectName() == name);
        btn->setProperty("active", active ? "true" : "false");
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
    }
}

/**
         * @brief 按登录角色刷新侧边栏可见项
         * @param role 角色标识
         */
void MainWindow::updateSidebarVisibility() {
    bool loggedIn = !m_user.isEmpty();
    m_sidebar->setVisible(loggedIn);
    for (auto* btn : m_navButtons) {
        QString id = btn->objectName();
        const QString role = m_user["role"].toString();
        // 管理员与管理用户可进入管理页面；以普通用户身份登录时按普通用户菜单显示
        bool canUseAdminPages = (role == SC::ROLE_ADMIN || role == SC::ROLE_MANAGER)
                                && !m_userModeAsNormalUser;
        bool show;
        if (canUseAdminPages) {
            // 借用/归还合并为borrowreturn入口（模拟会话）
            show = (id == "tools" || id == "borrowreturn" || id == "alerts")
                || (id == "users" || id == "checkin" || id == "checkout" || id == "ledger"
                     || id == SC::TOOL_MAINTENANCE || id == "settings");  // 管理侧可见系统维护
        } else {
  // 普通用户仅开放 工具管理
            show = (id == "tools");
        }
        btn->setVisible(show);
    }
}

/**
 * @brief 设置当前用户
 */
void MainWindow::setCurrentUser(const QJsonObject& user) {
    m_user = user;
    // 用户信息仅在TopBar显示，侧边栏不重复展示
    m_topBar->setUserName(user["realName"].toString());
    m_topBar->setDepartment(user["department"].toString());

    updateSidebarVisibility();
}

/**
         * @brief 登录成功处理：按角色进入对应首页
         * @param user 登录用户信息
         */
void MainWindow::onLoginSuccess(const QJsonObject& user) {
    // 登录成功后确保摄像头已关闭（兜底保险）
    m_loginPage->stopFaceRecognitionPublic();
    setCurrentUser(user);
    m_borrowPage->setUser(user);
    m_returnPage->setUser(user);
    // 出库页面设置当前用户（出库记录写入正确操作人）
    m_checkoutPage->setUser(user);
    // 入库页面设置当前用户（入库记录写入正确操作人）
    m_checkinPage->setUser(user);
    // 告警日志页面设置当前用户（忽略按钮权限控制）
    m_alertsPage->setUser(user);
    // 登录成功后刷新TopBar版本号显示（页面层读配置后注入）
    m_topBar->setVersionText(AppConfig::instance().appVersion());

    // 登录已通过但身份未定的过渡态：主界面切到中性遮罩页并隐藏侧边栏。
    // 原因：此时后续可能先弹"强制改密"或"身份选择"对话框，若不先切走，
    // 弹窗背景会停留在登录页的"身份验证通过"卡片上，且管理菜单会提前暴露。
    if (m_userFlowCover) m_stack->setCurrentIndex(m_stack->indexOf(m_userFlowCover));
    m_sidebar->setVisible(false);

    // 初始密码用户必须先自设新密码才能继续（管理员同样适用）
    // 校验不通过或主动退出登录时，一律回到登录页
    if (user["mustChangePwd"].toInt() == 1) {
        // 首次登录强制改密：初始口令是公开默认值，不显示也不校验原口令输入框
        ForceChangePasswordDialog dlg(user["userId"].toInt(),
                                      user["realName"].toString(), false, this);
        if (dlg.exec() != QDialog::Accepted) {
            onLogout();
            return;
        }
    }

    const QString role = user["role"].toString();

    // 普通用户：直接进功能选择页
    if (role == SC::ROLE_USER) {
        runUserEntryFlow(user);
        return;
    }

    // 管理用户（管理员+普通用户双重身份）：先选本次登录身份
    if (role == SC::ROLE_MANAGER) {
        RoleSelectDialog roleDlg(user["realName"].toString(), this);
        roleDlg.exec();
        if (roleDlg.choice() == RoleSelectDialog::Choice::User) {
            // 以普通用户身份进入：按普通用户权限显示菜单
            m_userModeAsNormalUser = true;
            updateSidebarVisibility();
            runUserEntryFlow(user);
            return;
        }
        if (roleDlg.choice() == RoleSelectDialog::Choice::Cancelled) {
            onLogout();
            return;
        }
        // 选择管理员身份：按管理侧流程继续
        m_userModeAsNormalUser = false;
    }

    // 进入管理界面：恢复侧边栏后再切页（遮罩过渡态在此结束）
    updateSidebarVisibility();
    showPage("tools");
    emit userLoggedIn(user);
}

/**
 * @brief 运行普通用户流程：功能选择页循环，直到用户退出登录
 * @param user 已登录用户信息
 * @note 流程期间主界面停在中性遮罩页并隐藏侧边栏，防止弹窗切换间隙闪现登录页；
 *       "退出登录"退出循环并调用 onLogout() 回到登录页
 */
void MainWindow::runUserEntryFlow(const QJsonObject& user) {
    // 整个普通用户流程期间：主界面停在中性遮罩页+隐藏侧边栏
    if (m_userFlowCover) m_stack->setCurrentIndex(m_stack->indexOf(m_userFlowCover));
    m_sidebar->setVisible(false);
    while (true) {
        UserEntryDialog entry(user, this);
        UserEntryDialog::Choice choice = entry.execChoice();
        if (choice == UserEntryDialog::Choice::Logout) break;
        if (choice == UserEntryDialog::Choice::BorrowReturn) {
            CabinetSessionDialog session(user, this);
            session.startSession();
            continue;
        }
        // Query兜底（查询按钮已为弹出对话框不accept，理论不可达）
        updateSidebarVisibility();
        showPage("tools");
        emit userLoggedIn(user);
        return;
    }
    onLogout();
}

/**
         * @brief 注销处理：回到登录页并停止人脸识别
         */
void MainWindow::onLogout() {
    m_user = QJsonObject();
    m_userModeAsNormalUser = false;  // 复位身份标志，避免下次登录沿用上次选择
    // 侧边栏已移除用户区域，仅重置TopBar
    m_topBar->setUserName(QStringLiteral("未登录"));
    m_topBar->setDepartment("");
    updateSidebarVisibility();
  // 退出登录必须重置LoginPage所有状态
    // 否则残留"身份验证通过"、张三识别信息、m_autoJumpTimer未停等问题
    m_loginPage->resetPageState();
    // 注销必须可靠回到登录刷脸页：直接切索引，
    // ⚠ 不可动：绕开showPage的遮罩/动画/异步刷新链路（规避偶发不切换停留原页的问题）
    m_stack->setCurrentIndex(0);
    m_topBar->setPageTitle(QStringLiteral(""));
    m_topBar->setUserAreaVisible(false);
    updateSidebarActive(QStringLiteral("login"));
    // 注销后抑制自动刷脸登录：人未离开摄像头画面时不立即自动回登，
    // 离开画面后恢复（onFaceLost清除），保证注销后稳定停在刷脸页面
    m_loginPage->suppressAutoLoginAfterLogout();
    // 摄像头识别恢复：注销已停掉摄像头，切回登录页时重新启动（否则需重启程序才能刷脸）
    m_loginPage->resumeFaceRecognition();
    emit userLoggedOut();
}

// 快捷操作/侧边栏的"工具借用/归还"入口统一在转交处拦截：
// borrowreturn 不属于页面栈，直接启动智能柜借用/归还会话，避免落入未知页面兜底到工具管理
void MainWindow::navigateToPage(const QString& name) {
    if (name == QStringLiteral("borrowreturn")) {
        openBorrowReturnSession();
        return;
    }
    showPage(name);
}

// 管理员"工具借用/归还"入口：与普通用户相同的智能柜会话流程
// CabinetSessionDialog为全模拟数据演示（开柜提示→演示面板模拟拿取/放回→关柜差异清单）
// 功能选择页(普通用户首页)仅普通用户可见，管理员流程结束回工具管理页
void MainWindow::openBorrowReturnSession() {
    if (m_user.isEmpty()) return;
    CabinetSessionDialog session(m_user, this);
    session.startSession();
    showPage("tools");  // 管理员流程结束 → 回工具管理页
}

// 自动锁屏超时：退出登录回到登录页
void MainWindow::onAutoLockTimeout() {
    // 仅在已登录状态下触发锁屏
    if (m_user.isEmpty()) return;

    qInfo() << "[AutoLock] Idle timeout, auto logout";
    onLogout();
}

// 定时检查是否到了数据库备份时间
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

    // 本工程为纯 MySQL 架构（SQLite 已彻底移除，无回退），备份统一走 mysqldump 导出。
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

// 重置空闲计时器（用户有操作时调用）
void MainWindow::resetIdleTimer() {
    m_lastActivity = QDateTime::currentDateTime();
    if (m_idleTimer && m_idleTimer->isActive()) {
        m_idleTimer->start();  // 重新计时
    }
}

// 全局事件过滤器：监听鼠标点击/移动、键盘按键，重置空闲计时器
bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::MouseMove ||
        event->type() == QEvent::KeyPress ||
        event->type() == QEvent::Wheel) {
        resetIdleTimer();
    }
    return QMainWindow::eventFilter(watched, event);
}
