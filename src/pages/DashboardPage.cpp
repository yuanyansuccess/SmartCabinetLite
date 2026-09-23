/**
 * @file DashboardPage.cpp
 * @brief 系统概览仪表盘实现（1:1复刻BS端Dashboard.vue）
 * @author 袁燕
 * @修改说明 V1.00.9 2026-06-15 完全重构以匹配BS端Dashboard.vue
 *   - 统计卡片改为：工具总数/在库工具/已借出/异常告警
 *   - 添加双面板布局（最近操作记录 + 快捷操作/实时告警）
 *   - 日志表格改为6列（时间/操作人/类型/工具名称/数量/状态）
 *   - 添加快捷操作网格（6个功能入口）
 *   - 添加实时告警面板
 *   - 触屏优化：按钮最小56px，字体17px+
 * @修改说明 2026-06-23 全局Web端符合性修复（三人团队检查）
 *   - P0: getRecentLogs数据源从sys_operation_log改为tool_borrow_record（字段对齐time/user/type/tool/qty/status）
 *   - P0: 用户视图替换TODO为getUserBorrowRecords，添加归还提醒数据填充
 *   - P1: stat-card padding对齐16px 22px，所有面板添加box-shadow
 *   - P1: 卡片副标题移除硬编码"较上周+2"改为动态在库率
 *   - P1: 添加error状态捕获
 */
#include "DashboardPage.h"
#include "utils/StyleHelper.h"
#include "services/SettingService.h"
#include "common/Constants.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QHeaderView>
#include <QScrollArea>
#include <QDateTime>
#include <QDebug>
#include <QPushButton>
#include <QLabel>

DashboardPage::DashboardPage(QWidget* parent) : QWidget(parent), 
    m_loading(false),
    m_toolCountLabel(nullptr),
    m_inStockLabel(nullptr),
    m_borrowedLabel(nullptr),
    m_checkedOutLabel(nullptr),  // [V2.03]
    m_alertsLabel(nullptr),
    m_logTable(nullptr),
    m_alertList(nullptr),
    m_alertBadge(nullptr),
    m_alertPanelWidget(nullptr),
    m_userRecentTable(nullptr),
    m_userReturnTable(nullptr),
    m_loadingWidget(nullptr),
    m_roleContent(nullptr) {
    // [v4.4修复] 所有成员指针必须初始化为nullptr
    //   用户视图路径下setupStatsCards()不执行，label指针为野指针(0xCD)
    //   若未初始化，updateStats()中if(!label)检查通过(0xCD≠null)，label->parentWidget()崩溃
    // [v4.6修复] 构造函数中不构建角色视图(m_user为空不能判断角色)
    //   setupUI()移入setUser()，确保用户对象已设置后再根据role构建对应视图
    setupBaseUI();
}

DashboardPage::~DashboardPage() = default;

void DashboardPage::setUser(const QJsonObject& user) {
    // [v4.6修复] 先设置m_user再构建UI——setupUI()需要m_user["role"]判断视图类型
    //   构造函数中m_user为空，setupUI()误判所有用户为普通用户
    m_user = user;
    
    // 清除旧的角色内容容器
    if (m_roleContent) {
        QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(layout());
        if (mainLayout) {
            mainLayout->removeWidget(m_roleContent);
        }
        m_roleContent->deleteLater();
        m_roleContent = nullptr;
        // 重置所有在角色内容中创建的指针
        m_toolCountLabel = nullptr;
        m_inStockLabel = nullptr;
        m_borrowedLabel = nullptr;
        m_checkedOutLabel = nullptr;  // [V2.03]
        m_alertsLabel = nullptr;
        m_logTable = nullptr;
        m_alertList = nullptr;
        m_alertBadge = nullptr;
        m_alertPanelWidget = nullptr;
        m_userRecentTable = nullptr;
        m_userReturnTable = nullptr;
    }
    
    // 根据角色构建对应视图
    setupUI();
    refresh();
}

void DashboardPage::setLoading(bool loading) {
    m_loading = loading;
    // [V1.00.9 新增] 加载状态切换
    if (m_loading) {
        // 显示加载状态
        if (m_loadingWidget) {
            m_loadingWidget->setVisible(true);
            // 启动动画
            m_loadingWidget->startAnimation();
        }
        
        // 隐藏其他内容控件
        QList<QWidget*> contentWidgets = findChildren<QWidget*>();
        for (QWidget* widget : contentWidgets) {
            if (widget != m_loadingWidget && widget != this) {
                widget->setVisible(false);
            }
        }
    } else {
        // 隐藏加载状态
        if (m_loadingWidget) {
            // 停止动画
            m_loadingWidget->stopAnimation();
            m_loadingWidget->setVisible(false);
        }
        
        // 显示其他内容控件
        QList<QWidget*> contentWidgets = findChildren<QWidget*>();
        for (QWidget* widget : contentWidgets) {
            if (widget != m_loadingWidget && widget != this) {
                widget->setVisible(true);
            }
        }
    }
}

void DashboardPage::setupBaseUI() {
    // [v4.6新增] 仅创建主布局框架 + 加载控件，不判断角色
    //   角色相关的视图创建移入setupUI()，由setUser()在用户对象就绪后调用
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(14);

    // 加载状态控件（初始隐藏）
    m_loadingWidget = new LoadingWidget(this);
    m_loadingWidget->setVisible(false);
    mainLayout->addWidget(m_loadingWidget);

    // 底部留白
    mainLayout->addStretch();
}

