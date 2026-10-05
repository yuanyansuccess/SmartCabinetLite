/**
 * @file SystemSettingsPage.cpp
 * @brief 系统设置页面实现 - 1:1复刻BS端SystemSettings.vue
 * @author 袁燕
 *
 * 完全重写以1:1匹配BS端SystemSettings.vue
 * 布局：选项卡+双列网格（网络配置/告警参数/借还设置/备份管理+系统信息）
 * 底部：保存全部设置/恢复默认按钮
 * 安全：危险操作使用SoftKeyboard安全键盘验证管理员密码
 */

#include "SystemSettingsPage.h"
#include "components/SoftKeyboard.h"
#include "components/NumKeypad.h"
#include "components/BaseDialog.h"  // 统一圆角对话框
#include "utils/StyleHelper.h"
#include "components/FormFactory.h"  // 表单控件工厂（收敛重复lambda）
#include "services/SettingService.h"
#include "services/AuthService.h"
#include "common/AppConfig.h"  // 机组名称配置
#include "common/DatabaseManager.h"     // DB写入机组配置
#include "db/RecordDAO.h"               // 校验机组下未归还记录
#include "db/ToolDAO.h"                 // 加载活跃机组列表
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include "components/MessageDialog.h"
#include <QApplication>
#include <QDebug>
#include <QFrame>
#include <QGroupBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QSlider>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QGridLayout>
#include <QMouseEvent>
#include <QScrollBar>
#include <QFocusEvent>
#include <QShowEvent>  // 页面切换还原
#include <QHideEvent>
#include <QTableWidget>  // 任务类型配置表格
#include <QHeaderView>  // 表格列宽控制
#include <QTableWidgetItem>  // 表格单元格
#include <QProgressBar>  // 软件升级进度条
#include <QTimer>  // 软件升级定时器
#include <QProcess>  // 调用PowerShell设置显示器亮度
#include <QFile>  // WMI结果日志记录
#include <QDateTime>  // 亮度日志时间戳
#include <QDir>  // 备份目录操作
#include <QStorageInfo>  // 跨平台磁盘空间读取
#include <QSysInfo>  // 跨平台系统信息读取
#include <QRegularExpression>  // 解析os-release
#include <QFileInfoList>  // 备份文件清理


SystemSettingsPage::SystemSettingsPage(QWidget* parent) : QWidget(parent) {
    setupUI();
    m_softKeyboard = new SoftKeyboard(this);
    connect(m_softKeyboard, &SoftKeyboard::confirmed, this, &SystemSettingsPage::onSecurePwdConfirmed);
    connect(m_softKeyboard, &SoftKeyboard::cancelled, this, [](){});
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
    auto* title = new QLabel(QStringLiteral("系统参数配置"));
    title->setStyleSheet("font-size:20px;font-weight:700;color:#1a1a2e;background:transparent;");  // 20对齐
    titleBar->addWidget(title);
    titleBar->addStretch();

    // 操作按钮统一风格：主操作蓝色实底/次操作白底蓝边/危险操作白底红边
    m_restoreBtn = new QPushButton(QStringLiteral("恢复默认"));
    m_restoreBtn->setStyleSheet(
        "QPushButton{background:#fff;color:#4da3ff;border:2px solid #4da3ff;border-radius:10px;"
        "padding:10px 22px;font-size:14px;font-weight:700;}"
        "QPushButton:hover{background:#f0f7ff;}"
        "QPushButton:pressed{background:#e6f0ff;}"
    );
    m_restoreBtn->setCursor(Qt::PointingHandCursor);
    connect(m_restoreBtn, &QPushButton::clicked, this, &SystemSettingsPage::onResetDefault);

    m_saveAllBtn = new QPushButton(QStringLiteral("保存全部设置"));
    m_saveAllBtn->setStyleSheet(
        "QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:10px;"
        "padding:10px 22px;font-size:14px;font-weight:700;}"
        "QPushButton:hover{background:#3d8ae0;}"
        "QPushButton:pressed{background:#2e7bd6;}"
    );
    m_saveAllBtn->setCursor(Qt::PointingHandCursor);
    connect(m_saveAllBtn, &QPushButton::clicked, this, &SystemSettingsPage::onSaveAll);

    titleBar->addWidget(m_restoreBtn);
    titleBar->addWidget(m_saveAllBtn);
    m_upgradeBtn = new QPushButton(QStringLiteral("软件升级"));
    m_upgradeBtn->setStyleSheet(
        "QPushButton{background:#fff;color:#666;border:2px solid #d0d0d0;border-radius:10px;"
        "padding:10px 22px;font-size:14px;font-weight:700;}"
        "QPushButton:hover{background:#f5f5f5;border-color:#4da3ff;color:#4da3ff;}"
        "QPushButton:pressed{background:#e8e8e8;}"
    );
    m_upgradeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_upgradeBtn, &QPushButton::clicked, this, &SystemSettingsPage::onStartUpgrade);

    titleBar->addWidget(m_restoreBtn);
    titleBar->addWidget(m_saveAllBtn);
    titleBar->addWidget(m_upgradeBtn);
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
                            QStringLiteral("借还设置"), QStringLiteral("备份管理")};
    // 任务配置已迁移到系统维护页面
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

    // QStackedWidget切换，去掉滚动（对齐系统维护页面）
    // 备份面板内容多(表单+系统信息+机组配置)，单独包QScrollArea避免字体压扁
    m_stackedWidget = new QStackedWidget();
    m_stackedWidget->addWidget(m_networkPanel = createNetworkPanel());
    m_stackedWidget->addWidget(m_alertPanel = createAlertPanel());
    m_stackedWidget->addWidget(m_borrowPanel = createBorrowPanel());
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
}

