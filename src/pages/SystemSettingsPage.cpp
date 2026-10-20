/**
 * @file SystemSettingsPage.cpp
 * @brief 系统设置页面实现 - 1:1复刻BS端SystemSettings.vue
 * @author 袁燕
 *
 * 实现系统设置页各项设置功能，与 BS 端功能对齐
 * 布局：选项卡+双列网格（网络配置/告警参数/借还设置/备份管理）
 * 顶部工具栏：保存全部设置按钮
 */

#include "SystemSettingsPage.h"
#include "utils/StyleHelper.h"
#include "components/FormFactory.h"  // 表单控件工厂（收敛重复lambda）
#include "common/AppConfig.h"  // 配置读写
#include "common/Constants.h"  // SC::BACKUP_PATH 等常量
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include "components/MessageDialog.h"
#include <QDebug>
#include <QFrame>
#include <QSpinBox>
#include <QSlider>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QShowEvent>  // 页面切换还原
#include <QHideEvent>
#include <QHostAddress>  // 网络配置IP格式校验
#include <QTimer>        // 自动备份定时调度
#include <QDate>         // 自动备份按日期去重


/**
 * @brief 构造函数，初始化 SystemSettingsPage 对象并连接信号槽
 * @param parent 父对象指针，作为 Qt 对象树父节点接管所有权
 */
SystemSettingsPage::SystemSettingsPage(QWidget* parent) : QWidget(parent) {
    setupUI();
}

/**
 * @brief 构建页面界面：读取 .ui 静态布局并补充动态控件
 */
void SystemSettingsPage::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);  // 24对齐其他页面
    mainLayout->setSpacing(16);  // 16对齐其他页面

    // 标题栏：对齐其他页面 20px标题 + 蓝色实底主操作按钮
    auto* titleBar = new QHBoxLayout();
    titleBar->setContentsMargins(0, 0, 0, 0);
    titleBar->setSpacing(12);
    auto* title = new QLabel(QStringLiteral("系统设置"));
    title->setStyleSheet("" + StyleHelper::fontSize(StyleHelper::Token::FontHeading) + ";font-weight:700;color:#1a1a2e;background:transparent;");  // 20对齐
    titleBar->addWidget(title);
    titleBar->addStretch();

    // 操作按钮统一风格：主操作蓝色实底
    m_saveAllBtn = new QPushButton(QStringLiteral("保存全部设置"));
    m_saveAllBtn->setStyleSheet(
        "QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:10px;"
        "padding:10px 22px;" + StyleHelper::fontSize(StyleHelper::Token::FontBody) + ";font-weight:700;}"
        "QPushButton:hover{background:#3d8ae0;}"
        "QPushButton:pressed{background:#2e7bd6;}"
    );
    m_saveAllBtn->setCursor(Qt::PointingHandCursor);
    connect(m_saveAllBtn, &QPushButton::clicked, this, &SystemSettingsPage::onSaveAll);

    titleBar->addWidget(m_saveAllBtn);
    mainLayout->addLayout(titleBar);

    // 选项卡栏：对齐借用页QTabWidget风格（灰底白选中+主色下划线）
    auto* tabContainer = new QFrame();
    tabContainer->setAttribute(Qt::WA_StyledBackground, true);
    tabContainer->setStyleSheet(
        "QFrame{background:#fff;border-radius:12px;border:1px solid #f0f0f0;}"
    );
    auto* tabBar = new QHBoxLayout(tabContainer);
    tabBar->setSpacing(4);
    tabBar->setContentsMargins(5, 5, 5, 5);

    QStringList tabLabels = {QStringLiteral("网络配置"), QStringLiteral("告警参数"),
                            QStringLiteral("借还设置"), QStringLiteral("任务配置"),
                            QStringLiteral("备份管理")};
    m_tabLabels.clear();
    for (int i = 0; i < tabLabels.size(); ++i) {
        auto* tab = new QLabel(tabLabels[i]);
        tab->setProperty("tabIndex", i);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setAlignment(Qt::AlignCenter);
        tab->installEventFilter(this);
        m_tabLabels.append(tab);
        //yy 隐藏告警日志
        if (i != 1)
        {
            tabBar->addWidget(tab);
        }        
    }
    tabBar->addStretch();
    mainLayout->addWidget(tabContainer);
    m_activeTabIndex = 0;
    updateTabStyles();

    // QStackedWidget切换，去掉滚动
    // 备份面板内容多，单独包QScrollArea避免字体压扁
    m_stackedWidget = new QStackedWidget();
    m_stackedWidget->addWidget(m_networkPanel = createNetworkPanel());
    m_stackedWidget->addWidget(m_alertPanel = createAlertPanel());
    m_stackedWidget->addWidget(m_borrowPanel = createBorrowPanel());
    // 任务配置面板：数据来自数据库，面板构造时立即加载
    m_stackedWidget->addWidget(createTaskConfigPanel());
    // 备份面板内容多，用QScrollArea包裹
    auto* backupScroll = new QScrollArea();
    backupScroll->setWidgetResizable(true);
    backupScroll->setFrameShape(QFrame::NoFrame);
    backupScroll->setStyleSheet("QScrollArea{background:transparent;border:none;}"
                                "QScrollBar:vertical{width:8px;background:#f5f5f5;border-radius:4px;}"
                                "QScrollBar::handle:vertical{background:#ccc;border-radius:4px;min-height:40px;}"
                                "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0px;}");
    m_backupPanel = createBackupPanel();
    m_backupPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    backupScroll->setWidget(m_backupPanel);
    m_stackedWidget->addWidget(backupScroll);
    mainLayout->addWidget(m_stackedWidget, 1);

    // 默认显示第一个Tab
    m_activeTabIndex = 0;
    updateTabStyles();

    // 启动自动备份调度（每小时轮询，按设定的周期+上次备份日期判断是否需要备份）
    initBackupScheduler();
}

