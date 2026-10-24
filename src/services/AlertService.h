/**
 * @file AlertService.h
 * @brief 告警业务服务（告警业务的唯一入口，页面不再直连AlertDAO）
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

    /// 记录流程校验异常告警（借用/归还/入库/出库四流程步骤3共用同一处理）
    /// 用户点"忽略"或核对倒计时超时后调用；typeId固定2（流程校验类），状态固定未处理
    /// @param toolId 关联工具ID（无法确定时传0，入库/出库等场景可为0）
    void recordVerifyAlert(int userId, int toolId, const QString& message);

    /**
     * @brief 记录边缘机摄像头自检状态异常变化告警（周期帧逐帧对比触发）
     * @param cameraId   摄像头编号，如 "CAM3"
     * @param oldStatus  变化前状态（ok/offline/low_light/dirty/comm_error）
     * @param newStatus  变化后状态（触发方保证非 ok，恢复不触发）
     * @note  复用已激活的硬件故障类型（hw_fault），无需新增字典；
     *        类型字典被停用（is_active=0）时跳过写入并告警，不产生坏数据
     */
    void recordCameraStatusChanged(const QString& cameraId,
                                   const QString& oldStatus,
                                   const QString& newStatus);

    /**
     * @brief 记录与边缘计算机通信中断告警（链路由在线判离线时触发）
     * @note  复用已激活的硬件通讯故障类型（hw_comm），无需新增字典；
     *        两条离线路径（TCP断开立即判/周期帧15秒超时判）都只在状态翻转时
     *        触发一次，本方法无重复写入风险；链路恢复不告警（与摄像头告警同语义：
     *        告警只记异常出现，不记恢复）
     */
    void recordEdgeOffline();

    // ── 读路径（自SettingService迁移，告警域查询统一入口） ──

    /// 所有启用的告警类型（筛选下拉）
    QJsonArray getAlertTypes();
};