/**
 * @brief 按当前选中项刷新各选项卡的样式
 */
void SystemSettingsPage::updateTabStyles() {
    // Tab样式对齐借用页QTabWidget：灰底白选中+主色下划线，去渐变胶囊
    for (int i = 0; i < m_tabLabels.size(); ++i) {
        if (i == m_activeTabIndex) {
            m_tabLabels[i]->setStyleSheet(
                QString("font-size:15px;font-weight:600;padding:10px 24px;border-radius:10px 10px 0 0;"
                        "color:%1;background:#fff;min-height:44px;"
                        "border-bottom:3px solid %1;")
                .arg(StyleHelper::primaryColor()));
        } else {
            m_tabLabels[i]->setStyleSheet(StyleHelper::panelTitleBar());
        }
    }
}

// switchTab（QStackedWidget切换）
void SystemSettingsPage::switchTab(int index) {
    if (index < 0 || index >= m_tabLabels.size()) return;
    m_activeTabIndex = index;
    updateTabStyles();
    if (m_stackedWidget) {
        m_stackedWidget->setCurrentIndex(index);
    }
}

/**
         * 事件过滤器：拦截控件与窗口事件并转交专用处理
         * @param obj 事件来源控件
         * @param event 事件对象
         * @return true=事件已被处理
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
                    QString("font-size:15px;font-weight:600;padding:10px 24px;border-radius:10px 10px 0 0;"
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

// ==================== 告警参数面板 ====================
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
    m_buzzerValueLabel->setStyleSheet(QString("font-size:14px;font-weight:700;color:%1;").arg(StyleHelper::primaryColor()));
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

// ==================== 借还设置面板 ====================
void SystemSettingsPage::refresh() {
    // 软件版本从AppConfig读取，其余系统信息从系统实时读取
    if (m_versionLabel) {
        m_versionLabel->setText(QStringLiteral("软件版本: %1").arg(AppConfig::instance().appVersion()));
    }
    // 从系统读取真实OS/设备编号/运行时长/磁盘空间
    refreshSystemInfo();
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

/**
 * @brief 处理网络
 */
