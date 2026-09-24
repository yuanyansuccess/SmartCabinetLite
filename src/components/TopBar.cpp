// 作者：袁燕  智能柜Qt Widget 2.0  TopBar实现
// 日期：2026-06-21 1:1复刻Web前端TopBar.vue
// [2026-06-21] 增强：底部分隔线+大号退出按钮+时间用户名清晰显示
// [2026-06-23] 退出按钮改为"退出系统"：发射exitSystemClicked信号退出整个应用（非注销）
// [V2.03 2026-06-29] 新增电池电量+网络状态指示器（小米极简美学，不抢眼但清晰）
// [V2.03b 2026-06-29] 网络检测改为数据库连接状态（网卡Up≠联网），电池增加麒麟支持
#include "TopBar.h"
#include "common/AppConfig.h"  // [2026-06-27] 从AppConfig读取软件版本号
#include "common/DatabaseManager.h"  // [V2.03b] 网络状态检测用数据库连接
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFont>
#include <QFrame>
#include <QDateTime>
#include <QPixmap>
#include <QNetworkInterface>  // [V2.03] 网络状态检测
#include <QFile>              // [V2.03b] 麒麟电池状态读取

#ifdef Q_OS_WIN
#include <windows.h>  // [V2.03] GetSystemPowerStatus 电池状态
#endif

TopBar::TopBar(QWidget* parent) : QWidget(parent) {
    setupUI();
    m_clockTimer = new QTimer(this);
    connect(m_clockTimer, &QTimer::timeout, this, &TopBar::updateClock);
    m_clockTimer->start(1000);
    updateClock();

    // [V2.03] 电池+网络状态定时器，每30秒刷新一次
    // 电池和网络状态变化较慢，30秒足够；避免频繁API调用影响性能
    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, [this]() {
        updateBatteryStatus();
        updateNetworkStatus();
    });
    m_statusTimer->start(15000);  // [V2.03l] 30s→15s 更快响应网络变化
    updateBatteryStatus();
    updateNetworkStatus();
}

