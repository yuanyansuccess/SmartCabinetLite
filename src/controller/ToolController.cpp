/**
 * @file ToolController.cpp
 * @brief 工具控制层实现：工具/分类/柜体/机组列表查询与详情装配
 * @author 袁燕
 */
#include "ToolController.h"
#include <QDebug>
#include "common/Constants.h"

ToolController::ToolController(QObject* parent) : QObject(parent) {}

/**
 * @brief 分页查询工具列表
 * @param page 页码，从1开始
 * @param pageSize 每页条数
 * @param keyword 关键字，为空不过滤
 * @param category 分类，为空不过滤
 * @param cabinet 柜体，为空不过滤
 * @return 含 list 数组与 total 总数的分页结果
 */
ToolController::PageResult ToolController::getToolList(int page, int pageSize,
    const QString& keyword, const QString& category, const QString& cabinet,
    const QString& status, const QString& machineGroup) {
    PageResult r;
    r.page = page; r.pageSize = pageSize;
    r.total = m_toolDao.countTools(keyword, category, cabinet, status, machineGroup);
    r.list  = m_toolDao.findAllTools(page, pageSize, keyword, category, cabinet, status, machineGroup);
    return r;
}

/**
 * @brief 按ID查询工具
 * @param toolId 工具ID
 * @return 工具实体；不存在时返回空实体
 */
ToolInfo ToolController::getToolById(int toolId) { return m_toolDao.findToolById(toolId); }

/**
 * @brief 新增工具
 * @param tool 工具实体
 * @return 新工具ID；工具编号校验不通过或写入失败返回 -1
 */
int ToolController::addTool(const ToolInfo& tool) {
    if (!validateToolCode(tool.toolCode)) return -1;
    ToolInfo t = tool;
    t.currentQty = t.totalQty;
    t.status = SC::TOOL_IN_STOCK;
    int id = m_toolDao.insertTool(t);
    return id;
}

/**
 * @brief 更新工具信息
 * @param tool 含工具ID与待更新字段的实体
 * @return true=更新成功；false=工具编号校验不通过或写入失败
 */
bool ToolController::updateTool(const ToolInfo& tool) {
    if (!validateToolCode(tool.toolCode, tool.toolId)) return false;
    return m_toolDao.updateTool(tool);
}

/**
 * @brief 删除工具记录
 * @param toolId 工具ID
 * @return true=删除成功
 */
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
/**
 * @brief 查询全部分类名称
 * @return 分类名称列表
 */
QStringList ToolController::categoryNames() { return m_toolDao.allCategoryNames(); }
/**
 * @brief 查询全部柜体名称
 * @return 柜体名称列表
 */
QStringList ToolController::cabinetNames()  { return m_toolDao.allCabinetNames(); }

// 统计数据
QJsonObject ToolController::getToolStats() { return m_toolDao.getToolStats(); }

// 工程机组列表
QList<QJsonObject> ToolController::getMachineGroups() { return m_toolDao.allMachineGroups(); }

// 机组详情
QJsonObject ToolController::getMachineGroupById(int groupId) { return m_toolDao.getMachineGroupById(groupId); }

/**
 * @brief 校验工具编号是否合法且未被占用
 * @param code 工具编号
 * @param excludeId 编辑场景下排除自身
 * @return true=合法且未重复
 */
bool ToolController::validateToolCode(const QString& code, int excludeId) {
    if (code.isEmpty()) return false;
    ToolInfo existing = m_toolDao.findToolByCode(code);
    if (existing.toolId > 0 && existing.toolId != excludeId) return false;
    return true;
}