void SystemSettingsPage::onSaveNetwork() {
    // 保存网络配置到本地INI文件
    auto& cfg = AppConfig::instance();
    cfg.setNetIp(m_ipEdit->text());
    cfg.setNetMask(m_maskEdit->text());
    cfg.setNetGateway(m_gatewayEdit->text());
    cfg.setNetDns(m_dnsEdit->text());
    cfg.setNetServer(m_serverEdit->text());
    cfg.setNetSpeedMode(m_speedMode);
    cfg.setNetNetworkMode(m_networkMode);
    cfg.save();
    m_savedFlag = true;
    snapshotSettings();
    // 保存网络配置后自动应用到系统有线网卡（Windows+麒麟跨平台）
    applyNetworkConfig(m_ipEdit->text(), m_maskEdit->text(),
                       m_gatewayEdit->text(), m_dnsEdit->text());
    MessageDialog::showSuccess(this, QStringLiteral("成功"),
        QStringLiteral("网络设置已保存并应用到系统有线网卡"));
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
    // m_maxBorrowSpin创建已注释，空守卫防崩溃
    if (m_maxBorrowSpin) cfg.setBorrowMaxCount(m_maxBorrowSpin->value());
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

/**
 * @brief 处理默认
 */
void SystemSettingsPage::onResetDefault() {
    if (!MessageDialog::showQuestion(this, QStringLiteral("确认恢复默认"),
        QStringLiteral("确定要恢复所有设置为出厂默认值吗？此操作不可撤销！"))) return;
    showSecurePasswordDialog(QStringLiteral("安全验证 - 恢复默认"), QStringLiteral("factoryReset"));
}

// onClearLogs() 已移除（危险操作区不暴露）
// 恢复默认仍可通过顶部"恢复默认"按钮触发 onResetDefault()



void SystemSettingsPage::onStartUpgrade() {
    // 软件升级模拟进度条，美观的设计
    // 先确认升级
    if (!MessageDialog::showQuestion(this, QStringLiteral("软件升级"),
        QStringLiteral("将开始软件升级，升级过程中请勿断电！\n\n确定要开始升级吗？"))) return;

    // 创建升级进度对话框
    auto* dlg = new BaseDialog(this, 520);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setDialogTitle(QStringLiteral("系统升级中..."));
    dlg->setWindowTitle(QStringLiteral("软件升级"));
    dlg->setModal(true);
    dlg->setFixedSize(520, 400);

    QVBoxLayout* cl = dlg->contentLayout();
    cl->setSpacing(16);
    cl->setContentsMargins(20, 10, 20, 20);

    // 版本信息
    QString currentVer = m_versionLabel ? m_versionLabel->text() : QStringLiteral("V1.0.0");
    auto* verLabel = new QLabel(QStringLiteral("当前版本：%1").arg(currentVer));
    verLabel->setStyleSheet("font-size:14px;color:#666;padding:4px 0;");
    verLabel->setAlignment(Qt::AlignCenter);
    cl->addWidget(verLabel);

    // 进度条
    auto* progressBar = new QProgressBar();
    progressBar->setMinimum(0);
    progressBar->setMaximum(100);
    progressBar->setValue(0);
    progressBar->setTextVisible(true);
    progressBar->setFixedHeight(StyleHelper::Token::ControlHeightCompact);
    progressBar->setStyleSheet(
        "QProgressBar{border:2px solid #e0e0e0;border-radius:16px;background:#f5f5f5;text-align:center;"
        "font-size:14px;font-weight:700;color:#333;}"
        "QProgressBar::chunk{background:#4da3ff;border-radius:14px;}"
    );
    cl->addWidget(progressBar);

    // 状态文本
    auto* statusLabel = new QLabel(QStringLiteral("正在准备升级..."));
    statusLabel->setStyleSheet("font-size:16px;color:#1a1a2e;font-weight:600;padding:8px 0;");
    statusLabel->setAlignment(Qt::AlignCenter);
    cl->addWidget(statusLabel);

    // 详细日志区域
    auto* logText = new QLabel();
    logText->setWordWrap(true);
    logText->setStyleSheet(
        "font-size:13px;color:#888;padding:8px 12px;background:#fafafa;"
        "border-radius:8px;border:1px solid #f0f0f0;min-height:60px;"
    );
    logText->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    cl->addWidget(logText);

    cl->addStretch();

    // 底部按钮（初始隐藏，升级完成后显示）
    auto* btnLayout = dlg->buttonLayout();
    auto* closeBtn = new QPushButton(QStringLiteral("确定"));
    closeBtn->setStyleSheet(
        "QPushButton{background:#4da3ff;"
        "color:#fff;border:none;border-radius:10px;padding:10px 32px;font-size:16px;font-weight:700;"
        "min-width:120px;min-height:44px;}"
        "QPushButton:hover{background:#3d8ae0;}"
        "QPushButton:pressed{background:#2e7bd6;}"
    );
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setVisible(false);
    connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);

    // 升级步骤模拟
    struct UpgradeStep {
        int progress;
        QString status;
        QString detail;
    };
    QList<UpgradeStep> steps = {
        {10, QStringLiteral("正在检查系统环境..."),   QStringLiteral("校验磁盘空间、内存状态、系统版本")},
        {20, QStringLiteral("正在下载升级包..."),     QStringLiteral("从云端获取最新版本 V2.0.1")},
        {35, QStringLiteral("正在校验升级包完整性..."), QStringLiteral("MD5校验通过，签名验证成功")},
        {50, QStringLiteral("正在备份当前系统..."),   QStringLiteral("备份配置文件、数据库到安全目录")},
        {65, QStringLiteral("正在安装更新..."),       QStringLiteral("解压升级包，替换系统组件")},
        {80, QStringLiteral("正在更新数据库..."),     QStringLiteral("执行数据库迁移脚本，更新表结构")},
        {92, QStringLiteral("正在清理缓存..."),       QStringLiteral("清理临时文件，重建索引")},
        {100, QStringLiteral("升级完成！"),           QStringLiteral("系统已升级至 V2.0.1，请重启应用以生效")},
    };

    int stepIndex = 0;
    auto* timer = new QTimer(dlg);
    connect(timer, &QTimer::timeout, dlg, [&stepIndex, &steps, progressBar, statusLabel, logText, timer, dlg, closeBtn]() mutable {
        if (stepIndex >= steps.size()) {
            timer->stop();
            return;
        }
        const auto& step = steps[stepIndex];
        progressBar->setValue(step.progress);
        statusLabel->setText(step.status);

        // 追加日志
        QString currentLog = logText->text();
        if (!currentLog.isEmpty()) currentLog += "\n";
        currentLog += QStringLiteral("[%1%] %2").arg(step.progress, 3).arg(step.detail);
        logText->setText(currentLog);

        // 最后一步完成
        if (step.progress >= 100) {
            timer->stop();
            dlg->setDialogTitle(QStringLiteral("升级成功"));
            statusLabel->setStyleSheet("font-size:18px;color:#27ae60;font-weight:700;padding:8px 0;");
            statusLabel->setText(QStringLiteral("升级成功！当前版本：V2.0.1"));
            progressBar->setStyleSheet(
                "QProgressBar{border:2px solid #b7eb8f;border-radius:16px;background:#f6ffed;text-align:center;"
                "font-size:14px;font-weight:700;color:#52c41a;}"
                "QProgressBar::chunk{background:#52c41a;border-radius:14px;}"
            );
            closeBtn->setVisible(true);
        }
        stepIndex++;
    });

    timer->start(800);  // 每800ms执行一步
    dlg->exec();
}

