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
 * @brief 读取配置项
 * @param key 配置键
 * @param def 键不存在时返回的默认值
 * @return 配置值
 */
QString SettingController::setting(const QString& key, const QString& def) const {
    return m_config->value(key, def);
}

/**
 * @brief 将配置写入配置文件
 */
void SettingController::saveSettings() { m_config->save(); }
