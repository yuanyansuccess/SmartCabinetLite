/**
 * @file CheckoutService.cpp
 * @brief 工具出库业务服务实现（落库逻辑与原页面内联实现逐字一致）
 * @author 袁燕
 */
#include "CheckoutService.h"

#include "db/ToolDAO.h"
#include "db/RecordDAO.h"
#include "common/Constants.h"

/**
 * @brief 执行出库
 * @param userId 操作人ID
 * @param items 出库明细列表，每项含位置映射ID与工具ID
 * @return success=是否全部成功，succeeded=成功条数，failed=失败条数，
 *         failedNames=失败项名称列表，message=结果说明
 * @note 逐项处理并在同一事务内提交，任一项失败时该项不计成功
 */
CheckoutService::CheckoutResult CheckoutService::executeCheckout(
    int userId, const QList<CheckoutItem>& items) {
    CheckoutResult out;
    if (items.isEmpty()) {
        out.message = QStringLiteral("没有待出库工具");
        return out;
    }

    db::ToolDAO toolDao;
    db::RecordDAO recDao;
    QStringList failedNames;

    // 整批单事务：任一件写失败则全部回滚，杜绝"扣了库存没写日志"的脏数据
    bool ok = toolDao.transaction([&]() -> bool {
        for (const auto& it : items) {
            bool stepOk = true;

            // 更新映射表status='checked_out'（位置维度出库，通过mappingId）
            if (it.mappingId > 0) {
                stepOk = toolDao.updateMappingStatus(it.mappingId, SC::TOOL_CHECKED_OUT);
            }
            if (!stepOk) {
                failedNames.append(it.toolName.isEmpty() ? it.toolCode : it.toolName);
                return false;
            }

            // 出库=工具从柜中取出使用（编制库存total_qty不变，放回时恢复）
            // current_qty 由 tool_position_mapping 触发器自动维护，此处不再手算
            // inStockCount 仅用于下方"全部取空则工具置 checked_out"的状态判断
            int inStockCount = toolDao.countInStockPositions(it.toolId);
            if (!stepOk) {
                failedNames.append(it.toolName.isEmpty() ? it.toolCode : it.toolName);
                return false;
            }
            if (inStockCount <= 0) {
                stepOk = toolDao.updateStatus(it.toolId, SC::TOOL_CHECKED_OUT);
            }
            if (!stepOk) {
                failedNames.append(it.toolName.isEmpty() ? it.toolCode : it.toolName);
                return false;
            }

            // 写入操作日志（target_id存tool_code与入库一致）
            QString content = QStringLiteral("出库工具「%1」编号[%2]×%3 位置%4，原因：%5")
                .arg(it.toolName)
                .arg(it.toolCode.isEmpty() ? QStringLiteral("--") : it.toolCode)
                .arg(it.quantity)
                .arg(it.position)
                .arg(it.reason);
            // 借出转出库：结清该工具仍在借的借用记录，避免台账一直显示"借用中"
            if (it.fromBorrow) {
                stepOk = recDao.finishBorrowsByCheckout(it.toolId, userId,
                                                        QStringLiteral("转出库（免开箱）"));
                if (!stepOk) {
                    failedNames.append(it.toolName.isEmpty() ? it.toolCode : it.toolName);
                    return false;
                }
                content += QStringLiteral("（原为借出未归还，本次转移出库）");
            }
            // insertOperationLog 返回日志ID（>0 表示成功）
            stepOk = recDao.insertOperationLog(
                userId, SC::OP_CHECKOUT, "tool", it.toolCode, content) > 0;
            if (!stepOk) {
                failedNames.append(it.toolName.isEmpty() ? it.toolCode : it.toolName);
                return false;
            }
        }
        return true;
    });

    if (!ok) {
        out.success = false;
        out.failed = items.size();  // 事务整体回滚，未提交任何一件
        out.failedNames = failedNames;
        out.message = QStringLiteral("出库失败：库存未变动，请重试\n失败工具：%1")
            .arg(failedNames.isEmpty() ? QStringLiteral("--") : failedNames.join("、"));
        return out;
    }

    out.success = true;
    out.succeeded = items.size();
    out.message = QStringLiteral("成功出库 %1 件工具").arg(items.size());
    return out;
}