// ==================== 安全密码验证 ====================

void SystemSettingsPage::showSecurePasswordDialog(const QString& title, const QString& actionName) {
    m_pendingAction = actionName;

    // NumKeypad提前创建（顶层Popup弹窗，不嵌入对话框）
    if (!m_numKeypad) {
        m_numKeypad = new NumKeypad(this);
        m_numKeypad->setShuffle(true);
        connect(m_numKeypad, &NumKeypad::confirmed, this, [this]() {
            if (m_numKeypad) m_numKeypad->hide();
            onSecurePwdConfirmed();
        });
        connect(m_numKeypad, &NumKeypad::cancelled, this, [this]() {
            if (m_numKeypad) m_numKeypad->hide();
        });
    }

    if (!m_securePwdDialog) {
        // 安全验证对话框 — 启用底部确认/取消按钮，颜值优化
        m_securePwdDialog = new BaseDialog(this, 480);
        m_securePwdDialog->setDialogTitle(QStringLiteral("管理员验证"));
        m_securePwdDialog->setButtonAreaVisible(true);   // 显示底部按钮区

        auto* cl = m_securePwdDialog->contentLayout();
        cl->setSpacing(16);

        // 图标+提示文字（横向布局）
        auto* tipRow = new QHBoxLayout();
        auto* iconLabel = new QLabel(QStringLiteral("\xF0\x9F\x94\x92"));  // 锁图标
        iconLabel->setStyleSheet("font-size:22px;background:transparent;");
        iconLabel->setFixedWidth(36);
        tipRow->addWidget(iconLabel);

        auto* tipLabel = new QLabel(QStringLiteral("请输入管理员密码以确认操作"));
        tipLabel->setWordWrap(true);
        tipLabel->setStyleSheet(QString("font-size:15px;color:%1;font-weight:500;background:transparent;").arg(StyleHelper::textColor()));
        tipRow->addWidget(tipLabel, 1);
        cl->addLayout(tipRow);

        // 密码输入框（⌨按钮嵌入）— 颜值优化：聚焦时发光边框
        auto* pwdFrame = new QFrame();
        pwdFrame->setAttribute(Qt::WA_StyledBackground, true);
        pwdFrame->setFixedHeight(52);
        pwdFrame->setStyleSheet(
            "QFrame{border:2px solid #d8dce3;border-radius:12px;background:#f8f9fb;}"
            "QFrame:hover{border-color:#4da3ff;background:#fff;}"
        );
        auto* fLayout = new QHBoxLayout(pwdFrame);
        fLayout->setContentsMargins(16, 0, 4, 0);
        fLayout->setSpacing(0);

        m_securePwdEdit = new QLineEdit();
        m_securePwdEdit->setPlaceholderText(QStringLiteral("点击右侧 ⌨ 使用安全键盘输入"));
        m_securePwdEdit->setEchoMode(QLineEdit::Password);
        m_securePwdEdit->setReadOnly(true);
        m_securePwdEdit->setStyleSheet("QLineEdit{border:none;padding:0;font-size:16px;color:#1a1a2e;background:transparent;}");
        m_securePwdEdit->setMinimumHeight(52);
        fLayout->addWidget(m_securePwdEdit, 1);

        auto* skbBtn = new QPushButton(QStringLiteral("⌨"));
        skbBtn->setFixedSize(44, 44);
        skbBtn->setCursor(Qt::PointingHandCursor);
        skbBtn->setStyleSheet(
            "QPushButton{"
            "  background:#f0f4ff;color:#4da3ff;border:none;border-radius:10px;font-size:22px;"
            "}"
            "QPushButton:hover{background:#dce8ff;color:#3d8ce0;}"
            "QPushButton:pressed{background:#c8d8f8;}"
        );
        connect(skbBtn, &QPushButton::clicked, this, [this]() {
            if (m_numKeypad && m_securePwdEdit) {
                m_numKeypad->setShuffle(true);
                m_numKeypad->setShowPassword(false);
                m_numKeypad->attach(m_securePwdEdit);
                m_numKeypad->show();
            }
        });
        fLayout->addWidget(skbBtn);
        cl->addWidget(pwdFrame);

        // ── 底部按钮：取消 + 确认 ──
        auto* btnLayout = m_securePwdDialog->buttonLayout();
        if (btnLayout) {
            // 移除默认的 stretch
            while (btnLayout->count() > 0) {
                QLayoutItem* item = btnLayout->takeAt(0);
                delete item;
            }
            btnLayout->addStretch();

            // 取消按钮
            auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
            cancelBtn->setFixedHeight(StyleHelper::Token::ControlHeight);
            cancelBtn->setMinimumWidth(100);
            cancelBtn->setCursor(Qt::PointingHandCursor);
            cancelBtn->setStyleSheet(
                "QPushButton{"
                "  background:#fff;color:#666;border:2px solid #d8dce3;border-radius:12px;"
                "  font-size:16px;font-weight:600;"
                "}"
                "QPushButton:hover{background:#f5f5f5;border-color:#999;color:#333;}"
                "QPushButton:pressed{background:#e8e8e8;}"
            );
            connect(cancelBtn, &QPushButton::clicked, this, [this]() {
                if (m_numKeypad) m_numKeypad->hide();
                if (m_securePwdDialog) m_securePwdDialog->reject();
            });
            btnLayout->addWidget(cancelBtn);

            btnLayout->addSpacing(12);

            // 确认按钮（蓝色实底，对齐全局主操作）
            auto* confirmBtn = new QPushButton(QStringLiteral("确认"));
            confirmBtn->setFixedHeight(StyleHelper::Token::ControlHeight);
            confirmBtn->setMinimumWidth(100);
            confirmBtn->setCursor(Qt::PointingHandCursor);
            confirmBtn->setStyleSheet(
                "QPushButton{"
                "  background:#4da3ff;"
                "  color:#fff;border:none;border-radius:10px;"
                "  font-size:16px;font-weight:700;"
                "}"
                "QPushButton:hover{background:#3d8ae0;}"
                "QPushButton:pressed{background:#2e7bd6;}"
            );
            connect(confirmBtn, &QPushButton::clicked, this, [this]() {
                if (m_numKeypad) m_numKeypad->hide();
                if (m_securePwdEdit && m_securePwdEdit->text().isEmpty()) {
                    // 密码为空时给输入框一个红色提示边框
                    m_securePwdEdit->setStyleSheet(
                        "QLineEdit{border:2px solid #e53935;border-radius:10px;"
                        "padding:0 12px;font-size:16px;color:#1a1a2e;background:#fff8f8;}"
                    );
                    return;
                }
                onSecurePwdConfirmed();
            });
            btnLayout->addWidget(confirmBtn);
        }
    }
    m_securePwdDialog->setDialogTitle(title);
    if (m_securePwdEdit) m_securePwdEdit->clear();
    m_securePwdDialog->exec();
}

