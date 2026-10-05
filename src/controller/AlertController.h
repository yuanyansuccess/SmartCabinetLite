/**
 * @file AlertController.h
 * @brief 告警控制层：告警列表查询、处理操作与告警统计装配
 * @author 袁燕
 */
#pragma once
#include <QObject>
#include <QDate>
#include "model/AlertLog.h"
#include "db/AlertDAO.h"

// 分层定位：Controller 负责告警的读查询与统计装配；告警的产生与状态变更
// 由 services/AlertService 及各业务 Service 触发。
class AlertController : public QObject {
    Q_OBJECT
public:
    explicit AlertController(QObject* parent = nullptr);
    struct PageResult { QList<AlertLog> list; int total = 0; int page = 1; int pageSize = 20; };

    PageResult getAlertList(int page, int pageSize, const QString& type = "",
                            const QString& level = "", int handled = -1,
                            const QDate& startDate = {}, const QDate& endDate = {});
    int        createAlert(const QString& typeCode, const QString& level,
                           const QString& message, int userId = 0, int toolId = 0, int recordId = 0);
    int        createAlert(int typeId, const QString& message, int userId = 0, int toolId = 0, int recordId = 0);  // 直接传typeId
    bool       markHandled(int alertId, const QString& handledBy);
    bool       markAllHandled(const QString& handledBy);
    bool       markIgnored(int alertId, const QString& handlerBy);  // 忽略告警
    int        unhandledCount();
    int        todayTotal();

    struct DashboardStats {
        int totalTools = 0, inStock = 0, borrowed = 0, alerts = 0;
        int activeBorrows = 0, overdueCount = 0, todayOperations = 0;
    };
    DashboardStats getDashboardStats();

signals:
    void newAlert(int alertId, const QString& level, const QString& message);
    void criticalAlert(int alertId, const QString& message);

private:
    db::AlertDAO m_dao;
};
