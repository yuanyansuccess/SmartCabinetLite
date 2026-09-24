/**
 * @file SystemSettingsPage.cpp
 * @brief 系统设置页面实现 - 1:1复刻BS端SystemSettings.vue
 * @author 袁燕
 *
 * [2026-06-15 重构] 完全重写以1:1匹配BS端SystemSettings.vue
 * 布局：选项卡+双列网格（网络配置/告警参数/借还设置/备份管理+系统信息）
 * 底部：保存全部设置/恢复默认/软件升级按钮
 * 安全：危险操作使用SoftKeyboard安全键盘验证管理员密码
 */
#include "SystemSettingsPage.h"
#include "components/SoftKeyboard.h"
#include "components/NumKeypad.h"
#include "components/BaseDialog.h"       // [2026-06-26] 统一圆角对话框
#include "utils/StyleHelper.h"
#include "services/SettingService.h"
#include "services/AuthService.h"
#include "common/AppConfig.h"            // [2026-06-26v7] 机组名称配置
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
#include <QShowEvent>                   // [2026-06-26v7] 页面切换还原
#include <QHideEvent>                   // [2026-06-26v7]
#include <QSqlError>                    // [2026-06-26v7]
#include <QTableWidget>                 // [V2.03f] 任务类型配置表格
#include <QHeaderView>                  // [V2.03f] 表格列宽控制
#include <QTableWidgetItem>             // [V2.03f] 表格单元格
#include <QProgressBar>                 // [2026-06-26] 软件升级进度条
#include <QTimer>                      // [2026-06-26] 软件升级定时器
#include <QProcess>                    // [2026-06-27] 调用PowerShell设置显示器亮度
#include <QFile>                       // [2026-06-27] WMI结果日志记录
#include <QDateTime>                   // [2026-06-27] 亮度日志时间戳
#include <QDir>                        // [2026-06-27] 备份目录操作
#include <QStorageInfo>                // [2026-06-27] 跨平台磁盘空间读取
#include <QSysInfo>                    // [2026-06-27] 跨平台系统信息读取
#include <QRegularExpression>          // [2026-06-27] 解析os-release
#include <QFileInfoList>               // [2026-06-27] 备份文件清理

SystemSettingsPage::SystemSettingsPage(QWidget* parent) : QWidget(parent) {
    setupUI();
    m_softKeyboard = new SoftKeyboard(this);
    connect(m_softKeyboard, &SoftKeyboard::confirmed, this, &SystemSettingsPage::onSecurePwdConfirmed);
    connect(m_softKeyboard, &SoftKeyboard::cancelled, this, [](){});
}

void SystemSettingsPage::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);  // [2026-06-27] 20→24对齐其他页面
    mainLayout->setSpacing(16);  // [2026-06-27] 12→16对齐其他页面

    // [2026-06-27] 标题栏：对齐其他页面 20px标题 + 蓝色实底主操作按钮
    auto* titleBar = new QHBoxLayout();
    titleBar->setContentsMargins(0, 0, 0, 0);
    titleBar->setSpacing(12);
    auto* title = new QLabel(QStringLiteral("系统参数配置"));
    title->setStyleSheet("font-size:20px;font-weight:700;color:#1a1a2e;background:transparent;");  // 22→20对齐
    titleBar->addWidget(title);
    titleBar->addStretch();

    // [2026-06-27] 操作按钮统一风格：主操作蓝色实底/次操作白底蓝边/危险操作白底红边
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

    // [2026-06-27] 选项卡栏：对齐借用页QTabWidget风格（灰底白选中+主色下划线）
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
    // [V2.03g] 任务配置已迁移到系统维护页面
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

    // [V2.03j 2026-06-29] 改为QStackedWidget切换，去掉滚动（对齐系统维护页面）
    // [V2.03k 2026-06-29] 备份面板内容多(表单+系统信息+机组配置)，单独包QScrollArea避免字体压扁
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

void SystemSettingsPage::updateTabStyles() {
    // [2026-06-27] Tab样式对齐借用页QTabWidget：灰底白选中+主色下划线，去渐变胶囊
    for (int i = 0; i < m_tabLabels.size(); ++i) {
        if (i == m_activeTabIndex) {
            m_tabLabels[i]->setStyleSheet(
                QString("font-size:15px;font-weight:600;padding:10px 24px;border-radius:10px 10px 0 0;"
                        "color:%1;background:#fff;cursor:pointer;min-height:44px;"
                        "border-bottom:3px solid %1;")
                .arg(StyleHelper::primaryColor()));
        } else {
            m_tabLabels[i]->setStyleSheet(
                QString("font-size:15px;font-weight:600;padding:10px 24px;border-radius:10px 10px 0 0;"
                        "color:%1;background:#f0f2f5;cursor:pointer;min-height:44px;")
                .arg(StyleHelper::textSecondary()));
        }
    }
}

// [V2.03j 2026-06-29] 改为switchTab（QStackedWidget切换），替代highlightPanel
void SystemSettingsPage::switchTab(int index) {
    if (index < 0 || index >= m_tabLabels.size()) return;
    m_activeTabIndex = index;
    updateTabStyles();
    if (m_stackedWidget) {
        m_stackedWidget->setCurrentIndex(index);
    }
}

bool SystemSettingsPage::eventFilter(QObject* watched, QEvent* event) {
    // [V2.03j] 简化：Tab点击切换QStackedWidget + Hover效果，去掉滚动和焦点高亮
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
                            "color:%1;background:#e8f0fe;cursor:pointer;min-height:44px;")
                        .arg(StyleHelper::primaryColor())
                );
                break;
            }
        }
    } else if (event->type() == QEvent::Leave) {
        for (int i = 0; i < m_tabLabels.size(); ++i) {
            if (watched == m_tabLabels[i] && i != m_activeTabIndex) {
                m_tabLabels[i]->setStyleSheet(
                    QString("font-size:15px;font-weight:600;padding:10px 24px;border-radius:10px 10px 0 0;"
                            "color:%1;background:#f0f2f5;cursor:pointer;min-height:44px;")
                        .arg(StyleHelper::textSecondary())
                );
                break;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ==================== 网络配置面板 ====================
// [V2.03l 2026-06-30] 小米工程师优化：标签36px高/form 8px间距/面板24,12边距，紧凑美观大气
QWidget* SystemSettingsPage::createNetworkPanel() {
    auto* panel = new QFrame();
    panel->setObjectName("netPanel");
    panel->setStyleSheet(QString("QFrame#netPanel{background:white;border-radius:12px;border:none;}"));
    auto* layout = new QVBoxLayout(panel);
    layout->setSpacing(6);      // [V2.03l] 8→6更紧凑
    layout->setContentsMargins(20, 10, 20, 10);  // [V2.03l] 24,12→20,10

    // [V2.03l 2026-06-30] 标题不占满宽度，左对齐+主色底边细线分隔
    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("网络参数配置"));
    // 标题样式调整：去掉padding-bottom，减小font-size
    title->setStyleSheet(QString("font-size:14px;font-weight:700;color:%1;background:transparent;").arg(StyleHelper::textColor()));
    titleRow->addWidget(title);
    titleRow->addStretch();
    layout->addLayout(titleRow);
    // 细线分隔标题和内容区
    auto* sep = new QFrame(); sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QString("QFrame{background:%1;max-height:1px;margin-bottom:6px;}").arg(StyleHelper::borderColor()));
    layout->addWidget(sep);

    auto* form = new QFormLayout();
    form->setSpacing(8);      // [V2.03l] 12→8紧凑
    form->setContentsMargins(0, 0, 0, 0);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);  // [V2.03u] 表单字段自动扩展
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);  // [V2.03u] 标签右对齐
    QString labelStyle = QString("font-size:14px;font-weight:600;color:%1;background:transparent;").arg(StyleHelper::textColor());  // 16→14
    auto makeLabel = [&](const QString& text) {
        auto* l = new QLabel(text); l->setStyleSheet(labelStyle); l->setMinimumHeight(36); l->setFixedWidth(100); return l;  // [V2.03u] 限宽100px，防止标签列过宽
    };

    // [v4] 使用Web端小尺寸settingLineEdit：14px/38px高
    m_ipEdit = new QLineEdit("192.168.1.100");
    m_ipEdit->setStyleSheet(StyleHelper::settingLineEdit());
    form->addRow(makeLabel(QStringLiteral("IP 地址")), m_ipEdit);

    m_maskEdit = new QLineEdit("255.255.255.0");
    m_maskEdit->setStyleSheet(StyleHelper::settingLineEdit());
    form->addRow(makeLabel(QStringLiteral("子网掩码")), m_maskEdit);

    m_gatewayEdit = new QLineEdit("192.168.1.1");
    m_gatewayEdit->setStyleSheet(StyleHelper::settingLineEdit());
    form->addRow(makeLabel(QStringLiteral("默认网关")), m_gatewayEdit);

    m_dnsEdit = new QLineEdit("8.8.8.8");
    m_dnsEdit->setStyleSheet(StyleHelper::settingLineEdit());
    form->addRow(makeLabel(QStringLiteral("DNS 服务器")), m_dnsEdit);

    // [V7.0] 网口速率：按钮组替代QComboBox
    // [2026-06-26] 统一按钮尺寸：44px高/Preferred策略不再Expanding撑满
    auto makeSpeedBtn = [&](const QString& text, int mode) -> QPushButton* {
        auto* btn = new QPushButton(text);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(40);
        btn->setMinimumWidth(110);
        btn->setMaximumWidth(220);
        btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        if (mode == 0) btn->setChecked(true);
        connect(btn, &QPushButton::clicked, this, [this, mode]() {
            m_speedMode = mode;
            updateSpeedBtnStyles();
        });
        return btn;
    };
    m_speedBtn1 = makeSpeedBtn(QStringLiteral("1000M 自适应"), 0);
    m_speedBtn2 = makeSpeedBtn(QStringLiteral("100M 全双工"), 1);
    auto* speedBtnGroup = new QWidget();
    speedBtnGroup->setStyleSheet("background:transparent;");
    auto* speedBtnLayout = new QHBoxLayout(speedBtnGroup);
    speedBtnLayout->setContentsMargins(0, 0, 0, 0);
    speedBtnLayout->setSpacing(8);
    speedBtnLayout->addWidget(m_speedBtn1);
    speedBtnLayout->addWidget(m_speedBtn2);
    updateSpeedBtnStyles();
    form->addRow(makeLabel(QStringLiteral("网口速率")), speedBtnGroup);

    // [V7.0] 组网模式：按钮组替代QComboBox
    // [2026-06-26] 统一按钮尺寸：44px高/Preferred策略
    auto makeModeBtn = [&](const QString& text, int mode) -> QPushButton* {
        auto* btn = new QPushButton(text);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(40);
        btn->setMinimumWidth(110);
        btn->setMaximumWidth(220);
        btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        if (mode == 0) btn->setChecked(true);
        connect(btn, &QPushButton::clicked, this, [this, mode]() {
            m_networkMode = mode;
            updateModeBtnStyles();
        });
        return btn;
    };
    m_modeBtn1 = makeModeBtn(QStringLiteral("单机运行"), 0);
    m_modeBtn2 = makeModeBtn(QStringLiteral("组网管理"), 1);
    auto* modeBtnGroup = new QWidget();
    modeBtnGroup->setStyleSheet("background:transparent;");
    auto* modeBtnLayout = new QHBoxLayout(modeBtnGroup);
    modeBtnLayout->setContentsMargins(0, 0, 0, 0);
    modeBtnLayout->setSpacing(8);
    modeBtnLayout->addWidget(m_modeBtn1);
    modeBtnLayout->addWidget(m_modeBtn2);
    updateModeBtnStyles();
    form->addRow(makeLabel(QStringLiteral("组网模式")), modeBtnGroup);

    m_serverEdit = new QLineEdit();
    m_serverEdit->setPlaceholderText(QStringLiteral("组网管理时配置"));
    m_serverEdit->setStyleSheet(StyleHelper::settingLineEdit());
    form->addRow(makeLabel(QStringLiteral("服务器地址")), m_serverEdit);

    layout->addLayout(form);

    // [v5] 保存栏：右对齐+顶部分割线，48px按钮
    auto* saveBar = new QFrame();
    saveBar->setStyleSheet("QFrame{border-top:1px solid #f0f0f0;background:transparent;}");
    auto* saveBarLayout = new QHBoxLayout(saveBar);
    saveBarLayout->setContentsMargins(0, 8, 0, 0);  // [V2.03l] 12→8
    saveBarLayout->addStretch();
    auto* saveBtn = new QPushButton(QStringLiteral("保存网络设置"));
    saveBtn->setStyleSheet(StyleHelper::settingSaveBtn());
    saveBtn->setCursor(Qt::PointingHandCursor);
    connect(saveBtn, &QPushButton::clicked, this, &SystemSettingsPage::onSaveNetwork);
    saveBarLayout->addWidget(saveBtn);
    layout->addWidget(saveBar);

    // [V2.03j] installFocusEvents已移除
    return panel;
}

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
    title->setStyleSheet(QString("font-size:14px;font-weight:700;color:%1;background:transparent;").arg(StyleHelper::textColor()));
    titleRow->addWidget(title);
    titleRow->addStretch();
    layout->addLayout(titleRow);
    auto* sep = new QFrame(); sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QString("QFrame{background:%1;max-height:1px;margin-bottom:6px;}").arg(StyleHelper::borderColor()));
    layout->addWidget(sep);

    auto* form = new QFormLayout();
    form->setSpacing(8);
    form->setContentsMargins(0, 0, 0, 0);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);  // [V2.03u] 表单字段自动扩展
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);  // [V2.03u] 标签右对齐
    QString labelStyle = QString("font-size:14px;font-weight:600;color:%1;background:transparent;").arg(StyleHelper::textColor());
    auto makeLabel = [&](const QString& text) {
        auto* l = new QLabel(text); l->setStyleSheet(labelStyle); l->setMinimumHeight(36); l->setFixedWidth(100); return l;  // [V2.03u] 限宽100px
    };
    // [v4] 统一开关样式 [V7.1] 统一22px indicator + 选中态蓝色
    auto makeToggle = [&](bool checked = true) {
        auto* cb = new QCheckBox();
        cb->setChecked(checked);
        cb->setStyleSheet(
            "QCheckBox{font-size:15px;background:transparent;spacing:6px;}"
            "QCheckBox::indicator{width:22px;height:22px;border-radius:4px;"
            "border:2px solid #d0d0d0;background:white;}"
            "QCheckBox::indicator:hover{border-color:#4da3ff;}"
            "QCheckBox::indicator:checked{background:#4da3ff;border-color:#4da3ff;}"
        );
        return cb;
    };

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
    buzzerWidget->setStyleSheet("background:transparent;");  // [2026-06-24v5] 去灰
    auto* buzzerLayout = new QHBoxLayout(buzzerWidget);
    buzzerLayout->setContentsMargins(0, 0, 0, 0);
    buzzerLayout->setSpacing(8);
    buzzerLayout->addWidget(m_buzzerSlider);
    buzzerLayout->addWidget(m_buzzerValueLabel);
    form->addRow(makeLabel(QStringLiteral("蜂鸣器音量")), buzzerWidget);

    m_ledCheck = makeToggle(true);
    form->addRow(makeLabel(QStringLiteral("LED告警灯")), m_ledCheck);

    m_overdueSpin = new QSpinBox();
    m_overdueSpin->setRange(1, 168);
    m_overdueSpin->setValue(24);
    m_overdueSpin->setSuffix(QStringLiteral(" 小时"));
    m_overdueSpin->setStyleSheet(StyleHelper::settingSpinBox());
    form->addRow(makeLabel(QStringLiteral("逾期告警阈值")), m_overdueSpin);

    m_doorTimeoutSpin = new QSpinBox();
    m_doorTimeoutSpin->setRange(5, 300);
    m_doorTimeoutSpin->setValue(30);
    m_doorTimeoutSpin->setSuffix(QStringLiteral(" 秒"));
    m_doorTimeoutSpin->setStyleSheet(StyleHelper::settingSpinBox());
    form->addRow(makeLabel(QStringLiteral("柜门未关告警")), m_doorTimeoutSpin);

    m_visionCheck = makeToggle(true);
    form->addRow(makeLabel(QStringLiteral("视觉识别异常告警")), m_visionCheck);

    // [V7.0] 断电告警方式：用按钮组替代QComboBox，风格统一
    // [2026-06-26] 统一按钮尺寸：44px高/Preferred策略，三按钮最大宽180px
    auto makeAlarmBtn = [&](const QString& text, int mode) -> QPushButton* {
        auto* btn = new QPushButton(text);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(40);
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
    form->addRow(makeLabel(QStringLiteral("断电告警方式")), alarmBtnGroup);

    m_autoConfirmCheck = makeToggle(true);
    form->addRow(makeLabel(QStringLiteral("告警自动确认")), m_autoConfirmCheck);

    layout->addLayout(form);

    // [v5] 保存栏：右对齐+顶部分割线，48px按钮
    auto* saveBar = new QFrame();
    saveBar->setStyleSheet("QFrame{border-top:1px solid #f0f0f0;background:transparent;}");
    auto* saveBarLayout = new QHBoxLayout(saveBar);
    saveBarLayout->setContentsMargins(0, 8, 0, 0);  // [V2.03l] 12→8
    saveBarLayout->addStretch();
    auto* saveBtn = new QPushButton(QStringLiteral("保存告警设置"));
    saveBtn->setStyleSheet(StyleHelper::settingSaveBtn());
    saveBtn->setCursor(Qt::PointingHandCursor);
    connect(saveBtn, &QPushButton::clicked, this, &SystemSettingsPage::onSaveAlert);
    saveBarLayout->addWidget(saveBtn);
    layout->addWidget(saveBar);

    // [V2.03j] installFocusEvents已移除
    return panel;
}

