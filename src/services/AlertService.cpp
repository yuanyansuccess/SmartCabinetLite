/**
 * @file AlertService.cpp
 * @brief 告警业务服务实现
 * @author 袁燕
 */
#include "AlertService.h"

#include "db/AlertDAO.h"
#include "common/Constants.h"

void AlertService::recordVerifyAlert(int userId, int toolId, const QString& toolCode, const QString& message) {
    db::AlertDAO alertDao;
    AlertLog alert;
    alert.typeId = 2;  // 流程校验类告警（四流程步骤3统一口径）
    alert.userId = userId;
    alert.toolId = toolId;
    alert.toolCode = toolCode;
    alert.message = message;
    alert.status = SC::ALERT_UNHANDLED;
    alertDao.insertAlert(alert);
}

int AlertService::unhandledCount() {
    db::AlertDAO dao;
    return dao.unhandledCount();
}

// ── 读路径（自SettingService迁移） ──

QJsonArray AlertService::getRecentAlerts(int limit) {
    db::AlertDAO dao;
    return dao.findRecentUnhandled(limit);
}

QJsonObject AlertService::getAlertDetail(int alertId) {
    db::AlertDAO dao;
    return dao.findDetailById(alertId);
}

QJsonObject AlertService::getAlertStats(const QString& type, const QString& level,
                                        const QString& keyword) {
    db::AlertDAO dao;
    return dao.getStats(type, level, keyword);
}

QJsonArray AlertService::getAlertTypes() {
    db::AlertDAO dao;
    return dao.getAlertTypes();
}