void DashboardPage::setupUI() {
    // [v4.6修复] 角色视图必须使用m_roleContent容器包装
    //   这样setUser()切换用户时可以整体清除旧视图
    QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(layout());
    if (!mainLayout) return;

    // 移除旧的底部stretch（稍后重新添加）
    QLayoutItem* stretch = mainLayout->takeAt(mainLayout->count() - 1);
    delete stretch;

    // 创建角色内容容器（所有角色相关UI都放入此容器）
    m_roleContent = new QWidget(this);
    auto* roleLayout = new QVBoxLayout(m_roleContent);
    roleLayout->setContentsMargins(0, 0, 0, 0);
    roleLayout->setSpacing(14);

    // ==================== 根据角色切换视图 ====================
    bool isAdmin = (m_user["role"].toString() == "admin");

    if (!isAdmin) {
        // ==================== 用户视图 ====================
        QWidget* welcomeBanner = createWelcomeBanner();
        welcomeBanner->setObjectName("welcomeBanner");
        roleLayout->addWidget(welcomeBanner);

        QWidget* funcGrid = createFunctionCards();
        funcGrid->setObjectName("funcGrid");
        roleLayout->addWidget(funcGrid);

        QHBoxLayout* userDualPanel = new QHBoxLayout();
        userDualPanel->setSpacing(14);
        // [v5.1修复] Web版 .dual-panel grid-template-columns:2fr 1fr, 统一2:1比例
        userDualPanel->addWidget(createUserRecentBorrowsPanel(), 2);
        userDualPanel->addWidget(createUserReturnRemindersPanel(), 1);
        roleLayout->addLayout(userDualPanel, 1);
    } else {
        // ==================== 管理员视图 ====================
        auto* cardsRow = new QHBoxLayout();
        cardsRow->setSpacing(14);
        setupStatsCards(cardsRow);
        roleLayout->addLayout(cardsRow);

        auto* dualPanel = new QHBoxLayout();
        dualPanel->setSpacing(14);

        auto* leftPanel = createRecentLogsPanel();
        leftPanel->setObjectName("leftPanel");
        dualPanel->addWidget(leftPanel, 2);

        auto* rightCol = new QVBoxLayout();
        rightCol->setContentsMargins(0, 0, 0, 0);  // [V6.2] 清除Qt默认(11,11,11,11)边距，对齐Web版
        rightCol->setSpacing(14);
        rightCol->addWidget(createQuickActionsPanel(), 1);
        rightCol->addWidget(createAlertPanel(), 1);
        auto* rightWidget = new QWidget();
        rightWidget->setLayout(rightCol);
        dualPanel->addWidget(rightWidget, 1);

        roleLayout->addLayout(dualPanel, 1);
    }

    // [V7.4 2026-06-26] 给m_roleContent设置stretch因子1，让其填满QStackedWidget分配给DashboardPage的空间
    //   之前mainLayout->addStretch()会抢占所有剩余空间，导致首页右侧/底部大片空白
    mainLayout->addWidget(m_roleContent, 1);
}

QWidget* DashboardPage::createStatCard(const QString& title, const QString& icon, 
                                        QLabel*& valueLabel, const QString& colorClass) {
    // [2026-06-23v6] 去除卡片边框(数字显示不全)，增大最小高度确保36px数字完整可见
    auto* card = new QWidget();
    card->setStyleSheet(QString(
        "background:white; border-radius:14px; border:none;"
    ));
    card->setMinimumHeight(120);
    auto* cl = new QHBoxLayout(card);  // [2026-06-23] 改为横向布局
    // [2026-06-23] 对齐Web端 .stat-card: padding:16px 22px
    cl->setContentsMargins(16, 22, 16, 22);
    cl->setSpacing(16);

    // 左侧图标
    QString bgColor;
    if (colorClass == "blue") bgColor = "#e6f0ff";
    else if (colorClass == "green") bgColor = "#f0fdf4";
    else if (colorClass == "orange") bgColor = "#fff7e6";
    else if (colorClass == "red") bgColor = "#fef2f2";
    else if (colorClass == "purple") bgColor = "#f3e8ff";  // [V2.03] 出库紫色
    else bgColor = "#f5f5f5";

    QString iconColor;
    if (colorClass == "blue") iconColor = "#4da3ff";
    else if (colorClass == "green") iconColor = "#52c41a";
    else if (colorClass == "orange") iconColor = "#fa8c16";
    else if (colorClass == "red") iconColor = "#e74c3c";
    else if (colorClass == "purple") iconColor = "#7c3aed";  // [V2.03] 出库紫色
    else iconColor = "#999";

    auto* iconBox = new QLabel(icon);
    iconBox->setFixedSize(SC::STAT_CARD_ICON_SIZE, SC::STAT_CARD_ICON_SIZE);
    iconBox->setAlignment(Qt::AlignCenter);
    iconBox->setStyleSheet(QString(
        "font-size:%1px; background:%2; border-radius:%3px;"
    ).arg(SC::STAT_CARD_ICON_FONT).arg(bgColor).arg(SC::STAT_CARD_ICON_RADIUS));
    cl->addWidget(iconBox);

    // 右侧文字区
    auto* rightWidget = new QWidget();
    rightWidget->setStyleSheet("background:transparent;");
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(4);

    // 标题
    auto* tl = new QLabel(title);
    tl->setStyleSheet("font-size:13px; color:#999; background:transparent;");
    rightLayout->addWidget(tl);

    // 数值 [2026-06-23] 对齐Web端 .card-value: font-size:36px; font-weight:800
    valueLabel = new QLabel("0");
    valueLabel->setStyleSheet(QString(
        "font-size:36px; font-weight:800; color:%1; background:transparent;"
    ).arg(iconColor));
    rightLayout->addWidget(valueLabel);

    // 副标题
    auto* subLabel = new QLabel(QStringLiteral(""));
    subLabel->setObjectName("sub_" + title);
    subLabel->setStyleSheet("font-size:12px; color:#bbb; background:transparent;");
    rightLayout->addWidget(subLabel);

    cl->addWidget(rightWidget, 1);

    // [2026-06-24v2] 数字滚动动画由setStatValue统一管理，此处仅初始化属性
    QVariantMap animData;
    animData["targetValue"] = 0;
    animData["currentValue"] = 0;
    animData["steps"] = 0;
    animData["maxSteps"] = 20;
    card->setProperty("animData", animData);

    return card;
}