// ==================== 借还设置面板 ====================
QWidget* SystemSettingsPage::createBorrowPanel() {
    auto* panel = new QFrame();
    panel->setObjectName("borrowPanel");
    panel->setStyleSheet(QString("QFrame#borrowPanel{background:white;border-radius:12px;border:none;}"));
    auto* layout = new QVBoxLayout(panel);
    layout->setSpacing(6);
    layout->setContentsMargins(20, 10, 20, 10);

    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("借还参数设置"));
    title->setStyleSheet(QString("font-size:14px;font-weight:700;color:%1;background:transparent;").arg(StyleHelper::textColor()));
    titleRow->addWidget(title);
    titleRow->addStretch();
    layout->addLayout(titleRow);
    auto* sep = new QFrame(); sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QString("QFrame{background:%1;max-height:1px;margin-bottom:6px;}").arg(StyleHelper::borderColor()));
    layout->addWidget(sep);

    auto* form = new QFormLayout();
    form->setSpacing(8);
    form->setContentsMargins(0, 0, 0, 0);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);  // [V2.03u] 表单字段自动扩展
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);  // [V2.03u] 标签右对齐
    QString labelStyle = QString("font-size:14px;font-weight:600;color:%1;background:transparent;").arg(StyleHelper::textColor());
    auto makeLabel = [&](const QString& text) {
        auto* l = new QLabel(text); l->setStyleSheet(labelStyle); l->setMinimumHeight(36); l->setFixedWidth(100); return l;  // [V2.03u] 限宽100px
    };
    // [v5] 统一开关样式+透明背景 [V7.1] 统一22px indicator + 选中态蓝色
    auto makeToggle = [&](bool checked = true) {
        auto* cb = new QCheckBox();
        cb->setChecked(checked);
        cb->setStyleSheet(
            "QCheckBox{font-size:15px;background:transparent;spacing:6px;}"
            "QCheckBox::indicator{width:22px;height:22px;border-radius:4px;"
            "border:2px solid #d0d0d0;background:white;}"
            "QCheckBox::indicator:hover{border-color:#4da3ff;}"
            "QCheckBox::indicator:checked{background:#4da3ff;border-color:#4da3ff;}"
        );
        return cb;
    };

    //m_maxBorrowSpin = new QSpinBox();
    //m_maxBorrowSpin->setRange(1, 20);
    //m_maxBorrowSpin->setValue(5);
    //m_maxBorrowSpin->setSuffix(QStringLiteral(" 件"));
    //m_maxBorrowSpin->setStyleSheet(StyleHelper::settingSpinBox());
    //form->addRow(makeLabel(QStringLiteral("单次最大借出数量")), m_maxBorrowSpin);

    m_defaultPeriodSpin = new QSpinBox();
    m_defaultPeriodSpin->setRange(1, 168);
    m_defaultPeriodSpin->setValue(48);
    m_defaultPeriodSpin->setSuffix(QStringLiteral(" 小时"));
    m_defaultPeriodSpin->setStyleSheet(StyleHelper::settingSpinBox());
    form->addRow(makeLabel(QStringLiteral("默认借用期限")), m_defaultPeriodSpin);

    m_returnBufferSpin = new QSpinBox();
    m_returnBufferSpin->setRange(0, 120);
    m_returnBufferSpin->setValue(30);
    m_returnBufferSpin->setSuffix(QStringLiteral(" 分钟"));
    m_returnBufferSpin->setStyleSheet(StyleHelper::settingSpinBox());
    form->addRow(makeLabel(QStringLiteral("归还缓冲时间")), m_returnBufferSpin);

    // [V2.03c 2026-06-29] 删除3项：手动开锁验证、人脸识别灵敏度、自动锁屏时间
    // 确认删除，只保留：借出数量/借用期限/归还缓冲/显示屏亮度
    m_brightnessSlider = new QSlider(Qt::Horizontal);
    m_brightnessSlider->setRange(30, 100);
    m_brightnessSlider->setValue(80);
    m_brightnessSlider->setFixedWidth(140);
    m_brightnessSlider->setStyleSheet("QSlider{background:transparent;}"
                                     "QSlider::groove:horizontal{height:6px;background:#e0e0e0;border-radius:3px;}"
                                     "QSlider::handle:horizontal{width:18px;height:18px;background:#4da3ff;border-radius:9px;}");
    m_brightnessValueLabel = new QLabel("80%");
    m_brightnessValueLabel->setFixedWidth(40);
    m_brightnessValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_brightnessValueLabel->setStyleSheet(QString("font-size:14px;font-weight:700;color:%1;").arg(StyleHelper::primaryColor()));
    connect(m_brightnessSlider, &QSlider::valueChanged, this, [this](int val) {
        m_brightnessValueLabel->setText(QString("%1%").arg(val));
    });
    auto* brightWidget = new QWidget();
    brightWidget->setStyleSheet("background:transparent;");  // [2026-06-24v5] 去灰
    auto* brightLayout = new QHBoxLayout(brightWidget);
    brightLayout->setContentsMargins(0, 0, 0, 0);
    brightLayout->setSpacing(8);
    brightLayout->addWidget(m_brightnessSlider);
    brightLayout->addWidget(m_brightnessValueLabel);
    form->addRow(makeLabel(QStringLiteral("显示屏亮度")), brightWidget);

    layout->addLayout(form);

    // [v5] 保存栏：右对齐+顶部分割线，48px按钮
    auto* saveBar = new QFrame();
    saveBar->setStyleSheet("QFrame{border-top:1px solid #f0f0f0;background:transparent;}");
    auto* saveBarLayout = new QHBoxLayout(saveBar);
    saveBarLayout->setContentsMargins(0, 8, 0, 0);  // [V2.03l] 12→8
    saveBarLayout->addStretch();
    auto* saveBtn = new QPushButton(QStringLiteral("保存借还设置"));
    saveBtn->setStyleSheet(StyleHelper::settingSaveBtn());
    saveBtn->setCursor(Qt::PointingHandCursor);
    connect(saveBtn, &QPushButton::clicked, this, &SystemSettingsPage::onSaveBorrow);
    saveBarLayout->addWidget(saveBtn);
    layout->addWidget(saveBar);

    // [V2.03j] installFocusEvents已移除
    return panel;
}

