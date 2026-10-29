/**
 * @file AlertService.cpp
 * @brief 告警业务服务实现
 * @author 袁燕
 */
#include "AlertService.h"

#include "db/AlertDAO.h"
#include "common/Constants.h"
#include <QDebug>

/**
 * @brief 记录一条识别核验告警
 * @param userId 用户ID
 * @param toolId 工具ID
 * @param message 告警内容
 */
void AlertService::recordVerifyAlert(int userId, int toolId, const QString& message) {
    db::AlertDAO alertDao;
    AlertLog alert;
    alert.typeId = 2;  // 流程校验类告警（四流程步骤3统一口径）
    alert.userId = userId;
    alert.toolId = toolId;
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

/**
 * @brief 记录边缘机摄像头自检状态变化告警（复用 hw_fault 激活类型）
 * @param cameraId   摄像头编号，如 "CAM3"
 * @param oldStatus  变化前状态
 * @param newStatus  变化后状态
 * @note  类型编码经常量取，字典查不回 type_id（类型被停用）时只告警不写库，
 *        避免外键失败把 DB 错误抛到通信链路里
 */
void AlertService::recordCameraStatusChanged(const QString& cameraId,
                                             const QString& oldStatus,
                                             const QString& newStatus) {
    db::AlertDAO dao;
    const int typeId = dao.findTypeIdByCode(SC::ALERT_TYPE_HW_FAULT);
    if (typeId <= 0) {
        qWarning() << "[AlertService] 摄像头状态变化告警跳过：字典中找不到已启用类型"
                   << SC::ALERT_TYPE_HW_FAULT;
        return;
    }
    AlertLog alert;
    alert.typeId = typeId;
    // 触发方已保证 newStatus 非 ok（恢复正常不触发），文案固定为异常变化
    alert.message = QString(QStringLiteral("边缘机摄像头 %1 状态异常变化：%2 → %3"))
                        .arg(cameraId, oldStatus, newStatus);
    alert.status = SC::ALERT_UNHANDLED;
    dao.insertAlert(alert);
}

/**
 * @brief 记录与边缘计算机通信中断告警（复用 hw_comm 激活类型）
 * @note  类型编码经常量取，字典查不回 type_id（类型被停用）时只告警不写库；
 *        触发方（MainWindow 的 onlineChanged 适配）只在由在线翻转为离线时调用，
 *        离线持续期间不会重复触发，本方法无去重负担
 */
void AlertService::recordEdgeOffline() {
    db::AlertDAO dao;
    const int typeId = dao.findTypeIdByCode(SC::ALERT_TYPE_HW_COMM);
    if (typeId <= 0) {
        qWarning() << "[AlertService] 边缘机离线告警跳过：字典中找不到已启用类型"
                   << SC::ALERT_TYPE_HW_COMM;
        return;
    }
    AlertLog alert;
    alert.typeId = typeId;
    // 两条离线路径（TCP断开立即判/周期帧15秒超时判）共用本方法，文案不写具体判定方式
    alert.message = QStringLiteral("与边缘计算机通信中断，出入库核对功能暂不可用，请检查边缘计算机");
    alert.status = SC::ALERT_UNHANDLED;
    dao.insertAlert(alert);
}

/**
 * @brief 记录识别故障告警（边缘机识别结果异常，归系统告警大类）
 * @param userId  触发会话的当前用户ID
 * @param message 告警内容（含货位数量对比等故障细节）
 * @note  类型编码经常量取，字典查不回 type_id（类型被停用）时只告警不写库；
 *        与借用归还核对失败、出入库识别异常共用识别故障类型
 */
void AlertService::recordRecogFaultAlert(int userId, const QString& message) {
    db::AlertDAO dao;
    const int typeId = dao.findTypeIdByCode(SC::ALERT_TYPE_RECOG_FAULT);
    if (typeId <= 0) {
        qWarning() << "[AlertService] 识别故障告警跳过：字典中找不到已启用类型"
                   << SC::ALERT_TYPE_RECOG_FAULT;
        return;
    }
    AlertLog alert;
    alert.typeId = typeId;
    alert.userId = userId;
    alert.message = message;
    alert.status = SC::ALERT_UNHANDLED;
    dao.insertAlert(alert);
}
