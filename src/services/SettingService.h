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
    // 告警写域与详情/统计/类型查询由 AlertService 承担；
    // 本方法作为 AlertLogsPage 数据源入口，SQL 由 db/AlertDAO::findAllPaged 实现
    QJsonObject getAllAlerts(int page = 1, int pageSize = 200,  // 获取告警分页列表（含JOIN关联信息）
                            const QString& type = "", const QString& level = "",
                            const QString& keyword = "");  // 返回QJsonObject含total
    QJsonObject getLedgerStats();
    QJsonArray getDepartments();
};