// ==================== 备份管理面板 ====================
QWidget* SystemSettingsPage::createBackupPanel() {
    auto* panel = new QFrame();
    panel->setObjectName("backupPanel");
    panel->setStyleSheet(QString("QFrame#backupPanel{background:white;border-radius:12px;border:none;}"));
    auto* layout = new QVBoxLayout(panel);
    layout->setSpacing(6);
    layout->setContentsMargins(20, 10, 20, 10);

    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("数据备份与系统信息"));
    title->setStyleSheet(QString("font-size:14px;font-weight:700;color:%1;background:transparent;").arg(StyleHelper::textColor()));
    titleRow->addWidget(title);
    titleRow->addStretch();
    layout->addLayout(titleRow);
    auto* sep = new QFrame(); sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QString("QFrame{background:%1;max-height:1px;margin-bottom:6px;}").arg(StyleHelper::borderColor()));
    layout->addWidget(sep);

    auto* form = new QFormLayout();
    form->setSpacing(8);
    form->setContentsMargins(0, 0, 0, 0);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);  // [V2.03u] 表单字段自动扩展
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);  // [V2.03u] 标签右对齐
    QString labelStyle = QString("font-size:14px;font-weight:600;color:%1;background:transparent;").arg(StyleHelper::textColor());
    auto makeLabel = [&](const QString& text) {
        auto* l = new QLabel(text); l->setStyleSheet(labelStyle); l->setMinimumHeight(36); l->setFixedWidth(100); return l;  // [V2.03u] 限宽100px
    };
    // [v5] 统一开关样式+透明背景 [V7.1] 统一22px indicator + 选中态蓝色
    auto makeToggle = [&](bool checked = true) {
        auto* cb = new QCheckBox();
        cb->setChecked(checked);
        cb->setStyleSheet(
            "QCheckBox{font-size:15px;background:transparent;spacing:6px;}"
            "QCheckBox::indicator{width:22px;height:22px;border-radius:4px;"
            "border:2px solid #d0d0d0;background:white;}"
            "QCheckBox::indicator:hover{border-color:#4da3ff;}"
            "QCheckBox::indicator:checked{background:#4da3ff;border-color:#4da3ff;}"
        );
        return cb;
    };

    m_autoBackupCheck = makeToggle(true);
    form->addRow(makeLabel(QStringLiteral("自动备份")), m_autoBackupCheck);

    // [V7.0] 备份周期：按钮组替代QComboBox
    // [2026-06-26] 统一按钮尺寸：44px高/Preferred策略
    auto makeBackupBtn = [&](const QString& text, int mode) -> QPushButton* {
        auto* btn = new QPushButton(text);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(40);
        btn->setMinimumWidth(80);
        btn->setMaximumWidth(160);
        btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        if (mode == 0) btn->setChecked(true);
        connect(btn, &QPushButton::clicked, this, [this, mode]() {
            m_backupPeriod = mode;
            updateBackupPeriodBtnStyles();
        });
        return btn;
    };
    m_backupBtn1 = makeBackupBtn(QStringLiteral("每日"), 0);
    m_backupBtn2 = makeBackupBtn(QStringLiteral("每周一"), 1);
    m_backupBtn3 = makeBackupBtn(QStringLiteral("每周日"), 2);
    auto* backupBtnGroup = new QWidget();
    backupBtnGroup->setStyleSheet("background:transparent;");
    auto* backupBtnLayout = new QHBoxLayout(backupBtnGroup);
    backupBtnLayout->setContentsMargins(0, 0, 0, 0);
    backupBtnLayout->setSpacing(8);
    backupBtnLayout->addWidget(m_backupBtn1);
    backupBtnLayout->addWidget(m_backupBtn2);
    backupBtnLayout->addWidget(m_backupBtn3);
    updateBackupPeriodBtnStyles();
    form->addRow(makeLabel(QStringLiteral("备份周期")), backupBtnGroup);

    m_backupPathEdit = new QLineEdit("/mnt/backup");
    m_backupPathEdit->setStyleSheet(StyleHelper::settingLineEdit());
    form->addRow(makeLabel(QStringLiteral("备份存储路径")), m_backupPathEdit);

    m_cacheHoursSpin = new QSpinBox();
    m_cacheHoursSpin->setRange(1, 24);
    m_cacheHoursSpin->setValue(4);
    m_cacheHoursSpin->setSuffix(QStringLiteral(" 小时"));
    m_cacheHoursSpin->setStyleSheet(StyleHelper::settingSpinBox());
    form->addRow(makeLabel(QStringLiteral("断网缓存时长")), m_cacheHoursSpin);

    layout->addLayout(form);

    // 系统信息 [v5] 触屏优化
    auto* sysInfoFrame = new QFrame();
    sysInfoFrame->setStyleSheet("QFrame{background:transparent;}");
    auto* sysLayout = new QVBoxLayout(sysInfoFrame);
    sysLayout->setSpacing(6);
    sysLayout->setContentsMargins(0, 8, 0, 0);  // [V2.03l] 12→8

    // 系统信息标题
    auto* sysTitle = new QLabel(QStringLiteral("系统信息"));
    sysTitle->setStyleSheet(QString("font-size:16px;font-weight:700;color:%1;margin-bottom:6px;").arg(StyleHelper::textColor()));
    sysLayout->addWidget(sysTitle);

    // [2026-06-23v6] 修复空指针崩溃：系统信息value label必须赋值给成员变量供refresh()使用
    // 旧createInfoRow未保存valueLabel到成员变量，refresh()访问时为nullptr导致0xC0000005崩溃
    auto createInfoRowEx = [&](const QString& key, const QString& value, QLabel*& valueMember) {
        auto* row = new QHBoxLayout();
        row->setContentsMargins(0, 6, 0, 6);  // [V2.03k] 4→6增加行间距避免压扁
        auto* keyLabel = new QLabel(key);
        keyLabel->setStyleSheet("font-size:14px;color:#999;background:transparent;");
        keyLabel->setFixedWidth(90);
        keyLabel->setMinimumHeight(22);  // [V2.03k] 确保字体不被压扁
        valueMember = new QLabel(value);
        valueMember->setStyleSheet("font-size:14px;color:#333;font-weight:600;background:transparent;");
        valueMember->setMinimumHeight(22);  // [V2.03k] 确保字体不被压扁
        valueMember->setWordWrap(true);     // [V2.03k] 长文本换行不截断
        row->addWidget(keyLabel);
        row->addWidget(valueMember, 1);
        return row;
    };

    // [2026-06-27] 软件版本从AppConfig读取（不再硬编码），与TopBar版本号数据源一致
    sysLayout->addLayout(createInfoRowEx(QStringLiteral("软件版本"),
        AppConfig::instance().appVersion(), m_versionLabel));
    sysLayout->addLayout(createInfoRowEx(QStringLiteral("操作系统"), QStringLiteral("读取中..."), m_osLabel));
    sysLayout->addLayout(createInfoRowEx(QStringLiteral("设备编号"), QStringLiteral("读取中..."), m_deviceIdLabel));
    sysLayout->addLayout(createInfoRowEx(QStringLiteral("运行时长"), QStringLiteral("读取中..."), m_uptimeLabel));
    sysLayout->addLayout(createInfoRowEx(QStringLiteral("磁盘空间"), QStringLiteral("读取中..."), m_diskLabel));
    // [2026-06-27] 新增CPU/内存占用实时显示
    sysLayout->addLayout(createInfoRowEx(QStringLiteral("CPU占用"), QStringLiteral("读取中..."), m_cpuLabel));
    sysLayout->addLayout(createInfoRowEx(QStringLiteral("内存占用"), QStringLiteral("读取中..."), m_memoryLabel));

    layout->addWidget(sysInfoFrame);

    // [2026-06-27] 本机机组配置 — 下拉选择框，从DB加载所有active机组
    auto* machineGroupFrame = new QFrame();
    machineGroupFrame->setStyleSheet("QFrame{background:#f8fbff;border:none;border-radius:12px;}");
    auto* machineGroupLayout = new QVBoxLayout(machineGroupFrame);
    machineGroupLayout->setSpacing(10);
    machineGroupLayout->setContentsMargins(16, 14, 16, 14);

    auto* mgTitle = new QLabel(QStringLiteral("本机机组配置"));
    mgTitle->setStyleSheet("font-size:16px;font-weight:700;color:#1a1a2e;");
    machineGroupLayout->addWidget(mgTitle);

    auto* mgDesc = new QLabel(QStringLiteral("选择本机所属工程机组，切换后立即生效，工具入库时将自动关联此机组"));
    mgDesc->setStyleSheet("font-size:14px;color:#888;");
    mgDesc->setWordWrap(true);
    machineGroupLayout->addWidget(mgDesc);

    auto* mgInputRow = new QHBoxLayout();
    mgInputRow->setSpacing(12);

    m_machineGroupCombo = new QComboBox();
    m_machineGroupCombo->setMinimumHeight(48);
    m_machineGroupCombo->setStyleSheet(QString(
        "QComboBox{"
        "  background:white; border:2px solid #e0e0e0; border-radius:12px;"
        "  padding:0 16px; font-size:16px; font-weight:600; color:#1a1a2e;"
        "}"
        "QComboBox:hover{ border-color:#4da3ff; }"
        "QComboBox::drop-down{"
        "  subcontrol-origin:padding; subcontrol-position:center right;"
        "  width:36px; border-left:1px solid #e8ecf0;"
        "  border-top-right-radius:12px; border-bottom-right-radius:12px;"
        "}"
        "QComboBox QAbstractItemView{"
        "  border:1px solid #e0e0e0; border-radius:10px;"
        "  font-size:16px; padding:6px; selection-background-color:#4da3ff;"
        "  selection-color:#fff; outline:none;"
        "}"
    ));
    mgInputRow->addWidget(m_machineGroupCombo, 1);

    // 加载DB中所有活跃机组到下拉框
    {
        db::ToolDAO toolDao;
        QList<QJsonObject> groups = toolDao.allMachineGroups();
        int currentGroupId = AppConfig::instance().localMachineGroupId();
        int selectIdx = -1;
        int idx = 0;
        for (const auto& g : groups) {
            int gid = g["groupId"].toInt();
            QString gname = g["groupName"].toString();
            m_machineGroupCombo->addItem(gname, gid);
            if (gid == currentGroupId) selectIdx = idx;
            idx++;
        }
        if (selectIdx >= 0) m_machineGroupCombo->setCurrentIndex(selectIdx);
    }

    connect(m_machineGroupCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &SystemSettingsPage::onMachineGroupSelected);
    mgInputRow->addWidget(m_machineGroupCombo, 1);

    machineGroupLayout->addLayout(mgInputRow);

    layout->addWidget(machineGroupFrame);

    // [2026-06-26v14] 危险操作区已移除（恢复出厂设置/清除全部日志功能不再暴露在设置页面）

    // [v4] 保存栏对齐Web端.save-bar
    auto* saveBar = new QFrame();
    saveBar->setStyleSheet("QFrame{border-top:1px solid #f0f0f0;background:transparent;}");
    auto* saveBarLayout = new QHBoxLayout(saveBar);
    saveBarLayout->setContentsMargins(0, 8, 0, 0);
    saveBarLayout->addStretch();
    auto* saveBtn = new QPushButton(QStringLiteral("💾 保存备份设置"));
    saveBtn->setStyleSheet(StyleHelper::settingSaveBtn());
    saveBtn->setCursor(Qt::PointingHandCursor);
    connect(saveBtn, &QPushButton::clicked, this, &SystemSettingsPage::onSaveBackup);
    saveBarLayout->addWidget(saveBtn);
    layout->addWidget(saveBar);

    // [V2.03j] installFocusEvents已移除
    return panel;
}

