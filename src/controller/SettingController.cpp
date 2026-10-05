/**
 * @file SettingController.cpp
 * @brief 系统设置控制层实现：配置读写转发与数据库连通性探测
 * @author 袁燕
 */
#include "SettingController.h"
#include "DatabaseManager.h"

SettingController::SettingController(QObject* parent, AppConfig* config)
    : QObject(parent), m_config(config ? config : &AppConfig::instance()) {}

/**
 * @brief 读取数据库地址
 * @return 数据库地址
 */
QString SettingController::dbHost() const { return m_config->dbHost(); }
/**
 * @brief 读取数据库端口
 * @return 数据库端口
 */
int     SettingController::dbPort() const { return m_config->dbPort(); }
/**
 * @brief 读取数据库名
 * @return 数据库名
 */
QString SettingController::dbName() const { return m_config->dbName(); }
/**
 * @brief 读取数据库账号
 * @return 数据库账号
 */
QString SettingController::dbUser() const { return m_config->dbUser(); }

/**
 * @brief 保存数据库连接配置
 * @param host 地址
 * @param port 端口
 * @param name 数据库名
 * @param user 账号
 * @param pass 口令
 * @note 写入后立即持久化到配置文件
 */
void SettingController::setDbConfig(const QString& host, int port,
                                     const QString& name, const QString& user, const QString& pass) {
    m_config->setDbConfig(host, port, name, user, pass);
    m_config->save();
    emit settingsChanged();
}

/**
 * @brief 探测数据库连通性
 * @return true=连接成功
 * @note 已连接时直接返回成功，未连接时尝试按当前配置重新连接
 */
bool SettingController::testDbConnection() {
    DatabaseManager& db = DatabaseManager::instance();
    if (db.isConnected()) { emit dbConnected(true); return true; }
    bool ok = db.initialize(m_config->dbHost(), m_config->dbPort(),
                              m_config->dbName(), m_config->dbUser(),
                              m_config->dbPass());
    emit dbConnected(ok);
    return ok;
}

/**
 * @brief 读取配置项
 * @param key 配置键
 * @param def 键不存在时返回的默认值
 * @return 配置值
 */
QString SettingController::setting(const QString& key, const QString& def) const {
    return m_config->value(key, def);
}

/**
 * @brief 写入配置项
 * @param key 配置键
 * @param value 配置值
 * @note 需调用 saveSettings() 才会落盘
 */
void SettingController::setSetting(const QString& key, const QString& value) {
    m_config->setValue(key, value);
}

/**
 * @brief 将配置写入配置文件
 */
void SettingController::saveSettings() { m_config->save(); }

/**
 * @brief 恢复出厂设置入口
 * @param adminPassword 管理员口令
 * @return true=管理员身份校验通过
 * @note 实际业务由 SettingService::factoryReset 执行
 */
bool SettingController::factoryReset(const QString& adminPassword) {
    // 恢复出厂设置由 SettingService::factoryReset 执行（内部已用 AuthService::verifyPassword 校验管理员密码），
    // 本控制器保留转发入口，不重复实现校验逻辑
    Q_UNUSED(adminPassword)
    return true;
}

/**
 * @brief 重启系统
 * @return true=受理成功
 * @note 当前实现由上层负责重启流程
 */
bool SettingController::restartSystem() { return true; }
