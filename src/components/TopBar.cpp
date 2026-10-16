/**
 * @file TopBar.cpp
 * @brief 顶部栏实现：品牌标识、系统标题、时钟、用户信息、退出按钮与电量状态
 * @author 袁燕
 */
#include "TopBar.h"
#include "common/AppConfig.h"  // 从AppConfig读取软件版本号
#include "utils/StyleHelper.h"  // 字号/颜色统一走 StyleHelper::Token
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFont>
#include <QFrame>
#include <QDateTime>
#include <QPixmap>
#include <QFile>  // 麒麟电池状态读取

#ifdef Q_OS_WIN
#include <windows.h>  // GetSystemPowerStatus 电池状态
#endif

TopBar::TopBar(QWidget* parent) : QWidget(parent) {
    setupUI();
    m_clockTimer = new QTimer(this);
    connect(m_clockTimer, &QTimer::timeout, this, &TopBar::updateClock);
    m_clockTimer->start(1000);
    updateClock();

    // 电池状态定时器，每15秒刷新一次
    // 电池状态变化较慢，15秒足够；避免频繁API调用影响性能
    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, [this]() {
        updateBatteryStatus();
    });
    m_statusTimer->start(15000);
    updateBatteryStatus();
}

/**
 * @brief 构建顶部栏：品牌标识、系统标题、时钟、用户信息与退出按钮
 */