void DashboardPage::setupStatsCards(QHBoxLayout* row) {
    // BS端统计卡片：工具项总数/在库工具项/已借出/已出库/异常告警
    // [2026-06-27] 标题改为"工具总数/在库工具/已借出"，统计按件数不再按种类
    // [V2.03 2026-06-29] 新增"已出库"卡片，工具总数 = 在库 + 已借出（出库的不计入总数）
    row->addWidget(createStatCard(QStringLiteral("工具总数"), QStringLiteral("🔧"), m_toolCountLabel, "blue"));
    row->addWidget(createStatCard(QStringLiteral("在库工具"), QStringLiteral("📦"), m_inStockLabel, "green"));
    row->addWidget(createStatCard(QStringLiteral("已借出"), QStringLiteral("📤"), m_borrowedLabel, "orange"));
    row->addWidget(createStatCard(QStringLiteral("已出库"), QStringLiteral("📦"), m_checkedOutLabel, "purple"));
    row->addWidget(createStatCard(QStringLiteral("异常告警"), QStringLiteral("⚠️"), m_alertsLabel, "red"));
}

QWidget* DashboardPage::createRecentLogsPanel() {
    auto* panel = new QFrame();
    panel->setStyleSheet(QString(
        "background:white; border-radius:12px; border:none;"
    ));
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 面板头部
    auto* head = new QWidget();
    head->setStyleSheet(QString("background:transparent; padding:14px 20px; border-bottom:1px solid #f0f0f0;"));
    auto* headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel(QStringLiteral("最近操作记录"));
    title->setStyleSheet("font-size:15px;font-weight:700;color:#1a1a2e;background:transparent;");
    headLayout->addWidget(title);
    headLayout->addStretch();
    // [2026-06-23] 查看全部按钮样式优化
    auto* more = new QPushButton(QStringLiteral("查看全部 →"));
    more->setStyleSheet("QPushButton{border:none; background:transparent; color:#4da3ff; font-size:13px; font-weight:600; padding:6px 12px;}"
                        "QPushButton:hover{color:#3d8ae0;}");
    more->setCursor(Qt::PointingHandCursor);
    more->setMinimumHeight(40);
    connect(more, &QPushButton::clicked, this, &DashboardPage::onQuickLedger);
    headLayout->addWidget(more);
    layout->addWidget(head);

    // 日志表格 [2026-06-25] 列宽Stretch均分+行高48px触屏优化
    m_logTable = new QTableWidget();
    m_logTable->setColumnCount(6);
    m_logTable->setHorizontalHeaderLabels({
        QStringLiteral("时间"), QStringLiteral("操作人"), QStringLiteral("类型"),
        QStringLiteral("工具名称"), QStringLiteral("数量"), QStringLiteral("状态")
    });
    m_logTable->horizontalHeader()->setStretchLastSection(false);
    m_logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_logTable->verticalHeader()->setVisible(false);
    m_logTable->setAlternatingRowColors(false);
    // [V2.02 2026-06-28] 移除内联表格QSS，使用全局QSS统一表格样式（小米设计语言）
    //   作者：袁燕 — 全局样式已在 styles/global.qss 中定义，字号15px+字重500+颜色更深
    // 列宽Stretch均分（6列等分），确保每列内容可见
    for (int i = 0; i < 6; i++) {
        m_logTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
    }
    m_logTable->horizontalHeader()->setMinimumSectionSize(50);
    layout->addWidget(m_logTable, 1);

    return panel;
}

QWidget* DashboardPage::createQuickActionsPanel() {
    auto* panel = new QFrame();
    panel->setStyleSheet(QString(
        "background:white; border-radius:12px; border:none;"
    ));
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 面板头部
    auto* head = new QWidget();
    head->setStyleSheet(QString("background:transparent; padding:12px 20px; border-bottom:1px solid #f0f0f0;"));
    auto* headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel(QStringLiteral("快捷操作"));
    title->setStyleSheet("font-size:15px;font-weight:700;color:#1a1a2e;background:transparent;");
    headLayout->addWidget(title);
    layout->addWidget(head);

    // [2026-06-23] 对齐Web端 quick-grid: grid-template-columns:repeat(3,1fr), gap:8px
    auto* grid = new QGridLayout();
    grid->setSpacing(8);
    grid->setContentsMargins(10, 10, 10, 10);  // 对齐Web .quick-grid padding:10px 20px
    for (int col = 0; col < 3; ++col) grid->setColumnStretch(col, 1);

    struct QuickAction { QString icon; QString text; QString path; bool isAdd; };
    // [2026-06-23] 对齐Web端 quickActions: 工具借用/归还/管理/人员管理/台账统计/出库管理 + 系统设置
    QuickAction actions[] = {
        {QStringLiteral("📤"), QStringLiteral("工具借用"), QStringLiteral("borrow"), false},
        {QStringLiteral("📥"), QStringLiteral("工具归还"), QStringLiteral("return"), false},
        {QStringLiteral("🔧"), QStringLiteral("工具管理"), QStringLiteral("tools"), false},
        {QStringLiteral("👥"), QStringLiteral("人员管理"), QStringLiteral("users"), false},
        {QStringLiteral("📊"), QStringLiteral("台账统计"), QStringLiteral("ledger"), false},
        {QStringLiteral("📦"), QStringLiteral("出库管理"), QStringLiteral("checkout"), false},
        {QStringLiteral("⚙"), QStringLiteral("系统设置"), QStringLiteral("settings"), true},  // 虚线边框特殊样式
    };

    for (int i = 0; i < 7; ++i) {
        auto* btn = new QPushButton();
        btn->setCursor(Qt::PointingHandCursor);
        btn->setMinimumHeight(72);

        bool isAdd = actions[i].isAdd;
        // [2026-06-23] 对齐Web端 .quick-btn: padding:16px 10px, border-radius:12px
        btn->setStyleSheet(QString(
            "%1"
            "QPushButton:hover { background:#e6f0ff; border-color:%2; }"
            "QPushButton:pressed { background:#e6f0ff; border-color:%2; }"
        ).arg(isAdd ? QString(
            "QPushButton { background:#f0f7ff; border:2px dashed #4da3ff; border-radius:12px; "
            "  padding:16px 10px; text-align:center; }")
          : QString(
            "QPushButton { background:#f8f9fc; border:2px solid #eef0f4; border-radius:12px; "
            "  padding:16px 10px; text-align:center; }"),
            "#4da3ff"));

        auto* vbox = new QVBoxLayout(btn);
        vbox->setContentsMargins(0, 0, 0, 0);
        vbox->setSpacing(6);  // [2026-06-23] 对齐Web .q-icon margin-bottom:6px
        auto* iconLabel = new QLabel(actions[i].icon);
        iconLabel->setStyleSheet(QString("font-size:%1px; background:transparent;")
            .arg(isAdd ? 24 : 28));  // [2026-06-23] 对齐Web .q-icon font-size:28px; 系统设置24px
        iconLabel->setAlignment(Qt::AlignCenter);
        auto* textLabel = new QLabel(actions[i].text);
        textLabel->setStyleSheet(QString("font-size:14px; font-weight:600; color:%1; background:transparent;")
            .arg(isAdd ? "#4da3ff" : "#333"));  // [2026-06-23] 系统设置文字蓝色对齐Web
        textLabel->setAlignment(Qt::AlignCenter);
        vbox->addWidget(iconLabel);
        vbox->addWidget(textLabel);

        connect(btn, &QPushButton::clicked, this, [this, path=actions[i].path]() {
            emit navigateRequested(path);
        });
        grid->addWidget(btn, i/3, i%3);  // [2026-06-23] 3列布局
    }

    layout->addLayout(grid);
    return panel;
}

