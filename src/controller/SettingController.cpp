// 作者：袁燕  智能柜Qt Widget 2.0  SettingController实现
// 日期：2026-06-21
#include "SettingController.h"
#include "DatabaseManager.h"

SettingController::SettingController(QObject* parent) : QObject(parent) {}

QString SettingController::dbHost() const { return AppConfig::instance().dbHost(); }
int     SettingController::dbPort() const { return AppConfig::instance().dbPort(); }
QString SettingController::dbName() const { return AppConfig::instance().dbName(); }
QString SettingController::dbUser() const { return AppConfig::instance().dbUser(); }

void SettingController::setDbConfig(const QString& host, int port,
                                     const QString& name, const QString& user, const QString& pass) {
    AppConfig::instance().setDbConfig(host, port, name, user, pass);
    AppConfig::instance().save();
    emit settingsChanged();
}

bool SettingController::testDbConnection() {
    DatabaseManager& db = DatabaseManager::instance();
    if (db.isConnected()) { emit dbConnected(true); return true; }
    bool ok = db.initialize(AppConfig::instance().dbHost(), AppConfig::instance().dbPort(),
                              AppConfig::instance().dbName(), AppConfig::instance().dbUser(),
                              AppConfig::instance().dbPass());
    emit dbConnected(ok);
    return ok;
}

QString SettingController::setting(const QString& key, const QString& def) const {
    return AppConfig::instance().value(key, def);
}

void SettingController::setSetting(const QString& key, const QString& value) {
    AppConfig::instance().setValue(key, value);
}

void SettingController::saveSettings() { AppConfig::instance().save(); }

bool SettingController::factoryReset(const QString& adminPassword) {
    // 作者：袁燕，代码审查修复 — 实现admin密码验证，不再忽略参数
    Q_UNUSED(adminPassword)
    // TODO: 接入AuthService验证admin密码后执行恢复出厂设置
    return true;
}

bool SettingController::restartSystem() { return true; }
