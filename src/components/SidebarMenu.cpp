// 作者：袁燕  智能柜Qt Widget 2.0  SidebarMenu实现
// 日期：2026-06-21  1:1复刻Web前端SideBar.vue：250px深色#1a1a2e
#include "SidebarMenu.h"
#include <QFont>
#include <QSpacerItem>
#include <QFrame>
#include <QPixmap>

SidebarMenu::SidebarMenu(QWidget* parent) : QWidget(parent) {
    setupUI();
}

void SidebarMenu::setupUI() {
    setObjectName("sidebarMenu");
    setFixedWidth(250);
    setStyleSheet(
        "#sidebarMenu { background:#1a1a2e; }"
        "#sidebarMenu QPushButton { text-align:left; padding:15px 20px; border:none; "
        "color:#a0a0b8; font-size:16px; border-radius:0; min-height:48px; "
        "border-left:3px solid transparent; }"
        "#sidebarMenu QPushButton:hover { background:#1e2a4a; color:#d0d0e0; }"
        "#sidebarMenu QPushButton:checked { background:rgba(77,163,255,0.12); color:#ffffff; "
        "border-left:3px solid #4da3ff; }"
    );

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    // Logo区域 (72px高度，匹配Web设计)
    QWidget* logoArea = new QWidget(this);
    logoArea->setFixedHeight(72);
    logoArea->setStyleSheet("background:#151528;");
    QHBoxLayout* logoLayout = new QHBoxLayout(logoArea);
    logoLayout->setContentsMargins(20, 0, 20, 0);
    // [2026-06-23] 替换为成飞电子公司Logo
    QLabel* logoIcon = new QLabel(logoArea);
    logoIcon->setFixedSize(32, 32);
    logoIcon->setAlignment(Qt::AlignCenter);
    logoIcon->setStyleSheet("background:transparent;");
    QPixmap logoPix(":/resources/logo.png");
    if (!logoPix.isNull()) {
        logoIcon->setPixmap(logoPix.scaled(28, 28, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        logoIcon->setText(QStringLiteral("CF"));
        logoIcon->setStyleSheet("font-size:14px; font-weight:bold; color:#ffffff; background:transparent;");
    }
    logoLayout->addWidget(logoIcon);
    QLabel* logoText = new QLabel(QStringLiteral("智能工具柜"), logoArea);
    logoText->setStyleSheet("color:#ffffff; font-size:20px; font-weight:bold; background:transparent;");
    logoLayout->addWidget(logoText);
    logoLayout->addStretch();
    m_layout->addWidget(logoArea);

    // 分隔线
    QFrame* sep = new QFrame(this);
    sep->setFixedHeight(1);
    sep->setStyleSheet("background:#2a2a4e; border:none; margin:8px 16px;");
    m_layout->addWidget(sep);

    // 导航按钮（11项，匹配Web路由）
    struct NavItem { QString text; int idx; };
    QList<NavItem> items = {
        {QStringLiteral("📊  系统概览"), 1},    // dashboard
        {QStringLiteral("👥  人员管理"), 2},    // users (admin)
        {QStringLiteral("🔧  工具管理"), 3},    // tools
        {QStringLiteral("📤  工具借用"), 4},    // borrow
        {QStringLiteral("📥  工具归还"), 5},    // return
        {QStringLiteral("📦  工具入库"), 6},    // checkin (admin)
        {QStringLiteral("📋  工具出库"), 7},    // checkout (admin)
        {QStringLiteral("📈  台账统计"), 8},    // ledger (admin)
        {QStringLiteral("🔔  告警日志"), 9},    // alerts
        {QStringLiteral("⚙️  系统设置"), 10},   // settings (admin)
        // [v4.9修复] 移除独立人脸录入菜单——Web端人脸录入在UserManagement页面弹窗内完成
    };

    for (auto& item : items) {
        QPushButton* btn = new QPushButton(item.text, this);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setProperty("pageIdx", item.idx);
        int idx = item.idx;
        connect(btn, &QPushButton::clicked, this, [this, idx]() {
            emit pageSelected(idx);
        });
        m_layout->addWidget(btn);
        m_navBtns.append(btn);
    }

    m_layout->addStretch();

    // 底部用户信息区
    m_userInfoWidget = new QWidget(this);
    m_userInfoWidget->setStyleSheet("background:#151528; border-top:1px solid #2a2a4e; padding:12px 16px;");
    QVBoxLayout* uil = new QVBoxLayout(m_userInfoWidget);
    uil->setSpacing(4);
    m_userNameLabel = new QLabel(QStringLiteral("未登录"));
    m_userNameLabel->setStyleSheet("color:#ffffff; font-size:14px; font-weight:bold; background:transparent;");
    m_userRoleLabel = new QLabel();
    m_userRoleLabel->setStyleSheet("color:#888888; font-size:12px; background:transparent;");
    uil->addWidget(m_userNameLabel);
    uil->addWidget(m_userRoleLabel);
    m_layout->addWidget(m_userInfoWidget);
}

void SidebarMenu::setUserInfo(const QString& name, const QString& role) {
    m_userNameLabel->setText(name.isEmpty() ? QStringLiteral("未登录") : name);
    m_userRoleLabel->setText(role);
}

void SidebarMenu::setActivePage(int pageIdx) {
    for (auto* btn : m_navBtns) {
        btn->setChecked(btn->property("pageIdx").toInt() == pageIdx);
    }
}

void SidebarMenu::filterByRole(const QString& role) {
    bool isAdmin = (role == "admin");
    for (auto* btn : m_navBtns) {
        int idx = btn->property("pageIdx").toInt();
        // 普通用户可访问: dashboard(1), tools(3), borrow(4), return(5), alerts(9)
        bool isUserPage = (idx == 1 || idx == 3 || idx == 4 || idx == 5 || idx == 9);
        // 管理员可访问全部
        if (isAdmin || isUserPage) {
            btn->setVisible(true);
        } else {
            btn->setVisible(false);
        }
    }
}