QWidget* DashboardPage::createAlertPanel() {
    auto* panel = new QFrame();
    panel->setStyleSheet(QString(
        "background:white; border-radius:12px; border:none;"
    ));
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 面板头部
    auto* head = new QWidget();
    head->setStyleSheet(QString("background:transparent; padding:12px 20px; border-bottom:1px solid #f0f0f0;"));
    auto* headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel(QStringLiteral("实时告警"));
    title->setStyleSheet("font-size:15px;font-weight:700;color:#1a1a2e;background:transparent;");
    headLayout->addWidget(title);
    // [2026-06-23] 告警数量角标（动态更新，保存引用）
    m_alertBadge = new QLabel(QStringLiteral("0"));
    m_alertBadge->setFixedSize(24, 24);
    m_alertBadge->setAlignment(Qt::AlignCenter);
    m_alertBadge->setStyleSheet("font-size:12px; font-weight:bold; color:white; background:#e74c3c; border-radius:12px;");
    headLayout->addWidget(m_alertBadge);
    headLayout->addStretch();
    layout->addWidget(head);

    // 告警列表（动态加载，初始为空）
    m_alertList = new QVBoxLayout();
    m_alertList->setSpacing(0);
    m_alertList->setContentsMargins(20, 0, 20, 0);

    // 初始占位（数据加载后会被updateAlerts清除）
    auto* loadingLabel = new QLabel(QStringLiteral("加载中..."));
    loadingLabel->setStyleSheet("font-size:14px; color:#ccc; padding:20px; background:transparent;");
    loadingLabel->setAlignment(Qt::AlignCenter);
    m_alertList->addWidget(loadingLabel);

    m_alertPanelWidget = new QWidget();
    m_alertPanelWidget->setLayout(m_alertList);
    layout->addWidget(m_alertPanelWidget);
    layout->addStretch();

    return panel;
}

void DashboardPage::refresh() {
    // [V1.00.9 新增] 显示加载状态
    setLoading(true);
    
    // [2026-06-23] 所有数据均从数据库加载，移除硬编码
    QTimer::singleShot(300, this, [this]() {
        SettingService svc;
        bool isAdmin = (m_user["role"].toString() == "admin");
        bool hasError = false;
        
        if (isAdmin) {
            // 管理员视图：统计卡片 + 操作日志 + 告警列表
            QJsonObject stats = svc.getDashboardStats();
            if (stats.isEmpty()) hasError = true;
            updateStats(stats);
            QJsonArray logs = svc.getRecentLogs(15);
            updateRecentLogs(logs);
            // 告警列表从数据库加载
            QJsonArray alerts = svc.getRecentAlerts(5);
            updateAlerts(alerts);
        } else {
            // 用户视图：加载个人统计数据
            int userId = m_user["userId"].toInt();
            QJsonObject userStats = svc.getUserDashboardStats(userId);
            if (userStats.isEmpty()) hasError = true;
            updateUserStats(userStats);
            // [2026-06-23] 使用getUserBorrowRecords获取用户专属借用记录
            QJsonArray records = svc.getUserBorrowRecords(userId, 10);
            updateUserRecentBorrows(records);
            // [2026-06-23] 归还提醒：筛选status='borrowing'的记录
            QJsonArray reminders;
            for (const auto& r : records) {
                QJsonObject obj = r.toObject();
                if (obj["status"].toString() == "borrowing") {
                    reminders.append(obj);
                }
            }
            updateUserReturnReminders(reminders);
        }
        
        // 隐藏加载状态
        setLoading(false);
        
        // [2026-06-23] 错误状态处理：数据加载失败时显示提示
        if (hasError) {
            qWarning() << "[DashboardPage] refresh: 部分数据加载失败，请检查数据库连接";
        }
    });
}