/**
 * @brief 按当前选中项刷新各选项卡的样式
 */
void SystemSettingsPage::updateTabStyles() {
    // Tab样式对齐借用页QTabWidget：灰底白选中+主色下划线，去渐变胶囊
    for (int i = 0; i < m_tabLabels.size(); ++i) {
        if (i == m_activeTabIndex) {
            m_tabLabels[i]->setStyleSheet(
                QString("" + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + ";font-weight:600;padding:10px 24px;border-radius:10px 10px 0 0;"
                        "color:%1;background:#fff;min-height:44px;"
                        "border-bottom:3px solid %1;")
                .arg(StyleHelper::primaryColor()));
        } else {
            m_tabLabels[i]->setStyleSheet(StyleHelper::panelTitleBar());
        }
    }
}

/**
 * @brief switchTab（QStackedWidget切换）
 * @param index 索引编号
 */
void SystemSettingsPage::switchTab(int index) {
    if (index < 0 || index >= m_tabLabels.size()) return;
    m_activeTabIndex = index;
    updateTabStyles();
    if (m_stackedWidget) {
        m_stackedWidget->setCurrentIndex(index);
    }
}

/**
 * @brief 事件过滤器：拦截控件与窗口事件并转交专用处理
 * @param watched 参数 watched（类型与取值约束见函数签名）
 * @param event 事件对象，由 Qt 事件系统传入
 * @return 操作是否成功（true=成功，false=失败）
 */