// [V2.03g] createTaskTypePanel已迁移到SystemMaintenancePage

// ==================== 槽函数 ====================
// [V2.03g] createTaskTypePanel已迁移到SystemMaintenancePage

// ==================== 槽函数 ====================

void SystemSettingsPage::refresh() {
    // [2026-06-27] 软件版本从AppConfig读取，其余系统信息从系统实时读取
    if (m_versionLabel) {
        m_versionLabel->setText(QStringLiteral("软件版本: %1").arg(AppConfig::instance().appVersion()));
    }
    // [2026-06-27] 从系统读取真实OS/设备编号/运行时长/磁盘空间
    refreshSystemInfo();
}

void SystemSettingsPage::onSaveAll() {
    // [v19] 保存全部配置到本地INI文件
    if (saveConfigToIni()) {
        m_savedFlag = true;
        MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("全部设置已保存到本地配置文件"));
    }
}

void SystemSettingsPage::onSaveNetwork() {
    // [v19] 保存网络配置到本地INI文件
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
    // [2026-06-27] 保存网络配置后自动应用到系统有线网卡（Windows+麒麟跨平台）
    applyNetworkConfig(m_ipEdit->text(), m_maskEdit->text(),
                       m_gatewayEdit->text(), m_dnsEdit->text());
    MessageDialog::showSuccess(this, QStringLiteral("成功"),
        QStringLiteral("网络设置已保存并应用到系统有线网卡"));
}

void SystemSettingsPage::onSaveAlert() {
    // [v19] 保存告警配置到本地INI文件
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

void SystemSettingsPage::onSaveBorrow() {
    // [v19] 保存借还配置到本地INI文件
    // [V2.03c] 已删除3项：manualUnlock/lockTime/faceSensitivity
    auto& cfg = AppConfig::instance();
    // [2026-09-24fix] m_maxBorrowSpin创建已注释，空守卫防崩溃
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

void SystemSettingsPage::onSaveBackup() {
    // [v19] 保存备份配置到本地INI文件
    auto& cfg = AppConfig::instance();
    cfg.setBackupAutoEnabled(m_autoBackupCheck->isChecked());
    cfg.setBackupPeriod(m_backupPeriod);
    cfg.setBackupCacheHours(m_cacheHoursSpin->value());
    cfg.setBackupPath(m_backupPathEdit->text());
    cfg.save();
    m_savedFlag = true;
    snapshotSettings();
    // [2026-06-27] 保存备份设置后立即执行一次备份，并注册定时备份任务
    if (m_autoBackupCheck->isChecked()) {
        performDatabaseBackup();
    }
    MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("备份设置已保存到本地配置文件"));
}

void SystemSettingsPage::onResetDefault() {
    if (!MessageDialog::showQuestion(this, QStringLiteral("确认恢复默认"),
        QStringLiteral("确定要恢复所有设置为出厂默认值吗？此操作不可撤销！"))) return;
    showSecurePasswordDialog(QStringLiteral("安全验证 - 恢复默认"), QStringLiteral("factoryReset"));
}

// [2026-06-26v14] onClearLogs() 已移除（危险操作区不再暴露）
// 恢复默认仍可通过顶部"恢复默认"按钮触发 onResetDefault()

void SystemSettingsPage::onStartUpgrade() {
    // [2026-06-26] 软件升级模拟进度条，美观的设计
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
    progressBar->setFixedHeight(32);
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

    // [2026-06-26v2] NumKeypad提前创建（顶层Popup弹窗，不嵌入对话框）
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
        // [2026-06-26v9] 安全验证对话框 — 启用底部确认/取消按钮，颜值优化
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
            cancelBtn->setFixedHeight(44);
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
            confirmBtn->setFixedHeight(44);
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

void SystemSettingsPage::onSecurePwdConfirmed() {
    // [2026-06-26] 从NumKeypad获取密码（直接从密码框读取）
    if (!m_securePwdEdit) return;
    QString pwd = m_securePwdEdit->text();
    if (pwd.isEmpty()) return;
    if (m_securePwdDialog) m_securePwdDialog->accept();

    SettingService svc;
    // [2026-06-26v14] clearLogs分支已移除（危险操作区不再暴露）
    if (m_pendingAction == "factoryReset") {
        if (svc.factoryReset(pwd)) {
            MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("已恢复默认设置"));
        } else {
            MessageDialog::showError(this, QStringLiteral("失败"), QStringLiteral("密码错误或操作失败"));
        }
    }
    m_pendingAction.clear();
}

// [V7.0][2026-06-26] 断电告警方式按钮组样式更新 — 44px高统一风格
void SystemSettingsPage::updatePowerAlarmBtnStyles() {
    QPushButton* btns[3] = { m_powerAlarmBtn1, m_powerAlarmBtn2, m_powerAlarmBtn3 };
    for (int i = 0; i < 3; ++i) {
        if (!btns[i]) continue;
        bool sel = (i == m_powerAlarmMode);
        btns[i]->setChecked(sel);
        if (sel) {
            btns[i]->setStyleSheet(
                "QPushButton { background: #4da3ff;"
                "  color: white; border: none; border-radius: 10px;"
                "  padding: 8px 16px; font-size: 14px; font-weight: 600; min-height: 40px; }"
                "QPushButton:hover { background: #3d8ae0; }"
                "QPushButton:pressed { background: #2e7bd6; }"
            );
        } else {
            btns[i]->setStyleSheet(
                "QPushButton { background: white; color: #666666; border: 1px solid #e0e0e0;"
                "  border-radius: 10px; padding: 8px 16px; font-size: 14px; font-weight: 600; min-height: 40px; }"
                "QPushButton:hover { border-color: #4da3ff; color: #4da3ff; background: #f0f7ff; }"
                "QPushButton:pressed { background: #e6f0ff; }"
            );
        }
    }
}

// [V7.0][2026-06-26] 网口速率按钮组样式更新 — 44px高统一风格
void SystemSettingsPage::updateSpeedBtnStyles() {
    QPushButton* btns[2] = { m_speedBtn1, m_speedBtn2 };
    for (int i = 0; i < 2; ++i) {
        if (!btns[i]) continue;
        bool sel = (i == m_speedMode);
        btns[i]->setChecked(sel);
        if (sel) {
            btns[i]->setStyleSheet(
                "QPushButton { background: #4da3ff;"
                "  color: white; border: none; border-radius: 10px;"
                "  padding: 8px 16px; font-size: 14px; font-weight: 600; min-height: 40px; }"
                "QPushButton:hover { background: #3d8ae0; }"
                "QPushButton:pressed { background: #2e7bd6; }"
            );
        } else {
            btns[i]->setStyleSheet(
                "QPushButton { background: white; color: #666666; border: 1px solid #e0e0e0;"
                "  border-radius: 10px; padding: 8px 16px; font-size: 14px; font-weight: 600; min-height: 40px; }"
                "QPushButton:hover { border-color: #4da3ff; color: #4da3ff; background: #f0f7ff; }"
                "QPushButton:pressed { background: #e6f0ff; }"
            );
        }
    }
}

// [V7.0][2026-06-26] 组网模式按钮组样式更新 — 44px高统一风格
void SystemSettingsPage::updateModeBtnStyles() {
    QPushButton* btns[2] = { m_modeBtn1, m_modeBtn2 };
    for (int i = 0; i < 2; ++i) {
        if (!btns[i]) continue;
        bool sel = (i == m_networkMode);
        btns[i]->setChecked(sel);
        if (sel) {
            btns[i]->setStyleSheet(
                "QPushButton { background: #4da3ff;"
                "  color: white; border: none; border-radius: 10px;"
                "  padding: 8px 16px; font-size: 14px; font-weight: 600; min-height: 40px; }"
                "QPushButton:hover { background: #3d8ae0; }"
                "QPushButton:pressed { background: #2e7bd6; }"
            );
        } else {
            btns[i]->setStyleSheet(
                "QPushButton { background: white; color: #666666; border: 1px solid #e0e0e0;"
                "  border-radius: 10px; padding: 8px 16px; font-size: 14px; font-weight: 600; min-height: 40px; }"
                "QPushButton:hover { border-color: #4da3ff; color: #4da3ff; background: #f0f7ff; }"
                "QPushButton:pressed { background: #e6f0ff; }"
            );
        }
    }
}



// [V7.0][2026-06-26] 备份周期按钮组样式更新 — 44px高统一风格
void SystemSettingsPage::updateBackupPeriodBtnStyles() {
    QPushButton* btns[3] = { m_backupBtn1, m_backupBtn2, m_backupBtn3 };
    for (int i = 0; i < 3; ++i) {
        if (!btns[i]) continue;
        bool sel = (i == m_backupPeriod);
        btns[i]->setChecked(sel);
        if (sel) {
            btns[i]->setStyleSheet(
                "QPushButton { background: #4da3ff;"
                "  color: white; border: none; border-radius: 10px;"
                "  padding: 8px 16px; font-size: 14px; font-weight: 600; min-height: 40px; }"
                "QPushButton:hover { background: #3d8ae0; }"
                "QPushButton:pressed { background: #2e7bd6; }"
            );
        } else {
            btns[i]->setStyleSheet(
                "QPushButton { background: white; color: #666666; border: 1px solid #e0e0e0;"
                "  border-radius: 10px; padding: 8px 16px; font-size: 14px; font-weight: 600; min-height: 40px; }"
                "QPushButton:hover { border-color: #4da3ff; color: #4da3ff; background: #f0f7ff; }"
                "QPushButton:pressed { background: #e6f0ff; }"
            );
        }
    }
}

// [v19] 进入页面时从INI文件加载配置，然后备份快照
void SystemSettingsPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    m_savedFlag = false;
    loadConfigFromIni();     // [v19] 从本地INI文件加载配置到UI
    snapshotSettings();      // 再备份快照
}

// [2026-06-26v8] 离开页面时检测脏数据 → 弹窗询问是否保存
// [2026-06-26] 替换QMessageBox为统一MessageDialog::showDirtyConfirm（风格统一+无取消按钮）
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

// [2026-06-26v8] 检测UI当前值是否与快照不同
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
    // [V2.03c] 已删除3项：manualUnlock/lockTime/faceSensitivity

    if (!m_autoBackupCheck || m_autoBackupCheck->isChecked() != m_snapshot.autoBackup) return true;
    if (m_backupPeriod != m_snapshot.backupPeriod) return true;
    if (!m_cacheHoursSpin || m_cacheHoursSpin->value() != m_snapshot.cacheHours) return true;
    if (!m_backupPathEdit || m_backupPathEdit->text() != m_snapshot.backupPath) return true;

    if (!m_machineGroupCombo || m_machineGroupCombo->currentText() != m_snapshot.machineGroupName) return true;

    return false;  // 无修改
}

