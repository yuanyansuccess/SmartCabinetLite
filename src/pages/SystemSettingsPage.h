/**
 * @file SystemSettingsPage.h
 * @brief 系统设置页面 - 1:1复刻BS端SystemSettings.vue
 * @author 袁燕
 *
 * [2026-06-15 重构] 完全重写以1:1匹配BS端
 * 布局：双列网格（网络配置/告警参数/借还设置/备份管理+系统信息）
 * 底部：保存全部设置/恢复默认/软件升级按钮
 */
#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>

class SoftKeyboard;
class NumKeypad;
class BaseDialog;

#include <QList>
#include <QScrollArea>
#include <QStackedWidget>

class SystemSettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SystemSettingsPage(QWidget* parent = nullptr);
    void refresh();

private slots:
    void onSaveAll();
    void onSaveNetwork();
    void onSaveAlert();
    void onSaveBorrow();
    void onSaveBackup();
    void onResetDefault();
    void onStartUpgrade();
    void onSecurePwdConfirmed();
    void onMachineGroupSelected(int index);  // [2026-06-27] 下拉选择机组自动保存

private:
    void setupUI();
    void updateTabStyles();
    bool eventFilter(QObject* watched, QEvent* event);
    // [V2.03j] 改为QStackedWidget切换，去掉highlightPanel和滚动
    void switchTab(int index);
    // [V7.0] 更新按钮组样式（断电告警/网口速率/组网模式/人脸灵敏度/备份周期）
    void updatePowerAlarmBtnStyles();
    void updateSpeedBtnStyles();
    void updateModeBtnStyles();
    // [V2.03c] 已删除：updateFaceSensitivityBtnStyles
    void updateBackupPeriodBtnStyles();

    // 创建四个面板
    QWidget* createNetworkPanel();
    QWidget* createAlertPanel();
    QWidget* createBorrowPanel();
    QWidget* createBackupPanel();
    // [V2.03g] createTaskTypePanel已迁移到SystemMaintenancePage

    void showSecurePasswordDialog(const QString& title, const QString& actionName);
    // [2026-06-27] 调用系统API设置显示器亮度(0-100)，异步执行不阻塞UI
    void applyDisplayBrightness(int percent);
    // [V2.03c] 已删除：applyAutoLockTime（自动锁屏时间已移除）
    // [2026-06-27] 从系统读取真实系统信息（OS/设备编号/运行时长/磁盘空间）
    void refreshSystemInfo();
    // [2026-06-27] 执行数据库自动备份（保存备份设置时触发首次备份+注册定时任务）
    void performDatabaseBackup();
    // [2026-06-27] 跨平台配置有线网卡（Windows用netsh，麒麟用ip/nmcli），仅配置有线网卡
    void applyNetworkConfig(const QString& ip, const QString& mask,
                            const QString& gateway, const QString& dns);
    // [2026-06-27] 获取第一块有线网卡名称（用于网络配置定位）
    QString detectWiredInterfaceName();

    // 选项卡
    int m_activeTabIndex = 0;
    QList<QLabel*> m_tabLabels;
    QStackedWidget* m_stackedWidget = nullptr;  // [V2.03j] 替代QScrollArea
    // [2026-06-24v2] 四个面板引用
    QWidget* m_networkPanel = nullptr;
    QWidget* m_alertPanel = nullptr;
    QWidget* m_borrowPanel = nullptr;
    QWidget* m_backupPanel = nullptr;
    // [V2.03g] 任务配置面板已迁移到SystemMaintenancePage

    // 底部按钮
    QPushButton* m_restoreBtn = nullptr;
    QPushButton* m_saveAllBtn = nullptr;
    QPushButton* m_upgradeBtn = nullptr;

    // 网络配置
    QLineEdit* m_ipEdit = nullptr;
    QLineEdit* m_maskEdit = nullptr;
    QLineEdit* m_gatewayEdit = nullptr;
    QLineEdit* m_dnsEdit = nullptr;
    // [V7.0] 网口速率改为按钮组
    QPushButton* m_speedBtn1 = nullptr;
    QPushButton* m_speedBtn2 = nullptr;
    int m_speedMode = 0;  // 0=1000M自适应, 1=100M全双工
    // [V7.0] 组网模式改为按钮组
    QPushButton* m_modeBtn1 = nullptr;
    QPushButton* m_modeBtn2 = nullptr;
    int m_networkMode = 0;  // 0=单机运行, 1=组网管理
    QLineEdit* m_serverEdit = nullptr;

    // 告警参数
    QSlider* m_buzzerSlider = nullptr;
    QLabel* m_buzzerValueLabel = nullptr;
    QCheckBox* m_ledCheck = nullptr;
    QSpinBox* m_overdueSpin = nullptr;
    QSpinBox* m_doorTimeoutSpin = nullptr;
    QCheckBox* m_visionCheck = nullptr;
    // [V7.0] 断电告警方式改为按钮组（替换QComboBox）
    QPushButton* m_powerAlarmBtn1 = nullptr;  // 声光同时告警
    QPushButton* m_powerAlarmBtn2 = nullptr;  // 仅灯光
    QPushButton* m_powerAlarmBtn3 = nullptr;  // 仅声音
    int m_powerAlarmMode = 0;  // 0=声光同时, 1=仅灯光, 2=仅声音
    QCheckBox* m_autoConfirmCheck = nullptr;

    // 借还设置
    QSpinBox* m_maxBorrowSpin = nullptr;
    QSpinBox* m_defaultPeriodSpin = nullptr;
    QSpinBox* m_returnBufferSpin = nullptr;
    // [V2.03c] 已删除：m_manualUnlockCheck/m_lockTimeSpin/m_faceBtn1-3/m_faceSensitivity
    QSlider* m_brightnessSlider = nullptr;
    QLabel* m_brightnessValueLabel = nullptr;

    // 备份管理
    QCheckBox* m_autoBackupCheck = nullptr;
    // [V7.0] 备份周期改为按钮组
    QPushButton* m_backupBtn1 = nullptr;
    QPushButton* m_backupBtn2 = nullptr;
    QPushButton* m_backupBtn3 = nullptr;
    int m_backupPeriod = 0;  // 0=每日, 1=每周一, 2=每周日
    QLineEdit* m_backupPathEdit = nullptr;
    QSpinBox* m_cacheHoursSpin = nullptr;

    // 系统信息
    QLabel* m_versionLabel = nullptr;
    QLabel* m_osLabel = nullptr;
    QLabel* m_deviceIdLabel = nullptr;
    QLabel* m_uptimeLabel = nullptr;
    QLabel* m_diskLabel = nullptr;
    // [2026-06-27] 新增CPU/内存占用显示
    QLabel* m_cpuLabel = nullptr;
    QLabel* m_memoryLabel = nullptr;

    // [2026-06-26v14] 危险操作区已移除（恢复出厂设置/清除全部日志功能不再暴露在设置页面）

    // [2026-06-26v7] 离开页面未保存时提示用户
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void snapshotSettings();      // 快照当前UI值
    void restoreSettings();       // 还原UI值到快照
    bool isDirty();               // [2026-06-26v8] 检测UI值是否与快照不同
    void loadConfigFromIni();     // [v19] 从本地INI文件加载配置到UI
    bool saveConfigToIni();       // [v19] 将当前UI值写入本地INI文件，返回是否成功
    bool m_savedFlag = false;     // 是否已点击过保存按钮

    // 安全密码验证
    SoftKeyboard* m_softKeyboard = nullptr;
    NumKeypad* m_numKeypad = nullptr;   // [2026-06-26] 独立数字键盘
    BaseDialog* m_securePwdDialog = nullptr;  // [2026-06-26] 统一圆角风格
    QLineEdit* m_securePwdEdit = nullptr;
    QString m_pendingAction;

    // [2026-06-26v7] 当前机组名称配置 → [2026-06-27] 改为下拉选择框
    QComboBox* m_machineGroupCombo = nullptr;

    // ── 设置快照（进入页面时备份，未保存离开时还原）──
    struct SettingsSnapshot {
        // 网络配置
        QString ip, mask, gateway, dns, server;
        int speedMode, networkMode;
        // 告警参数
        int buzzerVol, overdueHours, doorTimeoutSec, powerAlarmMode;
        bool ledAlert, visionAlert, autoConfirm;
        // 借还设置
        int maxBorrow, defaultPeriod, returnBuffer, brightness;
        // [V2.03c] 已删除：lockTime, faceSensitivity, manualUnlock
        // 备份管理
        bool autoBackup;
        int backupPeriod, cacheHours;
        QString backupPath;
        // 机组名称
        QString machineGroupName;
    } m_snapshot;
};