bool SystemSettingsPage::eventFilter(QObject* watched, QEvent* event) {
    // 简化：Tab点击切换QStackedWidget + Hover效果，去掉滚动和焦点高亮
    if (event->type() == QEvent::MouseButtonPress) {
        for (int i = 0; i < m_tabLabels.size(); ++i) {
            if (watched == m_tabLabels[i]) {
                switchTab(i);
                break;
            }
        }
    } else if (event->type() == QEvent::Enter) {
        for (int i = 0; i < m_tabLabels.size(); ++i) {
            if (watched == m_tabLabels[i] && i != m_activeTabIndex) {
                m_tabLabels[i]->setStyleSheet(
                    QString("" + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + ";font-weight:600;padding:10px 24px;border-radius:10px 10px 0 0;"
                            "color:%1;background:#e8f0fe;min-height:44px;")
                        .arg(StyleHelper::primaryColor())
                );
                break;
            }
        }
    } else if (event->type() == QEvent::Leave) {
        for (int i = 0; i < m_tabLabels.size(); ++i) {
            if (watched == m_tabLabels[i] && i != m_activeTabIndex) {
                m_tabLabels[i]->setStyleSheet(StyleHelper::panelTitleBar());
                break;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ==================== 网络配置面板 ====================
// 小米工程师优化：标签36px高/form 8px间距/面板24,12边距，紧凑美观大气

/**
 * @brief ==================== 告警参数面板 ====================
 * @return 构建并返回的界面控件，由调用方负责布局到页面
 */
QWidget* SystemSettingsPage::createAlertPanel() {
    auto* panel = new QFrame();
    panel->setObjectName("alertPanel");
    panel->setStyleSheet(QString("QFrame#alertPanel{background:white;border-radius:12px;border:none;}"));
    auto* layout = new QVBoxLayout(panel);
    layout->setSpacing(6);
    layout->setContentsMargins(20, 10, 20, 10);

    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("告警参数配置"));
    title->setStyleSheet(StyleHelper::sectionTitle());
    titleRow->addWidget(title);
    titleRow->addStretch();
    layout->addLayout(titleRow);
    auto* sep = new QFrame(); sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(StyleHelper::separatorLine());
    layout->addWidget(sep);

    auto* form = new QFormLayout();
    form->setSpacing(8);
    form->setContentsMargins(0, 0, 0, 0);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);  // 表单字段自动扩展
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);  // 标签右对齐
    // 统一开关样式 统一22px indicator + 选中态蓝色

    m_buzzerSlider = new QSlider(Qt::Horizontal);
    m_buzzerSlider->setRange(70, 110);
    m_buzzerSlider->setValue(85);
    m_buzzerSlider->setFixedWidth(140);
    m_buzzerSlider->setStyleSheet("QSlider{background:transparent;}"
                                  "QSlider::groove:horizontal{height:6px;background:#e0e0e0;border-radius:3px;}"
                                  "QSlider::handle:horizontal{width:18px;height:18px;background:#4da3ff;border-radius:9px;}");
    m_buzzerValueLabel = new QLabel("85dB");
    m_buzzerValueLabel->setFixedWidth(40);
    m_buzzerValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_buzzerValueLabel->setStyleSheet(QString("" + StyleHelper::fontSize(StyleHelper::Token::FontBody) + ";font-weight:700;color:%1;").arg(StyleHelper::primaryColor()));
    connect(m_buzzerSlider, &QSlider::valueChanged, this, [this](int val) {
        m_buzzerValueLabel->setText(QString("%1dB").arg(val));
    });
    auto* buzzerWidget = new QWidget();
    buzzerWidget->setStyleSheet("background:transparent;");  // 去灰
    auto* buzzerLayout = new QHBoxLayout(buzzerWidget);
    buzzerLayout->setContentsMargins(0, 0, 0, 0);
    buzzerLayout->setSpacing(8);
    buzzerLayout->addWidget(m_buzzerSlider);
    buzzerLayout->addWidget(m_buzzerValueLabel);
    form->addRow(FormFactory::formLabel(QStringLiteral("蜂鸣器音量")), buzzerWidget);

    m_ledCheck = FormFactory::toggle(true);
    form->addRow(FormFactory::formLabel(QStringLiteral("LED告警灯")), m_ledCheck);

    m_overdueSpin = new QSpinBox();
    m_overdueSpin->setRange(1, 168);
    m_overdueSpin->setValue(24);
    m_overdueSpin->setSuffix(QStringLiteral(" 小时"));
    m_overdueSpin->setStyleSheet(StyleHelper::settingSpinBox());
    form->addRow(FormFactory::formLabel(QStringLiteral("逾期告警阈值")), m_overdueSpin);

    m_doorTimeoutSpin = new QSpinBox();
    m_doorTimeoutSpin->setRange(5, 300);
    m_doorTimeoutSpin->setValue(30);
    m_doorTimeoutSpin->setSuffix(QStringLiteral(" 秒"));
    m_doorTimeoutSpin->setStyleSheet(StyleHelper::settingSpinBox());
    form->addRow(FormFactory::formLabel(QStringLiteral("柜门未关告警")), m_doorTimeoutSpin);

    m_visionCheck = FormFactory::toggle(true);
    form->addRow(FormFactory::formLabel(QStringLiteral("视觉识别异常告警")), m_visionCheck);

    // 断电告警方式：用按钮组替代QComboBox，风格统一
    // 统一按钮尺寸：44px高/Preferred策略，三按钮最大宽180px
    auto makeAlarmBtn = [&](const QString& text, int mode) -> QPushButton* {
        auto* btn = new QPushButton(text);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(StyleHelper::Token::ControlHeightCompactInput);
        btn->setMinimumWidth(100);
        btn->setMaximumWidth(180);
        btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        // 默认选中第一个
        if (mode == 0) btn->setChecked(true);
        connect(btn, &QPushButton::clicked, this, [this, mode]() {
            m_powerAlarmMode = mode;
            updatePowerAlarmBtnStyles();
        });
        return btn;
    };
    m_powerAlarmBtn1 = makeAlarmBtn(QStringLiteral("声光同时告警"), 0);
    m_powerAlarmBtn2 = makeAlarmBtn(QStringLiteral("仅灯光"), 1);
    m_powerAlarmBtn3 = makeAlarmBtn(QStringLiteral("仅声音"), 2);

    auto* alarmBtnGroup = new QWidget();
    alarmBtnGroup->setStyleSheet("background:transparent;");
    auto* alarmBtnLayout = new QHBoxLayout(alarmBtnGroup);
    alarmBtnLayout->setContentsMargins(0, 0, 0, 0);
    alarmBtnLayout->setSpacing(8);
    alarmBtnLayout->addWidget(m_powerAlarmBtn1);
    alarmBtnLayout->addWidget(m_powerAlarmBtn2);
    alarmBtnLayout->addWidget(m_powerAlarmBtn3);
    updatePowerAlarmBtnStyles();
    form->addRow(FormFactory::formLabel(QStringLiteral("断电告警方式")), alarmBtnGroup);

    m_autoConfirmCheck = FormFactory::toggle(true);
    form->addRow(FormFactory::formLabel(QStringLiteral("告警自动确认")), m_autoConfirmCheck);

    layout->addLayout(form);

    // 保存栏：右对齐+顶部分割线，48px按钮
    auto* saveBar = new QFrame();
    saveBar->setStyleSheet(StyleHelper::saveBarSeparator());
    auto* saveBarLayout = new QHBoxLayout(saveBar);
    saveBarLayout->setContentsMargins(0, 8, 0, 0);  // 8
    saveBarLayout->addStretch();
    auto* saveBtn = new QPushButton(QStringLiteral("保存告警设置"));
    saveBtn->setStyleSheet(StyleHelper::settingSaveBtn());
    saveBtn->setCursor(Qt::PointingHandCursor);
    connect(saveBtn, &QPushButton::clicked, this, &SystemSettingsPage::onSaveAlert);
    saveBarLayout->addWidget(saveBtn);
    layout->addWidget(saveBar);

    // installFocusEvents已移除
    return panel;
}

/**
 * @brief SystemSettingsPage::refresh 的业务实现
 */
void SystemSettingsPage::refresh() {
    // 任务配置面板数据来自数据库：进入页面时刷新任务类型与工具清单，
    // 保证其他入口（如借用页推荐）改动后本页看到的是最新配置
    loadTaskTypes();
    if (m_taskTypeCombo && m_taskTypeCombo->count() > 0) {
        loadTaskTools(m_taskTypeCombo->currentData().toInt());
    }
}

/**
 * @brief 处理全部
 */
void SystemSettingsPage::onSaveAll() {
    // 保存全部配置到本地INI文件
    if (saveConfigToIni()) {
        m_savedFlag = true;
        MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("全部设置已保存到本地配置文件"));
    }
}