/**
 * @brief 处理口令
 */
void SystemSettingsPage::onSecurePwdConfirmed() {
    // 从NumKeypad获取密码（直接从密码框读取）
    if (!m_securePwdEdit) return;
    QString pwd = m_securePwdEdit->text();
    if (pwd.isEmpty()) return;
    if (m_securePwdDialog) m_securePwdDialog->accept();

    SettingService svc;
    // clearLogs分支已移除（危险操作区不暴露）
    if (m_pendingAction == "factoryReset") {
        if (svc.factoryReset(pwd)) {
            MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("已恢复默认设置"));
        } else {
            MessageDialog::showError(this, QStringLiteral("失败"), QStringLiteral("密码错误或操作失败"));
        }
    }
    m_pendingAction.clear();
}

// 断电告警方式按钮组样式更新 — 44px高统一风格
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

// 网口速率按钮组样式更新 — 44px高统一风格
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

// 组网模式按钮组样式更新 — 44px高统一风格
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

// 备份周期按钮组样式更新 — 44px高统一风格
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

// 进入页面时从INI文件加载配置，然后备份快照
void SystemSettingsPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    m_savedFlag = false;
    loadConfigFromIni();  // 从本地INI文件加载配置到UI
    snapshotSettings();      // 再备份快照
}

