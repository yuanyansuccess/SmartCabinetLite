/**  
 * @file BorrowService.cpp
 * @brief 借用业务服务实现 - 表单校验、错误处理
 * @author 袁燕
 *
 * 映射表操作迁移到ToolDAO，Service层纯业务逻辑
 */
#include "BorrowService.h"
#include "db/RecordDAO.h"
#include "db/ToolDAO.h"
#include "db/TaskTypeDAO.h"
#include <QDateTime>
#include <QDebug>
#include "common/Constants.h"

// 修复LNK2005：db/层已包裹namespace db
using db::RecordDAO;
using db::ToolDAO;
using db::TaskTypeDAO;

BorrowService::BorrowService(QObject* parent) : QObject(parent) {}

/**
 * @brief 借用工具
 * @param userId 借用人ID
 * @param toolId 工具ID
 * @param mappingId 借用位置映射ID
 * @param quantity 借用数量，必须大于0
 * @param reason 借用原因
 * @param expectedReturnTime 预计归还时间文本
 * @param flowNo 借用流水号
 * @return success=是否成功，message=失败原因，recordId=新建记录ID（失败为0）
 * @note 校验入参与机组归属后在同一事务内写入借用记录并回写位置状态，任一步失败整体回滚
 */
BorrowService::Result BorrowService::borrowTool(int userId, int toolId, int mappingId, int quantity,
                                                 const QString& reason, const QString& expectedReturnTime,
                                                 const QString& flowNo, int machineGroupId) {
    Result r;  // 默认为 {success=false, recordId=0}，失败路径无需再赋 -1

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
    // 机组隔离校验：用户只能借用本机组工具
    // ⚠ 不可动：机型隔离是"查询层DAO过滤 + Service二次校验"双层，删掉任一层都会造成跨机组越权
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
    // 不检查tool_info.status（一个工具多位置时tool_info.status不代表单个位置状态）
    // 映射表status才是位置占用的权威数据源
    // 迁移到ToolDAO::findMappingStatus/updateMappingStatus
    QString posStatus = toolDao.findMappingStatus(mappingId);
    if (posStatus.isEmpty()) {
        r.message = QStringLiteral("位置映射记录不存在");
        qWarning() << "[BorrowService] 映射记录不存在: mappingId=" << mappingId;
        return r;
    }
    if (posStatus != SC::TOOL_IN_STOCK) {
        r.message = QStringLiteral("该位置工具不可借用（当前状态: %1）").arg(posStatus);
        qWarning() << "[BorrowService] 借用被拒绝: toolId=" << toolId << "mappingId=" << mappingId << "posStatus=" << posStatus;
        return r;
    }

    RecordDAO recDao;
    QJsonObject rec;
    rec["flowNo"] = flowNo;
    rec["toolId"] = toolId;
    rec["mappingId"] = mappingId;  // 记录借用的位置
    rec["userId"] = userId;
    rec["quantity"] = quantity;
    rec["purpose"] = reason;
    rec["expectedReturnTime"] = expectedReturnTime;
    rec["status"] = SC::RECORD_BORROWING;

    // 两步写（位置占位 + 借用记录）包单事务：记录创建失败时由事务回滚位置状态，
    // 不再依赖手工补偿，杜绝"补偿失败导致位置永久占用"
    QString failMsg;
    bool txnOk = toolDao.transaction([&]() -> bool {
        // 更新映射表status='borrowed'（前置已校验in_stock，委托到DAO）
        if (!toolDao.updateMappingStatus(mappingId, SC::TOOL_BORROWED)) {
            failMsg = QStringLiteral("更新位置状态失败");
            return false;
        }
        r.recordId = recDao.insert(rec);
        if (r.recordId <= 0) {
            failMsg = QStringLiteral("创建借用记录失败");
            return false;
        }
        return true;
    });

    if (!txnOk) {
        r.recordId = 0;
        r.message = failMsg;
        qWarning() << "[BorrowService] 借用事务已回滚: mappingId=" << mappingId << " reason=" << failMsg;
        return r;
    }
    r.success = true; r.message = "借用成功";
    return r;
}

// 所有借用记录（不限userId）
QJsonObject BorrowService::getAllRecords(int page, int pageSize) {
    RecordDAO dao;
    return dao.findAll(0, 0, "", "", "", page, pageSize);
}

/**
 * @brief 查询全部启用的任务类型
 * @return 任务类型数组
 */
QJsonArray BorrowService::getTaskTypes() { TaskTypeDAO dao; return dao.findAllActive(); }

/**
 * @brief 按任务类型查询推荐工具
 * @param typeIds 任务类型ID列表
 * @param machineGroupId 机组ID，用于限定只返回本机组工具
 * @return 工具数组
 */
QJsonArray BorrowService::getRecommendedTools(const QList<int>& typeIds, int machineGroupId) {
    TaskTypeDAO dao; return dao.findRecommendedToolsByTypes(typeIds, machineGroupId);
}

/**
 * @brief 生成借用流水号
 * @param reason 借用原因，取其前两个字符作为前缀
 * @return 流水号，格式为 JH-MMdd-前缀hhmm；前缀不足两位时用XX
 */
QString BorrowService::generateFlowNo(const QString& reason) {
    QDateTime now = QDateTime::currentDateTime();
    QString mmdd = now.toString("MMdd");
    QString hhmm = now.toString("hhmm");
    QString abbr = reason.left(2); if (abbr.length() < 2) abbr = "XX";
    return "JH-" + mmdd + "-" + abbr.toUpper() + hhmm;
}

/**
 * @brief 分页查询在库工具
 * @param page 页码，从1开始
 * @param pageSize 每页条数
 * @param machineGroupId 机组ID，用于限定只返回本机组工具
 * @return 含 list 数组与 total 总数的对象
 */
QJsonObject BorrowService::getAllInStockTools(int page, int pageSize, int machineGroupId) {
    ToolDAO dao;
    // 按工具种类聚合查询，同一工具一行+availableQty
    // 借用列表按工具种类显示，用户选数量后自动分配位置
    return dao.findAllInStockByTool("", "", page, pageSize, machineGroupId);
}
