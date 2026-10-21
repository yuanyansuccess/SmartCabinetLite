/**
 * @file MainWindow.cpp
 * @brief 主窗口实现 - TopBar(68px) + 左侧240px深蓝侧边栏 + 页面栈
 * @author 袁燕
 */
#include "MainWindow.h"
#include "components/TopBar.h"
#include "pages/LoginPage.h"
#include "pages/MenuHomePage.h"  // 功能导航主页（登录后大按钮入口页）
#include "pages/UserManagementPage.h"
#include "pages/ToolManagementPage.h"
#include "pages/ToolBorrowPage.h"
#include "pages/ToolReturnPage.h"
#include "pages/ToolCheckinPage.h"
#include "pages/LedgerStatsPage.h"
#include "pages/AlertLogsPage.h"
#include "pages/HelpManualDialog.h"  // 顶栏"帮助"按钮的全屏操作手册
#include "pages/SystemSettingsPage.h"
#include "pages/CabinetSessionDialog.h"  // 普通用户智能柜会话（开柜提示+关柜清单）
#include "pages/ForceChangePasswordDialog.h"  // 首次登录强制修改初始密码
#include "pages/RoleSelectDialog.h"  // 管理用户登录身份选择
#include "comm/EdgeService.h"  // 与边缘计算机通信接口（识别结果适配后转给出入库页）
#include "utils/StyleHelper.h"
#include "common/AppConfig.h"  // 读取备份配置
#include "common/Constants.h"   // SC 命名空间常量
#include "common/DatabaseManager.h"  // 备份检查
#include "common/DiskSpaceGuard.h"  // 备份前磁盘容量保护
#include "db/RecordDAO.h"  // 普通用户查询借用明细与操作日志（读直调DAO）
#include <QScrollArea>
#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QColor>
#include <QJsonArray>
#include <QTableWidget>
#include <QHeaderView>
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
        {"checkin",     5,  QStringLiteral("工具出入库")},
        {"ledger",      6,  QStringLiteral("导入导出")},
        {"alerts_sys",  7,  QStringLiteral("系统告警")},   // 与操作告警共用 AlertLogsPage，索引相同
        {"alerts_op",   7,  QStringLiteral("操作告警")},
        {"settings",    8,  QStringLiteral("系统设置")},
        {"home",        9,  QStringLiteral("功能主页")},
    };
    return reg;
}

// ── 管理侧功能菜单（唯一真相源：导航主页卡片与侧边栏共用） ──
// key 必须与 pageRegistry 页面名一致，点击卡片即按页面名跳转。
struct MenuEntry { QString key; QString icon; QString label; };
static const QVector<MenuEntry>& adminMenuEntries() {
    static const QVector<MenuEntry> entries = {
        {"users",    QStringLiteral("👥"),  QStringLiteral("人员管理")},
        {"tools",    QStringLiteral("🔧"),  QStringLiteral("工具管理")},
        {"checkin",  QStringLiteral("📦"),  QStringLiteral("工具出入库")},
        {"ledger",   QStringLiteral("📂"),  QStringLiteral("导入导出")},
        {"alerts_sys", QStringLiteral("🚨"), QStringLiteral("系统告警")},
        {"alerts_op",  QStringLiteral("📝"), QStringLiteral("操作告警")},
        {"settings", QStringLiteral("⚙️"),  QStringLiteral("系统设置")},
    };
    return entries;
}

