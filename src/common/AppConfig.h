#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0  应用配置管理（单例，基于本地INI文件）
// 日期：2026-06-21 功能：管理数据库连接、系统参数、读写本地ini配置文件
// [2026-06-26v19] 重构：QSettings改用显式INI文件路径，30个系统配置项统一走INI读写
#include <QString>
#include <QSettings>
#include <QMutex>

class AppConfig {
public:
    static AppConfig& instance();

    // ═══════════════════════════════════════════════
    // 数据库配置
    // ═══════════════════════════════════════════════
    QString dbHost() const;
    int     dbPort() const;
    QString dbName() const;
    QString dbUser() const;
    QString dbPass() const;
    void setDbConfig(const QString& host, int port,
                     const QString& name, const QString& user, const QString& pass);

    // [2026-06-27] 应用版本号（从INI的System/version读取，默认Constants.h中的APP_VERSION）
    QString appVersion() const;
    void    setAppVersion(const QString& version);

    // ═══════════════════════════════════════════════
    // 本机机组信息
    // ═══════════════════════════════════════════════
    int     localMachineGroupId() const;
    QString localMachineGroupName() const;
    void    setLocalMachineGroup(int groupId, const QString& groupName);

    // ═══════════════════════════════════════════════
    // 网络配置（Network节）
    // ═══════════════════════════════════════════════
    QString netIp() const;
    void    setNetIp(const QString& v);
    QString netMask() const;
    void    setNetMask(const QString& v);
    QString netGateway() const;
    void    setNetGateway(const QString& v);
    QString netDns() const;
    void    setNetDns(const QString& v);
    QString netServer() const;
    void    setNetServer(const QString& v);
    int     netSpeedMode() const;       // 0=1000M自适应, 1=100M
    void    setNetSpeedMode(int v);
    int     netNetworkMode() const;     // 0=单机运行, 1=联网运行
    void    setNetNetworkMode(int v);

    // ═══════════════════════════════════════════════
    // 告警配置（Alert节）
    // ═══════════════════════════════════════════════
    int  alertBuzzerVol() const;        // 蜂鸣器音量 dB（70-110）
    void setAlertBuzzerVol(int v);
    bool alertLedEnabled() const;       // LED闪烁开关
    void setAlertLedEnabled(bool v);
    int  alertOverdueHours() const;     // 逾期告警阈值(小时)
    void setAlertOverdueHours(int v);
    int  alertDoorTimeout() const;      // 柜门超时告警(秒)
    void setAlertDoorTimeout(int v);
    bool alertVisionEnabled() const;      // 视觉异常告警
    void setAlertVisionEnabled(bool v);
    int  alertPowerAlarmMode() const;   // 0=声光同时, 1=仅声音, 2=仅光
    void setAlertPowerAlarmMode(int v);
    bool alertAutoConfirm() const;      // 自动确认告警
    void setAlertAutoConfirm(bool v);

    // ═══════════════════════════════════════════════
    // 借还配置（Borrow节）
    // ═══════════════════════════════════════════════
    int  borrowMaxCount() const;        // 最大借用数
    void setBorrowMaxCount(int v);
    int  borrowDefaultPeriod() const;   // 默认借用期限(小时)
    void setBorrowDefaultPeriod(int v);
    int  borrowReturnBuffer() const;    // 归还缓冲时间(分钟)
    void setBorrowReturnBuffer(int v);
    bool borrowManualUnlock() const;    // 手动开锁
    void setBorrowManualUnlock(bool v);
    int  borrowBrightness() const;      // 屏幕亮度(%)
    void setBorrowBrightness(int v);
    int  borrowLockTime() const;        // 自动锁屏时间(分钟)
    void setBorrowLockTime(int v);
    int  borrowFaceSensitivity() const; // 0=标准, 1=宽松, 2=严格
    void setBorrowFaceSensitivity(int v);

    // ═══════════════════════════════════════════════
    // 备份配置（Backup节）
    // ═══════════════════════════════════════════════
    bool    backupAutoEnabled() const;
    void    setBackupAutoEnabled(bool v);
    int     backupPeriod() const;       // 0=每日, 1=每周, 2=每月
    void    setBackupPeriod(int v);
    int     backupCacheHours() const;
    void    setBackupCacheHours(int v);
    QString backupPath() const;
    void    setBackupPath(const QString& v);

    // ═══════════════════════════════════════════════
    // 通用读写 + 持久化
    // ═══════════════════════════════════════════════
    QString value(const QString& key, const QString& defaultValue = "") const;
    void    setValue(const QString& key, const QString& value);
    void    save();   // 同步写入INI文件
    void    load();   // 从INI文件加载
    QString iniFilePath() const;  // [v19] 返回INI文件路径

private:
    AppConfig();
    ~AppConfig();
    AppConfig(const AppConfig&) = delete;
    AppConfig& operator=(const AppConfig&) = delete;

    // [2026-06-27] 首次运行时创建默认INI文件（与执行文件同级）
    void createDefaultIni(const QString& path);

    QSettings* m_settings = nullptr;
    QString    m_iniPath;
    QString    m_host, m_name, m_user, m_pass;
    int        m_port = 3306;
    int        m_localGroupId = 0;
    QString    m_localGroupName;
    mutable QMutex m_mutex;
};
