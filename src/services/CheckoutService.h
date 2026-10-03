/**
 * @file CheckoutService.h
 * @brief 工具出库业务服务（出库落库的唯一业务入口）
 * @author 袁燕
 * @说明 出库涉及三个DAO协作（映射表状态/库存扣减/操作日志），
 *   收口到本服务，页面只负责界面与清单收集。
 */
#pragma once
#include <QString>
#include <QList>

/// 出库清单项（原ToolCheckoutPage内嵌结构，随业务下沉至服务层）
struct CheckoutItem {
    int mappingId;      // 映射表ID（位置唯一标识）
    int toolId;
    QString toolCode;
    QString toolName;
    QString position;
    int quantity;
    QString reason;
};

class CheckoutService {
public:
    CheckoutService() = default;

    /// 执行出库落库（在成功对话框关闭后调用）：
    /// 1)映射表状态→checked_out  2)扣减库存(current_qty/total_qty)
    /// 3)库存清零则工具状态→checked_out  4)写操作日志
    void executeCheckout(int userId, const QList<CheckoutItem>& items);
};
