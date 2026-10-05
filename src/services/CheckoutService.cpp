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

            // 扣减库存（current_qty 和 total_qty 都减）
            stepOk = toolDao.updateStock(it.toolId, -it.quantity);
            if (!stepOk) {
                failedNames.append(it.toolName.isEmpty() ? it.toolCode : it.toolName);
                return false;
            }

            // 出库后current_qty=0则状态设为checked_out(已出库)
            QJsonObject updatedTool = toolDao.findById(it.toolId);
            if (updatedTool["currentQty"].toInt() <= 0) {
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
