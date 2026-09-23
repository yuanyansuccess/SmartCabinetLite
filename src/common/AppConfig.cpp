// 作者：袁燕  智能柜Qt Widget 2.0  AppConfig实现
// 日期：2026-06-21
// [2026-06-26v19] 重构：QSettings改用显式INI文件，30个系统配置项统一走本地INI读写
#include "AppConfig.h"
#include "Constants.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>

AppConfig& AppConfig::instance() {
    static AppConfig inst;
    return inst;
}

AppConfig::AppConfig()
    : m_settings(nullptr)
    , m_host(SC::DB_HOST), m_port(SC::DB_PORT)
    , m_name(SC::DB_NAME), m_user(SC::DB_USER), m_pass(SC::DB_PASS)
    , m_localGroupId(0)
{
    // [v19] 使用显式INI文件路径，存放在程序目录下的config/子目录
    // [2026-06-27] 要求：必须有和执行文件同级的ini文件
    //   方案：ini文件直接放在程序根目录下（与exe同级），文件名system.ini
    //   原先放在config/子目录调整为与exe同级，更符合用户习惯
    QString appDir = QCoreApplication::applicationDirPath();
    m_iniPath = appDir + "/system.ini";
    // [2026-06-27] 首次运行时创建默认INI文件，写入软件版本等默认值
    if (!QFile::exists(m_iniPath)) {
        createDefaultIni(m_iniPath);
    }
    m_settings = new QSettings(m_iniPath, QSettings::IniFormat);
    m_settings->setFallbacksEnabled(false);
    load();
}

AppConfig::~AppConfig() { delete m_settings; }

QString AppConfig::iniFilePath() const { return m_iniPath; }

// [2026-06-27] 首次运行时创建默认INI文件（与执行文件同级）
//   写入软件版本号 V2.00 以及各配置节的默认值，确保用户可手动编辑
//   [Qt6兼容] QTextStream::setCodec已移除，改用QFile::write直接写UTF-8字节流
void AppConfig::createDefaultIni(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "[AppConfig] 无法创建默认INI文件:" << path;
        return;
    }
    // [2026-06-27] 用QString拼接INI内容，统一toUtf8写入，避免C++字面量指针相加编译错误
    QString content;
    content += "[System]\n";
    content += "version=" + SC::APP_VERSION + "\n";
    content += "\n";
    content += "[Database]\n";
    content += "host=" + SC::DB_HOST + "\n";
    content += "port=" + QString::number(SC::DB_PORT) + "\n";
    content += "name=" + SC::DB_NAME + "\n";
    content += "user=" + SC::DB_USER + "\n";
    content += "pass=" + SC::DB_PASS + "\n";
    content += "\n";
    content += "[Local]\n";
    content += "group_id=1\n";
    content += "group_name=发动机维护机组\n";
    content += "\n";
    content += "[Network]\n";
    content += "ip=" + SC::NET_IP + "\n";
    content += "mask=" + SC::NET_MASK + "\n";
    content += "gateway=" + SC::NET_GATEWAY + "\n";
    content += "dns=" + SC::NET_DNS + "\n";
    content += "server=" + SC::NET_SERVER + "\n";
    content += "speed_mode=" + QString::number(SC::NET_SPEED_MODE) + "\n";
    content += "network_mode=" + QString::number(SC::NET_NETWORK_MODE) + "\n";
    content += "\n";
    content += "[Alert]\n";
    content += "buzzer_vol=" + QString::number(SC::ALERT_BUZZER_VOL) + "\n";
    content += "led_enabled=" + QString(SC::ALERT_LED_ENABLED ? "true" : "false") + "\n";
    content += "overdue_hours=" + QString::number(SC::ALERT_OVERDUE_HOURS) + "\n";
    content += "door_timeout=" + QString::number(SC::ALERT_DOOR_TIMEOUT) + "\n";
    content += "rfid_enabled=" + QString(SC::ALERT_RFID_ENABLED ? "true" : "false") + "\n";
    content += "power_alarm_mode=" + QString::number(SC::ALERT_POWER_ALARM_MODE) + "\n";
    content += "auto_confirm=" + QString(SC::ALERT_AUTO_CONFIRM ? "true" : "false") + "\n";
    content += "\n";
    content += "[Borrow]\n";
    content += "max_count=" + QString::number(SC::BORROW_MAX_COUNT) + "\n";
    content += "default_period=" + QString::number(SC::BORROW_DEFAULT_PERIOD) + "\n";
    content += "return_buffer=" + QString::number(SC::BORROW_RETURN_BUFFER) + "\n";
    content += "manual_unlock=" + QString(SC::BORROW_MANUAL_UNLOCK ? "true" : "false") + "\n";
    content += "brightness=" + QString::number(SC::BORROW_BRIGHTNESS) + "\n";
    content += "lock_time=" + QString::number(SC::BORROW_LOCK_TIME) + "\n";
    content += "face_sensitivity=" + QString::number(SC::BORROW_FACE_SENSITIVITY) + "\n";
    content += "\n";
    content += "[Backup]\n";
    content += "auto_enabled=" + QString(SC::BACKUP_AUTO_ENABLED ? "true" : "false") + "\n";
    content += "period=" + QString::number(SC::BACKUP_PERIOD) + "\n";
    content += "cache_hours=" + QString::number(SC::BACKUP_CACHE_HOURS) + "\n";
    content += "path=" + SC::BACKUP_PATH + "\n";

    // UTF-8 BOM (QSettings IniFormat识别BOM确保中文不乱码)
    file.write("\xEF\xBB\xBF");
    file.write(content.toUtf8());
    file.close();
    qInfo() << "[AppConfig] 已创建默认INI文件:" << path;
}

