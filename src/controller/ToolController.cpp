// 作者：袁燕  智能柜Qt Widget 2.0  ToolController实现
// 日期：2026-06-21 工具借用/归还核心业务流程
// [V6.9 2026-06-24] 方法名更新为合并后的db::ToolDAO/RecordDAO新API
#include "ToolController.h"
#include <QDebug>

ToolController::ToolController(QObject* parent) : QObject(parent) {}

ToolController::PageResult ToolController::getToolList(int page, int pageSize,
    const QString& keyword, const QString& category, const QString& cabinet,
    const QString& status, const QString& machineGroup) {
    PageResult r;
    r.page = page; r.pageSize = pageSize;
    r.total = m_toolDao.countTools(keyword, category, cabinet, status, machineGroup);
    r.list  = m_toolDao.findAllTools(page, pageSize, keyword, category, cabinet, status, machineGroup);
    return r;
}

ToolInfo ToolController::getToolById(int toolId) { return m_toolDao.findToolById(toolId); }

int ToolController::addTool(const ToolInfo& tool) {
    if (!validateToolCode(tool.toolCode)) return -1;
    ToolInfo t = tool;
    t.currentQty = t.totalQty;
    t.status = "in_stock";
    int id = m_toolDao.insertTool(t);
    if (id > 0) emit toolCreated(id);
    return id;
}

bool ToolController::updateTool(const ToolInfo& tool) {
    if (!validateToolCode(tool.toolCode, tool.toolId)) return false;
    bool ok = m_toolDao.updateTool(tool);
    if (ok) emit toolUpdated(tool.toolId);
    return ok;
}

bool ToolController::deleteTool(int toolId) { return m_toolDao.deleteToolById(toolId); }

// [V2.02 2026-06-28] 详情页上传文档 — 更新工具文档路径
// 作者：袁燕 — 供ToolManagementPage详情页上传文档后调用
bool ToolController::updateToolDocument(int toolId, const QString& docPath) {
    bool ok = m_toolDao.updateDocumentPath(toolId, docPath);
    if (ok) emit toolUpdated(toolId);
    return ok;
}

// ══ 借用流程 ══
ToolController::BorrowResult ToolController::borrowTool(
    int toolId, int userId, int qty, const QString& reason,
    const QDateTime& expectReturn, const QString& flowNo) {
    BorrowResult r;
    ToolInfo tool = m_toolDao.findToolById(toolId);
    if (tool.toolId == 0) { r.msg = "工具不存在"; return r; }
    if (tool.status != "in_stock" || tool.currentQty < qty) {
        r.msg = "工具库存不足"; return r;
    }
    // 检查逾期
    if (m_recordDao.hasOverdue(userId)) {
        r.msg = "您有逾期未还的工具，请先归还"; return r;
    }
    // 事务
    bool ok = m_recordDao.transaction([&]() {
        // 扣库存
        if (!m_toolDao.borrowTool(toolId, qty)) return false;
        // 改状态
        ToolInfo updated = m_toolDao.findToolById(toolId);
        if (updated.currentQty <= 0)
            m_toolDao.updateToolStatus(toolId, "borrowed");
        // 写记录
        BorrowRecord br;
        br.flowNo = flowNo;
        br.userId = userId;
        br.toolId = toolId;
        br.quantity = qty;
        br.borrowTime = QDateTime::currentDateTime();
        br.expectReturnTime = expectReturn;
        br.borrowReason = reason;
        br.status = "borrowed";
        r.recordId = m_recordDao.insertBorrow(br);
        return r.recordId > 0;
    });
    r.ok = ok;
    r.msg = ok ? "借用成功" : "借用失败";
    if (ok) emit toolBorrowed(toolId, userId, r.recordId);
    return r;
}

// ══ 归还流程 ══
ToolController::ReturnResult ToolController::returnTool(
    int borrowId, const QString& condition, const QString& remark) {
    ReturnResult r;
    BorrowRecord br = m_recordDao.findBorrowById(borrowId);
    if (br.borrowId == 0) { r.msg = "记录不存在"; return r; }
    if (br.status == "returned") { r.msg = "该记录已归还"; return r; }

    QDateTime now = QDateTime::currentDateTime();
    bool overdue = br.expectReturnTime.isValid() && now > br.expectReturnTime;

    bool ok = m_recordDao.transaction([&]() {
        if (!m_recordDao.completeBorrowReturn(borrowId, now, condition, remark)) return false;
        if (!m_toolDao.returnTool(br.toolId, br.quantity)) return false;
        return true;
    });
    r.ok = ok;
    r.msg = ok ? "归还成功" : "归还失败";
    if (ok) emit toolReturned(br.toolId, br.userId, borrowId, overdue);
    return r;
}

// ══ 分类/柜体 ══
QList<ToolCategory> ToolController::getCategories() { return m_toolDao.allCategories(); }
QList<ToolCabinet>  ToolController::getCabinets()  { return m_toolDao.allCabinets(); }
QStringList ToolController::categoryNames() { return m_toolDao.allCategoryNames(); }
QStringList ToolController::cabinetNames()  { return m_toolDao.allCabinetNames(); }

// [V7.0] 统计数据
QJsonObject ToolController::getToolStats() { return m_toolDao.getToolStats(); }

// [V7.0] 工程机组列表
QList<QJsonObject> ToolController::getMachineGroups() { return m_toolDao.allMachineGroups(); }

// [V7.0] 机组详情
QJsonObject ToolController::getMachineGroupById(int groupId) { return m_toolDao.getMachineGroupById(groupId); }

bool ToolController::validateToolCode(const QString& code, int excludeId) {
    if (code.isEmpty()) return false;
    ToolInfo existing = m_toolDao.findToolByCode(code);
    if (existing.toolId > 0 && existing.toolId != excludeId) return false;
    return true;
}
