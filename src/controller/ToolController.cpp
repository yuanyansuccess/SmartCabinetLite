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
 * @brief 新建工具档案并同时把它放到指定货位，两步要么都成要么都不做
 * @param tool 待建档的工具信息
 * @param cabinetId 柜体ID，必须大于 0
 * @param layer 层号
 * @param position 位号
 * @return 新工具ID；编号校验不过或货位写不进去时返回 -1（已建的主档会被删掉，不留半成品）
 */
int ToolController::addToolWithPosition(const ToolInfo& tool, int cabinetId,
                                        const QString& layer, const QString& position)
{
    if (!validateToolCode(tool.toolCode)) return -1;
    if (cabinetId <= 0) return -1;
    ToolInfo t = tool;
    t.currentQty = t.totalQty;
    t.status = SC::TOOL_IN_STOCK;
    int id = m_toolDao.insertTool(t);
    if (id <= 0) return -1;
    // 建档即定位置：建立 in_stock 映射；位置被占用则回滚主档（删主档、返回-1）
    if (!m_toolDao.insertPositionMapping(id, cabinetId, layer, position, SC::TOOL_IN_STOCK)) {
        m_toolDao.deleteToolById(id);
        return -1;
    }
    return id;
}

/**
 * @brief 按柜体字母编号查出柜体ID
 * @param cabLetter 柜体字母编号，如 "A"
 * @return 柜体ID；没有对应柜体时返回 -1
 */
int ToolController::resolveCabinetId(const QString& cabLetter)
{
    return m_toolDao.findCabinetIdByPrefix(cabLetter);
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

/**
 * @brief 更新工具关联文档的存储路径
 * @param toolId 工具ID
 * @param docPath 上传文档的本地路径
 * @return true=更新成功
 */
bool ToolController::updateToolDocument(int toolId, const QString& docPath) {
    return m_toolDao.updateDocumentPath(toolId, docPath);
}

// 注意：借用/归还已由 BorrowService / ReturnService 承接（含事务保底与失败明细），
// Controller 层不再保留第二套借还实现，避免"两套逻辑"导致规则漂移。

// ══ 分类/柜体 ══
/**
 * @brief 查询全部分类（含分类对象列表）
 * @return 分类对象列表
 */
QList<ToolCategory> ToolController::getCategories() { return m_toolDao.allCategories(); }
/**
 * @brief 查询全部分类名称
 * @return 分类名称列表
 */
QStringList ToolController::categoryNames() { return m_toolDao.allCategoryNames(); }

/**
 * @brief 查询工具统计指标（按机组隔离口径）
 * @param machineGroupId 当前机组ID，保证与借用/出入库页同一数据口径
 * @return 统计指标对象（总数/在库/借出等）
 */
QJsonObject ToolController::getToolStats(int machineGroupId) { return m_toolDao.getToolStats(machineGroupId); }

/**
 * @brief 查询工程全部机组列表
 * @return 机组信息对象列表
 */
QList<QJsonObject> ToolController::getMachineGroups() { return m_toolDao.allMachineGroups(); }

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
