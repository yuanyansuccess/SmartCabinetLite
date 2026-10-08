/**
 * @file AlertDAO.h
 * @brief 告警数据访问对象 — db/目录统一namespace db，合并QJsonObject API + 实体类API
 * @author 袁燕
 */
#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDate>
#include "model/AlertLog.h"
#include "BaseDAO.h"

namespace db {

class AlertDAO : public BaseDAO {
public:
    AlertDAO() = default;

    // ═══════════════════════════════════════════════
    // QJsonObject API（Service层使用）
    // ═══════════════════════════════════════════════
    int         insert(const QJsonObject& alert);
    QJsonObject findById(int alertId);
    QJsonObject findAll(const QString& alertType, const QString& alertLevel,
                        const QString& status, const QString& startDate,
                        const QString& endDate, int page, int pageSize);
    // 分页查询告警列表（AlertLogsPage数据源，含tool_cabinet JOIN，显示所有状态含已忽略）
    // level 兼容显示值（严重/一般/提示）与库值；返回 {list, total}
    QJsonObject findAllPaged(const QString& type, const QString& level,
                             const QString& keyword, int page, int pageSize);
    bool        acknowledge(int alertId, int handlerId, const QString& remark = "");
    bool        resolve(int alertId, int handlerId, const QString& remark = "");
    int         getUnresolvedCount();
    QJsonArray  getAlertTypes();  // 获取所有启用的告警类型（用于前端筛选下拉）
    QJsonArray  findRecentUnhandled(int limit);   // 最近未处理告警（Dashboard用）
    QJsonObject findDetailById(int alertId);      // 告警详情（JOIN sys_alert_type+tool_info+sys_user）
    QJsonObject getStats(const QString& types, const QString& level, const QString& keyword);  // 告警统计

    // ═══════════════════════════════════════════════
    // 实体类API（Controller层使用）— 从dao/AlertDAO合并
    // ═══════════════════════════════════════════════
    int             insertAlert(const AlertLog& alert);
    QList<AlertLog> findAllAlerts(int page = 1, int pageSize = 20,
                                  const QString& type = "", const QString& level = "",
                                  int handled = -1, const QDate& startDate = {}, const QDate& endDate = {});
    int             countAlerts(const QString& type = "", const QString& level = "",
                                int handled = -1, const QDate& startDate = {}, const QDate& endDate = {});
    bool            markHandled(int alertId, const QString& handledBy);
    bool            markAllHandled(const QString& handledBy);
    bool            markIgnored(int alertId, const QString& handlerBy);  // 忽略告警（DB保留，列表不显示）
    int             unhandledCount();
    int             todayTotal();

    // 告警类型辅助查询（迁移自AlertController裸SQL）
    int     findTypeIdByCode(const QString& typeCode);  // 按type_code查type_id，-1=未找到
    QString findTypeLevelById(int typeId);              // 按type_id查alert_level，空=未找到

    static AlertLog fromQuery(const QSqlQuery& q);
};

} // namespace db
