// 智能柜Qt Widget 2.0  ToolController实现
// 工具借用/归还核心业务流程
// 方法名更新为合并后的db::ToolDAO/RecordDAO新API
#include "ToolController.h"
#include <QDebug>
#include "common/Constants.h"

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
    t.status = SC::TOOL_IN_STOCK;
    int id = m_toolDao.insertTool(t);
    return id;
}

bool ToolController::updateTool(const ToolInfo& tool) {
    if (!validateToolCode(tool.toolCode, tool.toolId)) return false;
    return m_toolDao.updateTool(tool);
}

bool ToolController::deleteTool(int toolId) { return m_toolDao.deleteToolById(toolId); }

// 详情页上传文档 — 更新工具文档路径
// 供ToolManagementPage详情页上传文档后调用
bool ToolController::updateToolDocument(int toolId, const QString& docPath) {
    return m_toolDao.updateDocumentPath(toolId, docPath);
}

// 注意：借用/归还已由 BorrowService / ReturnService 承接（含事务兜底与失败明细），
// Controller 层不再保留第二套借还实现，避免"两套逻辑"导致规则漂移。

// ══ 分类/柜体 ══
QList<ToolCategory> ToolController::getCategories() { return m_toolDao.allCategories(); }
QStringList ToolController::categoryNames() { return m_toolDao.allCategoryNames(); }
QStringList ToolController::cabinetNames()  { return m_toolDao.allCabinetNames(); }

// 统计数据
QJsonObject ToolController::getToolStats() { return m_toolDao.getToolStats(); }

// 工程机组列表
QList<QJsonObject> ToolController::getMachineGroups() { return m_toolDao.allMachineGroups(); }

// 机组详情
QJsonObject ToolController::getMachineGroupById(int groupId) { return m_toolDao.getMachineGroupById(groupId); }

bool ToolController::validateToolCode(const QString& code, int excludeId) {
    if (code.isEmpty()) return false;
    ToolInfo existing = m_toolDao.findToolByCode(code);
    if (existing.toolId > 0 && existing.toolId != excludeId) return false;
    return true;
}