void DashboardPage::updateStats(const QJsonObject& stats) {
    // [v4.5修复] 非admin用户路径下统计卡片未创建，添加全套null guard
    //   refresh()通过QTimer回调，调用时用户身份可能已变化
    if (!m_toolCountLabel || !m_inStockLabel || !m_borrowedLabel || !m_checkedOutLabel || !m_alertsLabel)
        return;
    // [2026-06-27] 统计改为按工具件数（总件数/在库件数/已借出件数），不再按种类数
    setStatValue(m_toolCountLabel, stats["totalTools"].toInt());
    setStatValue(m_inStockLabel, stats["inStock"].toInt());
    setStatValue(m_borrowedLabel, stats["borrowed"].toInt());
    setStatValue(m_checkedOutLabel, stats["checkedOut"].toInt());  // [V2.03] 已出库
    setStatValue(m_alertsLabel, stats["alerts"].toInt());

    // 更新副标题
    int inStock = stats["inStock"].toInt();
    int borrowed = stats["borrowed"].toInt();
    int alerts = stats["alerts"].toInt();

    // [2026-06-27] 在库比例 = 在库件数 / (在库件数 + 已借出件数)
    int denominator = inStock + borrowed;
    QString stockRate = (denominator > 0)
                        ? QString::number((inStock * 100.0 / denominator), 'f', 1) + "%"
                        : "0%";

    auto* sub1 = findChild<QLabel*>("sub_工具项总数");
    if (sub1) {
        sub1->setText(QStringLiteral("在库率 %1").arg(stockRate));
    }
    auto* sub2 = findChild<QLabel*>("sub_在库工具项");
    if (sub2) {
        sub2->setText(QString("占比 %1").arg(stockRate));
    }
    auto* sub3 = findChild<QLabel*>("sub_已借出");
    if (sub3) sub3->setText(QStringLiteral("当前外借中"));
    auto* sub3b = findChild<QLabel*>("sub_已出库");  // [V2.03]
    if (sub3b) sub3b->setText(QStringLiteral("永久出库"));
    auto* sub4 = findChild<QLabel*>("sub_异常告警");
    if (sub4) {
        sub4->setText(alerts > 0 ? QStringLiteral("需立即处理") : QString());
    }
}

void DashboardPage::setStatValue(QLabel* label, int targetValue) {
    // [V1.00.9 新增] 数字滚动动画
    // [2026-06-24v2] 修复：每次动画从1开始滚动到目标值，而非从当前显示值开始
    if (!label) return;

    QWidget* card = label->parentWidget();
    if (!card) {
        label->setText(QString::number(targetValue));
        return;
    }

    QVariantMap animData = card->property("animData").toMap();
    animData["targetValue"] = targetValue;
    animData["currentValue"] = 1;  // [2026-06-24v2] 始终从1开始滚动
    animData["steps"] = 0;
    animData["maxSteps"] = 20;  // 20步完成动画
    card->setProperty("animData", animData);

    // 启动动画定时器
    QTimer* animTimer = card->findChild<QTimer*>();
    if (!animTimer) {
        animTimer = new QTimer(card);
        animTimer->setInterval(30);  // 30ms刷新一次
        QObject::connect(animTimer, &QTimer::timeout, card, [card, label, animTimer]() {
            QVariantMap data = card->property("animData").toMap();
            if (data.isEmpty()) {
                animTimer->stop();
                return;
            }
            int current = data["currentValue"].toInt();
            int target = data["targetValue"].toInt();
            int steps = data["steps"].toInt();
            int maxSteps = data["maxSteps"].toInt();

            if (steps >= maxSteps) {
                label->setText(QString::number(target));
                card->setProperty("animData", QVariantMap());
                animTimer->stop();
                return;
            }

            int newValue = current + (target - current) * (steps + 1) / maxSteps;
            label->setText(QString::number(newValue));
            data["currentValue"] = newValue;
            data["steps"] = steps + 1;
            card->setProperty("animData", data);
        });
    }
    animTimer->start();
}

// ==================== LoadingWidget 实现 ====================
LoadingWidget::LoadingWidget(QWidget* parent) : QWidget(parent), m_dotCount(0) {
    setMinimumSize(100, 60);
    setStyleSheet("background:transparent;");
    
    m_timer = new QTimer(this);
    m_timer->setInterval(500);  // 500ms切换一次
    connect(m_timer, &QTimer::timeout, this, [this]() {
        m_dotCount = (m_dotCount + 1) % 4;
        update();  // 触发重绘
    });
}

void LoadingWidget::startAnimation() {
    m_dotCount = 0;
    m_timer->start();
    update();
}

void LoadingWidget::stopAnimation() {
    m_timer->stop();
    update();
}

void LoadingWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // 绘制三个脉冲圆点（骨架屏效果）
    int dotSize = 12;
    int spacing = 20;
    int totalWidth = 3 * dotSize + 2 * spacing;
    int startX = (width() - totalWidth) / 2;
    int centerY = height() / 2;
    
    // 绘制"加载中..."文字
    painter.setPen(QPen(QColor("#999"), 1));
    painter.setFont(StyleHelper::getChineseFont(14));  // [2026-06-21] 麒麟适配：使用动态字体检测
    QRect textRect(0, centerY - 30, width(), 20);
    painter.drawText(textRect, Qt::AlignCenter, QStringLiteral("加载中"));
    
    // 绘制三个圆点
    for (int i = 0; i < 3; ++i) {
        int x = startX + i * (dotSize + spacing);
        int alpha = 80;
        
        // 当前激活的点更亮
        if (i == m_dotCount) {
            alpha = 255;
        }
        
        painter.setBrush(QBrush(QColor(77, 163, 255, alpha)));  // #4da3ff主色
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPoint(x + dotSize/2, centerY + 20), dotSize/2, dotSize/2);
    }
}

void DashboardPage::updateRecentLogs(const QJsonArray& logs) {
    // [v4.5修复] 非admin用户路径下m_logTable未创建，必须null guard
    //   即使构造函数初始化为nullptr，refresh()中QTimer回调时用户可能已切换
    if (!m_logTable) return;
    m_logTable->setRowCount(logs.size());
    // [2026-06-25] 设置行高48px触屏标准，确保序号等内容完整显示
    for (int i = 0; i < logs.size(); ++i) {
        m_logTable->setRowHeight(i, 48);
        QJsonObject log = logs[i].toObject();
        // BS端字段：time/user/type/tool/qty/status
        m_logTable->setItem(i, 0, new QTableWidgetItem(log["time"].toString().left(19)));
        m_logTable->setItem(i, 1, new QTableWidgetItem(log["user"].toString()));
        m_logTable->setItem(i, 2, new QTableWidgetItem(log["type"].toString()));
        m_logTable->setItem(i, 3, new QTableWidgetItem(log["tool"].toString()));
        m_logTable->setItem(i, 4, new QTableWidgetItem(QString::number(log["qty"].toInt())));
        QString status = log["status"].toString();
        auto* statusItem = new QTableWidgetItem(status);
        statusItem->setTextAlignment(Qt::AlignCenter);
        if (status == QStringLiteral("已归还")) {
            // [2026-06-21] 添加tag背景色 对齐Vue版 .tag--ok
            statusItem->setForeground(QColor("#389e0d"));
            statusItem->setBackground(QColor("#f6ffed"));
        } else {
            // [2026-06-21] 添加tag背景色 对齐Vue版 .tag--out
            statusItem->setForeground(QColor("#d46b08"));
            statusItem->setBackground(QColor("#fff7e6"));
        }
        m_logTable->setItem(i, 5, statusItem);
    }
}