/// 网络配置字段校验辅助（抽成独立函数，便于单元测试，避免在UI回调里堆叠逻辑）
/// @param s 待校验字符串
/// @return 是否为合法 IPv4 地址
static bool isIPv4Address(const QString& s) {
    if (s.isEmpty()) return false;
    QHostAddress addr;
    return addr.setAddress(s) && addr.protocol() == QHostAddress::IPv4Protocol;
}

/**
 * @brief 处理网络
 * @note 保存前先做 IP/掩码/网关/DNS 格式校验，避免非法值被写入并应用到系统网卡导致断网
 */
void SystemSettingsPage::onSaveNetwork() {
    // 取出并去除首尾空白，统一用于校验/保存/应用
    const QString ip      = m_ipEdit->text().trimmed();
    const QString mask    = m_maskEdit->text().trimmed();
    const QString gateway = m_gatewayEdit->text().trimmed();
    const QString dns     = m_dnsEdit->text().trimmed();

    if (!isIPv4Address(ip)) {
        MessageDialog::showError(this, QStringLiteral("格式错误"),
            QStringLiteral("IP 地址不合法，请检查（示例：192.168.1.100）"));
        return;
    }
    if (!isIPv4Address(mask)) {
        MessageDialog::showError(this, QStringLiteral("格式错误"),
            QStringLiteral("子网掩码不合法（示例：255.255.255.0）"));
        return;
    }
    if (!gateway.isEmpty() && !isIPv4Address(gateway)) {
        MessageDialog::showError(this, QStringLiteral("格式错误"),
            QStringLiteral("默认网关不合法"));
        return;
    }
    if (!dns.isEmpty() && !isIPv4Address(dns)) {
        MessageDialog::showError(this, QStringLiteral("格式错误"),
            QStringLiteral("DNS 服务器不合法"));
        return;
    }

    // 保存网络配置到本地INI文件
    auto& cfg = AppConfig::instance();
    cfg.setNetIp(ip);
    cfg.setNetMask(mask);
    cfg.setNetGateway(gateway);
    cfg.setNetDns(dns);
    cfg.setNetServer(m_serverEdit->text().trimmed());
    cfg.setNetSpeedMode(m_speedMode);
    cfg.setNetNetworkMode(m_networkMode);
    cfg.save();
    m_savedFlag = true;
    snapshotSettings();
    // 保存网络配置后自动应用到系统有线网卡（Windows+麒麟跨平台，异步执行）
    applyNetworkConfig(ip, mask, gateway, dns, m_speedMode);
    MessageDialog::showSuccess(this, QStringLiteral("成功"),
        QStringLiteral("网络配置已保存到本地，正在应用到系统网卡（结果将另行提示）"));
}

