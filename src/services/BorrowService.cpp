/**  
 * @file BorrowService.cpp
 * @brief 借用业务服务实现 - 表单校验、错误处理
 * @author 袁燕
 *
 * [V2.15 2026-07-05] 映射表操作迁移到ToolDAO，Service层纯业务逻辑
 */
#include "BorrowService.h"
#include "db/RecordDAO.h"
#include "db/ToolDAO.h"
#include "db/TaskTypeDAO.h"
#include <QDateTime>
#include <QDebug>

// [2026-06-21] 修复LNK2005：db/层已包裹namespace db
using db::RecordDAO;
using db::ToolDAO;
using db::TaskTypeDAO;

BorrowService::BorrowService(QObject* parent) : QObject(parent) {}

BorrowService::Result BorrowService::borrowTool(int userId, int toolId, int mappingId, int quantity,
                                                 const QString& reason, const QString& expectedReturnTime,
                                                 const QString& flowNo, int machineGroupId) {
    Result r; r.success = false; r.recordId = -1;

    // 表单校验
    if (userId <= 0) { r.message = "用户ID无效"; return r; }
    if (toolId <= 0) { r.message = "工具ID无效"; return r; }
    if (mappingId <= 0) { r.message = "位置映射ID无效"; return r; }
    if (quantity <= 0) { r.message = "借用数量必须大于0"; return r; }
    if (reason.isEmpty()) { r.message = "借用原因不能为空"; return r; }
    if (expectedReturnTime.isEmpty()) { r.message = "预计归还时间不能为空"; return r; }

    // 检查预计归还时间是否大于当前时间
    QDateTime expected = QDateTime::fromString(expectedReturnTime, "yyyy-MM-dd HH:mm:ss");
    if (!expected.isValid()) { r.message = "预计归还时间格式错误"; return r; }
    if (expected <= QDateTime::currentDateTime()) { r.message = "预计归还时间必须晚于当前时间"; return r; }

    ToolDAO toolDao;
    QJsonObject tool = toolDao.findById(toolId);
    if (tool.isEmpty()) { r.message = "工具不存在"; return r; }
    // [2026-06-27] 机组隔离校验：用户只能借用本机组工具
    if (machineGroupId > 0) {
        int toolGroupId = tool["machineGroupId"].toInt();
        if (toolGroupId > 0 && toolGroupId != machineGroupId) {
            r.message = QStringLiteral("不能借用其他机组的工具");
            qWarning() << "[BorrowService] 跨机组借用被拒绝: userId=" << userId
                       << "toolId=" << toolId << "toolGroupId=" << toolGroupId
                       << "userGroupId=" << machineGroupId;
            return r;
        }
    }

    // 按位置维度借用：检查映射表status必须为in_stock
    // 不再检查tool_info.status（一个工具多位置时tool_info.status不代表单个位置状态）
    // 映射表status才是位置占用的权威数据源
    // [V2.15 2026-07-05] 迁移到ToolDAO::findMappingStatus/updateMappingStatus
    QString posStatus = toolDao.findMappingStatus(mappingId);
    if (posStatus.isEmpty()) {
        r.message = QStringLiteral("位置映射记录不存在");
        qWarning() << "[BorrowService] 映射记录不存在: mappingId=" << mappingId;
        return r;
    }
    if (posStatus != "in_stock") {
        r.message = QStringLiteral("该位置工具不可借用（当前状态: %1）").arg(posStatus);
        qWarning() << "[BorrowService] 借用被拒绝: toolId=" << toolId << "mappingId=" << mappingId << "posStatus=" << posStatus;
        return r;
    }

    // [V2.15 2026-07-05] 更新映射表status='borrowed'（前置已校验in_stock，委托到DAO）
    if (!toolDao.updateMappingStatus(mappingId, "borrowed")) {
        r.message = QStringLiteral("更新位置状态失败");
        qWarning() << "[BorrowService] 更新映射表status失败: mappingId=" << mappingId;
        return r;
    }

    RecordDAO recDao;
    QJsonObject rec;
    rec["flowNo"] = flowNo;
    rec["toolId"] = toolId;
    rec["mappingId"] = mappingId;  // [V2.11] 记录借用的位置
    rec["userId"] = userId;
    rec["quantity"] = quantity;
    rec["purpose"] = reason;
    rec["expectedReturnTime"] = expectedReturnTime;
    rec["status"] = "borrowing";
    r.recordId = recDao.insert(rec);
    if (r.recordId <= 0) {
        // [V2.15 2026-07-05] 回滚：映射表status改回in_stock（委托到ToolDAO）
        if (!toolDao.updateMappingStatus(mappingId, "in_stock")) {
            qWarning() << "[BorrowService] 回滚映射表状态失败! mappingId=" << mappingId;
        }
        r.message = "创建借用记录失败"; return r;
    }
    r.success = true; r.message = "借用成功";
    return r;
}

QJsonObject BorrowService::getUserRecords(int userId, int page, int pageSize) {
    RecordDAO dao;
    return dao.findAll(userId, 0, "", "", "", page, pageSize);
}

// 所有借用记录（不限userId）
QJsonObject BorrowService::getAllRecords(int page, int pageSize) {
    RecordDAO dao;
    return dao.findAll(0, 0, "", "", "", page, pageSize);
}

QJsonArray BorrowService::getTaskTypes() { TaskTypeDAO dao; return dao.findAllActive(); }

QJsonArray BorrowService::getRecommendedTools(const QList<int>& typeIds, int machineGroupId) {
    TaskTypeDAO dao; return dao.findRecommendedToolsByTypes(typeIds, machineGroupId);
}

QString BorrowService::generateFlowNo(const QString& reason) {
    QDateTime now = QDateTime::currentDateTime();
    QString mmdd = now.toString("MMdd");
    QString hhmm = now.toString("hhmm");
    QString abbr = reason.left(2); if (abbr.length() < 2) abbr = "XX";
    return "JH-" + mmdd + "-" + abbr.toUpper() + hhmm;
}

QJsonObject BorrowService::getAllInStockTools(int page, int pageSize, int machineGroupId) {
    ToolDAO dao;
    // 改为按工具种类聚合查询，同一工具一行+availableQty
    // 借用列表按工具种类显示，用户选数量后自动分配位置
    return dao.findAllInStockByTool("", "", page, pageSize, machineGroupId);
}

// [V1.00.9.1 架构修复] 新增searchTools方法，替代页面直接调用db/ToolDAO —— 作者：袁燕
QJsonObject BorrowService::searchTools(const QString& keyword, int page, int pageSize, int machineGroupId) {
    ToolDAO dao;
    // 改用位置维度查询，与getAllInStockTools一致
    return dao.findAllByPosition(keyword, "", "in_stock", 0, page, pageSize, machineGroupId);
}
