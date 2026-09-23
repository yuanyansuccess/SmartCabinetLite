/**
 * @file SettingService.h
 * @brief 系统设置服务
 * @author 袁燕
 */
#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>

class SettingService : public QObject {
    Q_OBJECT
public:
    explicit SettingService(QObject* parent = nullptr);
    QJsonObject getDashboardStats();
    QJsonArray getRecentLogs(int limit = 10);
    QJsonArray getRecentAlerts(int limit = 5);          // 获取最近告警列表
    QJsonObject getAllAlerts(int page = 1, int pageSize = 200,  // 获取告警分页列表（含JOIN关联信息）
                            const QString& type = "", const QString& level = "",
                            const QString& keyword = "");  // AlertLogsPage数据源 [2026-06-25] 返回QJsonObject含total
    QJsonObject getAlertDetail(int alertId);  // 获取单条告警详情（含处理人信息）
    QJsonObject getAlertStats(const QString& type, const QString& level,
                              const QString& keyword);  // 获取告警全局统计（含筛选条件，不受分页影响）
    QJsonArray getAlertTypes();               // 获取告警类型列表（供前端筛选使用）
    QJsonObject getUserDashboardStats(int userId);       // 获取用户首页统计
    QJsonArray getUserBorrowRecords(int userId, int limit = 10);  // 获取用户借用记录（含归还提醒数据）
    QJsonObject getLedgerStats();
    struct UpgradeResult { bool hasUpdate; QString newVersion; };
    UpgradeResult checkUpgrade();
    bool factoryReset(const QString& adminPassword);
    bool clearAllLogs(const QString& adminPassword);
    QJsonArray getDepartments();

    // 系统配置持久化（读写 system_config 表）
    QJsonObject loadAllConfig();                                  // 加载全部配置
    bool saveConfig(const QJsonObject& config);                  // 批量保存配置
    bool saveConfigValue(const QString& key, const QString& value);  // 保存单个配置项
    QString loadConfigValue(const QString& key, const QString& defaultValue = "");  // 读取单个配置项
};