/**
 * @brief 处理告警
 */
void SystemSettingsPage::onSaveAlert() {
    // 保存告警配置到本地INI文件
    auto& cfg = AppConfig::instance();
    cfg.setAlertBuzzerVol(m_buzzerSlider->value());
    cfg.setAlertLedEnabled(m_ledCheck->isChecked());
    cfg.setAlertOverdueHours(m_overdueSpin->value());
    cfg.setAlertDoorTimeout(m_doorTimeoutSpin->value());
    cfg.setAlertVisionEnabled(m_visionCheck->isChecked());
    cfg.setAlertPowerAlarmMode(m_powerAlarmMode);
    cfg.setAlertAutoConfirm(m_autoConfirmCheck->isChecked());
    cfg.save();
    m_savedFlag = true;
    snapshotSettings();
    MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("告警设置已保存到本地配置文件"));
}

/**
 * @brief 处理借用
 */
void SystemSettingsPage::onSaveBorrow() {
    // 保存借还配置到本地INI文件
    auto& cfg = AppConfig::instance();
    cfg.setBorrowDefaultPeriod(m_defaultPeriodSpin->value());
    cfg.setBorrowReturnBuffer(m_returnBufferSpin->value());
    cfg.setBorrowBrightness(m_brightnessSlider->value());
    cfg.save();
    m_savedFlag = true;
    snapshotSettings();
    applyDisplayBrightness(m_brightnessSlider->value());
    MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("借还设置已保存到本地配置文件"));
}

/**
 * @brief 处理备份
 */
void SystemSettingsPage::onSaveBackup() {
    // 保存备份配置到本地INI文件
    auto& cfg = AppConfig::instance();
    cfg.setBackupAutoEnabled(m_autoBackupCheck->isChecked());
    cfg.setBackupPeriod(m_backupPeriod);
    cfg.setBackupCacheHours(m_cacheHoursSpin->value());
    cfg.setBackupPath(m_backupPathEdit->text());
    cfg.save();
    m_savedFlag = true;
    snapshotSettings();
    // 保存备份设置后立即执行一次备份，并注册定时备份任务
    if (m_autoBackupCheck->isChecked()) {
        performDatabaseBackup();
    }
    MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("备份设置已保存到本地配置文件"));
}

/// 判断是否应在今天执行一次自动备份（已按"今天是否已备份"去重）
/// @param period     备份周期：0=每日, 1=每周一, 2=每周日
/// @param lastBackup 上次成功备份日期（空=从未备份）
/// @param today      今天日期
/// @return 今天是否应备份一次
static bool shouldRunAutoBackup(int period, const QDate& lastBackup, const QDate& today) {
    if (!today.isValid()) return false;
    // 今天已经备份过则跳过；空日期视为从未备份，不跳过
    if (!lastBackup.isNull() && today <= lastBackup) return false;
    switch (period) {
    case 1: return today.dayOfWeek() == Qt::Monday;    // 每周一
    case 2: return today.dayOfWeek() == Qt::Sunday;    // 每周日
    default: return true;                               // 每日
    }
}

void SystemSettingsPage::initBackupScheduler() {
    if (m_backupTimer) return;
    m_backupTimer = new QTimer(this);
    m_backupTimer->setInterval(60 * 60 * 1000);  // 每小时轮询一次
    connect(m_backupTimer, &QTimer::timeout, this, &SystemSettingsPage::onBackupTimerTick);
    m_backupTimer->start();
    // 启动后立即检查一次：捕捉"当天是备份日但程序在备份时间点之后才启动"的场景
    QTimer::singleShot(0, this, &SystemSettingsPage::onBackupTimerTick);
}

