/**
 * @file AlertService.cpp
 * @brief 告警业务服务实现
 * @author 袁燕
 */
#include "AlertService.h"

#include "db/AlertDAO.h"
#include "common/Constants.h"

/**
 * @brief 记录一条识别核验告警
 * @param userId 用户ID
 * @param toolId 工具ID
 * @param toolCode 工具编号
 * @param message 告警内容
 */
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

/**
 * @brief 统计未处理告警数量
 * @return 未处理告警条数
 */
int AlertService::unhandledCount() {
    db::AlertDAO dao;
    return dao.unhandledCount();
}

// ── 读路径（自SettingService迁移） ──

QJsonArray AlertService::getRecentAlerts(int limit) {
    db::AlertDAO dao;
    return dao.findRecentUnhandled(limit);
}

/**
 * @brief 查询告警详情
 * @param alertId 告警ID
 * @return 详情对象；不存在时返回空对象
 */
QJsonObject AlertService::getAlertDetail(int alertId) {
    db::AlertDAO dao;
    return dao.findDetailById(alertId);
}

/**
 * @brief 统计告警数量
 * @param type 告警类型，为空不过滤
 * @param level 告警级别，为空不过滤
 * @param keyword 关键字，为空不过滤
 * @return 统计对象
 */
QJsonObject AlertService::getAlertStats(const QString& type, const QString& level,
                                        const QString& keyword) {
    db::AlertDAO dao;
    return dao.getStats(type, level, keyword);
}

/**
 * @brief 查询告警类型字典
 * @return 元素含 typeId、typeCode、typeName、alertLevel 的数组
 */
QJsonArray AlertService::getAlertTypes() {
    db::AlertDAO dao;
    return dao.getAlertTypes();
}
