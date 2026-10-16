/**
 * @file SystemSettingsPagePanels.cpp
 * @brief 系统设置页 - 各设置面板的界面构建（借还设置/备份管理）
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
#include "common/Constants.h"  // SC::BACKUP_PATH 等常量
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
#include <QRegularExpression>  // 解析os-release
#include <QFileInfoList>  // 备份文件清理


/**
 * @brief 创建借用面板
 */
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
    auto* title = new QLabel(QStringLiteral("数据备份"));
    title->setStyleSheet(StyleHelper::sectionTitle());
    titleRow->addWidget(title);
    titleRow->addStretch();
    layout->addLayout(titleRow);
    auto* sep = new QFrame(); sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(StyleHelper::separatorLine());
    layout->addWidget(sep);

    buildBackupForm(layout);
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
    // 手动备份按钮与自动备份开关并排：随时可导出一次数据库，不受开关与周期限制
    m_manualBackupBtn = new QPushButton(QStringLiteral("手动备份"));
    m_manualBackupBtn->setStyleSheet(StyleHelper::settingSaveBtn());
    m_manualBackupBtn->setCursor(Qt::PointingHandCursor);
    m_manualBackupBtn->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
    m_manualBackupBtn->setMinimumWidth(120);
    connect(m_manualBackupBtn, &QPushButton::clicked, this, &SystemSettingsPage::onManualBackupClicked);
    auto* autoBackupWidget = new QWidget();
    autoBackupWidget->setStyleSheet("background:transparent;");
    auto* autoBackupLayout = new QHBoxLayout(autoBackupWidget);
    autoBackupLayout->setContentsMargins(0, 0, 0, 0);
    autoBackupLayout->setSpacing(12);
    autoBackupLayout->addWidget(m_autoBackupCheck);
    autoBackupLayout->addStretch();
    autoBackupLayout->addWidget(m_manualBackupBtn);
    form->addRow(FormFactory::formLabel(QStringLiteral("自动备份")), autoBackupWidget);

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