void DashboardPage::updateAlerts(const QJsonArray& alerts) {
    // [2026-06-23] 从数据库加载真实告警数据，替换硬编码占位
    if (!m_alertList) return;
    
    // 清除旧的告警项
    QLayoutItem* child;
    while ((child = m_alertList->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }
    
    // 更新角标
    int count = alerts.size();
    if (m_alertBadge) {
        m_alertBadge->setText(QString::number(count));
        m_alertBadge->setVisible(count > 0);
    }
    
    if (count == 0) {
        // 无告警时显示占位
        auto* emptyLabel = new QLabel(QStringLiteral("暂无告警"));
        emptyLabel->setStyleSheet("font-size:14px; color:#ccc; padding:20px; background:transparent;");
        emptyLabel->setAlignment(Qt::AlignCenter);
        m_alertList->addWidget(emptyLabel);
        return;
    }
    
    for (int i = 0; i < count; ++i) {
        QJsonObject alert = alerts[i].toObject();
        QString level = alert["alertLevel"].toString();
        QString content = alert["content"].toString();
        QString timeStr = alert["createdAt"].toString();
        // 提取时间 HH:mm
        QDateTime dt = QDateTime::fromString(timeStr, Qt::ISODate);
        if (!dt.isValid()) dt = QDateTime::fromString(timeStr, "yyyy-MM-dd HH:mm:ss");
        QString timeLabel = dt.isValid() ? dt.toString("HH:mm") : timeStr.left(5);
        
        auto* item = new QWidget();
        auto* hbox = new QHBoxLayout(item);
        hbox->setContentsMargins(0, 11, 0, 11);
        hbox->setSpacing(10);
        
        // [2026-06-23] 对齐Web端 .alert-dot: width:8px; height:8px; border-radius:50% 纯色实心圆
        auto* dot = new QLabel();
        dot->setFixedSize(8, 8);
        QString dotColor;
        if (level == "crit" || level == "error") {
            dotColor = "#e74c3c";
        } else if (level == "warn") {
            dotColor = "#fa8c16";
        } else {
            dotColor = "#4da3ff";
        }
        dot->setStyleSheet(QString(
            "border-radius:4px; background:%1; border:none;"
        ).arg(dotColor));
        hbox->addWidget(dot);
        
        auto* contentLabel = new QLabel(content);
        contentLabel->setWordWrap(true);
        contentLabel->setStyleSheet("font-size:14px; color:#333; background:transparent;");
        hbox->addWidget(contentLabel);
        hbox->addStretch();
        
        auto* timeLbl = new QLabel(timeLabel);
        timeLbl->setStyleSheet("font-size:12px; color:#bbb; background:transparent;");
        hbox->addWidget(timeLbl);
        
        m_alertList->addWidget(item);
        
        // 分隔线
        if (i < count - 1) {
            auto* sep = new QFrame();
            sep->setFrameShape(QFrame::HLine);
            sep->setStyleSheet("background:#f5f5f5; max-height:1px;");
            m_alertList->addWidget(sep);
        }
    }
}

// ==================== 用户视图相关方法 ====================

QWidget* DashboardPage::createWelcomeBanner() {
    // 复刻Vue版 .welcome-banner
    auto* banner = new QWidget();
    banner->setStyleSheet(
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #4da3ff,stop:1 #3672d4);"
        "border-radius:14px; padding:24px 32px;"
    );
    // [V6.2] CSS padding已设24px 32px，layout不能再加边距（双重padding导致横幅内容挤压）
    auto* layout = new QHBoxLayout(banner);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(20);

    // 左侧：欢迎文字
    auto* left = new QWidget();
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    auto* welcome = new QLabel(QStringLiteral("欢迎回来，%1").arg(m_user["realName"].toString()));
    welcome->setStyleSheet("font-size:24px; font-weight:700; color:white; background:transparent; margin-bottom:6px;");
    leftLayout->addWidget(welcome);
    auto* info = new QLabel(QStringLiteral("%1 · 工号 %2 · 今日已借用 %3 件工具")
        .arg(m_user["department"].toString())
        .arg(m_user["workNo"].toString())
        .arg("--"));  // [2026-06-23] 初始占位，refresh()后updateUserStats会更新
    info->setObjectName("welcomeInfo");  // 保存objectName供updateUserStats查找更新
    info->setWordWrap(true);  // [2026-06-21] 长部门名/工号拼接文本不裁剪
    info->setStyleSheet("font-size:15px; color:rgba(255,255,255,0.85); background:transparent;");  // [V6.2] 17→15对齐Web .w-left p:15px
    leftLayout->addWidget(info);
    layout->addWidget(left, 1);

    // 右侧：统计数据（动态加载，初始显示占位）
    auto* right = new QWidget();
    auto* rightLayout = new QHBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(20);

    // [2026-06-23] 统计数字标签保存为banner的子控件，方便updateUserStats更新
    QString statNames[3] = {QStringLiteral("今日借用"), QStringLiteral("待归还"), QStringLiteral("本月借用")};
    for (int i = 0; i < 3; ++i) {
        auto* stat = new QWidget();
        auto* statLayout = new QVBoxLayout(stat);
        statLayout->setContentsMargins(0, 0, 0, 0);
        statLayout->setAlignment(Qt::AlignCenter);
        auto* num = new QLabel("--");
        num->setObjectName(QString("userStat_%1").arg(i));
        num->setStyleSheet("font-size:32px; font-weight:800; color:white; background:transparent;");
        statLayout->addWidget(num);
        auto* label = new QLabel(statNames[i]);
        label->setStyleSheet("font-size:13px; color:rgba(255,255,255,0.8); background:transparent;");
        statLayout->addWidget(label);
        rightLayout->addWidget(stat);
    }
    layout->addWidget(right);

    return banner;
}

QWidget* DashboardPage::createFunctionCards() {
    // 复刻Vue版 .func-grid — grid-template-columns: repeat(3, 1fr); gap:14px;
    auto* grid = new QWidget();
    auto* layout = new QGridLayout(grid);
    layout->setSpacing(14);
    layout->setContentsMargins(0, 0, 0, 0);

    struct FuncCard { QString icon; QString title; QString desc; QString path; };
    // [1:1复刻] Vue版 func-grid仅3张卡片: 工具借用/工具归还/工具机组查询
    FuncCard cards[] = {
        {QStringLiteral("📤"), QStringLiteral("工具借用"), QStringLiteral("智能推荐工具清单 · 刷脸认证一键借用"), QStringLiteral("borrow")},
        {QStringLiteral("📥"), QStringLiteral("工具归还"), QStringLiteral("扫描工具快速归还 · 自动识别存放位置"), QStringLiteral("return")},
        {QStringLiteral("🔧"), QStringLiteral("工具机组查询"), QStringLiteral("查询工具库存状态 · 借用记录一览"), QStringLiteral("tools")},
    };

    // [v5.1修复] Web版3卡片一行(grid-template-columns:repeat(3,1fr))，Qt之前用2列(i/2,i%2)是错误的
    for (int i = 0; i < 3; ++i) {
        auto* card = new QPushButton();
        card->setCursor(Qt::PointingHandCursor);
        // [v5.1修复] 移除setMinimumHeight(120)，让内容自适应高度，对齐Web版无min-height
        // [V6.2修复] 双重padding导致内容溢出截断：CSS padding已设32px 20px，layout margins不能再加！
        //   Web版 .func-card: padding:32px 20px, .fc-icon: margin-bottom:12px, .fc-title: margin-bottom:6px
        //   修复：layout margins归零，用个体label的margin-bottom控制间距，避免72px可用高度装不下126px内容
        card->setStyleSheet(
            "QPushButton{background:white; border-radius:14px; border:2px solid transparent;"
            " padding:32px 20px; text-align:center;}"
            "QPushButton:hover{border-color:#4da3ff;}"
            "QPushButton:pressed{transform:scale(0.97);}"
        );
        auto* vbox = new QVBoxLayout(card);
        vbox->setContentsMargins(0, 0, 0, 0);  // CSS padding已负责外边距，layout内不再加
        vbox->setSpacing(0);                   // 用个体margin-bottom而非统一spacing
        vbox->setAlignment(Qt::AlignCenter);
        auto* icon = new QLabel(cards[i].icon);
        icon->setStyleSheet("font-size:48px; background:transparent; margin-bottom:12px;");  // 对齐Web .fc-icon
        icon->setAlignment(Qt::AlignCenter);
        vbox->addWidget(icon);
        auto* title = new QLabel(cards[i].title);
        title->setStyleSheet("font-size:20px; font-weight:700; color:#1a1a2e; background:transparent; margin-bottom:6px;");  // 对齐Web .fc-title
        title->setAlignment(Qt::AlignCenter);
        vbox->addWidget(title);
        auto* desc = new QLabel(cards[i].desc);
        desc->setStyleSheet("font-size:13px; color:#999; background:transparent;");  // [1:1复刻] Vue .fc-desc: font-size:13px
        desc->setAlignment(Qt::AlignCenter);
        desc->setWordWrap(true);
        vbox->addWidget(desc);

        connect(card, &QPushButton::clicked, this, [this, path=cards[i].path]() {
            emit navigateRequested(path);
        });
        // [v5.1修复] 3列布局对齐Web版 grid-template-columns: repeat(3, 1fr)
        layout->addWidget(card, 0, i);  // 全部放第0行，i=0,1,2各占一列
    }
    // [V6.2] 确保3列等宽，防止卡片文字因列宽不均导致截断
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 1);
    layout->setColumnStretch(2, 1);

    return grid;
}

