#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0  系统设置业务控制层
// 日期：2026-06-21 功能：数据库配置、系统参数的管理
#include <QObject>
#include "common/AppConfig.h"

class SettingController : public QObject {
    Q_OBJECT
public:
    explicit SettingController(QObject* parent = nullptr);

    // DB配置
    QString dbHost() const;
    int     dbPort() const;
    QString dbName() const;
    QString dbUser() const;
    void    setDbConfig(const QString& host, int port, const QString& name, const QString& user, const QString& pass);
    bool    testDbConnection();

    // 通用设置
    QString setting(const QString& key, const QString& def = "") const;
    void    setSetting(const QString& key, const QString& value);
    void    saveSettings();

    // 系统操作
    bool factoryReset(const QString& adminPassword);
    bool restartSystem();

signals:
    void settingsChanged();
    void dbConnected(bool ok);
};