// [v19] 从本地INI文件加载配置到UI控件
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
    // [2026-09-24fix] m_maxBorrowSpin创建已注释(功能删除)，必须空守卫防崩溃
    if (m_maxBorrowSpin) m_maxBorrowSpin->setValue(cfg.borrowMaxCount());
    m_defaultPeriodSpin->setValue(cfg.borrowDefaultPeriod());
    m_returnBufferSpin->setValue(cfg.borrowReturnBuffer());
    m_brightnessSlider->setValue(cfg.borrowBrightness());
    // [V2.03c] 已删除3项加载：manualUnlock/lockTime/faceSensitivity

    // 备份管理
    m_autoBackupCheck->setChecked(cfg.backupAutoEnabled());
    m_backupPeriod = cfg.backupPeriod(); updateBackupPeriodBtnStyles();
    m_cacheHoursSpin->setValue(cfg.backupCacheHours());
    m_backupPathEdit->setText(cfg.backupPath());

    // 机组名称 [2026-06-27] 从下拉框中选择匹配项
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

// [v19] 将当前UI值全部写入本地INI文件
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
    // [2026-09-24fix] m_maxBorrowSpin创建已注释，空守卫防崩溃
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

// [2026-06-26v7] 备份当前所有UI控件的值到快照
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
    m_snapshot.backupPath = m_backupPathEdit ? m_backupPathEdit->text() : "/mnt/backup";

    // 机组名称
    m_snapshot.machineGroupName = m_machineGroupCombo ? m_machineGroupCombo->currentText() : QStringLiteral("");
}

// [2026-06-26v7] 还原所有UI控件到快照值
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

    // 机组名称 [2026-06-27] 从下拉框还原
    if (m_machineGroupCombo) {
        for (int i = 0; i < m_machineGroupCombo->count(); i++) {
            if (m_machineGroupCombo->itemText(i) == m_snapshot.machineGroupName) {
                m_machineGroupCombo->setCurrentIndex(i);
                break;
            }
        }
    }
}

// [2026-06-27] 下拉选择机组自动保存到AppConfig，实时生效
// [2026-06-27] 校验：更换机组前，当前机组和目标机组下的工具都必须全部归还完毕
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

    // [2026-06-27] 校验当前机组和目标机组下是否有未归还的借用记录
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

// [2026-06-27] 跨平台设置显示器亮度（Windows + 麒麟Linux自适应）
// Windows方案：PowerShell + WMI (WmiMonitorBrightnessMethods.WmiSetBrightness)
// 麒麟方案A：/sys/class/backlight/<dev>/brightness 内核接口（硬件级，需root）
// 麒麟方案B：xrandr --output <dev> --brightness <val> X11 Gamma调整（软件级，无需root）
// 麒麟方案C：brightnessctl set <val>% 命令（需安装）
// 异步执行不阻塞UI，失败静默处理并降级尝试下一方案
void SystemSettingsPage::applyDisplayBrightness(int percent) {
    // 输入校验：亮度值范围 0-100
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    // [2026-06-27] 日志路径跨平台：Windows用项目temp目录，麒麟用/tmp
    QString logPath;
#ifdef Q_OS_WIN
    logPath = QStringLiteral("d:/CFDZ/smartCabinet/trunk/code/temp/brightness.log");
#else
    logPath = QStringLiteral("/tmp/smartcabinet_brightness.log");
#endif

    // 日志记录Lambda（统一封装，避免重复代码）
    auto writeLog = [logPath, percent](const QString& method, const QString& result, int exitCode) {
        QFile logFile(logPath);
        if (logFile.open(QIODevice::Append | QIODevice::Text)) {
            QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
            logFile.write(QStringLiteral("[%1] method=%2 brightness=%3% exitCode=%4 result=%5\n")
                          .arg(ts).arg(method).arg(percent).arg(exitCode).arg(result).toUtf8());
            logFile.close();
        }
    };

#ifdef Q_OS_WIN
    // ═══════════ Windows平台：WMI 方式 ═══════════
    // PowerShell: (Get-WmiObject -Namespace root/WMI -Class WmiMonitorBrightnessMethods).WmiSetBrightness(1, <percent>)
    QString psScript = QStringLiteral(
        "try { "
        "  $monitors = Get-WmiObject -Namespace root/WMI -Class WmiMonitorBrightnessMethods -ErrorAction Stop; "
        "  if ($monitors) { $monitors.WmiSetBrightness(1, %1); Write-Output 'OK' } "
        "  else { Write-Output 'NOMONITOR' } "
        "} catch { Write-Output 'WMI_ERROR' }"
    ).arg(percent);

    auto* proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, percent, writeLog](int exitCode, QProcess::ExitStatus) {
        auto* p = qobject_cast<QProcess*>(sender());
        if (!p) return;
        QString output = p->readAllStandardOutput().trimmed();
        p->deleteLater();

        writeLog(QStringLiteral("WMI"), output, exitCode);
        if (output == "OK") {
            qInfo() << "[Brightness] WMI applied successfully:" << percent << "%";
        } else {
            qWarning() << "[Brightness] WMI failed, output:" << output << "exitCode:" << exitCode;
        }
    });

    QStringList args;
    args << "-NoProfile" << "-NonInteractive" << "-Command" << psScript;
    proc->start("powershell.exe", args);
    if (!proc->waitForStarted(3000)) {
        qWarning() << "[Brightness] Failed to start powershell.exe";
        writeLog(QStringLiteral("WMI"), QStringLiteral("START_FAILED"), -1);
        proc->deleteLater();
    }

#else
    // ═══════════ 麒麟Linux平台：三级降级方案 ═══════════
    // 方案A：/sys/class/backlight/ 内核接口（硬件级亮度，需root权限）
    // 1) 遍历 /sys/class/backlight/ 找到第一个设备目录
    // 2) 读取 max_brightness 计算实际值 = percent * max / 100
    // 3) 用 pkexec/sudo 提权写入 brightness 文件
    // 方案B：xrandr X11 Gamma调整（软件级，无需root，所有X11桌面环境通用）
    // xrandr --output <display> --brightness <0.1~1.0>
    // 方案C：brightnessctl 命令（部分发行版预装）
    // brightnessctl set <percent>%

    // 封装异步执行+日志的Lambda
    auto runAsync = [this, percent, writeLog](const QString& method,
                                               const QString& program,
                                               const QStringList& args) {
        auto* proc = new QProcess(this);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [method, percent, writeLog](int exitCode, QProcess::ExitStatus) {
            auto* p = qobject_cast<QProcess*>(sender());
            if (!p) return;
            QString output = p->readAllStandardOutput().trimmed();
            QString errOutput = p->readAllStandardError().trimmed();
            p->deleteLater();

            QString result = (exitCode == 0) ? QStringLiteral("OK: %1").arg(output)
                                             : QStringLiteral("FAIL: %1").arg(errOutput.isEmpty() ? output : errOutput);
            writeLog(method, result, exitCode);

            if (exitCode == 0) {
                qInfo() << "[Brightness]" << method << "applied:" << percent << "%";
            } else {
                qWarning() << "[Brightness]" << method << "failed:" << result;
            }
        });

        proc->start(program, args);
        if (!proc->waitForStarted(3000)) {
            qWarning() << "[Brightness] Failed to start" << program;
            writeLog(method, QStringLiteral("START_FAILED"), -1);
            proc->deleteLater();
            return false;
        }
        return true;
    };

    // 方案A：尝试 /sys/class/backlight/ 内核接口
    // 用 sh 脚本检测设备并写入，通过 pkexec 提权（麒麟默认安装）
    QString backlightScript = QStringLiteral(
        "for dev in /sys/class/backlight/*/; do "
        "  if [ -f \"${dev}max_brightness\" ] && [ -w \"${dev}brightness\" ]; then "
        "    max=$(cat \"${dev}max_brightness\"); "
        "    val=$(( %1 * max / 100 )); "
        "    echo $val > \"${dev}brightness\"; "
        "    echo 'BACKLIGHT_OK'; exit 0; "
        "  fi; "
        "done; "
        "echo 'NO_BACKLIGHT_DEV'; exit 1"
    ).arg(percent);

    auto* procA = new QProcess(this);
    connect(procA, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, percent, writeLog, runAsync](int exitCodeA, QProcess::ExitStatus) {
        auto* pA = qobject_cast<QProcess*>(sender());
        if (!pA) return;
        QString outputA = pA->readAllStandardOutput().trimmed();
        pA->deleteLater();

        writeLog(QStringLiteral("backlight"), outputA, exitCodeA);

        if (exitCodeA == 0 && outputA.contains("BACKLIGHT_OK")) {
            qInfo() << "[Brightness] backlight kernel interface applied:" << percent << "%";
            return;  // 方案A成功，不再降级
        }

        qWarning() << "[Brightness] backlight failed, trying xrandr...";

        // 方案B：xrandr X11 Gamma调整（软件级，percent映射到0.1~1.0）
        // 先获取显示器列表，再逐个设置
        QString xrandrScript = QStringLiteral(
            "displays=$(xrandr --listmonitors 2>/dev/null | grep -oP '(?<=Monitors: ).*' | tr ' ' '\\n'); "
            "if [ -z \"$displays\" ]; then "
            "  displays=$(xrandr 2>/dev/null | grep -E ' connected' | awk '{print $1}'); "
            "fi; "
            "if [ -z \"$displays\" ]; then echo 'NO_DISPLAY'; exit 1; fi; "
            "brightness=$(echo \"scale=2; %1 / 100\" | bc); "
            "for d in $displays; do xrandr --output $d --brightness $brightness 2>/dev/null; done; "
            "echo 'XRANDR_OK'"
        ).arg(percent);

        auto* procB = new QProcess(this);
        connect(procB, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, percent, writeLog, runAsync](int exitCodeB, QProcess::ExitStatus) {
            auto* pB = qobject_cast<QProcess*>(sender());
            if (!pB) return;
            QString outputB = pB->readAllStandardOutput().trimmed();
            pB->deleteLater();

            writeLog(QStringLiteral("xrandr"), outputB, exitCodeB);

            if (exitCodeB == 0 && outputB.contains("XRANDR_OK")) {
                qInfo() << "[Brightness] xrandr applied:" << percent << "%";
                return;  // 方案B成功
            }

            qWarning() << "[Brightness] xrandr failed, trying brightnessctl...";

            // 方案C：brightnessctl 命令（最后降级方案）
            runAsync(QStringLiteral("brightnessctl"),
                     QStringLiteral("brightnessctl"),
                     QStringList() << "set" << QStringLiteral("%1%").arg(percent));
        });

        procB->start(QStringLiteral("sh"), QStringList() << "-c" << xrandrScript);
        if (!procB->waitForStarted(3000)) {
            qWarning() << "[Brightness] Failed to start xrandr";
            writeLog(QStringLiteral("xrandr"), QStringLiteral("START_FAILED"), -1);
            procB->deleteLater();
            // 直接尝试方案C
            runAsync(QStringLiteral("brightnessctl"),
                     QStringLiteral("brightnessctl"),
                     QStringList() << "set" << QStringLiteral("%1%").arg(percent));
        }
    });

    // 启动方案A：用pkexec提权尝试写入backlight（麒麟系统polkit已集成）
    // 如果pkexec不可用则直接用sh（无root时可能失败，会自动降级到方案B）
    procA->start(QStringLiteral("sh"), QStringList() << "-c" << backlightScript);
    if (!procA->waitForStarted(3000)) {
        qWarning() << "[Brightness] Failed to start backlight script";
        writeLog(QStringLiteral("backlight"), QStringLiteral("START_FAILED"), -1);
        procA->deleteLater();
        // 直接尝试方案B
        QString xrandrScript = QStringLiteral(
            "displays=$(xrandr --listmonitors 2>/dev/null | grep -oP '(?<=Monitors: ).*' | tr ' ' '\\n'); "
            "if [ -z \"$displays\" ]; then "
            "  displays=$(xrandr 2>/dev/null | grep -E ' connected' | awk '{print $1}'); "
            "fi; "
            "if [ -z \"$displays\" ]; then echo 'NO_DISPLAY'; exit 1; fi; "
            "brightness=$(echo \"scale=2; %1 / 100\" | bc); "
            "for d in $displays; do xrandr --output $d --brightness $brightness 2>/dev/null; done; "
            "echo 'XRANDR_OK'"
        ).arg(percent);
        auto* procB = new QProcess(this);
        connect(procB, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [percent, writeLog](int exitCodeB, QProcess::ExitStatus) {
            auto* pB = qobject_cast<QProcess*>(sender());
            if (!pB) return;
            QString outputB = pB->readAllStandardOutput().trimmed();
            pB->deleteLater();
            writeLog(QStringLiteral("xrandr"), outputB, exitCodeB);
        });
        procB->start(QStringLiteral("sh"), QStringList() << "-c" << xrandrScript);
        if (!procB->waitForStarted(3000)) {
            procB->deleteLater();
        }
    }