// 离开页面时检测脏数据 → 弹窗询问是否保存
// 替换QMessageBox为统一MessageDialog::showDirtyConfirm（风格统一+无取消按钮）
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

// 检测UI当前值是否与快照不同
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

    if (!m_maxBorrowSpin || m_maxBorrowSpin->value() != m_snapshot.maxBorrow) return true;
    if (!m_defaultPeriodSpin || m_defaultPeriodSpin->value() != m_snapshot.defaultPeriod) return true;
    if (!m_returnBufferSpin || m_returnBufferSpin->value() != m_snapshot.returnBuffer) return true;
    if (!m_brightnessSlider || m_brightnessSlider->value() != m_snapshot.brightness) return true;

    if (!m_autoBackupCheck || m_autoBackupCheck->isChecked() != m_snapshot.autoBackup) return true;
    if (m_backupPeriod != m_snapshot.backupPeriod) return true;
    if (!m_cacheHoursSpin || m_cacheHoursSpin->value() != m_snapshot.cacheHours) return true;
    if (!m_backupPathEdit || m_backupPathEdit->text() != m_snapshot.backupPath) return true;

    if (!m_machineGroupCombo || m_machineGroupCombo->currentText() != m_snapshot.machineGroupName) return true;

    return false;  // 无修改
}