void TopBar::setupUI() {
    setFixedHeight(72);  // [2026-06-21] 68→72 增加触屏友好高度
    setStyleSheet(
        "TopBar { background:#ffffff; border-bottom:2px solid #e8ecf1; }");

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(28, 0, 28, 0);  // [2026-06-21] 32→28 微调边距
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignVCenter);

    // ── 左侧：品牌区 ──
    QHBoxLayout* leftLayout = new QHBoxLayout();
    leftLayout->setSpacing(16);

    // Logo图标 [2026-06-23] 替换为成飞电子公司Logo
    QLabel* logoIcon = new QLabel();
    logoIcon->setFixedSize(48, 48);
    logoIcon->setAlignment(Qt::AlignCenter);
    logoIcon->setStyleSheet("background:transparent;");
    QPixmap logoPix(":/resources/logo.png");
    if (!logoPix.isNull()) {
        logoIcon->setPixmap(logoPix.scaled(44, 44, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        logoIcon->setText(QStringLiteral("CF"));
        logoIcon->setStyleSheet("font-size:18px; font-weight:bold; color:#1a1a2e; background:transparent;");
    }
    leftLayout->addWidget(logoIcon);

    // 分隔线
    QFrame* divider = new QFrame();
    divider->setFrameShape(QFrame::VLine);
    divider->setFixedSize(2, 32);
    divider->setStyleSheet("background:#e0e4e8; border:none;");
    leftLayout->addWidget(divider);

    // 品牌标题
    QLabel* brandTitle = new QLabel(QStringLiteral("智能工具柜管理系统"));
    brandTitle->setStyleSheet(
        "color:#1a1a2e; font-size:20px; font-weight:bold; "
        "letter-spacing:2px; background:transparent;");
    leftLayout->addWidget(brandTitle);

    // 面包屑
    m_pageTitle = new QLabel();
    m_pageTitle->setStyleSheet(
        "color:#99a0aa; font-size:14px; background:transparent; "
        "margin-left:4px;");
    leftLayout->addWidget(m_pageTitle);

    // [2026-06-27] 软件版本号标签，放在面包屑后面，蓝色徽章风格
    // 版本号从AppConfig读取（非硬编码），用户可在exe同级system.ini中修改
    m_versionLabel = new QLabel();
    // [2026-06-27] 小米极简风格：淡灰色文字+左侧细线分隔，不抢眼但精致
    m_versionLabel->setStyleSheet(
        "color:#b0b8c1; font-size:11px; font-weight:400; background:transparent; "
        "margin-left:6px; padding-left:10px; border-left:1px solid #e0e4e8;");
    leftLayout->addWidget(m_versionLabel);
    refreshVersionLabel();  // [2026-06-27] 从AppConfig读取版本号显示

    layout->addLayout(leftLayout);
    layout->addStretch();

    // ── 右侧：时钟 + 用户信息 + 退出 ──
    // [2026-06-21] 用容器包装，登录页隐藏
    // [2026-06-27] 小米/Apple极简风格重设计：
    // 去掉所有"·"分隔点和竖线，按钮去边框改hover背景，增加呼吸感
    m_rightArea = new QWidget();
    m_rightArea->setStyleSheet("background:transparent;");
    QHBoxLayout* rightLayout = new QHBoxLayout(m_rightArea);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(24);  // 增加间距，呼吸感更好

    // [V2.03 2026-06-29] 电池电量+网络状态指示器 — 小米极简美学
    // 设计理念：小图标+文字，淡色不抢眼，状态变化时颜色提醒
    // 放在时钟左侧，与用户信息区自然分隔

    // 电池电量指示器
    m_batteryLabel = new QLabel();
    m_batteryLabel->setStyleSheet(
        "color:#8a8a8a; font-size:13px; background:transparent; font-weight:400;"
        "padding:4px 10px; border-radius:14px;");
    m_batteryLabel->setAlignment(Qt::AlignCenter);
    rightLayout->addWidget(m_batteryLabel);

    // 网络状态指示器
    m_networkLabel = new QLabel();
    m_networkLabel->setStyleSheet(
        "color:#8a8a8a; font-size:13px; background:transparent; font-weight:400;"
        "padding:4px 10px; border-radius:14px;");
    m_networkLabel->setAlignment(Qt::AlignCenter);
    rightLayout->addWidget(m_networkLabel);

    // 实时时钟 — 淡灰色小字，弱化不抢眼，不带任何装饰背景
    m_clockLabel = new QLabel();
    m_clockLabel->setStyleSheet(
        "color:#8a8a8a; font-size:13px; background:transparent; font-weight:400;");
    m_clockLabel->setAlignment(Qt::AlignCenter);
    rightLayout->addWidget(m_clockLabel);

    // 用户信息区：头像 + 名字/部门双行，自然衔接无需分隔点
    QHBoxLayout* userLayout = new QHBoxLayout();
    userLayout->setSpacing(10);

    // 用户头像（渐变圆形，36px精致尺寸）
    m_userAvatar = new QLabel();
    m_userAvatar->setFixedSize(36, 36);
    m_userAvatar->setAlignment(Qt::AlignCenter);
    m_userAvatar->setStyleSheet(
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        "stop:0 #4da3ff,stop:1 #6c5ce7);"
        "color:#ffffff; font-size:16px; font-weight:600; "
        "border-radius:18px;");
    userLayout->addWidget(m_userAvatar);

    // 用户名+部门双行，紧凑布局
    QWidget* userTextWidget = new QWidget();
    userTextWidget->setStyleSheet("background:transparent;");
    QVBoxLayout* userTextLayout = new QVBoxLayout(userTextWidget);
    userTextLayout->setContentsMargins(0, 0, 0, 0);
    userTextLayout->setSpacing(1);

    m_userNameLabel = new QLabel(QStringLiteral("未登录"));
    m_userNameLabel->setStyleSheet(
        "color:#1a1a2e; font-size:15px; font-weight:600; background:transparent;");

    m_userDeptLabel = new QLabel();
    m_userDeptLabel->setStyleSheet(
        "color:#8a8a8a; font-size:12px; background:transparent;");

    userTextLayout->addWidget(m_userNameLabel);
    userTextLayout->addWidget(m_userDeptLabel);
    userLayout->addWidget(userTextWidget);

    rightLayout->addLayout(userLayout);

    // [2026-06-27] 小米极简按钮：无边框纯文字，hover时浅色背景胶囊形
    // 注销按钮：灰色文字，hover浅灰背景
    QPushButton* logoutBtn = new QPushButton(QStringLiteral("注销"));
    logoutBtn->setCursor(Qt::PointingHandCursor);
    logoutBtn->setFixedHeight(36);
    logoutBtn->setStyleSheet(
        "QPushButton { "
        "  color:#666666; font-size:14px; font-weight:500; "
        "  border:none; border-radius:18px; "
        "  background:transparent; padding:0 18px; "
        "}"
        "QPushButton:hover { "
        "  background:#f0f0f0; color:#333333; "
        "}"
        "QPushButton:pressed { "
        "  background:#e8e8e8; "
        "}");
    connect(logoutBtn, &QPushButton::clicked, this, &TopBar::logoutClicked);
    rightLayout->addWidget(logoutBtn);

    // 退出按钮：红色文字强调，hover红色背景白字
    QPushButton* exitBtn = new QPushButton(QStringLiteral("退出"));
    exitBtn->setCursor(Qt::PointingHandCursor);
    exitBtn->setFixedHeight(36);
    exitBtn->setStyleSheet(
        "QPushButton { "
        "  color:#e74c3c; font-size:14px; font-weight:600; "
        "  border:none; border-radius:18px; "
        "  background:transparent; padding:0 18px; "
        "}"
        "QPushButton:hover { "
        "  background:#e74c3c; color:#ffffff; "
        "}"
        "QPushButton:pressed { "
        "  background:#c0392b; color:#ffffff; "
        "}");
    connect(exitBtn, &QPushButton::clicked, this, &TopBar::exitSystemClicked);
    rightLayout->addWidget(exitBtn);

    layout->addWidget(m_rightArea);
}

void TopBar::setUserName(const QString& name) {
    m_userNameLabel->setText(name);
    if (!name.isEmpty()) {
        m_userAvatar->setText(name.left(1));
    } else {
        m_userAvatar->setText("?");
    }
}

void TopBar::setDepartment(const QString& dept) {
    m_userDeptLabel->setText(dept);
}

void TopBar::setPageTitle(const QString& title) {
    if (title.isEmpty()) {
        m_pageTitle->setText("");
    } else {
        m_pageTitle->setText(QStringLiteral("  /  ") + title);
    }
}

void TopBar::updateClock() {
    // [2026-06-21] 显示格式：2026-06-21 19:01:05
    m_clockLabel->setText(
        QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
}

/// [V2.03b 2026-06-29] 更新电池电量显示（跨平台：Windows API + 麒麟sysfs）
/// Windows: GetSystemPowerStatus
/// 麒麟/Linux: 读取 /sys/class/power_supply/BAT0/capacity 和 status
/// 电量<20%红色警告，20-50%橙色，>50%灰色正常，充电中蓝色
void TopBar::updateBatteryStatus() {
    if (!m_batteryLabel) return;

    int percent = -1;     // -1表示未知
    bool charging = false;

#ifdef Q_OS_WIN
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
        percent = sps.BatteryLifePercent;
        if (percent > 100) percent = -1;
        charging = (sps.ACLineStatus == 1);
    }
#else
    // [V2.03b] 麒麟/Linux：读取sysfs电池信息
    QFile capFile("/sys/class/power_supply/BAT0/capacity");
    if (capFile.open(QIODevice::ReadOnly)) {
        percent = capFile.readAll().trimmed().toInt();
        capFile.close();
    }
    QFile statusFile("/sys/class/power_supply/BAT0/status");
    if (statusFile.open(QIODevice::ReadOnly)) {
        QString status = statusFile.readAll().trimmed();
        charging = (status == "Charging" || status == "Full");
        statusFile.close();
    }
#endif

    // 格式化显示
    QString percentStr = (percent >= 0 && percent <= 100) ? QString::number(percent) + "%" : QStringLiteral("--");
    QString icon;
    QString color;

    if (charging) {
        icon = QStringLiteral("⚡");
        color = (percent >= 80) ? "#43a047" : "#4da3ff";  // 充电中：高电量绿/低电量蓝
    } else if (percent < 0) {
        icon = QStringLiteral("🔋");
        color = "#8a8a8a";
    } else if (percent < 20) {
        icon = QStringLiteral("🔋");
        color = "#e74c3c";  // 低电量红色警告
    } else if (percent < 50) {
        icon = QStringLiteral("🔋");
        color = "#fa8c16";  // 中等电量橙色提醒
    } else {
        icon = QStringLiteral("🔋");
        color = "#8a8a8a";  // 正常灰色
    }

    m_batteryLabel->setText(QStringLiteral("%1 %2").arg(icon, percentStr));
    m_batteryLabel->setStyleSheet(QString(
        "color:%1; font-size:13px; background:transparent; font-weight:500;"
        "padding:4px 10px; border-radius:14px;").arg(color));
}

/// 更新网络连接状态显示
/// 改为检测真实物理网络连接（DatabaseManager::isNetworkConnected），
/// 不再用数据库连接（isConnected）作为代理判断，
/// 物理网卡断开时立即显示"未联网"
void TopBar::updateNetworkStatus() {
    if (!m_networkLabel) return;

    bool networkOk = DatabaseManager::instance().isNetworkConnected();

    if (networkOk) {
        m_networkLabel->setText(QStringLiteral("🟢 已联网"));
        m_networkLabel->setStyleSheet(
            "color:#43a047; font-size:13px; background:transparent; font-weight:500;"
            "padding:4px 10px; border-radius:14px;");
    } else {
        m_networkLabel->setText(QStringLiteral("🔴 未联网"));
        m_networkLabel->setStyleSheet(
            "color:#e74c3c; font-size:13px; background:transparent; font-weight:500;"
            "padding:4px 10px; border-radius:14px;");
    }
}

/// [2026-06-21] 登录页隐藏右侧用户信息区（时间/用户名/退出）
void TopBar::setUserAreaVisible(bool visible) {
    if (m_rightArea) m_rightArea->setVisible(visible);
}

/// [2026-06-27] 更新版本号显示（从AppConfig读取system.ini中的System/version）
/// 版本号格式示例：V2.00，显示在TopBar左侧品牌区后面
void TopBar::refreshVersionLabel() {
    if (!m_versionLabel) return;
    QString version = AppConfig::instance().appVersion();
    m_versionLabel->setText(version);
}
