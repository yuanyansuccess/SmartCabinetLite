/**
 * @file SettingService.cpp
 * @brief 系统设置服务实现 — 仪表盘统计、告警日志、用户概览、台账统计、系统配置
 *        本层只做业务编排，SQL 统一在 db/ 层（AlertDAO/RecordDAO/ConfigDAO）
 * @author 袁燕
 */
#include "SettingService.h"
#include "db/UserDAO.h"
#include "db/ToolDAO.h"
#include "db/RecordDAO.h"
#include "db/AlertDAO.h"
#include "db/DepartmentDAO.h"
#include "db/ConfigDAO.h"
#include "AuthService.h"
#include <QDebug>

// 统一使用 db/ 目录，消除 dao/ 与 db/ 混用冗余
using db::UserDAO;
using db::ToolDAO;
using db::RecordDAO;
using db::AlertDAO;
using db::DepartmentDAO;
using db::ConfigDAO;

SettingService::SettingService(QObject* parent) : QObject(parent) {}

/**
 * @brief 汇总系统概览页统计数据
 * @return 含工具总数、在借数、逾期数、告警数与近期借用记录的对象
 */
QJsonObject SettingService::getDashboardStats() {
    QJsonObject s;
    // 按工具件数统计（total_qty/in_stock_qty/borrowed_qty），不按种类数
    // 工具总数 = 所有工具的 total_qty 之和
    // 在库工具 = 所有工具的 current_qty 之和（不论状态，当前实际在库的件数）
    // 已借出 = 活跃借用记录的 borrow_qty 之和
    // 在库比例 = 在库件数 / (在库件数 + 已借出件数)
    ToolDAO toolDao;
    QJsonObject toolStats = toolDao.getToolStats();
    // 统计为按条目数(COUNT)：一个位置一个工具，总数=列表条数
    // 工具总数 = 在库条目数 + 已借出条目数（不含已出库的，出库是永久离开）
    s["totalTools"] = toolStats["inStockCount"].toInt() + toolStats["borrowedCount"].toInt();
    s["inStock"]    = toolStats["inStockCount"].toInt();     // 在库条目数
    s["borrowed"]   = toolStats["borrowedCount"].toInt();    // 已借出条目数
    s["checkedOut"] = toolStats["checkedOutCount"].toInt();   // 已出库条目数

    // 未处理告警
    AlertDAO alertDao;
    s["alerts"] = alertDao.getUnresolvedCount();

    return s;
}

/**
 * @brief 汇总普通用户首页统计数据
 * @param userId 用户ID
 * @return 含该用户借用与归还相关统计的对象
 */
QJsonObject SettingService::getUserDashboardStats(int userId) {
    RecordDAO dao;
    return dao.getUserStats(userId);
}

/**
 * @brief 查询最近的操作日志
 * @param limit 最多返回条数
 * @return 日志数组，按时间倒序
 */
QJsonArray SettingService::getRecentLogs(int limit) {
    RecordDAO dao;
    return dao.findRecentActivity(limit);
}

/**
 * @brief 查询指定用户的借用记录
 * @param userId 用户ID
 * @param limit 最多返回条数
 * @return 借用记录数组，按时间倒序
 */
QJsonArray SettingService::getUserBorrowRecords(int userId, int limit) {
    RecordDAO dao;
    return dao.findByUserId(userId, limit);
}

/**
 * @brief 分页查询告警列表
 * @param page 页码，从1开始
 * @param pageSize 每页条数
 * @param type 告警类型（逗号分隔的type_code），为空不过滤
 * @param level 告警级别，为空不过滤
 * @param keyword 关键字，为空不过滤
 * @return 含 list 数组与 total 总数的对象
 */
QJsonObject SettingService::getAllAlerts(int page, int pageSize, const QString& type,
                                           const QString& level, const QString& keyword) {
    // AlertLogsPage数据源：SQL 已下沉 AlertDAO::findAllPaged（含JOIN与筛选），
    // level 由 DAO 内经 SC::alertLevelCodeFromText 统一做显示值→库值映射
    AlertDAO alertDao;
    return alertDao.findAllPaged(type, level, keyword, page, pageSize);
}

/**
 * @brief 汇总台账统计数据
 * @return 含借用总次数、归还总次数、逾期数与活跃用户数的对象
 */
QJsonObject SettingService::getLedgerStats() {
    // SQL 已下沉 RecordDAO::getLedgerStats（含分类/部门维度统计）
    RecordDAO dao;
    return dao.getLedgerStats();
}

/**
 * @brief 恢复出厂设置入口
 * @param adminPassword 管理员口令
 * @return true=管理员身份校验通过
 */
bool SettingService::factoryReset(const QString& adminPassword) {
    return verifyAdminPassword(adminPassword);
}

/**
 * @brief 校验危险操作的管理员身份
 * @param adminPassword 管理员口令
 * @return true=存在启用状态的管理员且口令匹配
 */
bool SettingService::verifyAdminPassword(const QString& adminPassword) {
    // 危险操作鉴权：必须验证真实管理员身份（密码与库中哈希比对）
    // 禁止任何硬编码口令后门，也禁止跳过状态校验
    if (adminPassword.isEmpty()) {
        qWarning() << "[SettingService] verifyAdminPassword: 口令为空";
        return false;
    }

    db::UserDAO userDao;
    QList<User> admins = userDao.findAllUsers(1, 200, QString(), QString(), QString(), SC::ROLE_ADMIN);
    for (const User& admin : admins) {
        if (admin.status != SC::USER_ACTIVE) continue;  // 停用管理员不得执行危险操作
        if (AuthService::verifyPassword(adminPassword, admin.passwordSalt, admin.passwordHash))
            return true;
    }
    qWarning() << "[SettingService] verifyAdminPassword: 管理员身份验证失败";
    return false;
}

/**
 * @brief 清空全部操作日志
 * @param adminPassword 管理员口令
 * @return true=校验通过且日志已清空
 */
bool SettingService::clearAllLogs(const QString& adminPassword) {
    if (!factoryReset(adminPassword)) {
        qWarning() << "[SettingService] clearAllLogs: 管理员验证失败";
        return false;
    }
    RecordDAO dao;
    return dao.clearAllLogs();
}

/**
 * @brief 查询全部部门
 * @return 部门名称数组
 */
QJsonArray SettingService::getDepartments() {
    QJsonArray arr;
    // 使用DepartmentDAO替代裸SQL
    DepartmentDAO deptDao;
    QStringList names = deptDao.allNames();
    for (const QString& name : names)
        arr.append(name);
    return arr;
}

// 从 system_config 表加载全部配置（SQL 在 ConfigDAO）
QJsonObject SettingService::loadAllConfig() {
    ConfigDAO dao;
    return dao.loadAll();
}

// 批量保存配置到 system_config 表（事务在 ConfigDAO 内）
bool SettingService::saveConfig(const QJsonObject& config) {
    ConfigDAO dao;
    return dao.saveBatch(config);
}

// 保存单个配置项
bool SettingService::saveConfigValue(const QString& key, const QString& value) {
    QJsonObject obj;
    obj[key] = value;
    return saveConfig(obj);
}

// 读取单个配置项
QString SettingService::loadConfigValue(const QString& key, const QString& defaultValue) {
    ConfigDAO dao;
    return dao.value(key, defaultValue);
}