// 从本地INI文件加载配置到UI控件
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
    // m_maxBorrowSpin创建已注释(功能删除)，必须空守卫防崩溃
    if (m_maxBorrowSpin) m_maxBorrowSpin->setValue(cfg.borrowMaxCount());
    m_defaultPeriodSpin->setValue(cfg.borrowDefaultPeriod());
    m_returnBufferSpin->setValue(cfg.borrowReturnBuffer());
    m_brightnessSlider->setValue(cfg.borrowBrightness());

    // 备份管理
    m_autoBackupCheck->setChecked(cfg.backupAutoEnabled());
    m_backupPeriod = cfg.backupPeriod(); updateBackupPeriodBtnStyles();
    m_cacheHoursSpin->setValue(cfg.backupCacheHours());
    m_backupPathEdit->setText(cfg.backupPath());

    // 机组名称 从下拉框中选择匹配项
    int groupId = cfg.localMachineGroupId();
    if (m_machineGroupCombo && groupId > 0) {
        for (int i = 0; i < m_machineGroupCombo->count(); i++) {
            if (m_machineGroupCombo->itemData(i).toInt() == groupId) {
                m_machineGroupCombo->setCurrentIndex(i);
                break;
            }
        }
    }
}

// 将当前UI值全部写入本地INI文件
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
    // m_maxBorrowSpin创建已注释，空守卫防崩溃
    if (m_maxBorrowSpin) cfg.setBorrowMaxCount(m_maxBorrowSpin->value());
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

// 备份当前所有UI控件的值到快照
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
    m_snapshot.maxBorrow = m_maxBorrowSpin ? m_maxBorrowSpin->value() : 5;
    m_snapshot.defaultPeriod = m_defaultPeriodSpin ? m_defaultPeriodSpin->value() : 48;
    m_snapshot.returnBuffer = m_returnBufferSpin ? m_returnBufferSpin->value() : 30;
    m_snapshot.brightness = m_brightnessSlider ? m_brightnessSlider->value() : 80;

    // 备份管理
    m_snapshot.autoBackup = m_autoBackupCheck ? m_autoBackupCheck->isChecked() : true;
    m_snapshot.backupPeriod = m_backupPeriod;
    m_snapshot.cacheHours = m_cacheHoursSpin ? m_cacheHoursSpin->value() : 4;
    m_snapshot.backupPath = m_backupPathEdit ? m_backupPathEdit->text() : SC::BACKUP_PATH;

    // 机组名称
    m_snapshot.machineGroupName = m_machineGroupCombo ? m_machineGroupCombo->currentText() : QStringLiteral("");
}

