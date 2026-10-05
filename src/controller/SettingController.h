/**
 * @file SettingController.h
 * @brief 系统设置控制层：配置读写转发与数据库连通性探测
 * @author 袁燕
 */
#pragma once
#include <QObject>
#include "common/AppConfig.h"

// 分层定位：Controller 负责配置读写转发与数据库连通性探测；
// 恢复出厂设置等业务动作走 services/SettingService（内含管理员口令校验）。
class SettingController : public QObject {
    Q_OBJECT
public:
    /// @param parent Qt父对象
    /// @param config 配置来源，默认指向全局单例；单元测试可传入独立 AppConfig 实例
    ///               （config 置于第二参数，确保既有 SettingController(this) 调用不受影响）
    explicit SettingController(QObject* parent = nullptr,
                               AppConfig* config = &AppConfig::instance());

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

private:
    AppConfig* m_config = nullptr;

signals:
    void settingsChanged();
    void dbConnected(bool ok);
};
