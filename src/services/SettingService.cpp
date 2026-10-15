/**
 * @file SettingService.cpp
 * @brief 系统设置服务实现 — 告警日志、台账统计、系统配置
 *        本层只做业务编排，SQL 统一在 db/ 层（AlertDAO/RecordDAO/ConfigDAO）
 * @author 袁燕
 */
#include "SettingService.h"
#include "db/RecordDAO.h"
#include "db/AlertDAO.h"
#include "db/DepartmentDAO.h"
#include "db/ConfigDAO.h"
#include <QDebug>

// 统一使用 db/ 目录，消除 dao/ 与 db/ 混用冗余
using db::RecordDAO;
using db::AlertDAO;
using db::DepartmentDAO;
using db::ConfigDAO;

SettingService::SettingService(QObject* parent) : QObject(parent) {}

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

// 批量保存配置到 system_config 表（事务在 ConfigDAO 内）
bool SettingService::saveConfig(const QJsonObject& config) {
    ConfigDAO dao;
    return dao.saveBatch(config);
}