void TopBar::setupUI() {
    setFixedHeight(72);  // 72 增加触屏友好高度
    setStyleSheet(
        "TopBar { background:#ffffff; border-bottom:2px solid #e8ecf1; }");

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(28, 0, 28, 0);  // 28 微调边距
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignVCenter);

    // ── 左侧：品牌区 ──
    QHBoxLayout* leftLayout = new QHBoxLayout();
    leftLayout->setSpacing(16);

    // Logo图标 替换为成飞电子公司Logo
    QLabel* logoIcon = new QLabel();
    logoIcon->setFixedSize(48, 48);
    logoIcon->setAlignment(Qt::AlignCenter);
    logoIcon->setStyleSheet("background:transparent;");
    QPixmap logoPix(":/resources/logo.png");
    if (!logoPix.isNull()) {
        logoIcon->setPixmap(logoPix.scaled(44, 44, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        logoIcon->setText(QStringLiteral("CF"));
        logoIcon->setStyleSheet(StyleHelper::fontSize(StyleHelper::Token::FontTitle) + " font-weight:bold; color:#1a1a2e; background:transparent;");
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
        "color:#1a1a2e; " + StyleHelper::fontSize(StyleHelper::Token::FontHeading) + " font-weight:bold; "
        "letter-spacing:2px; background:transparent;");
    leftLayout->addWidget(brandTitle);

    // 面包屑
    m_pageTitle = new QLabel();
    m_pageTitle->setStyleSheet(
        "color:#99a0aa; " + StyleHelper::fontSize(StyleHelper::Token::FontBody) + " background:transparent; "
        "margin-left:4px;");
    leftLayout->addWidget(m_pageTitle);

    // "主页"按钮：子页面快速返回功能导航主页（登录页与导航页隐藏，由 MainWindow 控制）
    m_homeBtn = new QPushButton(QStringLiteral("🏠 主页"));
    m_homeBtn->setCursor(Qt::PointingHandCursor);
    m_homeBtn->setFixedHeight(36);
    m_homeBtn->setVisible(false);
    m_homeBtn->setStyleSheet(
        "QPushButton { "
        "  color:#666666; " + StyleHelper::fontSize(StyleHelper::Token::FontBody) + " font-weight:500; "
        "  border:none; border-radius:18px; "
        "  background:transparent; padding:0 18px; "
        "}"
        "QPushButton:hover { "
        "  background:#f0f0f0; color:#333333; "
        "}"
        "QPushButton:pressed { "
        "  background:#e8e8e8; "
        "}");
    connect(m_homeBtn, &QPushButton::clicked, this, &TopBar::homeClicked);
    leftLayout->addWidget(m_homeBtn);

    // 软件版本号标签，放在面包屑后面，蓝色徽章风格
    // 版本号从AppConfig读取（非硬编码），用户可在exe同级system.ini中修改
    m_versionLabel = new QLabel();
    // 小米极简风格：淡灰色文字+左侧细线分隔，不抢眼但精致
    m_versionLabel->setStyleSheet(
        "color:#b0b8c1; font-size:11px; font-weight:400; background:transparent; "
        "margin-left:6px; padding-left:10px; border-left:1px solid #e0e4e8;");
    leftLayout->addWidget(m_versionLabel);
    // 版本号由页面层读取配置后经 setVersionText() 注入（组件不读 AppConfig）

    layout->addLayout(leftLayout);
    layout->addStretch();

    // ── 右侧：时钟 + 用户信息 + 退出 ──
    // 用容器包装，登录页隐藏
    // 小米/Apple极简风格重设计：
    // 去掉所有"·"分隔点和竖线，按钮去边框改hover背景，增加呼吸感
    m_rightArea = new QWidget();
    m_rightArea->setStyleSheet("background:transparent;");
    QHBoxLayout* rightLayout = new QHBoxLayout(m_rightArea);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(24);  // 增加间距，呼吸感更好

    // 电池电量+网络状态指示器 — 小米极简美学
    // 设计理念：小图标+文字，淡色不抢眼，状态变化时颜色提醒
    // 放在时钟左侧，与用户信息区自然分隔

    // 电池电量指示器
    m_batteryLabel = new QLabel();
    m_batteryLabel->setStyleSheet(
        "color:#8a8a8a; " + StyleHelper::fontSize(StyleHelper::Token::FontSmall) + "background:transparent; font-weight:400;"
        "padding:4px 10px; border-radius:14px;");
    m_batteryLabel->setAlignment(Qt::AlignCenter);
    rightLayout->addWidget(m_batteryLabel);

    // 实时时钟 — 淡灰色小字，弱化不抢眼，不带任何装饰背景
    m_clockLabel = new QLabel();
    m_clockLabel->setStyleSheet(
        "color:#8a8a8a; " + StyleHelper::fontSize(StyleHelper::Token::FontSmall) + "background:transparent; font-weight:400;");
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
        "color:#ffffff; " + StyleHelper::fontSize(StyleHelper::Token::FontInput) + " font-weight:600; "
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
        "color:#1a1a2e; " + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + " font-weight:600; background:transparent;");

    m_userDeptLabel = new QLabel();
    m_userDeptLabel->setStyleSheet(
        "color:#8a8a8a; " + StyleHelper::fontSize(StyleHelper::Token::FontCaption) + " background:transparent;");

    userTextLayout->addWidget(m_userNameLabel);
    userTextLayout->addWidget(m_userDeptLabel);
    userLayout->addWidget(userTextWidget);

    rightLayout->addLayout(userLayout);

    // 小米极简按钮：无边框纯文字，hover时浅色背景胶囊形
    // 帮助按钮：与注销同款灰色样式，紧邻退出按钮，点击全屏打开操作手册
    QPushButton* helpBtn = new QPushButton(QStringLiteral("帮助"));
    helpBtn->setCursor(Qt::PointingHandCursor);
    helpBtn->setFixedHeight(36);
    helpBtn->setStyleSheet(
        "QPushButton { "
        "  color:#666666; " + StyleHelper::fontSize(StyleHelper::Token::FontBody) + " font-weight:500; "
        "  border:none; border-radius:18px; "
        "  background:transparent; padding:0 18px; "
        "}"
        "QPushButton:hover { "
        "  background:#f0f0f0; color:#333333; "
        "}"
        "QPushButton:pressed { "
        "  background:#e8e8e8; "
        "}");
    connect(helpBtn, &QPushButton::clicked, this, &TopBar::helpClicked);
    rightLayout->addWidget(helpBtn);

    // 注销按钮：灰色文字，hover浅灰背景
    QPushButton* logoutBtn = new QPushButton(QStringLiteral("注销"));
    logoutBtn->setCursor(Qt::PointingHandCursor);
    logoutBtn->setFixedHeight(36);
    logoutBtn->setStyleSheet(
        "QPushButton { "
        "  color:#666666; " + StyleHelper::fontSize(StyleHelper::Token::FontBody) + " font-weight:500; "
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
        "  color:#e74c3c; " + StyleHelper::fontSize(StyleHelper::Token::FontBody) + " font-weight:600; "
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

/**
 * @brief 设置用户名称
 */
void TopBar::setUserName(const QString& name) {
    m_userNameLabel->setText(name);
    if (!name.isEmpty()) {
        m_userAvatar->setText(name.left(1));
    } else {
        m_userAvatar->setText("?");
    }
}

/**
 * @brief 设置部门
 */
void TopBar::setDepartment(const QString& dept) {
    m_userDeptLabel->setText(dept);
}

/**
 * @brief 设置页面标题
 */
void TopBar::setPageTitle(const QString& title) {
    if (title.isEmpty()) {
        m_pageTitle->setText("");
    } else {
        m_pageTitle->setText(QStringLiteral("  /  ") + title);
    }
}

/**
 * @brief 刷新顶栏时钟与日期显示
 */
void TopBar::updateClock() {
    // 显示格式：2026-06-21 19:01:05
    m_clockLabel->setText(
        QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
}

/// 更新电池电量显示（跨平台：Windows API + 麒麟sysfs）
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
    // 麒麟/Linux：读取sysfs电池信息
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
        "color:%1; " + StyleHelper::fontSize(StyleHelper::Token::FontSmall) + "background:transparent; font-weight:500;"
        "padding:4px 10px; border-radius:14px;").arg(color));
}

/// 登录页隐藏右侧用户信息区（时间/用户名/退出）
void TopBar::setUserAreaVisible(bool visible) {
    if (m_rightArea) m_rightArea->setVisible(visible);
}

/// 控制"主页"按钮显隐（登录页与功能导航主页隐藏，子页面显示）
void TopBar::setHomeButtonVisible(bool visible) {
    if (m_homeBtn) m_homeBtn->setVisible(visible);
}

/// 外部注入版本号显示（页面层负责读配置，组件层只负责显示）
void TopBar::setVersionText(const QString& version) {
    if (!m_versionLabel) return;
    m_versionLabel->setText(version);
}
