// 作者：袁燕  智能柜Qt Widget 2.0  AlertController实现
// 日期：2026-06-21  [V6.9 2026-06-24] 统一到db/目录namespace db，方法名更新
// [2026-06-25] 重构：createAlert支持typeId参数
// [V2.15 2026-07-05] 裸SQL迁移到AlertDAO
#include "AlertController.h"
#include "db/ToolDAO.h"
#include "db/RecordDAO.h"
#include <QDebug>

AlertController::AlertController(QObject* parent) : QObject(parent) {}

AlertController::PageResult AlertController::getAlertList(int page, int pageSize,
    const QString& type, const QString& level, int handled,
    const QDate& startDate, const QDate& endDate) {
    PageResult r;
    r.page = page; r.pageSize = pageSize;
    r.total = m_dao.countAlerts(type, level, handled, startDate, endDate);
    r.list  = m_dao.findAllAlerts(page, pageSize, type, level, handled, startDate, endDate);
    return r;
}

// [V2.15 2026-07-05] 通过typeCode查询typeId后创建告警（迁移到AlertDAO）
int AlertController::createAlert(const QString& typeCode, const QString& level,
                                  const QString& message, int userId, int toolId, int recordId) {
    int typeId = m_dao.findTypeIdByCode(typeCode);
    if (typeId <= 0) {
        qWarning() << "[AlertController] createAlert: 无效的typeCode:" << typeCode;
        return -1;
    }
    return createAlert(typeId, message, userId, toolId, recordId);
}

// [V2.15 2026-07-05] 通过typeId创建告警（迁移alert_level查询到AlertDAO）
int AlertController::createAlert(int typeId, const QString& message,
                                  int userId, int toolId, int recordId) {
    AlertLog alert;
    alert.typeId   = typeId;
    alert.message  = message;
    alert.userId   = userId;
    alert.toolId   = toolId;
    alert.recordId = recordId;
    int id = m_dao.insertAlert(alert);
    if (id > 0) {
        QString level = m_dao.findTypeLevelById(typeId);
        if (level.isEmpty()) level = "warn";
        emit newAlert(id, level, message);
        if (level == "error" || level == "crit")
            emit criticalAlert(id, message);
    }
    return id;
}

bool AlertController::markHandled(int alertId, const QString& handledBy) {
    return m_dao.markHandled(alertId, handledBy);
}

bool AlertController::markAllHandled(const QString& handledBy) {
    return m_dao.markAllHandled(handledBy);
}

// [V7.9 2026-06-24] 忽略告警：DB保留，列表不显示
bool AlertController::markIgnored(int alertId, const QString& handlerBy) {
    return m_dao.markIgnored(alertId, handlerBy);
}

int AlertController::unhandledCount() { return m_dao.unhandledCount(); }
int AlertController::todayTotal() { return m_dao.todayTotal(); }

AlertController::DashboardStats AlertController::getDashboardStats() {
    DashboardStats s;
    db::ToolDAO toolDao;
    db::RecordDAO recordDao;
    s.totalTools    = toolDao.countTools("", "", "", "");
    s.inStock       = toolDao.countTools("", "", "", "in_stock");
    s.borrowed      = toolDao.countTools("", "", "", "borrowed");
    s.alerts        = m_dao.unhandledCount();
    s.activeBorrows = recordDao.activeBorrowCount();
    s.overdueCount  = recordDao.overdueCount();
    QDate today = QDate::currentDate();
    s.todayOperations = recordDao.borrowCount("", "", 0, today, today);
    return s;
}
