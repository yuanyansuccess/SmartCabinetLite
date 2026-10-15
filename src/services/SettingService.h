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
    // 告警写域与详情/统计/类型查询已迁移至 AlertService；
    // 本方法保留 AlertLogsPage 数据源入口，SQL 实现已下沉 db/AlertDAO::findAllPaged
    QJsonObject getAllAlerts(int page = 1, int pageSize = 200,  // 获取告警分页列表（含JOIN关联信息）
                            const QString& type = "", const QString& level = "",
                            const QString& keyword = "");  // 返回QJsonObject含total
    QJsonObject getLedgerStats();
    QJsonArray getDepartments();

    bool saveConfig(const QJsonObject& config);                  // 批量保存配置
};