// [2026-06-27] 应用版本号（从INI的System/version读取，默认Constants.h中的APP_VERSION）
//   修改版本号只需编辑exe同级目录的system.ini中[System]节的version字段
QString AppConfig::appVersion() const {
    QMutexLocker l(&m_mutex);
    return m_settings->value("System/version", SC::APP_VERSION).toString();
}

void AppConfig::setAppVersion(const QString& version) {
    QMutexLocker l(&m_mutex);
    m_settings->setValue("System/version", version);
}

// ═══════════════════════════════════════════════
// 数据库配置
// ═══════════════════════════════════════════════
QString AppConfig::dbHost() const { QMutexLocker l(&m_mutex); return m_host; }
int     AppConfig::dbPort() const { QMutexLocker l(&m_mutex); return m_port; }
QString AppConfig::dbName() const { QMutexLocker l(&m_mutex); return m_name; }
QString AppConfig::dbUser() const { QMutexLocker l(&m_mutex); return m_user; }
QString AppConfig::dbPass() const { QMutexLocker l(&m_mutex); return m_pass; }

void AppConfig::setDbConfig(const QString& host, int port,
                            const QString& name, const QString& user, const QString& pass) {
    QMutexLocker l(&m_mutex);
    m_host = host; m_port = port;
    m_name = name; m_user = user; m_pass = pass;
}

// ═══════════════════════════════════════════════
// 本机机组
// ═══════════════════════════════════════════════
int AppConfig::localMachineGroupId() const {
    QMutexLocker l(&m_mutex); return m_localGroupId;
}
QString AppConfig::localMachineGroupName() const {
    QMutexLocker l(&m_mutex); return m_localGroupName;
}
void AppConfig::setLocalMachineGroup(int groupId, const QString& groupName) {
    QMutexLocker l(&m_mutex);
    m_localGroupId = groupId;
    m_localGroupName = groupName;
}

// ═══════════════════════════════════════════════
// 网络配置
// ═══════════════════════════════════════════════
QString AppConfig::netIp() const       { QMutexLocker l(&m_mutex); return m_settings->value("Network/ip",       SC::NET_IP).toString(); }
void AppConfig::setNetIp(const QString& v)       { QMutexLocker l(&m_mutex); m_settings->setValue("Network/ip", v); }
QString AppConfig::netMask() const     { QMutexLocker l(&m_mutex); return m_settings->value("Network/mask",     SC::NET_MASK).toString(); }
void AppConfig::setNetMask(const QString& v)     { QMutexLocker l(&m_mutex); m_settings->setValue("Network/mask", v); }
QString AppConfig::netGateway() const  { QMutexLocker l(&m_mutex); return m_settings->value("Network/gateway",  SC::NET_GATEWAY).toString(); }
void AppConfig::setNetGateway(const QString& v)  { QMutexLocker l(&m_mutex); m_settings->setValue("Network/gateway", v); }
QString AppConfig::netDns() const     { QMutexLocker l(&m_mutex); return m_settings->value("Network/dns",      SC::NET_DNS).toString(); }
void AppConfig::setNetDns(const QString& v)      { QMutexLocker l(&m_mutex); m_settings->setValue("Network/dns", v); }
QString AppConfig::netServer() const  { QMutexLocker l(&m_mutex); return m_settings->value("Network/server",   SC::NET_SERVER).toString(); }
void AppConfig::setNetServer(const QString& v)   { QMutexLocker l(&m_mutex); m_settings->setValue("Network/server", v); }
int AppConfig::netSpeedMode() const   { QMutexLocker l(&m_mutex); return m_settings->value("Network/speed_mode",   SC::NET_SPEED_MODE).toInt(); }
void AppConfig::setNetSpeedMode(int v)          { QMutexLocker l(&m_mutex); m_settings->setValue("Network/speed_mode", v); }
int AppConfig::netNetworkMode() const { QMutexLocker l(&m_mutex); return m_settings->value("Network/network_mode", SC::NET_NETWORK_MODE).toInt(); }
void AppConfig::setNetNetworkMode(int v)        { QMutexLocker l(&m_mutex); m_settings->setValue("Network/network_mode", v); }

