/**
 * @file AlertService.h
 * @brief 告警业务服务（告警闭环的唯一业务入口，页面不再直连AlertDAO）
 * @author 袁燕
 */
#pragma once
#include <QString>
#include <QJsonObject>
#include <QJsonArray>

class AlertService {
public:
    AlertService() = default;

    // ── 写路径 ──

    /// 记录流程校验异常告警（借用/归还/入库/出库四流程步骤3共用闭环）
    /// 用户点"忽略"或核对倒计时超时后调用；typeId固定2（流程校验类），状态固定未处理
    /// @param toolId 关联工具ID（无法确定时传0，入库/出库等场景可为0）
    void recordVerifyAlert(int userId, int toolId, const QString& message);

    // ── 读路径（自SettingService迁移，告警域查询统一入口） ──

    /// 所有启用的告警类型（筛选下拉）
    QJsonArray getAlertTypes();
};