void SystemSettingsPage::onBackupTimerTick() {
    auto& cfg = AppConfig::instance();
    if (!cfg.backupAutoEnabled()) return;
    const QDate today = QDate::currentDate();
    if (!shouldRunAutoBackup(cfg.backupPeriod(), cfg.lastBackupDate(), today)) return;
    performDatabaseBackup();
}

/**
 * @brief 断电告警方式按钮组样式更新 — 44px高统一风格
 */
void SystemSettingsPage::updatePowerAlarmBtnStyles() {
    QPushButton* btns[3] = { m_powerAlarmBtn1, m_powerAlarmBtn2, m_powerAlarmBtn3 };
    for (int i = 0; i < 3; ++i) {
        if (!btns[i]) continue;
        bool sel = (i == m_powerAlarmMode);
        btns[i]->setChecked(sel);
        if (sel) {
            btns[i]->setStyleSheet(StyleHelper::buttonGroupPrimary());
        } else {
            btns[i]->setStyleSheet(StyleHelper::buttonGroupDefault());
        }
    }
}

/**
 * @brief 网口速率按钮组样式更新 — 44px高统一风格
 */
void SystemSettingsPage::updateSpeedBtnStyles() {
    QPushButton* btns[2] = { m_speedBtn1, m_speedBtn2 };
    for (int i = 0; i < 2; ++i) {
        if (!btns[i]) continue;
        bool sel = (i == m_speedMode);
        btns[i]->setChecked(sel);
        if (sel) {
            btns[i]->setStyleSheet(StyleHelper::buttonGroupPrimary());
        } else {
            btns[i]->setStyleSheet(StyleHelper::buttonGroupDefault());
        }
    }
}

/**
 * @brief 组网模式按钮组样式更新 — 44px高统一风格
 */
void SystemSettingsPage::updateModeBtnStyles() {
    QPushButton* btns[2] = { m_modeBtn1, m_modeBtn2 };
    for (int i = 0; i < 2; ++i) {
        if (!btns[i]) continue;
        bool sel = (i == m_networkMode);
        btns[i]->setChecked(sel);
        if (sel) {
            btns[i]->setStyleSheet(StyleHelper::buttonGroupPrimary());
        } else {
            btns[i]->setStyleSheet(StyleHelper::buttonGroupDefault());
        }
    }
}

/**
 * @brief 备份周期按钮组样式更新 — 44px高统一风格
 */
void SystemSettingsPage::updateBackupPeriodBtnStyles() {
    QPushButton* btns[3] = { m_backupBtn1, m_backupBtn2, m_backupBtn3 };
    for (int i = 0; i < 3; ++i) {
        if (!btns[i]) continue;
        bool sel = (i == m_backupPeriod);
        btns[i]->setChecked(sel);
        if (sel) {
            btns[i]->setStyleSheet(StyleHelper::buttonGroupPrimary());
        } else {
            btns[i]->setStyleSheet(StyleHelper::buttonGroupDefault());
        }
    }
}

/**
 * @brief 进入页面时从INI文件加载配置，然后备份快照
 * @param event 事件对象，由 Qt 事件系统传入
 */
void SystemSettingsPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    m_savedFlag = false;
    loadConfigFromIni();  // 从本地INI文件加载配置到UI
    snapshotSettings();      // 再备份快照
}

/**
 * @brief 替换QMessageBox为统一MessageDialog::showDirtyConfirm（风格统一+无取消按钮） 离开页面时检测脏数据 → 弹窗询问是否保存
 * @param event 事件对象，由 Qt 事件系统传入
 */
void SystemSettingsPage::hideEvent(QHideEvent* event) {
    if (!m_savedFlag && isDirty()) {
        // 有未保存修改 → 弹窗询问（保存 / 不保存，无取消按钮）
        int choice = MessageDialog::showDirtyConfirm(
            this,
            QStringLiteral("保存配置"),
            QStringLiteral("系统设置有修改，是否保存到本地配置文件？")
        );
        if (choice == 1) {
            // 用户点击"保存"
            saveConfigToIni();
            m_savedFlag = true;
        }
        // choice == 0 → "不保存"，直接离开
    }
    QWidget::hideEvent(event);
}

/**
 * @brief 检测UI当前值是否与快照不同
 * @return 操作是否成功（true=成功，false=失败）
 */
