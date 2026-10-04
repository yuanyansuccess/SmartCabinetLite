/**
 * @file SystemSettingsPagePanels.cpp
 * @brief 系统设置页 - 各设置面板的界面构建（借还设置/备份管理/系统信息/机组）
 * @author 袁燕
 *
 * 拆分说明：原 SystemSettingsPage.cpp 达 2500+ 行，新人难以定位。
 * 本文件集中存放各设置面板的 UI 构建方法；主文件保留页面骨架、
 * 读写保存与对话框逻辑。同一类的成员方法可分布于多个 cpp，行为完全不变。
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
#include <QProcess>  // 调用PowerShell设置显示器亮度
#include <QFile>  // WMI结果日志记录
#include <QDateTime>  // 亮度日志时间戳
#include <QDir>  // 备份目录操作
#include <QStorageInfo>  // 跨平台磁盘空间读取
#include <QSysInfo>  // 跨平台系统信息读取
#include <QRegularExpression>  // 解析os-release
#include <QFileInfoList>  // 备份文件清理


QWidget* SystemSettingsPage::createBorrowPanel() {
    auto* panel = new QFrame();
    panel->setObjectName("borrowPanel");
    panel->setStyleSheet(QString("QFrame#borrowPanel{background:white;border-radius:12px;border:none;}"));
    auto* layout = new QVBoxLayout(panel);
    layout->setSpacing(6);
    layout->setContentsMargins(20, 10, 20, 10);

    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("借还参数设置"));
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
    // 统一开关样式+透明背景 统一22px indicator + 选中态蓝色

    //m_maxBorrowSpin = new QSpinBox();
    //m_maxBorrowSpin->setRange(1, 20);
    //m_maxBorrowSpin->setValue(5);
    //m_maxBorrowSpin->setSuffix(QStringLiteral(" 件"));
    //m_maxBorrowSpin->setStyleSheet(StyleHelper::settingSpinBox());
    //form->addRow(FormFactory::formLabel(QStringLiteral("单次最大借出数量")), m_maxBorrowSpin);

    m_defaultPeriodSpin = new QSpinBox();
    m_defaultPeriodSpin->setRange(1, 168);
    m_defaultPeriodSpin->setValue(48);
    m_defaultPeriodSpin->setSuffix(QStringLiteral(" 小时"));
    m_defaultPeriodSpin->setStyleSheet(StyleHelper::settingSpinBox());
    form->addRow(FormFactory::formLabel(QStringLiteral("默认借用期限")), m_defaultPeriodSpin);

    m_returnBufferSpin = new QSpinBox();
    m_returnBufferSpin->setRange(0, 120);
    m_returnBufferSpin->setValue(30);
    m_returnBufferSpin->setSuffix(QStringLiteral(" 分钟"));
    m_returnBufferSpin->setStyleSheet(StyleHelper::settingSpinBox());
    form->addRow(FormFactory::formLabel(QStringLiteral("归还缓冲时间")), m_returnBufferSpin);

    // 删除3项：手动开锁验证、人脸识别灵敏度、自动锁屏时间
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
    brightWidget->setStyleSheet("background:transparent;");  // 去灰
    auto* brightLayout = new QHBoxLayout(brightWidget);
    brightLayout->setContentsMargins(0, 0, 0, 0);
    brightLayout->setSpacing(8);
    brightLayout->addWidget(m_brightnessSlider);
    brightLayout->addWidget(m_brightnessValueLabel);
    form->addRow(FormFactory::formLabel(QStringLiteral("显示屏亮度")), brightWidget);

    layout->addLayout(form);

    // 保存栏：右对齐+顶部分割线，48px按钮
    auto* saveBar = new QFrame();
    saveBar->setStyleSheet(StyleHelper::saveBarSeparator());
    auto* saveBarLayout = new QHBoxLayout(saveBar);
    saveBarLayout->setContentsMargins(0, 8, 0, 0);  // 8
    saveBarLayout->addStretch();
    auto* saveBtn = new QPushButton(QStringLiteral("保存借还设置"));
    saveBtn->setStyleSheet(StyleHelper::settingSaveBtn());
    saveBtn->setCursor(Qt::PointingHandCursor);
    connect(saveBtn, &QPushButton::clicked, this, &SystemSettingsPage::onSaveBorrow);
    saveBarLayout->addWidget(saveBtn);
    layout->addWidget(saveBar);

    // installFocusEvents已移除
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
    title->setStyleSheet(StyleHelper::sectionTitle());
    titleRow->addWidget(title);
    titleRow->addStretch();
    layout->addLayout(titleRow);
    auto* sep = new QFrame(); sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(StyleHelper::separatorLine());
    layout->addWidget(sep);

    buildBackupForm(layout);
    buildSystemInfoSection(layout);
    buildMachineGroupSection(layout);
    buildBackupSaveBar(layout);

    return panel;
}

/** 构建备份设置表单：自动备份开关/备份周期按钮组/存储路径/断网缓存时长 */
void SystemSettingsPage::buildBackupForm(QVBoxLayout* layout) {
    auto* form = new QFormLayout();
    form->setSpacing(8);
    form->setContentsMargins(0, 0, 0, 0);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);  // 表单字段自动扩展
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);  // 标签右对齐
    // 统一开关样式：透明背景 22px indicator 选中态蓝色

    m_autoBackupCheck = FormFactory::toggle(true);
    form->addRow(FormFactory::formLabel(QStringLiteral("自动备份")), m_autoBackupCheck);

    // 备份周期按钮组（统一44px高触屏尺寸）
    auto makeBackupBtn = [&](const QString& text, int mode) -> QPushButton* {
        auto* btn = new QPushButton(text);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(StyleHelper::Token::ControlHeightCompactInput);
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
    form->addRow(FormFactory::formLabel(QStringLiteral("备份周期")), backupBtnGroup);

    m_backupPathEdit = new QLineEdit(SC::BACKUP_PATH);
    m_backupPathEdit->setStyleSheet(StyleHelper::settingLineEdit());
    form->addRow(FormFactory::formLabel(QStringLiteral("备份存储路径")), m_backupPathEdit);

    m_cacheHoursSpin = new QSpinBox();
    m_cacheHoursSpin->setRange(1, 24);
    m_cacheHoursSpin->setValue(4);
    m_cacheHoursSpin->setSuffix(QStringLiteral(" 小时"));
    m_cacheHoursSpin->setStyleSheet(StyleHelper::settingSpinBox());
    form->addRow(FormFactory::formLabel(QStringLiteral("断网缓存时长")), m_cacheHoursSpin);

    layout->addLayout(form);
}

