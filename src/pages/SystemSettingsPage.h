/**
 * @file SystemSettingsPage.h
 * @brief 系统设置页面 - 1:1复刻BS端SystemSettings.vue
 * @author 袁燕
 *
 * 实现系统设置页各项设置功能，与 BS 端功能对齐
 * 布局：双列网格（网络配置/告警参数/借还设置/备份管理）
 * 顶部工具栏：保存全部设置按钮
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
#include <functional>  // 备份完成回调（std::function）

class QVBoxLayout;
class QTableWidget;

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
    // 手动备份：立即执行一次数据库导出，不受自动备份开关与备份周期限制
    void onManualBackupClicked();

private:
    void setupUI();
    void updateTabStyles();
    bool eventFilter(QObject* watched, QEvent* event);
    // QStackedWidget切换，去掉highlightPanel和滚动
    void switchTab(int index);
    // 更新按钮组样式（断电告警/网口速率/组网模式/人脸灵敏度/备份周期）
    void updatePowerAlarmBtnStyles();
    void updateSpeedBtnStyles();
    void updateModeBtnStyles();
    void updateBackupPeriodBtnStyles();

    // 创建四个面板
    QWidget* createNetworkPanel();
    QWidget* createAlertPanel();
    QWidget* createBorrowPanel();
    QWidget* createBackupPanel();
    void buildBackupForm(QVBoxLayout* layout);
    void buildBackupSaveBar(QVBoxLayout* layout);

    // ── 任务配置面板（任务类型与工具推荐数量配置）──
    QWidget* createTaskConfigPanel();  ///< 构建面板：任务类型下拉+工具表格+保存栏
    void loadTaskTypes();              ///< 加载任务类型下拉选项
    void loadTaskTools(int typeId);    ///< 加载指定任务类型已配置的工具清单
    void saveTaskToolConfig();         ///< 将表格中各工具的推荐数量写回数据库
    void onAddTaskTool();              ///< 新增任务工具（弹窗：工具类型/工具/推荐数量）
    void onEditTaskTool(int row);      ///< 修改任务工具推荐数量
    void onDeleteTaskTool(int row);    ///< 从当前任务类型中移除指定工具

    // 调用系统API设置显示器亮度(0-100)，异步执行不阻塞UI
    void applyDisplayBrightness(int percent);
    /** 亮度调整平台分支与日志（applyDisplayBrightness拆解） */
    void writeBrightnessLog(int percent, const QString& method, const QString& result, int exitCode);
    void applyBrightnessWindows(int percent);
    void applyBrightnessLinux(int percent);
    // 执行数据库自动备份（保存备份设置时触发首次备份；定时调度也复用此入口）
    void performDatabaseBackup();
    void initBackupScheduler();   // 启动自动备份定时器（每小时轮询一次）
    void onBackupTimerTick();     // 定时器超时：按周期判断今天是否应备份
    /**
     * @brief 启动一次 mysqldump 导出，异步执行不阻塞界面
     * @param backupPath 备份文件存放目录（不存在时自动创建）
     * @param onFinished 完成回调：参数1=true 表示导出成功，参数2=备份文件完整路径（失败为空串）
     */
    void startMysqlDumpBackup(const QString& backupPath,
                              const std::function<void(bool, const QString&)>& onFinished);
    // 跨平台配置有线网卡（Windows用netsh，麒麟用ip/ethtool/nmcli），仅配置有线网卡
    // @param speedMode 网口速率：0=1000M自适应，1=100M全双工（麒麟侧通过ethtool/nmcli落地）
    void applyNetworkConfig(const QString& ip, const QString& mask,
                            const QString& gateway, const QString& dns,
                            int speedMode = 0);
    // 获取第一块有线网卡名称（用于网络配置定位）
    QString detectWiredInterfaceName();

    // 选项卡
    int m_activeTabIndex = 0;
    QList<QLabel*> m_tabLabels;
    QStackedWidget* m_stackedWidget = nullptr;  // 替代QScrollArea
    // 四个面板引用
    QWidget* m_networkPanel = nullptr;
    QWidget* m_alertPanel = nullptr;
    QWidget* m_borrowPanel = nullptr;
    QWidget* m_backupPanel = nullptr;

    // 任务配置面板控件
    QComboBox* m_taskTypeCombo = nullptr;    ///< 任务类型下拉
    QTableWidget* m_taskToolTable = nullptr; ///< 任务类型已配置工具表格
    QPushButton* m_addTaskToolBtn = nullptr; ///< "新增任务工具"按钮
    QPushButton* m_taskSaveBtn = nullptr;    ///< "保存配置"按钮

    // 顶部工具栏按钮
    QPushButton* m_saveAllBtn = nullptr;

    // 网络配置
    QLineEdit* m_ipEdit = nullptr;
    QLineEdit* m_maskEdit = nullptr;
    QLineEdit* m_gatewayEdit = nullptr;
    QLineEdit* m_dnsEdit = nullptr;
    // 网口速率为按钮组
    QPushButton* m_speedBtn1 = nullptr;
    QPushButton* m_speedBtn2 = nullptr;
    int m_speedMode = 0;  // 0=1000M自适应, 1=100M全双工
    // 组网模式为按钮组
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
    // 断电告警方式为按钮组
    QPushButton* m_powerAlarmBtn1 = nullptr;  // 声光同时告警
    QPushButton* m_powerAlarmBtn2 = nullptr;  // 仅灯光
    QPushButton* m_powerAlarmBtn3 = nullptr;  // 仅声音
    int m_powerAlarmMode = 0;  // 0=声光同时, 1=仅灯光, 2=仅声音
    QCheckBox* m_autoConfirmCheck = nullptr;

    // 借还设置
    QSpinBox* m_defaultPeriodSpin = nullptr;
    QSpinBox* m_returnBufferSpin = nullptr;
    QSlider* m_brightnessSlider = nullptr;
    QLabel* m_brightnessValueLabel = nullptr;

    // 备份管理
    QCheckBox* m_autoBackupCheck = nullptr;
    QPushButton* m_manualBackupBtn = nullptr;  // 手动备份按钮（立即导出一次数据库）
    // 备份周期为按钮组
    QPushButton* m_backupBtn1 = nullptr;
    QPushButton* m_backupBtn2 = nullptr;
    QPushButton* m_backupBtn3 = nullptr;
    int m_backupPeriod = 0;  // 0=每日, 1=每周一, 2=每周日
    QLineEdit* m_backupPathEdit = nullptr;
    QSpinBox* m_cacheHoursSpin = nullptr;
    QTimer* m_backupTimer = nullptr;   // 自动备份调度定时器（SystemSettingsPage 常驻，随程序生命周期运行）
    bool    m_backingUp = false;       // 防止自动/保存触发并发启动多个 mysqldump

    // 离开页面未保存时提示用户
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void snapshotSettings();      // 快照当前UI值
    bool isDirty();  // 检测UI值是否与快照不同
    void loadConfigFromIni();  // 从本地INI文件加载配置到UI
    bool saveConfigToIni();  // 将当前UI值写入本地INI文件，返回是否成功
    bool m_savedFlag = false;     // 是否已点击过保存按钮

    // ── 设置快照（进入页面时备份，未保存离开时还原）──
    struct SettingsSnapshot {
        // 网络配置
        QString ip, mask, gateway, dns, server;
        int speedMode, networkMode;
        // 告警参数
        int buzzerVol, overdueHours, doorTimeoutSec, powerAlarmMode;
        bool ledAlert, visionAlert, autoConfirm;
        // 借还设置
        int defaultPeriod, returnBuffer, brightness;
        // 备份管理
        bool autoBackup;
        int backupPeriod, cacheHours;
        QString backupPath;
    } m_snapshot;
};
