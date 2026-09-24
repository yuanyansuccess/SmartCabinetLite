/**  
 * @file ReturnService.cpp
 * @brief 归还业务服务实现 - 超期计算、损坏检测
 * @author 袁燕
 *
 * [V2.15 2026-07-05] 映射表操作已迁移到ToolDAO，移除死代码
 */
#include "ReturnService.h"
#include "RecordDAO.h"
#include "ToolDAO.h"
#include <QDateTime>
#include <QDebug>

// [2026-06-21] 修复LNK2005：db/层已包裹namespace db
using db::RecordDAO;
using db::ToolDAO;

ReturnService::ReturnService(QObject* parent) : QObject(parent) {}

ReturnService::Result ReturnService::returnTools(const QList<int>& recordIds, int userId, const QJsonObject& returnInfo) {
    Result r; r.success = false; r.count = 0;
    if (recordIds.isEmpty()) { r.message = "未选择归还项目"; return r; }
    
    RecordDAO recDao; 
    ToolDAO toolDao;
    QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    QString condition = returnInfo["condition"].toString("正常"); // 工具状态：正常/损坏/丢失
    QString remark = returnInfo["remark"].toString("");
    
    for (int recId : recordIds) {
        QJsonObject rec = recDao.findById(recId);
        if (rec.isEmpty()) continue;
        
        int toolId = rec["toolId"].toInt();
        int mappingId = rec["mappingId"].toInt();  // [V2.11] 位置映射ID
        int qty = rec["borrowQty"].toInt();
        QString expectedReturnTime = rec["expectedReturnTime"].toString();
        QString borrowTime = rec["borrowTime"].toString();
        
        // 计算是否超期
        bool isOverdue = false;
        if (!expectedReturnTime.isEmpty()) {
            QDateTime expected = QDateTime::fromString(expectedReturnTime, "yyyy-MM-dd HH:mm:ss");
            QDateTime current = QDateTime::currentDateTime();
            if (current > expected) {
                isOverdue = true;
            }
        }
        
        // 构建归还备注
        QString returnRemark = condition;
        if (isOverdue) {
            returnRemark += "（超期归还）";
        }
        if (!remark.isEmpty()) {
            returnRemark += " - " + remark;
        }
        
        // 完成归还
        if (recDao.completeReturn(recId, now, condition, userId, returnRemark)) {
            // 按位置维度归还：更新映射表status
            // 正常→in_stock，损坏→maintenance（映射表无maintenance状态，保持in_stock但tool_info标记maintenance）
            // 丢失→保持borrowed（位置仍被占用，工具丢失不在库）
            if (mappingId > 0) {
                QString newStatus = (condition == "丢失") ? "borrowed" : "in_stock";
                toolDao.updateMappingStatus(mappingId, newStatus);
            }
            // 损坏的工具更新tool_info状态为maintenance
            if (condition == "损坏") {
                toolDao.updateStatus(toolId, "maintenance");
            }
            ++r.count;
        }
    }
    
    r.success = r.count > 0;
    if (r.success) {
        r.message = QString("成功归还 %1 件工具").arg(r.count);
        if (condition != "正常") {
            r.message += "，状态：" + condition;
        }
    } else {
        r.message = "归还失败";
    }
    
    return r;
}

QJsonObject ReturnService::getUserBorrowingRecords(int userId, int page, int pageSize) {
    RecordDAO dao;
    return dao.findAll(userId, 0, "borrowing", "", "", page, pageSize);
}

// 所有位置的待归还记录（不限userId，管理员可查看全部）
QJsonObject ReturnService::getAllBorrowingRecords(int page, int pageSize) {
    RecordDAO dao;
    return dao.findAll(0, 0, "borrowing", "", "", page, pageSize);
}
