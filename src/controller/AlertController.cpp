/**
 * @file AlertController.cpp
 * @brief 告警控制层实现：告警列表查询、处理操作与告警统计装配
 * @author 袁燕
 */
#include "AlertController.h"
#include "db/ToolDAO.h"
#include "db/RecordDAO.h"
#include <QDebug>
#include "common/Constants.h"

AlertController::AlertController(QObject* parent) : QObject(parent) {}

/**
 * @brief 分页查询告警列表
 * @param page 页码，从1开始
 * @param pageSize 每页条数
 * @param type 告警类型编码，为空不过滤
 * @param level 告警级别，为空不过滤
 * @param handled 处理状态：-1 不限，0 待处理，1 已处理
 * @return 含 list 数组与 total 总数的分页结果
 */
AlertController::PageResult AlertController::getAlertList(int page, int pageSize,
    const QString& type, const QString& level, int handled,
    const QDate& startDate, const QDate& endDate) {
    PageResult r;
    r.page = page; r.pageSize = pageSize;
    r.total = m_dao.countAlerts(type, level, handled, startDate, endDate);
    r.list  = m_dao.findAllAlerts(page, pageSize, type, level, handled, startDate, endDate);
    return r;
}

// 通过typeCode查询typeId后创建告警（迁移到AlertDAO）
int AlertController::createAlert(const QString& typeCode, const QString& level,
                                  const QString& message, int userId, int toolId, int recordId) {
    int typeId = m_dao.findTypeIdByCode(typeCode);
    if (typeId <= 0) {
        qWarning() << "[AlertController] createAlert: 无效的typeCode:" << typeCode;
        return -1;
    }
    return createAlert(typeId, message, userId, toolId, recordId);
}

// 通过typeId创建告警（迁移alert_level查询到AlertDAO）
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

/**
 * @brief 将告警标记为已处理
 * @param alertId 告警ID
 * @param handledBy 处理人标识
 * @return true=更新成功
 */
bool AlertController::markHandled(int alertId, const QString& handledBy) {
    return m_dao.markHandled(alertId, handledBy);
}

/**
 * @brief 将全部未处理告警标记为已处理
 * @param handledBy 处理人标识
 * @return true=更新成功
 */
bool AlertController::markAllHandled(const QString& handledBy) {
    return m_dao.markAllHandled(handledBy);
}

// 忽略告警：DB保留，列表不显示
bool AlertController::markIgnored(int alertId, const QString& handlerBy) {
    return m_dao.markIgnored(alertId, handlerBy);
}

/**
 * @brief 统计未处理告警数量
 * @return 未处理告警条数
 */
int AlertController::unhandledCount() { return m_dao.unhandledCount(); }
/**
 * @brief 统计今日新增告警数量
 * @return 今日创建的告警条数
 */
int AlertController::todayTotal() { return m_dao.todayTotal(); }

/**
 * @brief 汇总概览页告警统计数据
 * @return 含未处理数、今日新增数等指标的结构
 */
AlertController::DashboardStats AlertController::getDashboardStats() {
    DashboardStats s;
    db::ToolDAO toolDao;
    db::RecordDAO recordDao;
    s.totalTools    = toolDao.countTools("", "", "", "");
    s.inStock       = toolDao.countTools("", "", "", SC::TOOL_IN_STOCK);
    s.borrowed      = toolDao.countTools("", "", "", SC::TOOL_BORROWED);
    s.alerts        = m_dao.unhandledCount();
    s.activeBorrows = recordDao.activeBorrowCount();
    s.overdueCount  = recordDao.overdueCount();
    QDate today = QDate::currentDate();
    s.todayOperations = recordDao.borrowCount("", "", 0, today, today);
    return s;
}