// ═══════════════════════════════════════════════
// 告警配置
// ═══════════════════════════════════════════════
int AppConfig::alertBuzzerVol() const   { QMutexLocker l(&m_mutex); return m_settings->value("Alert/buzzer_vol",       SC::ALERT_BUZZER_VOL).toInt(); }
void AppConfig::setAlertBuzzerVol(int v)          { QMutexLocker l(&m_mutex); m_settings->setValue("Alert/buzzer_vol", v); }
bool AppConfig::alertLedEnabled() const { QMutexLocker l(&m_mutex); return m_settings->value("Alert/led_enabled",      SC::ALERT_LED_ENABLED).toBool(); }
void AppConfig::setAlertLedEnabled(bool v)        { QMutexLocker l(&m_mutex); m_settings->setValue("Alert/led_enabled", v); }
int AppConfig::alertOverdueHours() const { QMutexLocker l(&m_mutex); return m_settings->value("Alert/overdue_hours",     SC::ALERT_OVERDUE_HOURS).toInt(); }
void AppConfig::setAlertOverdueHours(int v)       { QMutexLocker l(&m_mutex); m_settings->setValue("Alert/overdue_hours", v); }
int AppConfig::alertDoorTimeout() const  { QMutexLocker l(&m_mutex); return m_settings->value("Alert/door_timeout",      SC::ALERT_DOOR_TIMEOUT).toInt(); }
void AppConfig::setAlertDoorTimeout(int v)        { QMutexLocker l(&m_mutex); m_settings->setValue("Alert/door_timeout", v); }
bool AppConfig::alertRfidEnabled() const { QMutexLocker l(&m_mutex); return m_settings->value("Alert/rfid_enabled",      SC::ALERT_RFID_ENABLED).toBool(); }
void AppConfig::setAlertRfidEnabled(bool v)       { QMutexLocker l(&m_mutex); m_settings->setValue("Alert/rfid_enabled", v); }
int AppConfig::alertPowerAlarmMode() const{ QMutexLocker l(&m_mutex); return m_settings->value("Alert/power_alarm_mode",  SC::ALERT_POWER_ALARM_MODE).toInt(); }
void AppConfig::setAlertPowerAlarmMode(int v)     { QMutexLocker l(&m_mutex); m_settings->setValue("Alert/power_alarm_mode", v); }
bool AppConfig::alertAutoConfirm() const { QMutexLocker l(&m_mutex); return m_settings->value("Alert/auto_confirm",      SC::ALERT_AUTO_CONFIRM).toBool(); }
void AppConfig::setAlertAutoConfirm(bool v)       { QMutexLocker l(&m_mutex); m_settings->setValue("Alert/auto_confirm", v); }

