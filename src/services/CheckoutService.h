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

/// 出库清单项：一项对应一个货位上的一件工具
struct CheckoutItem {
    int mappingId = 0;  // 映射表ID（位置唯一标识）
    int toolId = 0;
    QString toolCode;
    QString toolName;
    QString position;
    int quantity = 0;
    QString reason;
    bool fromBorrow = false;  // true=该工具是"已借出未归还"转出库（需一并结清借用记录）
};

class CheckoutService {
public:
    CheckoutService() = default;

    /// 出库结果：区分成功件数与失败明细，杜绝"部分失败静默吞掉"
    struct CheckoutResult {
        bool success = false;          // 全部出库成功才为 true
        int  succeeded = 0;            // 成功件数
        int  failed = 0;               // 失败件数
        QString message;               // 面向用户的汇总说明
        QStringList failedNames;       // 失败工具名称清单
    };

    /// 执行出库落库（整批单事务，任一失败整体回滚）：
    /// 1)映射表状态→checked_out  2)扣减库存(current_qty/total_qty)
    /// 3)库存清零则工具状态→checked_out  4)写操作日志
    /// 5)fromBorrow=true 的项额外结清该工具仍在借的记录（借出转出库）
    /// @param userId 操作人用户ID
    /// @param items 出库清单项（含在库位置出库与借出转出库两种）
    /// @return 出库结果：success 表示全部成功，failedNames 列出失败工具名
    CheckoutResult executeCheckout(int userId, const QList<CheckoutItem>& items);
};