/** 构建系统信息区：版本/系统/设备编号/运行时长/磁盘/CPU/内存（label成员供refresh()更新） */
void SystemSettingsPage::buildSystemInfoSection(QVBoxLayout* layout) {
    auto* sysInfoFrame = new QFrame();
    sysInfoFrame->setStyleSheet("QFrame{background:transparent;}");
    auto* sysLayout = new QVBoxLayout(sysInfoFrame);
    sysLayout->setSpacing(6);
    sysLayout->setContentsMargins(0, 8, 0, 0);

    auto* sysTitle = new QLabel(QStringLiteral("系统信息"));
    sysTitle->setStyleSheet(QString("font-size:16px;font-weight:700;color:%1;margin-bottom:6px;").arg(StyleHelper::textColor()));
    sysLayout->addWidget(sysTitle);

    // value label必须赋值给成员变量供refresh()使用（否则refresh()访问nullptr崩溃）
    auto createInfoRowEx = [&](const QString& key, const QString& value, QLabel*& valueMember) {
        auto* row = new QHBoxLayout();
        row->setContentsMargins(0, 6, 0, 6);  // 行间距避免压扁
        auto* keyLabel = new QLabel(key);
        keyLabel->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontBody, StyleHelper::textMuted()));
        keyLabel->setFixedWidth(90);
        keyLabel->setMinimumHeight(22);  // 确保字体不被压扁
        valueMember = new QLabel(value);
        valueMember->setStyleSheet("font-size:14px;color:#333;font-weight:600;background:transparent;");
        valueMember->setMinimumHeight(22);  // 确保字体不被压扁
        valueMember->setWordWrap(true);  // 长文本换行不截断
        row->addWidget(keyLabel);
        row->addWidget(valueMember, 1);
        return row;
    };

    // 软件版本从AppConfig读取（不硬编码），与TopBar版本号数据源一致
    sysLayout->addLayout(createInfoRowEx(QStringLiteral("软件版本"),
        AppConfig::instance().appVersion(), m_versionLabel));
    sysLayout->addLayout(createInfoRowEx(QStringLiteral("操作系统"), QStringLiteral("读取中..."), m_osLabel));
    sysLayout->addLayout(createInfoRowEx(QStringLiteral("设备编号"), QStringLiteral("读取中..."), m_deviceIdLabel));
    sysLayout->addLayout(createInfoRowEx(QStringLiteral("运行时长"), QStringLiteral("读取中..."), m_uptimeLabel));
    sysLayout->addLayout(createInfoRowEx(QStringLiteral("磁盘空间"), QStringLiteral("读取中..."), m_diskLabel));
    sysLayout->addLayout(createInfoRowEx(QStringLiteral("CPU占用"), QStringLiteral("读取中..."), m_cpuLabel));
    sysLayout->addLayout(createInfoRowEx(QStringLiteral("内存占用"), QStringLiteral("读取中..."), m_memoryLabel));

    layout->addWidget(sysInfoFrame);
}

/** 构建本机机组配置区：下拉选择框，从DB加载所有active机组 */
void SystemSettingsPage::buildMachineGroupSection(QVBoxLayout* layout) {
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
    m_machineGroupCombo->setMinimumHeight(StyleHelper::Token::ControlHeightTouch);
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
}

/** 构建备份设置保存栏（对齐Web端.save-bar） */
void SystemSettingsPage::buildBackupSaveBar(QVBoxLayout* layout) {
    auto* saveBar = new QFrame();
    saveBar->setStyleSheet(StyleHelper::saveBarSeparator());
    auto* saveBarLayout = new QHBoxLayout(saveBar);
    saveBarLayout->setContentsMargins(0, 8, 0, 0);
    saveBarLayout->addStretch();
    auto* saveBtn = new QPushButton(QStringLiteral("💾 保存备份设置"));
    saveBtn->setStyleSheet(StyleHelper::settingSaveBtn());
    saveBtn->setCursor(Qt::PointingHandCursor);
    connect(saveBtn, &QPushButton::clicked, this, &SystemSettingsPage::onSaveBackup);
    saveBarLayout->addWidget(saveBtn);
    layout->addWidget(saveBar);
}

// createTaskTypePanel已迁移到SystemMaintenancePage

// ==================== 槽函数 ====================
// createTaskTypePanel已迁移到SystemMaintenancePage

// ==================== 槽函数 ====================