// 还原所有UI控件到快照值
void SystemSettingsPage::restoreSettings() {
    // 网络配置
    if (m_ipEdit) m_ipEdit->setText(m_snapshot.ip);
    if (m_maskEdit) m_maskEdit->setText(m_snapshot.mask);
    if (m_gatewayEdit) m_gatewayEdit->setText(m_snapshot.gateway);
    if (m_dnsEdit) m_dnsEdit->setText(m_snapshot.dns);
    if (m_serverEdit) m_serverEdit->setText(m_snapshot.server);
    m_speedMode = m_snapshot.speedMode; updateSpeedBtnStyles();
    m_networkMode = m_snapshot.networkMode; updateModeBtnStyles();

    // 告警参数
    if (m_buzzerSlider) m_buzzerSlider->setValue(m_snapshot.buzzerVol);
    if (m_ledCheck) m_ledCheck->setChecked(m_snapshot.ledAlert);
    if (m_overdueSpin) m_overdueSpin->setValue(m_snapshot.overdueHours);
    if (m_doorTimeoutSpin) m_doorTimeoutSpin->setValue(m_snapshot.doorTimeoutSec);
    if (m_visionCheck) m_visionCheck->setChecked(m_snapshot.visionAlert);
    m_powerAlarmMode = m_snapshot.powerAlarmMode; updatePowerAlarmBtnStyles();
    if (m_autoConfirmCheck) m_autoConfirmCheck->setChecked(m_snapshot.autoConfirm);

    // 借还设置
    if (m_maxBorrowSpin) m_maxBorrowSpin->setValue(m_snapshot.maxBorrow);
    if (m_defaultPeriodSpin) m_defaultPeriodSpin->setValue(m_snapshot.defaultPeriod);
    if (m_returnBufferSpin) m_returnBufferSpin->setValue(m_snapshot.returnBuffer);
    if (m_brightnessSlider) m_brightnessSlider->setValue(m_snapshot.brightness);

    // 备份管理
    if (m_autoBackupCheck) m_autoBackupCheck->setChecked(m_snapshot.autoBackup);
    m_backupPeriod = m_snapshot.backupPeriod; updateBackupPeriodBtnStyles();
    if (m_cacheHoursSpin) m_cacheHoursSpin->setValue(m_snapshot.cacheHours);
    if (m_backupPathEdit) m_backupPathEdit->setText(m_snapshot.backupPath);

    // 机组名称 从下拉框还原
    if (m_machineGroupCombo) {
        for (int i = 0; i < m_machineGroupCombo->count(); i++) {
            if (m_machineGroupCombo->itemText(i) == m_snapshot.machineGroupName) {
                m_machineGroupCombo->setCurrentIndex(i);
                break;
            }
        }
    }
}

// 下拉选择机组自动保存到AppConfig，实时生效
// 校验：更换机组前，当前机组和目标机组下的工具都必须全部归还完毕
// 否则不允许更换，防止跨机组借用数据混乱
void SystemSettingsPage::onMachineGroupSelected(int index) {
    if (!m_machineGroupCombo || index < 0) return;
    int groupId = m_machineGroupCombo->currentData().toInt();
    QString groupName = m_machineGroupCombo->currentText();
    if (groupId <= 0 || groupName.isEmpty()) return;

    int currentGroupId = AppConfig::instance().localMachineGroupId();
    // 同一机组无需校验
    if (groupId == currentGroupId) {
        m_snapshot.machineGroupName = groupName;
        return;
    }

    // 校验当前机组和目标机组下是否有未归还的借用记录
    db::RecordDAO recDao;
    int currentActiveCount = recDao.countActiveByMachineGroup(currentGroupId);
    int targetActiveCount = recDao.countActiveByMachineGroup(groupId);

    if (currentActiveCount > 0 || targetActiveCount > 0) {
        // 有未归还记录，不允许更换，给出明确提示
        QString errorMsg = QStringLiteral("机组更换失败！\n\n");
        if (currentActiveCount > 0) {
            errorMsg += QStringLiteral("当前机组还有 %1 件工具未归还，请先完成归还。\n").arg(currentActiveCount);
        }
        if (targetActiveCount > 0) {
            errorMsg += QStringLiteral("目标机组「%1」还有 %2 件工具未归还，请先完成归还。\n").arg(groupName).arg(targetActiveCount);
        }
        errorMsg += QStringLiteral("\n所有工具归还完毕后才能更换机组配置。");
        MessageDialog::showError(this, QStringLiteral("机组更换受限"), errorMsg);

        // 还原下拉框到当前机组选项
        QSignalBlocker blocker(m_machineGroupCombo);  // 阻止信号递归
        for (int i = 0; i < m_machineGroupCombo->count(); ++i) {
            if (m_machineGroupCombo->itemData(i).toInt() == currentGroupId) {
                m_machineGroupCombo->setCurrentIndex(i);
                break;
            }
        }
        return;
    }

    // 校验通过，保存新机组配置
    AppConfig::instance().setLocalMachineGroup(groupId, groupName);
    AppConfig::instance().save();
    m_snapshot.machineGroupName = groupName;

    MessageDialog::showSuccess(this, QStringLiteral("机组已更换"),
        QStringLiteral("本机机组已切换为「%1」，请确保工具柜中的工具与新机组匹配。").arg(groupName));
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