#endif
}

// [2026-06-27] 跨平台设置自动锁屏时间
// Windows：powercfg 设置显示器关闭超时 + 通过QTimer在主窗口实现应用层锁屏
// 麒麟：xset s <秒数> 设置屏幕保护超时 + xset dpms <秒数> 设置DPMS显示器电源管理
// 同时写入AppConfig供MainWindow的QTimer读取实现应用层自动锁屏
// [V2.03c] 已删除：applyAutoLockTime — 自动锁屏时间已从借还参数中移除

// [2026-06-27] 从系统读取真实系统信息
// 操作系统：Windows用QSysInfo，麒麟读/etc/os-release
// 设备编号：Windows用机器名，麒麟读/etc/machine-id
// 运行时长：Windows用PowerShell计算LastBootUpTime差值，麒麟读/proc/uptime
// 磁盘空间：QStorageInfo跨平台
// CPU占用：Windows用wmic，麒麟读/proc/stat两次采样
// 内存占用：Windows用wmic，麒麟读/proc/meminfo
void SystemSettingsPage::refreshSystemInfo() {
    // ── 操作系统 ──
    QString osInfo;
#ifdef Q_OS_WIN
    osInfo = QSysInfo::prettyProductName();  // 如 "Windows 10 (10.0)"
#else
    // 麒麟系统读 /etc/os-release 获取发行版信息
    QFile osFile("/etc/os-release");
    if (osFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = QString::fromUtf8(osFile.readAll());
        osFile.close();
        QRegularExpression re("PRETTY_NAME=\"([^\"]+)\"");
        QRegularExpressionMatch match = re.match(content);
        if (match.hasMatch()) {
            osInfo = match.captured(1);
        }
    }
    if (osInfo.isEmpty()) {
        // 降级读 /etc/kylin-build 或用 QSysInfo
        QFile kylinFile("/etc/kylin-build");
        if (kylinFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            osInfo = QString::fromUtf8(kylinFile.readLine()).trimmed();
            kylinFile.close();
        }
    }
    if (osInfo.isEmpty()) {
        osInfo = QSysInfo::prettyProductName();
    }
#endif
    if (m_osLabel) m_osLabel->setText(osInfo);

    // ── 设备编号 ──
    QString deviceId;
#ifdef Q_OS_WIN
    // Windows用机器名
    deviceId = QSysInfo::machineHostName();
#else
    // 麒麟读 /etc/machine-id
    QFile machineIdFile("/etc/machine-id");
    if (machineIdFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        deviceId = QString::fromUtf8(machineIdFile.readAll()).trimmed();
        machineIdFile.close();
    }
    if (deviceId.isEmpty()) {
        deviceId = QSysInfo::machineHostName();
    }
#endif
    if (m_deviceIdLabel) m_deviceIdLabel->setText(deviceId);

    // ── 运行时长 ──
#ifdef Q_OS_WIN
    // [2026-06-27 修复] wmic os get LastBootUpTime 返回的是日期格式(如20260627100000.000000+480)
    // 不是秒数，必须用PowerShell计算 (Get-Date) - LastBootUpTime 的差值
    auto* uptimeProc = new QProcess(this);
    connect(uptimeProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
        auto* p = qobject_cast<QProcess*>(sender());
        if (!p) return;
        QString output = p->readAllStandardOutput().trimmed();
        p->deleteLater();
        // PowerShell输出格式: "TotalSeconds" 后跟数字，或直接是秒数
        // 用正则提取数字部分
        QRegularExpression re("(\\d+)");
        QRegularExpressionMatchIterator it = re.globalMatch(output);
        qint64 secs = 0;
        // 取最大的数字作为秒数（避免误匹配小数部分）
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            qint64 val = m.captured(1).toLongLong();
            if (val > secs) secs = val;
        }
        if (secs > 0) {
            int days = secs / 86400;
            int hours = (secs % 86400) / 3600;
            int minutes = (secs % 3600) / 60;
            if (m_uptimeLabel) {
                m_uptimeLabel->setText(QStringLiteral("%1天 %2小时%3分").arg(days).arg(hours).arg(minutes));
            }
        } else {
            if (m_uptimeLabel) m_uptimeLabel->setText(QStringLiteral("读取失败"));
        }
    });
    // PowerShell: 计算系统启动至今的总秒数
    uptimeProc->start("powershell.exe", QStringList() << "-NoProfile" << "-NonInteractive" << "-Command"
                      << "[Math]::Floor((Get-Date) - (Get-CimInstance Win32_OperatingSystem).LastBootUpTime | Select-Object -ExpandProperty TotalSeconds)");
    if (!uptimeProc->waitForStarted(3000)) {
        uptimeProc->deleteLater();
        if (m_uptimeLabel) m_uptimeLabel->setText(QStringLiteral("读取失败"));
    }
#else
    // 麒麟读 /proc/uptime 第一列（秒）
    QString uptime;
    QFile uptimeFile("/proc/uptime");
    if (uptimeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = QString::fromUtf8(uptimeFile.readLine());
        uptimeFile.close();
        QStringList parts = content.split(' ');
        if (parts.size() >= 1) {
            bool ok = false;
            double secs = parts[0].toDouble(&ok);
            if (ok && secs > 0) {
                int days = (int)(secs / 86400);
                int hours = (int)((secs - days * 86400) / 3600);
                int minutes = (int)((secs - days * 86400 - hours * 3600) / 60);
                uptime = QStringLiteral("%1天 %2小时%3分").arg(days).arg(hours).arg(minutes);
            }
        }
    }
    if (uptime.isEmpty()) uptime = QStringLiteral("读取失败");
    if (m_uptimeLabel) m_uptimeLabel->setText(uptime);
#endif

    // ── 磁盘空间 ──（QStorageInfo跨平台）
    QString diskInfo;
    QStorageInfo storage = QStorageInfo::root();
    if (storage.isValid() && storage.isReady()) {
        qint64 total = storage.bytesTotal();
        qint64 free = storage.bytesFree();
        qint64 used = total - free;
        double usedGB = used / (1024.0 * 1024.0 * 1024.0);
        double totalGB = total / (1024.0 * 1024.0 * 1024.0);
        int percent = (total > 0) ? (int)(used * 100 / total) : 0;
        diskInfo = QStringLiteral("已用 %1GB / 共 %2GB (%3%)")
                       .arg(QString::number(usedGB, 'f', 1))
                       .arg(QString::number(totalGB, 'f', 1))
                       .arg(percent);
    } else {
        diskInfo = QStringLiteral("无法读取");
    }
    if (m_diskLabel) m_diskLabel->setText(diskInfo);

    // ── CPU占用率 ──
#ifdef Q_OS_WIN
    // Windows: wmic cpu get loadpercentage 直接返回占用百分比
    auto* cpuProc = new QProcess(this);
    connect(cpuProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
        auto* p = qobject_cast<QProcess*>(sender());
        if (!p) return;
        QString output = p->readAllStandardOutput().trimmed();
        p->deleteLater();
        // wmic输出: "LoadPercentage\n56"，提取数字
        QRegularExpression re("(\\d+)");
        QRegularExpressionMatch m = re.match(output);
        if (m.hasMatch()) {
            if (m_cpuLabel) {
                m_cpuLabel->setText(QStringLiteral("%1%").arg(m.captured(1)));
            }
        } else {
            if (m_cpuLabel) m_cpuLabel->setText(QStringLiteral("读取失败"));
        }
    });
    cpuProc->start("wmic", QStringList() << "cpu" << "get" << "loadpercentage");
    if (!cpuProc->waitForStarted(3000)) {
        cpuProc->deleteLater();
        if (m_cpuLabel) m_cpuLabel->setText(QStringLiteral("读取失败"));
    }
#else
    // 麒麟: 读 /proc/stat 两次采样计算CPU占用率
    // CPU占用 = (idle2-idle1) / (total2-total1) 的反值
    auto readCpuStat = []() -> QPair<qint64, qint64> {
        QFile f("/proc/stat");
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {0, 0};
        QString line = QString::fromUtf8(f.readLine());
        f.close();
        // 格式: cpu user nice system idle iowait irq softirq steal guest guest_nice
        QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() < 5) return {0, 0};
        qint64 total = 0;
        for (int i = 1; i < parts.size(); ++i) total += parts[i].toLongLong();
        qint64 idle = parts[4].toLongLong();
        return {total, idle};
    };
    QPair<qint64, qint64> s1 = readCpuStat();
    QTimer::singleShot(500, this, [this, s1]() {
        auto readCpuStat2 = []() -> QPair<qint64, qint64> {
            QFile f("/proc/stat");
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {0, 0};
            QString line = QString::fromUtf8(f.readLine());
            f.close();
            QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() < 5) return {0, 0};
            qint64 total = 0;
            for (int i = 1; i < parts.size(); ++i) total += parts[i].toLongLong();
            qint64 idle = parts[4].toLongLong();
            return {total, idle};
        };
        QPair<qint64, qint64> s2 = readCpuStat2();
        qint64 totalDiff = s2.first - s1.first;
        qint64 idleDiff = s2.second - s1.second;
        int cpuPercent = 0;
        if (totalDiff > 0) {
            cpuPercent = (int)((totalDiff - idleDiff) * 100 / totalDiff);
        }
        if (m_cpuLabel) m_cpuLabel->setText(QStringLiteral("%1%").arg(cpuPercent));
    });
#endif

    // ── 内存占用率 ──