// 普通用户导航卡片动作标识（非页面名，onMenuCardClicked 内分流处理）
const QString kActionBorrowReturn = QStringLiteral("borrow_return");  // 借用/归还会话
const QString kActionQuery        = QStringLiteral("query");          // 借用明细查询
const QString kActionOpLog        = QStringLiteral("oplog");          // 操作日志查看
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
    , m_topBar(nullptr), m_stack(nullptr), m_sidebar(nullptr), m_contentArea(nullptr)
    , m_loginPage(nullptr), m_userMgmtPage(nullptr)
    , m_toolMgmtPage(nullptr), m_borrowPage(nullptr), m_returnPage(nullptr)
    , m_checkinPage(nullptr), m_ledgerPage(nullptr)
    , m_alertsPage(nullptr), m_settingsPage(nullptr)
{
    setWindowTitle(QStringLiteral("智能工具柜管理系统"));
    resize(1024, 768);            // 设计基准分辨率（8 寸 1024x768 工控屏）
    setMinimumSize(640, 480);
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
    m_menuHomePage  = new MenuHomePage();
    m_userMgmtPage  = new UserManagementPage();
    m_toolMgmtPage  = new ToolManagementPage();
    m_borrowPage    = new ToolBorrowPage();
    m_returnPage    = new ToolReturnPage();
    m_checkinPage   = new ToolCheckinPage();
    m_ledgerPage    = new LedgerStatsPage();
    m_alertsPage    = new AlertLogsPage();
    m_settingsPage  = new SystemSettingsPage();

    // 人脸录入功能集成在人员管理页
    // 索引：0:登录 1:用户管理 2:工具管理 3:借用 4:归还 5:出入库 6:台账 7:告警 8:设置 9:功能主页
    m_stack->addWidget(m_loginPage);
    m_stack->addWidget(m_userMgmtPage);
    m_stack->addWidget(m_toolMgmtPage);
    m_stack->addWidget(m_borrowPage);
    m_stack->addWidget(m_returnPage);
    m_stack->addWidget(m_checkinPage);
    m_stack->addWidget(m_ledgerPage);
    m_stack->addWidget(m_alertsPage);
    m_stack->addWidget(m_settingsPage);     // 系统设置(索引8)
    m_stack->addWidget(m_menuHomePage);     // 功能导航主页(索引9)

    // 流程过渡遮罩页：登录后强制改密/身份选择等弹窗期间主界面停在本页，
    // 避免弹窗间隙露出背后的登录页（闪现按键页面问题）
    m_userFlowCover = new QWidget();
    m_userFlowCover->setStyleSheet("background:" + StyleHelper::bgColor() + ";");
    m_stack->addWidget(m_userFlowCover);

    contentLayout->addWidget(m_stack, 1);
    mainLayout->addWidget(m_contentArea, 1);

    // 信号连接
    connect(m_loginPage, &LoginPage::loginSuccess, this, &MainWindow::onLoginSuccess);
    // 功能导航主页卡片点击：管理员页面名跳转/普通用户动作弹窗，统一分流
    connect(m_menuHomePage, &MenuHomePage::cardClicked, this, &MainWindow::onMenuCardClicked);
    // TopBar"主页"按钮 → 返回功能导航主页
    connect(m_topBar, &TopBar::homeClicked, this, [this] { navigateToPage("home"); });
    // 工具借用页面点击归还按钮 → 跳转工具归还页面并自动选中对应工具
    connect(m_borrowPage, &ToolBorrowPage::returnRequested, this, [this](int recordId) {
        m_returnPage->setPendingReturnRecordId(recordId);
        navigateToPage("return");
    });
    // TopBar退出按钮 → 退出整个系统（非注销）
    connect(m_topBar, &TopBar::exitSystemClicked, qApp, &QApplication::quit);
    // TopBar帮助按钮 → 全屏打开操作手册
    connect(m_topBar, &TopBar::helpClicked, this, &MainWindow::showHelpManual);
    // 侧边栏"退出登录"按钮 → 注销回登录页（保留）
    connect(m_topBar, &TopBar::logoutClicked, this, &MainWindow::onLogout);

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
    // 菜单项与功能导航主页共用 adminMenuEntries 数据源，避免两处维护漂移
    QList<NavItem> items;
    for (const auto& entry : adminMenuEntries()) {
        items.append({entry.key, entry.icon + " " + entry.label});
    }

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
  // 普通用户仅允许访问 工具管理与功能导航主页（卡片按角色注入，无越权入口），
    // 防止其他入口（如告警跳转）绕过页面权限限制
    bool canUseAdminPages = (m_user["role"].toString() == SC::ROLE_ADMIN
                          || m_user["role"].toString() == SC::ROLE_MANAGER)
                          && !m_userModeAsNormalUser;
    bool allowedForNormal = (name == "tools" || name == "home");
    if (!m_user.isEmpty() && !canUseAdminPages && !allowedForNormal) {
        qInfo() << "[MainWindow] 普通用户无权访问页面:" << name << "，重定向到 tools";
        showPage("tools");
        return;
    }
    // 覆盖层遮罩切换：overlay盖住页面→切换→overlay淡出→透出新页
    int idx = 2;  // 未知名默认tools（工具管理）
    for (const auto& pd : pageRegistry()) {
        if (pd.name == name) { idx = pd.index; break; }
    }

    // 更新TopBar页面标题（标题来自页面注册表）
    // 必须在"同页只刷新"分支之前执行：系统告警/操作告警共用同一栈索引，
    // 两者互切属于同页切换，标题仍需随入口名更新
    QString pageTitle;
    for (const auto& pd : pageRegistry()) {
        if (pd.name == name) { pageTitle = pd.title; break; }
    }
    if (name == "login") {
        m_topBar->setPageTitle("");
        m_topBar->setUserAreaVisible(false);  // 登录页隐藏时间/用户名/退出
        m_topBar->setHomeButtonVisible(false);
    } else {
        m_topBar->setPageTitle(pageTitle);
        m_topBar->setUserAreaVisible(true);
        // 功能导航主页自身隐藏"主页"按钮，其余子页面显示用于返回主页
        m_topBar->setHomeButtonVisible(name != "home");
    }

    // 去掉"同页面不刷新"限制，菜单点击时即使同页面也强制refresh
    // 举一反三：所有页面都需要在菜单切换时刷新数据（可能后台数据已变化）
    bool isSamePage = (idx == m_stack->currentIndex());

    // 页面刷新统一入口（同页点击与跨页切换共用）
    auto refreshPage = [this](const QString& n) {
        if (n == "users" && m_userMgmtPage)      m_userMgmtPage->refresh();
        else if (n == "tools" && m_toolMgmtPage)      m_toolMgmtPage->refresh();
        else if (n == "borrow" && m_borrowPage)        m_borrowPage->refresh();
        else if (n == "return" && m_returnPage)        m_returnPage->refresh();
        else if (n == "checkin" && m_checkinPage)      m_checkinPage->refresh();
        else if (n == "ledger" && m_ledgerPage)        m_ledgerPage->refresh();
        // 系统告警/操作告警两个入口共用同一页面实例，按入口名注入类别后刷新
        else if ((n == "alerts_sys" || n == "alerts_op") && m_alertsPage) {
            m_alertsPage->setCategory(n == "alerts_sys"
                                      ? AlertLogsPage::Category::System
                                      : AlertLogsPage::Category::Operation);
            m_alertsPage->refresh();
        }
        else if (n == "settings" && m_settingsPage)    m_settingsPage->refresh();
        else if (n == "home")                          refreshMenuCards();
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
    // 8寸屏布局：菜单入口由功能导航主页承载，侧边栏不参与显示
    m_sidebar->setVisible(false);
    for (auto* btn : m_navButtons) {
        QString id = btn->objectName();
        const QString role = m_user["role"].toString();
        // 管理员与管理用户可进入管理页面；以普通用户身份登录时按普通用户菜单显示
        bool canUseAdminPages = (role == SC::ROLE_ADMIN || role == SC::ROLE_MANAGER)
                                && !m_userModeAsNormalUser;
        bool show;
        if (canUseAdminPages) {
            show = (id == "tools" || id == "alerts_sys" || id == "alerts_op")
                || (id == "users" || id == "checkin" || id == "ledger"
                     || id == "settings");
        } else {
  // 普通用户仅开放 工具管理
            show = (id == "tools");
        }
        btn->setVisible(show);
    }
}

/**
 * @brief 注入边缘机通信服务并把识别结果适配转发出入库页
 * @param svc 边缘机通信服务（main.cpp 创建，本窗口不持有生命周期）
 * @note  分层适配：通信层类型（RecognizeResult）在本类转成页面中立结构 RecogSpot，
 *        页面不直接包含通信层头文件；心跳在线状态同样转发出入库页显示
 */
void MainWindow::setEdgeService(EdgeService* svc) {
    m_edgeService = svc;
    if (!m_edgeService || !m_checkinPage) return;

    // 页面请求识别 → 通信层发起（离线/写入失败时回传空串，页面走拒绝分支）
    connect(m_checkinPage, &ToolCheckinPage::recognitionRequested, this, [this]() {
        const QString requestId = m_edgeService->requestRecognize();
        m_checkinPage->onRecognitionStarted(requestId);
    });

    // 识别成功 → 转页面中立结构转发
    connect(m_edgeService, &EdgeService::recognizeFinished, this,
        [this](const QString& requestId, const RecognizeResult& result) {
            QList<ToolService::RecogSpot> spots;
            for (const RecognizeItem& item : result.items) {
                spots.append(ToolService::RecogSpot{item.position, item.toolId, item.confidence});
            }
            m_checkinPage->onRecognitionResult(requestId, spots);
        });

    // 识别失败（超时/离线/补发放弃） → 透传原因
    connect(m_edgeService, &EdgeService::recognizeFailed, this,
        [this](const QString& requestId, const QString& reason) {
            m_checkinPage->onRecognitionFailed(requestId, reason);
        });

    // 链路心跳状态变化 → 转发出入库页显示（等待中掉线时页面可即时收敛）
    connect(m_edgeService, &EdgeService::onlineChanged, this, [this](bool online) {
        m_checkinPage->onEdgeOnlineChanged(online);
    });

    // 页面请求开箱 → 通信层发起（离线/写入失败时回传空串，页面走拒绝分支）
    connect(m_checkinPage, &ToolCheckinPage::openRequested, this, [this]() {
        const QString requestId = m_edgeService->requestOpen();
        m_checkinPage->onOpenStarted(requestId);
    });
    connect(m_edgeService, &EdgeService::openFinished, this,
        [this](const QString& requestId, const OpenResult& result) {
            m_checkinPage->onOpenFinished(requestId, result.success, result.message);
        });
    connect(m_edgeService, &EdgeService::openFailed, this,
        [this](const QString& requestId, const QString& reason) {
            m_checkinPage->onOpenFailed(requestId, reason);
        });

    // 补发当前在线状态，避免页面订阅前边缘机已在线导致初始误判离线（开箱闸门误拒）
    m_checkinPage->onEdgeOnlineChanged(m_edgeService->isOnline());
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
    // 防重入：身份选择/强制改密弹窗打开期间loginSuccess再次到达（如刷脸自动登录重复触发），
    // 直接忽略，杜绝同一登录流程叠出两个弹窗、或弹窗与登录页叠加
    if (m_loginFlowBusy) {
        qInfo() << "[MainWindow] 忽略重复loginSuccess：登录过渡流程进行中";
        return;
    }
    m_loginFlowBusy = true;

    // 登录成功后确保摄像头已关闭（兜底保险）
    m_loginPage->stopFaceRecognitionPublic();

    // 硬性前置条件（第一个UI动作）：先离开登录页切到中性遮罩页，之后才允许出现任何弹窗。
    // 结构性保证"登录页"与"身份选择/强制改密弹窗"互斥：弹窗只可能出现在遮罩页之上，
    // 不存在登录页与弹窗叠加的路径。
    if (m_userFlowCover) m_stack->setCurrentWidget(m_userFlowCover);
    m_sidebar->setVisible(false);

    setCurrentUser(user);
    m_borrowPage->setUser(user);
    m_returnPage->setUser(user);
    // 出入库页面设置当前用户（出入库记录写入正确操作人）
    m_checkinPage->setUser(user);
    // 告警日志页面设置当前用户（忽略按钮权限控制）
    m_alertsPage->setUser(user);
    // 登录成功后刷新TopBar版本号显示（页面层读配置后注入）
    m_topBar->setVersionText(AppConfig::instance().appVersion());

    // 初始密码用户必须先自设新密码才能继续（管理员同样适用）
    // 校验不通过或主动退出登录时，一律回到登录页
    if (user["mustChangePwd"].toInt() == 1) {
        // 首次登录强制改密：初始口令是公开默认值，不显示也不校验原口令输入框
        ForceChangePasswordDialog dlg(user["userId"].toInt(),
                                      user["realName"].toString(), false, this);
        if (dlg.exec() != QDialog::Accepted) {
            m_loginFlowBusy = false;  // 先解锁再注销，否则注销后的自动刷脸登录会被本锁拦死
            onLogout();
            return;
        }
    }

    const QString role = user["role"].toString();
    // 登录分流埋点：排查登录类问题先看这行日志即可定位实际走的分支
    // （措辞避开日志敏感词过滤列表，保证落盘可见）
    qInfo() << "[MainWindow] 登录分流: 工号=" << user["workNo"].toString()
            << "角色=" << role;

    // 普通用户：直接进功能导航主页（卡片为借用归还/查询/操作日志）
    if (role == SC::ROLE_USER) {
        m_loginFlowBusy = false;
        enterMenuHomePage();
        return;
    }

    // 管理用户（管理员+普通用户双重身份）：先选本次登录身份。
    // 能走到这里的唯一前提：登录已完成且主界面已离开登录页（本函数第一个UI动作保证），
    // 弹窗前再强制停回遮罩页一次，双保险确保身份选择框永远不会叠在登录页上
    if (role == SC::ROLE_MANAGER) {
        if (m_userFlowCover) m_stack->setCurrentWidget(m_userFlowCover);
        RoleSelectDialog roleDlg(user["realName"].toString(), this);
        roleDlg.exec();
        // 身份选择埋点：取消=弹窗被关闭（含"退出登录"按钮），将自动注销回登录页
        qInfo() << "[MainWindow] 身份选择结果:"
                << (roleDlg.choice() == RoleSelectDialog::Choice::Admin ? QStringLiteral("管理员")
                    : roleDlg.choice() == RoleSelectDialog::Choice::User  ? QStringLiteral("普通用户")
                                                                          : QStringLiteral("取消"));
        if (roleDlg.choice() == RoleSelectDialog::Choice::User) {
            // 以普通用户身份进入：导航主页按普通用户卡片注入
            m_userModeAsNormalUser = true;
            m_loginFlowBusy = false;
            enterMenuHomePage();
            return;
        }
        if (roleDlg.choice() == RoleSelectDialog::Choice::Cancelled) {
            m_loginFlowBusy = false;  // 先解锁再注销，保证注销后自动刷脸登录可正常触发
            onLogout();
            return;
        }
        // 选择管理员身份：按管理侧流程继续
        m_userModeAsNormalUser = false;
    }

    m_loginFlowBusy = false;
    // 管理员身份进入：显示功能导航主页（遮罩过渡态在此结束）
    enterMenuHomePage();
}

/**
 * @brief 进入功能导航主页：显示侧边栏状态收敛后切页（遮罩过渡态在此结束）
 * @note 卡片内容由 refreshMenuCards 按当前登录角色注入；注销由 TopBar 注销按钮承担
 */
void MainWindow::enterMenuHomePage() {
    updateSidebarVisibility();
    showPage("home");
    emit userLoggedIn(m_user);
}

/**
 * @brief 按当前登录角色刷新导航主页：欢迎语与功能卡片
 * @note 管理侧注入8项功能卡片（与侧边栏菜单同源）；普通用户注入3张卡片
 */
void MainWindow::refreshMenuCards() {
    if (!m_menuHomePage) return;
    m_menuHomePage->setUserName(m_user["realName"].toString());

    const bool adminSide = (m_user["role"].toString() == SC::ROLE_ADMIN
                         || m_user["role"].toString() == SC::ROLE_MANAGER)
                         && !m_userModeAsNormalUser;
    QList<MenuHomePage::MenuCard> cards;
    if (adminSide) {
        for (const auto& entry : adminMenuEntries()) {
            cards.append({entry.key, entry.icon, entry.label});
        }
    } else {
        cards.append({kActionBorrowReturn, QStringLiteral("🔄"), QStringLiteral("借用归还")});
        cards.append({kActionQuery,        QStringLiteral("🔍"), QStringLiteral("查  询")});
        cards.append({kActionOpLog,        QStringLiteral("📝"), QStringLiteral("操作日志")});
    }
    m_menuHomePage->setCards(cards);
}

/**
 * @brief 功能导航主页卡片点击分流
 * @param key 卡片标识：管理侧为页面注册名直接跳转（权限收敛在 showPage 内）；
 *            普通用户为动作标识，弹出对应会话/明细/操作日志对话框
 */
void MainWindow::onMenuCardClicked(const QString& key) {
    if (key == kActionBorrowReturn) { startCabinetSession(); return; }
    if (key == kActionQuery)        { showUserBorrowDetail(); return; }
    if (key == kActionOpLog)        { showUserOperationLog(); return; }
    navigateToPage(key);  // 页面注册名：走统一跳转链路（刷新/遮罩动画/权限收敛）
}

/**
 * @brief 启动普通用户开柜借用/归还会话
 * @note 会话为全屏模态对话框，结束后回到功能导航主页（页面常驻栈中）；
 *       会话内顶栏点"注销"时先结束会话，再由主窗口执行注销回登录页
 */
void MainWindow::startCabinetSession() {
    CabinetSessionDialog session(m_user, this);
    connect(&session, &CabinetSessionDialog::logoutRequested, this, &MainWindow::onLogout);
    session.startSession();
}

/**
 * @brief 弹出当前用户借用明细对话框（普通用户"查询"卡片，全屏模态触屏风格）
 * @note 展示该用户全部借用记录（当前借用在前），关闭后停留在功能导航主页；
 *       数据源 RecordDAO::findBorrowsByUser（DAO层现成接口，零新增SQL）；
 *       顶部挂管理员同款TopBar（页面标题由顶栏承载，页内不再重复放大标题）
 */
void MainWindow::showUserBorrowDetail() {
    db::RecordDAO dao;
    const QList<BorrowRecord> records = dao.findBorrowsByUser(m_user["userId"].toInt(), 200);

    // 统计：当前借用件数（借用中/已逾期）+ 累计借用次数
    int activeCount = 0;
    for (const auto& r : records) {
        if (r.status == SC::RECORD_BORROWING || r.status == SC::RECORD_OVERDUE) activeCount++;
    }

    QDialog dlg(this);
    dlg.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dlg.setModal(true);
    dlg.setStyleSheet(StyleHelper::dialogBackground());

    auto* lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(40, 28, 40, 36);
    lay->setSpacing(14);

    // 顶部管理员同款顶栏（页面标题/时钟/用户信息/主页/注销）
    attachDialogTopBar(&dlg, QStringLiteral("借用明细"));

    // 统计行
    auto* statLabel = new QLabel(QStringLiteral("当前 %1 件 · 累计 %2 次")
                                     .arg(activeCount).arg(records.size()));
    statLabel->setAlignment(Qt::AlignCenter);
    statLabel->setStyleSheet(QString("font-size: 17px; color: %1; background: transparent;")
                                 .arg(StyleHelper::textSecondary()));
    lay->addWidget(statLabel);

    // 明细表格
    auto* table = new QTableWidget();
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({QStringLiteral("借用时间"), QStringLiteral("工具名称"),
                                      QStringLiteral("工具编号"), QStringLiteral("数量"),
                                      QStringLiteral("预计归还"), QStringLiteral("状态")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(false);
    table->setStyleSheet(
        "QTableWidget { background:white; border:1px solid #e0e0e0; border-radius:10px;"
        "  font-size:16px; color:#333; gridline-color:#eee; }"
        "QHeaderView::section { background:#f5f6f8; color:#999; font-size:14px; font-weight:600;"
        "  border:none; border-bottom:1px solid #e0e0e0; padding:10px 6px; }"
        "QTableWidget::item { padding:8px 6px; }");
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(46);  // 触屏行高
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);   // 工具名称撑满
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    table->setColumnWidth(0, 170);
    table->setRowCount(records.size());
    for (int i = 0; i < records.size(); ++i) {
        const BorrowRecord& r = records.at(i);
        table->setItem(i, 0, new QTableWidgetItem(r.borrowTime.toString("yyyy-MM-dd HH:mm")));
        table->setItem(i, 1, new QTableWidgetItem(r.toolName));
        table->setItem(i, 2, new QTableWidgetItem(r.toolCode));
        table->setItem(i, 3, new QTableWidgetItem(QString::number(r.quantity)));
        table->setItem(i, 4, new QTableWidgetItem(r.expectReturnTime.toString("yyyy-MM-dd HH:mm")));

        // 状态列：中文+配色（借用中蓝/已逾期红/已归还绿）
        QString statusText;
        QString statusColor;
        if (r.status == SC::RECORD_BORROWING) { statusText = "借用中"; statusColor = StyleHelper::primaryColor(); }
        else if (r.status == SC::RECORD_OVERDUE)  { statusText = "已逾期"; statusColor = "#ff4d4f"; }
        else if (r.status == SC::RECORD_RETURNED) { statusText = "已归还"; statusColor = StyleHelper::successColor(); }
        else                              { statusText = r.status; statusColor = StyleHelper::textSecondary(); }
        auto* statusItem = new QTableWidgetItem(statusText);
        statusItem->setForeground(QColor(statusColor));
        statusItem->setFont(QFont(QString(), -1, QFont::Bold));
        table->setItem(i, 5, statusItem);
    }
    if (records.isEmpty()) {
        auto* emptyLabel = new QLabel(QStringLiteral("暂无借用记录"));
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setStyleSheet(QString("font-size: 18px; color: %1; background: transparent; padding: 24px;")
                                      .arg(StyleHelper::textSecondary()));
        lay->addWidget(emptyLabel);
    }
    lay->addWidget(table, 1);

    // 关闭按钮（大，触屏友好）
    auto* closeBtn = new QPushButton(QStringLiteral("关  闭"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(QString(
        "QPushButton { background: %1; color: white; border: none; border-radius: 12px;"
        "  font-size: 20px; font-weight: 700; min-height: 56px; min-width: 260px; }"
        "QPushButton:hover { background: %2; }"
    ).arg(StyleHelper::primaryColor(), StyleHelper::primaryHover()));
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    lay->addWidget(closeBtn, 0, Qt::AlignCenter);

    dlg.showFullScreen();
    dlg.exec();
}

/**
 * @brief 弹出操作日志对话框（普通用户"操作日志"卡片，全屏模态触屏风格）
 * @note 展示系统全部操作日志（按时间倒序取最近200条），列：时间/类型/内容/操作人；
 *       数据源 RecordDAO::findOperationLogsAll（DAO层现成接口）；
 *       顶部挂管理员同款TopBar（页面标题由顶栏承载，页内不再重复放大标题）
 */
void MainWindow::showUserOperationLog() {
    db::RecordDAO dao;
    const QJsonObject result = dao.findOperationLogsAll(1, 200);
    const QJsonArray list = result["list"].toArray();

    QDialog dlg(this);
    dlg.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dlg.setModal(true);
    dlg.setStyleSheet(StyleHelper::dialogBackground());

    auto* lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(40, 28, 40, 36);
    lay->setSpacing(14);

    // 顶部管理员同款顶栏（页面标题/时钟/用户信息/主页/注销）
    attachDialogTopBar(&dlg, QStringLiteral("操作日志"));

    // 统计行
    auto* statLabel = new QLabel(QStringLiteral("共 %1 条操作")
                                     .arg(list.size()));
    statLabel->setAlignment(Qt::AlignCenter);
    statLabel->setStyleSheet(QString("font-size: 17px; color: %1; background: transparent;")
                                 .arg(StyleHelper::textSecondary()));
    lay->addWidget(statLabel);

    // 操作日志表格：时间/类型/内容/操作人
    auto* table = new QTableWidget();
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("类型"),
                                      QStringLiteral("内容"), QStringLiteral("操作人")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setStyleSheet(
        "QTableWidget { background:white; border:1px solid #e0e0e0; border-radius:10px;"
        "  font-size:16px; color:#333; gridline-color:#eee; }"
        "QHeaderView::section { background:#f5f6f8; color:#999; font-size:14px; font-weight:600;"
        "  border:none; border-bottom:1px solid #e0e0e0; padding:10px 6px; }"
        "QTableWidget::item { padding:8px 6px; }");
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(46);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);   // 内容撑满
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table->setColumnWidth(0, 190);
    table->setRowCount(list.size());
    for (int i = 0; i < list.size(); ++i) {
        const QJsonObject r = list.at(i).toObject();
        QString time = r["createdAt"].toString();
        if (time.length() > 19) time = time.left(19);
        table->setItem(i, 0, new QTableWidgetItem(time));

        // 类型中文+配色（入库绿/出库红，其余类型显示原值）
        const QString type = r["type"].toString();
        QString typeText = type;
        QString typeColor = StyleHelper::textSecondary();
        if (type == SC::OP_CHECKIN)       { typeText = QStringLiteral("入库"); typeColor = StyleHelper::successColor(); }
        else if (type == SC::OP_CHECKOUT) { typeText = QStringLiteral("出库"); typeColor = "#e53935"; }
        auto* typeItem = new QTableWidgetItem(typeText);
        typeItem->setForeground(QColor(typeColor));
        table->setItem(i, 1, typeItem);

        table->setItem(i, 2, new QTableWidgetItem(r["content"].toString()));
        table->setItem(i, 3, new QTableWidgetItem(r["realName"].toString()));
    }
    if (list.isEmpty()) {
        auto* emptyLabel = new QLabel(QStringLiteral("暂无操作日志"));
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setStyleSheet(QString("font-size: 18px; color: %1; background: transparent; padding: 24px;")
                                      .arg(StyleHelper::textSecondary()));
        lay->addWidget(emptyLabel);
    }
    lay->addWidget(table, 1);

    // 关闭按钮
    auto* closeBtn = new QPushButton(QStringLiteral("关  闭"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(QString(
        "QPushButton { background: %1; color: white; border: none; border-radius: 12px;"
        "  font-size: 20px; font-weight: 700; min-height: 56px; min-width: 260px; }"
        "QPushButton:hover { background: %2; }" 
    ).arg(StyleHelper::primaryColor(), StyleHelper::primaryHover()));
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    lay->addWidget(closeBtn, 0, Qt::AlignCenter);

    dlg.showFullScreen();
    dlg.exec();
}

/**
 * @brief 给全屏对话框挂管理员同款顶栏（复用TopBar组件，普通用户子界面头部统一）
 * @param dlg 目标全屏对话框（主布局为QVBoxLayout，顶栏插入第0行）
 * @param title 顶栏页面标题
 * @return 创建的TopBar指针
 * @note 信号语义与主窗口一致：主页=关闭对话框回功能主页；
 *       注销=关闭对话框后注销回登录页；退出=结束整个程序；帮助=全屏打开操作手册
 */
TopBar* MainWindow::attachDialogTopBar(QDialog* dlg, const QString& title) {
    TopBar* topBar = new TopBar(dlg);
    topBar->setFixedHeight(68);
    topBar->setPageTitle(title);
    topBar->setUserAreaVisible(true);
    topBar->setHomeButtonVisible(true);
    topBar->setUserName(m_user["realName"].toString());
    topBar->setDepartment(m_user["department"].toString());
    topBar->setVersionText(AppConfig::instance().appVersion());
    if (auto* lay = qobject_cast<QVBoxLayout*>(dlg->layout())) {
        lay->insertWidget(0, topBar);
    }
    connect(topBar, &TopBar::homeClicked, dlg, &QDialog::accept);
    connect(topBar, &TopBar::logoutClicked, dlg, [this, dlg] {
        dlg->reject();
        onLogout();
    });
    connect(topBar, &TopBar::exitSystemClicked, qApp, &QApplication::quit);
    connect(topBar, &TopBar::helpClicked, dlg, [dlg] {
        HelpManualDialog manual(dlg);
        manual.showFullScreen();
        manual.exec();
    });
    return topBar;
}

/**
 * @brief 全屏打开内置HTML操作手册（系统框架图与各功能操作说明）
 * @note 手册HTML编译进程序资源，管理员与普通用户均可在顶栏"帮助"按钮打开
 */
void MainWindow::showHelpManual() {
    HelpManualDialog dlg(this);
    dlg.showFullScreen();
    dlg.exec();
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
    m_topBar->setHomeButtonVisible(false);  // 注销回登录页后隐藏"主页"按钮
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

void MainWindow::navigateToPage(const QString& name) {
    showPage(name);
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

    // 磁盘容量保护：可用比例不足时先静默清理最旧的备份文件，用户无感知
    DiskGuard::ensureFreePercent(backupPath, { QStringLiteral("smartcabinet_backup_*.sql") });

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


