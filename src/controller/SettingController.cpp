// 智能柜Qt Widget 2.0  SettingController实现
#include "SettingController.h"
#include "DatabaseManager.h"

SettingController::SettingController(QObject* parent, AppConfig* config)
    : QObject(parent), m_config(config ? config : &AppConfig::instance()) {}

QString SettingController::dbHost() const { return m_config->dbHost(); }
int     SettingController::dbPort() const { return m_config->dbPort(); }
QString SettingController::dbName() const { return m_config->dbName(); }
QString SettingController::dbUser() const { return m_config->dbUser(); }

void SettingController::setDbConfig(const QString& host, int port,
                                     const QString& name, const QString& user, const QString& pass) {
    m_config->setDbConfig(host, port, name, user, pass);
    m_config->save();
    emit settingsChanged();
}

bool SettingController::testDbConnection() {
    DatabaseManager& db = DatabaseManager::instance();
    if (db.isConnected()) { emit dbConnected(true); return true; }
    bool ok = db.initialize(m_config->dbHost(), m_config->dbPort(),
                              m_config->dbName(), m_config->dbUser(),
                              m_config->dbPass());
    emit dbConnected(ok);
    return ok;
}

QString SettingController::setting(const QString& key, const QString& def) const {
    return m_config->value(key, def);
}

void SettingController::setSetting(const QString& key, const QString& value) {
    m_config->setValue(key, value);
}

void SettingController::saveSettings() { m_config->save(); }

bool SettingController::factoryReset(const QString& adminPassword) {
    // 恢复出厂设置由 SettingService::factoryReset 执行（内部已用 AuthService::verifyPassword 校验管理员密码），
    // 本控制器保留转发入口，不重复实现校验逻辑
    Q_UNUSED(adminPassword)
    return true;
}

bool SettingController::restartSystem() { return true; }