QWidget* DashboardPage::createUserRecentBorrowsPanel() {
    // 复刻Vue版 用户最近借用记录面板
    auto* panel = new QFrame();
    panel->setStyleSheet("background:white; border-radius:12px; border:none;");
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 面板头部
    auto* head = new QWidget();
    head->setStyleSheet("background:transparent; padding:12px 20px; border-bottom:1px solid #f0f0f0;");
    auto* headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel(QStringLiteral("最近借用记录"));
    title->setStyleSheet("font-size:15px; font-weight:700; color:#1a1a2e; background:transparent;");
    headLayout->addWidget(title);
    headLayout->addStretch();
    auto* more = new QPushButton(QStringLiteral("查看全部 →"));
    more->setStyleSheet("border:none; background:transparent; color:#4da3ff; font-size:13px; font-weight:600; padding:6px 12px;");  // [2026-06-21] 16→13对齐Vue
    more->setCursor(Qt::PointingHandCursor);
    more->setMinimumHeight(48);  // [触屏优化] 按钮最小高度48px
    connect(more, &QPushButton::clicked, this, &DashboardPage::onQuickLedger);
    headLayout->addWidget(more);
    layout->addWidget(head);

    // 表格
    m_userRecentTable = new QTableWidget();
    m_userRecentTable->setColumnCount(4);
    m_userRecentTable->setHorizontalHeaderLabels({
        QStringLiteral("时间"), QStringLiteral("工具名称"), QStringLiteral("数量"), QStringLiteral("状态")
    });
    m_userRecentTable->horizontalHeader()->setStretchLastSection(false);
    m_userRecentTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_userRecentTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_userRecentTable->verticalHeader()->setVisible(false);
    m_userRecentTable->setAlternatingRowColors(false);  // [1:1复刻] Vue版无斑马纹
    m_userRecentTable->setMinimumHeight(150);
    // [V2.02 2026-06-28] 移除内联表格QSS，使用全局QSS统一表格样式
    // [2026-06-25] Stretch均分列宽+行高48px触屏优化
    for (int i = 0; i < 4; i++) {
        m_userRecentTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
    }
    m_userRecentTable->horizontalHeader()->setMinimumSectionSize(50);
    layout->addWidget(m_userRecentTable, 1);

    return panel;
}