bool SystemSettingsPage::isDirty() {
    if (!m_ipEdit || m_ipEdit->text() != m_snapshot.ip) return true;
    if (!m_maskEdit || m_maskEdit->text() != m_snapshot.mask) return true;
    if (!m_gatewayEdit || m_gatewayEdit->text() != m_snapshot.gateway) return true;
    if (!m_dnsEdit || m_dnsEdit->text() != m_snapshot.dns) return true;
    if (!m_serverEdit || m_serverEdit->text() != m_snapshot.server) return true;
    if (m_speedMode != m_snapshot.speedMode) return true;
    if (m_networkMode != m_snapshot.networkMode) return true;

    if (!m_buzzerSlider || m_buzzerSlider->value() != m_snapshot.buzzerVol) return true;
    if (!m_ledCheck || m_ledCheck->isChecked() != m_snapshot.ledAlert) return true;
    if (!m_overdueSpin || m_overdueSpin->value() != m_snapshot.overdueHours) return true;
    if (!m_doorTimeoutSpin || m_doorTimeoutSpin->value() != m_snapshot.doorTimeoutSec) return true;
    if (!m_visionCheck || m_visionCheck->isChecked() != m_snapshot.visionAlert) return true;
    if (m_powerAlarmMode != m_snapshot.powerAlarmMode) return true;
    if (!m_autoConfirmCheck || m_autoConfirmCheck->isChecked() != m_snapshot.autoConfirm) return true;

    // 借还设置仅检测下方三个控件："单次最大借出数量"没有对应输入控件，不存在用户修改入口，不参与脏数据检测
    if (!m_defaultPeriodSpin || m_defaultPeriodSpin->value() != m_snapshot.defaultPeriod) return true;
    if (!m_returnBufferSpin || m_returnBufferSpin->value() != m_snapshot.returnBuffer) return true;
    if (!m_brightnessSlider || m_brightnessSlider->value() != m_snapshot.brightness) return true;

    if (!m_autoBackupCheck || m_autoBackupCheck->isChecked() != m_snapshot.autoBackup) return true;
    if (m_backupPeriod != m_snapshot.backupPeriod) return true;
    if (!m_cacheHoursSpin || m_cacheHoursSpin->value() != m_snapshot.cacheHours) return true;
    if (!m_backupPathEdit || m_backupPathEdit->text() != m_snapshot.backupPath) return true;

    return false;  // 无修改
}

/**
 * @brief 从本地INI文件加载配置到UI控件
 */
void SystemSettingsPage::loadConfigFromIni() {
    auto& cfg = AppConfig::instance();

    // 网络配置
    m_ipEdit->setText(cfg.netIp());
    m_maskEdit->setText(cfg.netMask());
    m_gatewayEdit->setText(cfg.netGateway());
    m_dnsEdit->setText(cfg.netDns());
    m_serverEdit->setText(cfg.netServer());
    m_speedMode = cfg.netSpeedMode();   updateSpeedBtnStyles();
    m_networkMode = cfg.netNetworkMode(); updateModeBtnStyles();

    // 告警参数
    m_buzzerSlider->setValue(cfg.alertBuzzerVol());
    m_ledCheck->setChecked(cfg.alertLedEnabled());
    m_overdueSpin->setValue(cfg.alertOverdueHours());
    m_doorTimeoutSpin->setValue(cfg.alertDoorTimeout());
    m_visionCheck->setChecked(cfg.alertVisionEnabled());
    m_powerAlarmMode = cfg.alertPowerAlarmMode(); updatePowerAlarmBtnStyles();
    m_autoConfirmCheck->setChecked(cfg.alertAutoConfirm());

    // 借还设置
    m_defaultPeriodSpin->setValue(cfg.borrowDefaultPeriod());
    m_returnBufferSpin->setValue(cfg.borrowReturnBuffer());
    m_brightnessSlider->setValue(cfg.borrowBrightness());

    // 备份管理
    m_autoBackupCheck->setChecked(cfg.backupAutoEnabled());
    m_backupPeriod = cfg.backupPeriod(); updateBackupPeriodBtnStyles();
    m_cacheHoursSpin->setValue(cfg.backupCacheHours());
    m_backupPathEdit->setText(cfg.backupPath());
}

/**
 * @brief 将当前UI值全部写入本地INI文件
 * @return 操作是否成功（true=成功，false=失败）
 */