// ═══════════════════════════════════════════════
// 借还配置
// ═══════════════════════════════════════════════
int AppConfig::borrowMaxCount() const       { QMutexLocker l(&m_mutex); return m_settings->value("Borrow/max_count",         SC::BORROW_MAX_COUNT).toInt(); }
void AppConfig::setBorrowMaxCount(int v)              { QMutexLocker l(&m_mutex); m_settings->setValue("Borrow/max_count", v); }
int AppConfig::borrowDefaultPeriod() const  { QMutexLocker l(&m_mutex); return m_settings->value("Borrow/default_period",    SC::BORROW_DEFAULT_PERIOD).toInt(); }
void AppConfig::setBorrowDefaultPeriod(int v)         { QMutexLocker l(&m_mutex); m_settings->setValue("Borrow/default_period", v); }
int AppConfig::borrowReturnBuffer() const   { QMutexLocker l(&m_mutex); return m_settings->value("Borrow/return_buffer",     SC::BORROW_RETURN_BUFFER).toInt(); }
void AppConfig::setBorrowReturnBuffer(int v)          { QMutexLocker l(&m_mutex); m_settings->setValue("Borrow/return_buffer", v); }
bool AppConfig::borrowManualUnlock() const  { QMutexLocker l(&m_mutex); return m_settings->value("Borrow/manual_unlock",     SC::BORROW_MANUAL_UNLOCK).toBool(); }
void AppConfig::setBorrowManualUnlock(bool v)         { QMutexLocker l(&m_mutex); m_settings->setValue("Borrow/manual_unlock", v); }
int AppConfig::borrowBrightness() const     { QMutexLocker l(&m_mutex); return m_settings->value("Borrow/brightness",       SC::BORROW_BRIGHTNESS).toInt(); }
void AppConfig::setBorrowBrightness(int v)            { QMutexLocker l(&m_mutex); m_settings->setValue("Borrow/brightness", v); }
int AppConfig::borrowLockTime() const       { QMutexLocker l(&m_mutex); return m_settings->value("Borrow/lock_time",        SC::BORROW_LOCK_TIME).toInt(); }
void AppConfig::setBorrowLockTime(int v)              { QMutexLocker l(&m_mutex); m_settings->setValue("Borrow/lock_time", v); }
int AppConfig::borrowFaceSensitivity() const{ QMutexLocker l(&m_mutex); return m_settings->value("Borrow/face_sensitivity", SC::BORROW_FACE_SENSITIVITY).toInt(); }
void AppConfig::setBorrowFaceSensitivity(int v)       { QMutexLocker l(&m_mutex); m_settings->setValue("Borrow/face_sensitivity", v); }

// ═══════════════════════════════════════════════
// 备份配置
// ═══════════════════════════════════════════════
bool AppConfig::backupAutoEnabled() const  { QMutexLocker l(&m_mutex); return m_settings->value("Backup/auto_enabled", SC::BACKUP_AUTO_ENABLED).toBool(); }
void AppConfig::setBackupAutoEnabled(bool v)       { QMutexLocker l(&m_mutex); m_settings->setValue("Backup/auto_enabled", v); }
int AppConfig::backupPeriod() const        { QMutexLocker l(&m_mutex); return m_settings->value("Backup/period",       SC::BACKUP_PERIOD).toInt(); }
void AppConfig::setBackupPeriod(int v)             { QMutexLocker l(&m_mutex); m_settings->setValue("Backup/period", v); }
int AppConfig::backupCacheHours() const    { QMutexLocker l(&m_mutex); return m_settings->value("Backup/cache_hours",  SC::BACKUP_CACHE_HOURS).toInt(); }
void AppConfig::setBackupCacheHours(int v)         { QMutexLocker l(&m_mutex); m_settings->setValue("Backup/cache_hours", v); }
QString AppConfig::backupPath() const      { QMutexLocker l(&m_mutex); return m_settings->value("Backup/path",         SC::BACKUP_PATH).toString(); }
void AppConfig::setBackupPath(const QString& v)    { QMutexLocker l(&m_mutex); m_settings->setValue("Backup/path", v); }

// ═══════════════════════════════════════════════
// 通用读写
// ═══════════════════════════════════════════════
QString AppConfig::value(const QString& key, const QString& defaultValue) const {
    QMutexLocker l(&m_mutex);
    return m_settings->value(key, defaultValue).toString();
}

void AppConfig::setValue(const QString& key, const QString& value) {
    QMutexLocker l(&m_mutex);
    m_settings->setValue(key, value);
}

// ═══════════════════════════════════════════════
// 持久化：从INI文件加载 / 同步写入
// ═══════════════════════════════════════════════
void AppConfig::save() {
    QMutexLocker l(&m_mutex);
    m_settings->setValue("Database/host", m_host);
    m_settings->setValue("Database/port", m_port);
    m_settings->setValue("Database/name", m_name);
    m_settings->setValue("Database/user", m_user);
    m_settings->setValue("Database/pass", m_pass);
    m_settings->setValue("Local/group_id",   m_localGroupId);
    m_settings->setValue("Local/group_name", m_localGroupName);
    m_settings->sync();
}

void AppConfig::load() {
    QMutexLocker l(&m_mutex);
    m_host = m_settings->value("Database/host", SC::DB_HOST).toString();
    m_port = m_settings->value("Database/port", SC::DB_PORT).toInt();
    m_name = m_settings->value("Database/name", SC::DB_NAME).toString();
    m_user = m_settings->value("Database/user", SC::DB_USER).toString();
    m_pass = m_settings->value("Database/pass", SC::DB_PASS).toString();
    m_localGroupId  = m_settings->value("Local/group_id",   1).toInt();
    m_localGroupName = m_settings->value("Local/group_name", QStringLiteral("发动机维护机组")).toString();
}
