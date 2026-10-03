/**
 * @file CheckoutService.cpp
 * @brief 工具出库业务服务实现（落库逻辑与原页面内联实现逐字一致）
 * @author 袁燕
 */
#include "CheckoutService.h"

#include "db/ToolDAO.h"
#include "db/RecordDAO.h"
#include "common/Constants.h"

void CheckoutService::executeCheckout(int userId, const QList<CheckoutItem>& items) {
    db::ToolDAO toolDao;
    db::RecordDAO recDao;
    for (const auto& it : items) {
        // 更新映射表status='checked_out'（位置维度出库，通过mappingId）
        if (it.mappingId > 0) {
            toolDao.updateMappingStatus(it.mappingId, SC::TOOL_CHECKED_OUT);
        }
        // 扣减库存（current_qty 和 total_qty 都减）
        toolDao.updateStock(it.toolId, -it.quantity);
        // 出库后current_qty=0则状态设为checked_out(已出库)
        QJsonObject updatedTool = toolDao.findById(it.toolId);
        if (updatedTool["currentQty"].toInt() <= 0) {
            toolDao.updateStatus(it.toolId, SC::TOOL_CHECKED_OUT);
        }

        // 写入操作日志（target_id存tool_code与入库一致）
        QString content = QStringLiteral("出库工具「%1」编号[%2]×%3 位置%4，原因：%5")
            .arg(it.toolName)
            .arg(it.toolCode.isEmpty() ? QStringLiteral("--") : it.toolCode)
            .arg(it.quantity)
            .arg(it.position)
            .arg(it.reason);
        recDao.insertOperationLog(userId, SC::OP_CHECKOUT, "tool", it.toolCode, content);
    }
}