bool SystemSettingsPage::saveConfigToIni() {
    auto& cfg = AppConfig::instance();

    // 网络配置
    cfg.setNetIp(m_ipEdit->text());
    cfg.setNetMask(m_maskEdit->text());
    cfg.setNetGateway(m_gatewayEdit->text());
    cfg.setNetDns(m_dnsEdit->text());
    cfg.setNetServer(m_serverEdit->text());
    cfg.setNetSpeedMode(m_speedMode);
    cfg.setNetNetworkMode(m_networkMode);

    // 告警参数
    cfg.setAlertBuzzerVol(m_buzzerSlider->value());
    cfg.setAlertLedEnabled(m_ledCheck->isChecked());
    cfg.setAlertOverdueHours(m_overdueSpin->value());
    cfg.setAlertDoorTimeout(m_doorTimeoutSpin->value());
    cfg.setAlertVisionEnabled(m_visionCheck->isChecked());
    cfg.setAlertPowerAlarmMode(m_powerAlarmMode);
    cfg.setAlertAutoConfirm(m_autoConfirmCheck->isChecked());

    // 借还设置
    cfg.setBorrowDefaultPeriod(m_defaultPeriodSpin->value());
    cfg.setBorrowReturnBuffer(m_returnBufferSpin->value());
    cfg.setBorrowBrightness(m_brightnessSlider->value());

    // 备份管理
    cfg.setBackupAutoEnabled(m_autoBackupCheck->isChecked());
    cfg.setBackupPeriod(m_backupPeriod);
    cfg.setBackupCacheHours(m_cacheHoursSpin->value());
    cfg.setBackupPath(m_backupPathEdit->text());

    cfg.save();
    snapshotSettings();
    return true;
}

/**
 * @brief 备份当前所有UI控件的值到快照
 */
void SystemSettingsPage::snapshotSettings() {
    // 网络配置
    m_snapshot.ip = m_ipEdit ? m_ipEdit->text() : "";
    m_snapshot.mask = m_maskEdit ? m_maskEdit->text() : "";
    m_snapshot.gateway = m_gatewayEdit ? m_gatewayEdit->text() : "";
    m_snapshot.dns = m_dnsEdit ? m_dnsEdit->text() : "";
    m_snapshot.server = m_serverEdit ? m_serverEdit->text() : "";
    m_snapshot.speedMode = m_speedMode;
    m_snapshot.networkMode = m_networkMode;

    // 告警参数
    m_snapshot.buzzerVol = m_buzzerSlider ? m_buzzerSlider->value() : 85;
    m_snapshot.ledAlert = m_ledCheck ? m_ledCheck->isChecked() : true;
    m_snapshot.overdueHours = m_overdueSpin ? m_overdueSpin->value() : 24;
    m_snapshot.doorTimeoutSec = m_doorTimeoutSpin ? m_doorTimeoutSpin->value() : 30;
    m_snapshot.visionAlert = m_visionCheck ? m_visionCheck->isChecked() : true;
    m_snapshot.powerAlarmMode = m_powerAlarmMode;
    m_snapshot.autoConfirm = m_autoConfirmCheck ? m_autoConfirmCheck->isChecked() : true;

    // 借还设置
    m_snapshot.defaultPeriod = m_defaultPeriodSpin ? m_defaultPeriodSpin->value() : 48;
    m_snapshot.returnBuffer = m_returnBufferSpin ? m_returnBufferSpin->value() : 30;
    m_snapshot.brightness = m_brightnessSlider ? m_brightnessSlider->value() : 80;

    // 备份管理
    m_snapshot.autoBackup = m_autoBackupCheck ? m_autoBackupCheck->isChecked() : true;
    m_snapshot.backupPeriod = m_backupPeriod;
    m_snapshot.cacheHours = m_cacheHoursSpin ? m_cacheHoursSpin->value() : 4;
    m_snapshot.backupPath = m_backupPathEdit ? m_backupPathEdit->text() : SC::BACKUP_PATH;
}

// 跨平台设置显示器亮度（Windows + 麒麟Linux自适应）
// Windows方案：PowerShell + WMI (WmiMonitorBrightnessMethods.WmiSetBrightness)
// 麒麟方案A：/sys/class/backlight/<dev>/brightness 内核接口（硬件级，需root）
// 麒麟方案B：xrandr --output <dev> --brightness <val> X11 Gamma调整（软件级，无需root）
// 麒麟方案C：brightnessctl set <val>% 命令（需安装）
// 异步执行不阻塞UI，失败静默处理并降级尝试下一方案

// 跨平台设置自动锁屏时间
// Windows：powercfg 设置显示器关闭超时 + 通过QTimer在主窗口实现应用层锁屏
// 麒麟：xset s <秒数> 设置屏幕保护超时 + xset dpms <秒数> 设置DPMS显示器电源管理
// 同时写入AppConfig供MainWindow的QTimer读取实现应用层自动锁屏