QWidget* DashboardPage::createUserReturnRemindersPanel() {
    // 复刻Vue版 归还提醒面板
    auto* panel = new QFrame();
    panel->setStyleSheet("background:white; border-radius:12px; border:none;");
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 面板头部
    auto* head = new QWidget();
    head->setStyleSheet("background:transparent; padding:12px 20px; border-bottom:1px solid #f0f0f0;");
    auto* headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel(QStringLiteral("归还提醒"));
    title->setStyleSheet("font-size:15px; font-weight:700; color:#1a1a2e; background:transparent;");
    headLayout->addWidget(title);
    layout->addWidget(head);

    // 表格
    m_userReturnTable = new QTableWidget();
    m_userReturnTable->setColumnCount(4);
    m_userReturnTable->setHorizontalHeaderLabels({
        QStringLiteral("借用时间"), QStringLiteral("工具名称"), QStringLiteral("数量"), QStringLiteral("状态")
    });
    m_userReturnTable->horizontalHeader()->setStretchLastSection(false);
    m_userReturnTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_userReturnTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_userReturnTable->verticalHeader()->setVisible(false);
    m_userReturnTable->setAlternatingRowColors(false);  // [1:1复刻] Vue版无斑马纹
    m_userReturnTable->setMinimumHeight(150);
    // [V2.02 2026-06-28] 移除内联表格QSS，使用全局QSS统一表格样式
    // [2026-06-25] Stretch均分列宽+行高48px触屏优化
    for (int i = 0; i < 4; i++) {
        m_userReturnTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
    }
    m_userReturnTable->horizontalHeader()->setMinimumSectionSize(50);
    layout->addWidget(m_userReturnTable, 1);

    return panel;
}

void DashboardPage::updateUserStats(const QJsonObject& stats) {
    // [2026-06-23] 更新用户欢迎横幅中的统计数据（今日借用/待归还/本月借用）
    // [2026-06-24v2] 用户视图统计数据也加入数字滚动动画
    int values[3] = {
        stats["todayBorrow"].toInt(),
        stats["pendingReturn"].toInt(),
        stats["monthBorrow"].toInt()
    };
    for (int i = 0; i < 3; ++i) {
        QLabel* numLabel = findChild<QLabel*>(QString("userStat_%1").arg(i));
        if (numLabel) {
            setStatValue(numLabel, values[i]);
        }
    }
    
    // [2026-06-23] 同步更新欢迎横幅中"今日已借用 X 件工具"文字
    QLabel* infoLabel = findChild<QLabel*>("welcomeInfo");
    if (infoLabel && !m_user.isEmpty()) {
        infoLabel->setText(QStringLiteral("%1 · 工号 %2 · 今日已借用 %3 件工具")
            .arg(m_user["department"].toString())
            .arg(m_user["workNo"].toString())
            .arg(QString::number(stats["todayBorrow"].toInt())));
    }
}

void DashboardPage::updateUserRecentBorrows(const QJsonArray& records) {
    m_userRecentTable->setRowCount(records.size());
    // [2026-06-25] 行高48px触屏标准
    for (int i = 0; i < records.size(); ++i) {
        m_userRecentTable->setRowHeight(i, 48);
        QJsonObject r = records[i].toObject();
        m_userRecentTable->setItem(i, 0, new QTableWidgetItem(r["borrowTime"].toString()));
        m_userRecentTable->setItem(i, 1, new QTableWidgetItem(r["toolName"].toString()));
        m_userRecentTable->setItem(i, 2, new QTableWidgetItem(QString::number(r["quantity"].toInt())));
        QString status = r["status"].toString();
        // [2026-06-23] 对齐Web端 .tag样式：已归还=tag--ok(绿底), 已借出=tag--out(橙底)
        QString displayStatus = (status == "returned") ? QStringLiteral("已归还") : QStringLiteral("已借出");
        auto* statusItem = new QTableWidgetItem(displayStatus);
        statusItem->setTextAlignment(Qt::AlignCenter);
        if (status == "returned") {
            statusItem->setForeground(QColor("#389e0d"));
            statusItem->setBackground(QColor("#f6ffed"));
        } else {
            statusItem->setForeground(QColor("#d46b08"));
            statusItem->setBackground(QColor("#fff7e6"));
        }
        m_userRecentTable->setItem(i, 3, statusItem);
    }
}

void DashboardPage::updateUserReturnReminders(const QJsonArray& reminders) {
    // [v4.5修复] admin路径下m_userReturnTable未创建，必须null guard
    if (!m_userReturnTable) return;
    m_userReturnTable->setRowCount(reminders.size());
    // [2026-06-25] 行高48px触屏标准
    for (int i = 0; i < reminders.size(); ++i) {
        m_userReturnTable->setRowHeight(i, 48);
        QJsonObject r = reminders[i].toObject();
        m_userReturnTable->setItem(i, 0, new QTableWidgetItem(r["borrowTime"].toString()));
        m_userReturnTable->setItem(i, 1, new QTableWidgetItem(r["toolName"].toString()));
        m_userReturnTable->setItem(i, 2, new QTableWidgetItem(QString::number(r["quantity"].toInt())));
        // [2026-06-23] 对齐Web端 .tag--out: 橙底+橙色文字
        auto* statusItem = new QTableWidgetItem(QStringLiteral("使用中"));
        statusItem->setTextAlignment(Qt::AlignCenter);
        statusItem->setForeground(QColor("#d46b08"));
        statusItem->setBackground(QColor("#fff7e6"));
        m_userReturnTable->setItem(i, 3, statusItem);
    }
}

void DashboardPage::onQuickBorrow() { emit navigateRequested("borrow"); }
void DashboardPage::onQuickReturn() { emit navigateRequested("return"); }
void DashboardPage::onQuickLedger() { emit navigateRequested("ledger"); }
