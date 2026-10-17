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
 * @brief 查询告警类型字典
 * @return 元素含 typeId、typeCode、typeName、alertLevel 的数组
 */
QJsonArray AlertService::getAlertTypes() {
    db::AlertDAO dao;
    return dao.getAlertTypes();
}
