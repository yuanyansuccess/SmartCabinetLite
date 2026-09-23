#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0  告警业务控制层
// 日期：2026-06-21  功能：告警查询、处理、统计
// [V6.9 2026-06-24] 统一到db/目录namespace db
#include <QObject>
#include <QDate>
#include "model/AlertLog.h"
#include "db/AlertDAO.h"

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
    int        createAlert(int typeId, const QString& message, int userId = 0, int toolId = 0, int recordId = 0);  // [2026-06-25] 直接传typeId
    bool       markHandled(int alertId, const QString& handledBy);
    bool       markAllHandled(const QString& handledBy);
    bool       markIgnored(int alertId, const QString& handlerBy);  // [V7.9 2026-06-24] 忽略告警
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