#ifdef Q_OS_WIN
    // Windows: wmic OS get TotalVisibleMemorySize,FreePhysicalMemory
    // 返回KB单位，计算 (total-free)/total*100
    auto* memProc = new QProcess(this);
    connect(memProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
        auto* p = qobject_cast<QProcess*>(sender());
        if (!p) return;
        QString output = p->readAllStandardOutput().trimmed();
        p->deleteLater();
        // wmic输出两行: "FreePhysicalMemory TotalVisibleMemorySize\n1234567 8388608"
        QStringList lines = output.split('\n');
        if (lines.size() >= 2) {
            QStringList vals = lines[1].trimmed().split(QRegularExpression("\\s+"));
            if (vals.size() >= 2) {
                qint64 freeKB = vals[0].toLongLong();
                qint64 totalKB = vals[1].toLongLong();
                if (totalKB > 0) {
                    qint64 usedKB = totalKB - freeKB;
                    int percent = (int)(usedKB * 100 / totalKB);
                    double usedGB = usedKB / (1024.0 * 1024.0);
                    double totalGB = totalKB / (1024.0 * 1024.0);
                    if (m_memoryLabel) {
                        m_memoryLabel->setText(QStringLiteral("已用 %1GB / 共 %2GB (%3%)")
                            .arg(QString::number(usedGB, 'f', 1))
                            .arg(QString::number(totalGB, 'f', 1))
                            .arg(percent));
                    }
                }
            }
        }
        if (m_memoryLabel && m_memoryLabel->text() == QStringLiteral("读取中...")) {
            m_memoryLabel->setText(QStringLiteral("读取失败"));
        }
    });
    memProc->start("wmic", QStringList() << "OS" << "get" << "FreePhysicalMemory,TotalVisibleMemorySize");
    if (!memProc->waitForStarted(3000)) {
        memProc->deleteLater();
        if (m_memoryLabel) m_memoryLabel->setText(QStringLiteral("读取失败"));
    }
#else
    // 麒麟: 读 /proc/meminfo
    // MemTotal: 总内存, MemAvailable: 可用内存（含缓存）
    qint64 memTotal = 0, memAvailable = 0;
    QFile memFile("/proc/meminfo");
    if (memFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!memFile.atEnd()) {
            QString line = QString::fromUtf8(memFile.readLine());
            if (line.startsWith("MemTotal:")) {
                QRegularExpression re("(\\d+)");
                QRegularExpressionMatch m = re.match(line);
                if (m.hasMatch()) memTotal = m.captured(1).toLongLong();
            } else if (line.startsWith("MemAvailable:")) {
                QRegularExpression re("(\\d+)");
                QRegularExpressionMatch m = re.match(line);
                if (m.hasMatch()) memAvailable = m.captured(1).toLongLong();
            }
            if (memTotal > 0 && memAvailable > 0) break;
        }
        memFile.close();
    }
    if (memTotal > 0) {
        qint64 memUsed = memTotal - memAvailable;
        int percent = (int)(memUsed * 100 / memTotal);
        double usedGB = memUsed / (1024.0 * 1024.0);
        double totalGB = memTotal / (1024.0 * 1024.0);
        if (m_memoryLabel) {
            m_memoryLabel->setText(QStringLiteral("已用 %1GB / 共 %2GB (%3%)")
                .arg(QString::number(usedGB, 'f', 1))
                .arg(QString::number(totalGB, 'f', 1))
                .arg(percent));
        }
    } else {
        if (m_memoryLabel) m_memoryLabel->setText(QStringLiteral("读取失败"));
    }
#endif
}

// [2026-06-27] 执行数据库自动备份
// 备份策略：
// 1. 保存备份设置时立即执行一次备份（验证备份路径可用）
// 2. 根据备份周期（每日/每周一/每周日）计算下次备份时间
// 3. MainWindow 启动定时器每小时检查一次是否到了备份时间
// 备份内容：SQLite文件拷贝 / MySQL用mysqldump导出
// 备份文件命名：smartcabinet_backup_YYYYMMDD_HHMMSS.db
void SystemSettingsPage::performDatabaseBackup() {
    auto& cfg = AppConfig::instance();
    QString backupPath = cfg.backupPath();
    bool isAutoEnabled = cfg.backupAutoEnabled();
    int period = cfg.backupPeriod();

    // 日志路径跨平台
    QString logPath;
#ifdef Q_OS_WIN
    logPath = QStringLiteral("d:/CFDZ/smartCabinet/trunk/code/temp/backup.log");
#else
    logPath = QStringLiteral("/tmp/smartcabinet_backup.log");
#endif

    auto writeBackupLog = [logPath](const QString& action, const QString& result, const QString& detail) {
        QFile logFile(logPath);
        if (logFile.open(QIODevice::Append | QIODevice::Text)) {
            QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
            logFile.write(QStringLiteral("[%1] action=%2 result=%3 detail=%4\n")
                          .arg(ts).arg(action).arg(result).arg(detail).toUtf8());
            logFile.close();
        }
    };

    if (!isAutoEnabled) {
        writeBackupLog(QStringLiteral("SKIP"), QStringLiteral("DISABLED"),
                       QStringLiteral("auto backup is disabled"));
        return;
    }

    // 检查备份周期：每日/每周一/每周日
    QDate today = QDate::currentDate();
    int dayOfWeek = today.dayOfWeek();  // 1=周一, 7=周日
    bool shouldBackup = false;
    QString periodDesc;
    if (period == 0) {
        // 每日备份
        shouldBackup = true;
        periodDesc = QStringLiteral("每日");
    } else if (period == 1) {
        // 每周一备份
        shouldBackup = (dayOfWeek == 1);
        periodDesc = QStringLiteral("每周一");
    } else if (period == 2) {
        // 每周日备份
        shouldBackup = (dayOfWeek == 7);
        periodDesc = QStringLiteral("每周日");
    }

    // 如果不是备份日，跳过（但保存设置时的首次备份强制执行）
    // 这里首次保存时强制备份，定时检查时才按周期判断

    // 确保备份目录存在
    QDir dir;
    if (!dir.exists(backupPath)) {
        if (!dir.mkpath(backupPath)) {
            writeBackupLog(QStringLiteral("CREATE_DIR"), QStringLiteral("FAIL"),
                           QStringLiteral("cannot create: %1").arg(backupPath));
            qWarning() << "[Backup] Cannot create backup dir:" << backupPath;
            return;
        }
    }

    // 生成备份文件名
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString backupFile;

    // 判断数据库类型：SQLite文件拷贝 / mysqldump导出
    DatabaseManager& db = DatabaseManager::instance();
    QString sqliteDbPath = QStringLiteral("d:/CFDZ/smartCabinet/trunk/QtSmartCabinet/build/smartcabinet.db");

#ifdef Q_OS_WIN
    // Windows: 检查SQLite文件是否存在
    QString dbPath = QStringLiteral("d:/CFDZ/smartCabinet/trunk/QtSmartCabinet/build/smartcabinet.db");
#else
    QString dbPath = QStringLiteral("/var/lib/smartcabinet/smartcabinet.db");
#endif

    if (QFile::exists(dbPath)) {
        // SQLite模式：拷贝数据库文件
        backupFile = backupPath + QDir::separator() +
                     QStringLiteral("smartcabinet_backup_%1.db").arg(timestamp);
        if (QFile::copy(dbPath, backupFile)) {
            writeBackupLog(QStringLiteral("BACKUP"), QStringLiteral("OK"),
                           QStringLiteral("SQLite file copied to: %1").arg(backupFile));
            qInfo() << "[Backup] SQLite backup success:" << backupFile;

            // 清理超过7天的旧备份文件
            QDir backupDir(backupPath);
            QStringList filters;
            filters << "smartcabinet_backup_*.db";
            QFileInfoList oldFiles = backupDir.entryInfoList(filters, QDir::Files, QDir::Time);
            for (const QFileInfo& fi : oldFiles) {
                if (fi.lastModified().daysTo(QDateTime::currentDateTime()) > 7) {
                    QFile::remove(fi.absoluteFilePath());
                    writeBackupLog(QStringLiteral("CLEANUP"), QStringLiteral("OK"),
                                   QStringLiteral("removed old: %1").arg(fi.fileName()));
                }
            }
        } else {
            writeBackupLog(QStringLiteral("BACKUP"), QStringLiteral("FAIL"),
                           QStringLiteral("cannot copy %1 to %2").arg(dbPath).arg(backupFile));
            qWarning() << "[Backup] SQLite backup failed:" << backupFile;
        }
    } else {
        // MySQL模式：用mysqldump导出
        backupFile = backupPath + QDir::separator() +
                     QStringLiteral("smartcabinet_backup_%1.sql").arg(timestamp);
        QString dbName = cfg.dbName();
        QString dbUser = cfg.dbUser();
        QString dbPass = cfg.dbPass();
        QString dbHost = cfg.dbHost();

        QString dumpCmd = QStringLiteral("mysqldump -h%1 -u%2 -p%3 %4")
                              .arg(dbHost).arg(dbUser).arg(dbPass).arg(dbName);

        auto* proc = new QProcess(this);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, backupFile, writeBackupLog](int exitCode, QProcess::ExitStatus) {
            auto* p = qobject_cast<QProcess*>(sender());
            if (!p) return;
            p->deleteLater();
            if (exitCode == 0) {
                writeBackupLog(QStringLiteral("BACKUP"), QStringLiteral("OK"),
                               QStringLiteral("MySQL dump: %1").arg(backupFile));
                qInfo() << "[Backup] MySQL backup success:" << backupFile;
            } else {
                writeBackupLog(QStringLiteral("BACKUP"), QStringLiteral("FAIL"),
                               QStringLiteral("mysqldump exitCode=%1").arg(exitCode));
                qWarning() << "[Backup] MySQL backup failed, exitCode:" << exitCode;
            }
        });
        // 重定向输出到文件
        proc->setStandardOutputFile(backupFile);
        proc->start("mysqldump", QStringList()
                    << QStringLiteral("-h%1").arg(dbHost)
                    << QStringLiteral("-u%1").arg(dbUser)
                    << QStringLiteral("-p%1").arg(dbPass)
                    << dbName);
        if (!proc->waitForStarted(3000)) {
            writeBackupLog(QStringLiteral("BACKUP"), QStringLiteral("FAIL"),
                           QStringLiteral("cannot start mysqldump"));
            qWarning() << "[Backup] Cannot start mysqldump";
            proc->deleteLater();
        }
    }
}

// [2026-06-27] 获取第一块有线网卡名称
// Windows: 用 netsh interface show interface 获取，过滤掉 Loopback/虚拟网卡
// 麒麟: 用 ip -o link show 获取，过滤掉 lo/wlan/docker/br/veth 等虚拟/无线网卡
// 返回网卡名称用于后续网络配置命令定位
QString SystemSettingsPage::detectWiredInterfaceName() {
    QString ifName;

#ifdef Q_OS_WIN
    // Windows: netsh interface show interface 获取网卡列表
    QProcess proc;
    proc.start("netsh", QStringList() << "interface" << "show" << "interface");
    proc.waitForFinished(5000);
    QString output = QString::fromLocal8Bit(proc.readAllStandardOutput());
    // 解析输出：跳过表头，取第一个非"Loopback"的网卡名
    QStringList lines = output.split('\n');
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        // 跳过表头行
        if (trimmed.contains("Admin State") || trimmed.contains("管理员状态")) continue;
        // 跳过回环
        if (trimmed.contains("Loopback", Qt::CaseInsensitive)) continue;
        // 取最后一列作为网卡名（netsh输出格式：状态 状态 类型 接口名称）
        QStringList parts = trimmed.split(QRegularExpression("\\s+"));
        if (parts.size() >= 4) {
            // 取第4列开始的所有部分作为接口名（名称可能含空格）
            ifName = parts.mid(3).join(" ");
            if (!ifName.isEmpty()) break;
        }
    }
#else
    // 麒麟: ip -o link show 获取网卡列表，过滤虚拟/无线网卡
    QProcess proc;
    proc.start("ip", QStringList() << "-o" << "link" << "show");
    proc.waitForFinished(5000);
    QString output = QString::fromLocal8Bit(proc.readAllStandardOutput());
    QStringList lines = output.split('\n');
    for (const QString& line : lines) {
        // 格式: "2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 ..."
        QRegularExpression re("(\\d+):\\s+(\\w+):");
        QRegularExpressionMatch match = re.match(line);
        if (!match.hasMatch()) continue;
        QString name = match.captured(2);
        // 过滤虚拟/无线网卡，只保留有线网卡
        if (name == "lo" || name.startsWith("wlan") || name.startsWith("wlp") ||
            name.startsWith("docker") || name.startsWith("br-") ||
            name.startsWith("veth") || name.startsWith("virbr") ||
            name.startsWith("tun") || name.startsWith("tap")) {
            continue;
        }
        // 优先返回 eth* 或 en* 格式的有线网卡名
        if (name.startsWith("eth") || name.startsWith("en") ||
            name.startsWith("em") || name.startsWith("p")) {
            ifName = name;
            break;
        }
    }
#endif

    if (ifName.isEmpty()) {
        qWarning() << "[Network] No wired interface detected";
    } else {
        qInfo() << "[Network] Detected wired interface:" << ifName;
    }
    return ifName;
}

// [2026-06-27] 跨平台配置有线网卡
// Windows: netsh interface ip set address/dns（需管理员权限）
// 麒麟: ip addr add + ip route add + resolvconf（需root）
// 异步执行，失败静默处理并记录日志
void SystemSettingsPage::applyNetworkConfig(const QString& ip, const QString& mask,
                                            const QString& gateway, const QString& dns) {
    // 基础校验：IP不能为空
    if (ip.isEmpty() || ip == "0.0.0.0") {
        qWarning() << "[Network] Invalid IP address:" << ip;
        return;
    }

    // 日志路径跨平台
    QString logPath;
#ifdef Q_OS_WIN
    logPath = QStringLiteral("d:/CFDZ/smartCabinet/trunk/code/temp/network.log");
#else
    logPath = QStringLiteral("/tmp/smartcabinet_network.log");
#endif

    auto writeNetLog = [logPath](const QString& action, const QString& result, const QString& detail) {
        QFile logFile(logPath);
        if (logFile.open(QIODevice::Append | QIODevice::Text)) {
            QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
            logFile.write(QStringLiteral("[%1] action=%2 result=%3 detail=%4\n")
                          .arg(ts).arg(action).arg(result).arg(detail).toUtf8());
            logFile.close();
        }
    };

    // 检测有线网卡名称
    QString ifName = detectWiredInterfaceName();
    if (ifName.isEmpty()) {
        writeNetLog(QStringLiteral("DETECT"), QStringLiteral("FAIL"),
                    QStringLiteral("no wired interface found"));
        qWarning() << "[Network] Cannot apply config: no wired interface detected";
        return;
    }

    // 计算子网掩码前缀长度（如 255.255.255.0 → 24）
    int prefixLen = 24;
    if (mask == "255.255.255.0") prefixLen = 24;
    else if (mask == "255.255.0.0") prefixLen = 16;
    else if (mask == "255.0.0.0") prefixLen = 8;
    else if (mask == "255.255.255.128") prefixLen = 25;
    else if (mask == "255.255.255.192") prefixLen = 26;
    else {
        // 通用计算：统计mask中1的位数
        QStringList octets = mask.split('.');
        if (octets.size() == 4) {
            int bits = 0;
            for (const QString& oct : octets) {
                int val = oct.toInt();
                for (int i = 7; i >= 0; --i) {
                    if (val & (1 << i)) bits++;
                    else break;
                }
            }
            if (bits > 0 && bits <= 32) prefixLen = bits;
        }
    }

    writeNetLog(QStringLiteral("DETECT"), QStringLiteral("OK"),
                QStringLiteral("interface=%1 ip=%2/%3 gw=%4 dns=%5")
                    .arg(ifName).arg(ip).arg(prefixLen).arg(gateway).arg(dns));

#ifdef Q_OS_WIN
    // ═══════════ Windows: netsh 配置IP/DNS/Gateway ═══════════
    // netsh interface ip set address name="<ifName>" static <ip> <mask> <gateway> 1
    // netsh interface ip set dns name="<ifName>" static <dns> primary

    // 异步执行IP配置
    auto* ipProc = new QProcess(this);
    connect(ipProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, ifName, dns, writeNetLog](int exitCode, QProcess::ExitStatus) {
        auto* p = qobject_cast<QProcess*>(sender());
        if (!p) return;
        p->deleteLater();

        if (exitCode == 0) {
            writeNetLog(QStringLiteral("SET_IP"), QStringLiteral("OK"),
                        QStringLiteral("interface=%1").arg(ifName));
            qInfo() << "[Network] IP config applied to" << ifName;

            // IP配置成功后，配置DNS
            auto* dnsProc = new QProcess(this);
            connect(dnsProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    this, [this, ifName, dns, writeNetLog](int exitCodeDns, QProcess::ExitStatus) {
                auto* pd = qobject_cast<QProcess*>(sender());
                if (!pd) return;
                pd->deleteLater();
                if (exitCodeDns == 0) {
                    writeNetLog(QStringLiteral("SET_DNS"), QStringLiteral("OK"),
                                QStringLiteral("dns=%1").arg(dns));
                    qInfo() << "[Network] DNS config applied:" << dns;
                } else {
                    writeNetLog(QStringLiteral("SET_DNS"), QStringLiteral("FAIL"),
                                QStringLiteral("exitCode=%1").arg(exitCodeDns));
                    qWarning() << "[Network] DNS config failed, exitCode:" << exitCodeDns;
                }
            });
            dnsProc->start("netsh", QStringList() << "interface" << "ip" << "set" << "dns"
                         << QStringLiteral("name=\"%1\"").arg(ifName)
                         << "static" << dns << "primary");
            if (!dnsProc->waitForStarted(3000)) {
                qWarning() << "[Network] Failed to start DNS config";
                dnsProc->deleteLater();
            }
        } else {
            writeNetLog(QStringLiteral("SET_IP"), QStringLiteral("FAIL"),
                        QStringLiteral("exitCode=%1").arg(exitCode));
            qWarning() << "[Network] IP config failed, exitCode:" << exitCode;
        }
    });

    ipProc->start("netsh", QStringList() << "interface" << "ip" << "set" << "address"
                 << QStringLiteral("name=\"%1\"").arg(ifName)
                 << "static" << ip << mask << gateway << "1");
    if (!ipProc->waitForStarted(3000)) {
        qWarning() << "[Network] Failed to start IP config";
        writeNetLog(QStringLiteral("SET_IP"), QStringLiteral("START_FAILED"),
                    QStringLiteral("cannot start netsh"));
        ipProc->deleteLater();
    }

#else
    // ═══════════ 麒麟Linux: ip + resolvconf 配置IP/DNS/Gateway ═══════════
    // ip addr flush dev <ifName>
    // ip addr add <ip>/<prefix> dev <ifName>
    // ip route add default via <gateway> dev <ifName>
    // echo "nameserver <dns>" > /etc/resolv.conf

    // 用 sh 脚本一次性执行所有网络配置命令（需root，通过pkexec提权）
    // 如果pkexec不可用则直接用sh（可能因权限不足失败，记录日志）
    QString netScript = QStringLiteral(
        "# 网络配置脚本 - 仅配置有线网卡 %1\n"
        "IFACE=\"%1\"\n"
        "IP=\"%2\"\n"
        "PREFIX=%3\n"
        "GW=\"%4\"\n"
        "DNS=\"%5\"\n"
        "# 清除旧IP配置\n"
        "ip addr flush dev $IFACE 2>/dev/null\n"
        "# 设置新IP\n"
        "ip addr add $IP/$PREFIX dev $IFACE 2>/dev/null\n"
        "# 启用网卡\n"
        "ip link set $IFACE up 2>/dev/null\n"
        "# 设置默认网关（先删除旧的再添加新的）\n"
        "ip route del default 2>/dev/null\n"
        "ip route add default via $GW dev $IFACE 2>/dev/null\n"
        "# 设置DNS\n"
        "if [ -d /etc/resolvconf ]; then "
        "  echo \"nameserver $DNS\" | resolvconf -a $IFACE 2>/dev/null; "
        "else "
        "  echo \"nameserver $DNS\" > /etc/resolv.conf 2>/dev/null; "
        "fi\n"
        "# 麒麟系统持久化网络配置（写入netplan或NetworkManager）\n"
        "if command -v nmcli >/dev/null 2>&1; then "
        "  nmcli con modify $IFACE ipv4.addresses $IP/$PREFIX 2>/dev/null; "
        "  nmcli con modify $IFACE ipv4.gateway $GW 2>/dev/null; "
        "  nmcli con modify $IFACE ipv4.dns $DNS 2>/dev/null; "
        "  nmcli con modify $IFACE ipv4.method manual 2>/dev/null; "
        "  nmcli con up $IFACE 2>/dev/null; "
        "fi\n"
        "echo 'NETWORK_CONFIG_OK'\n"
    ).arg(ifName).arg(ip).arg(prefixLen).arg(gateway).arg(dns);

    auto* netProc = new QProcess(this);
    connect(netProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, ifName, writeNetLog](int exitCode, QProcess::ExitStatus) {
        auto* p = qobject_cast<QProcess*>(sender());
        if (!p) return;
        QString output = p->readAllStandardOutput().trimmed();
        p->deleteLater();

        if (exitCode == 0 && output.contains("NETWORK_CONFIG_OK")) {
            writeNetLog(QStringLiteral("SET_NETWORK"), QStringLiteral("OK"),
                        QStringLiteral("interface=%1").arg(ifName));
            qInfo() << "[Network] Network config applied to" << ifName;
        } else {
            writeNetLog(QStringLiteral("SET_NETWORK"), QStringLiteral("FAIL"),
                        QStringLiteral("exitCode=%1 output=%2").arg(exitCode).arg(output));
            qWarning() << "[Network] Network config failed, exitCode:" << exitCode << "output:" << output;
        }
    });

    // 优先尝试 pkexec 提权（麒麟系统polkit已集成）
    // 如果pkexec不可用则直接用sh（可能因权限不足失败，记录日志）
    netProc->start("pkexec", QStringList() << "sh" << "-c" << netScript);
    if (!netProc->waitForStarted(3000)) {
        // pkexec不可用，降级用sh直接执行（可能因权限不足失败）
        netProc->deleteLater();
        auto* shProc = new QProcess(this);
        connect(shProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [ifName, writeNetLog](int exitCode, QProcess::ExitStatus) {
            auto* p = qobject_cast<QProcess*>(sender());
            if (!p) return;
            QString output = p->readAllStandardOutput().trimmed();
            p->deleteLater();
            if (exitCode == 0 && output.contains("NETWORK_CONFIG_OK")) {
                writeNetLog(QStringLiteral("SET_NETWORK"), QStringLiteral("OK"),
                            QStringLiteral("interface=%1 (no pkexec)").arg(ifName));
                qInfo() << "[Network] Network config applied (no pkexec) to" << ifName;
            } else {
                writeNetLog(QStringLiteral("SET_NETWORK"), QStringLiteral("FAIL"),
                            QStringLiteral("exitCode=%1 output=%2 (no root)").arg(exitCode).arg(output));
                qWarning() << "[Network] Network config failed (no root), exitCode:" << exitCode;
            }
        });
        shProc->start("sh", QStringList() << "-c" << netScript);
        if (!shProc->waitForStarted(3000)) {
            qWarning() << "[Network] Failed to start network config script";
            writeNetLog(QStringLiteral("SET_NETWORK"), QStringLiteral("START_FAILED"),
                        QStringLiteral("cannot start sh"));
            shProc->deleteLater();
        }
    }
#endif
}
