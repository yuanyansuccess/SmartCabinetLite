/**
 * @file ToolDAO.h
 * @brief 工具数据访问对象 — QJsonObject API + 实体类API，含分类/柜体/机组/位置映射查询
 * @author 袁燕
 */
#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QSqlDatabase>
#include <QSqlQuery>
#include "model/ToolInfo.h"
#include "model/ToolCategory.h"
#include "model/ToolCabinet.h"
#include "BaseDAO.h"
#include "common/Constants.h"

namespace db {

class ToolDAO : public BaseDAO {
public:
    // ═══════════════════════════════════════════════
    // 位置维度查询（JSON API，Service层使用）
    // ═══════════════════════════════════════════════
    // 位置维度查询：每个在库位置一行（同一工具多位置分行显示）
    // 入参：keyword关键字, category类别, status映射表status, cabinetId柜体, page页码, pageSize每页, machineGroupId机组
    // 返回：QJsonObject含list(QJsonArray)和total(int)，每项含mappingId/toolId/toolName/toolCode/spec/category/position等
    QJsonObject findAllByPosition(const QString& keyword, const QString& category,
                                   const QString& status, int cabinetId, int page, int pageSize,
                                   int machineGroupId = 0);
    // 按工具种类聚合查询在库工具（借用页面用）
    // 同一工具一行，availableQty=映射表in_stock位置数
    // 入参：keyword, category, page, pageSize, machineGroupId
    // 返回：QJsonObject含list(QJsonArray)和total，每项含toolId/toolName/toolCode/availableQty等
    QJsonObject findAllInStockByTool(const QString& keyword, const QString& category,
                                      int page, int pageSize, int machineGroupId = 0);
    // 按工具ID查询工具详情（JSON接口，含分类名/机组名）
    // 入参：toolId工具ID
    // 返回：含分类名、机组名、识别方式与文档路径的详情对象；不存在时返回空对象
    QJsonObject findById(int toolId);
    // 按JSON字段更新工具（驱动入库/出库状态联动）
    // 入参：toolId工具ID, ups待更新字段对象
    // 返回：更新成功返回true
    bool        update(int toolId, const QJsonObject& ups);
    // 更新工具状态（tool_info.status）
    // 入参：toolId工具ID, status目标状态（SC::TOOL_*）
    // 返回：更新成功返回true
    bool        updateStatus(int toolId, const QString& status);

    // ═══════════════════════════════════════════════
    // 实体类API（Controller层使用）
    // ═══════════════════════════════════════════════
    // 分页查询全部工具（位置维度聚合后的工具列表）
    // 入参：page页码, pageSize每页, keyword/category/cabinet/status/machineGroup过滤条件
    // 返回：QList<ToolInfo> 工具实体列表
    QList<ToolInfo> findAllTools(int page = 1, int pageSize = 20,
                                 const QString& keyword = "", const QString& category = "",
                                 const QString& cabinet = "", const QString& status = "",
                                 const QString& machineGroup = "");
    // 统计工具总数（按过滤条件）
    // 入参：keyword/category/cabinet/status/machineGroup过滤条件
    // 返回：满足条件的工具数量
    int             countTools(const QString& keyword = "", const QString& category = "",
                               const QString& cabinet = "", const QString& status = "",
                               const QString& machineGroup = "");
    // 按工具ID查询工具实体
    // 入参：toolId工具ID
    // 返回：ToolInfo实体；不存在时返回默认构造实体（toolId=0）
    ToolInfo        findToolById(int toolId);
    // 按工具编码查询工具实体
    // 入参：toolCode工具编码
    // 返回：ToolInfo实体；不存在时返回默认构造实体
    ToolInfo        findToolByCode(const QString& toolCode);
    // 新增工具实体
    // 入参：tool待插入工具实体
    // 返回：新工具ID；失败返回-1
    int             insertTool(const ToolInfo& tool);
    // 更新工具实体全字段
    // 入参：tool待更新工具实体（按toolId匹配）
    // 返回：更新成功返回true
    bool            updateTool(const ToolInfo& tool);
    // 轻量更新文档路径 — 详情页上传文档专用，避免全字段updateTool
    // 入参：toolId工具ID, docPath文档路径
    // 返回：更新成功返回true
    bool            updateDocumentPath(int toolId, const QString& docPath);
    // 按工具ID删除工具
    // 入参：toolId工具ID
    // 返回：删除成功返回true
    bool            deleteToolById(int toolId);

    // ═══════════════════════════════════════════════
    // 分类/柜体查询
    // ═══════════════════════════════════════════════
    // 查询全部分类
    // 返回：QList<ToolCategory> 分类实体列表
    QList<ToolCategory> allCategories();
    // 查询全部柜体
    // 返回：QList<ToolCabinet> 柜体实体列表
    QList<ToolCabinet>  allCabinets();
    // 查询全部分类名称（下拉框用）
    // 返回：分类名称字符串列表
    QStringList         allCategoryNames();
    // 通过柜体名称前缀（如"A"）解析柜体ID
    // 入参：prefix柜体名前缀
    // 返回：cabinet_id；找不到返回-1
    int findCabinetIdByPrefix(const QString& prefix);

    // ═══════════════════════════════════════════════
    // 统计与机组查询
    // ═══════════════════════════════════════════════
    // 工具统计（总数/在库/已借出/待入库/已出库/维护中）——全系统"工具数量"唯一口径
    // 入参：machineGroupId机组（>0只统计该机组，<=0统计全库）
    // 返回：QJsonObject含inStockCount/borrowedCount/pendingCount/checkedOutCount/maintenanceCount/totalCount
    QJsonObject getToolStats(int machineGroupId = 0);
    // 工程机组列表
    // 返回：QList<QJsonObject> 各机组{groupId,groupName,...}
    QList<QJsonObject> allMachineGroups();
    // 机组详情
    // 入参：groupId机组ID
    // 返回：QJsonObject含groupName/machineCount等
    QJsonObject getMachineGroupById(int groupId);

    // ═══════════════════════════════════════════════
    // 位置映射查询与更新
    // ═══════════════════════════════════════════════
    // 通过工具ID查询在库位置列表（借用时自动分配位置），limit限定返回条数
    // 入参：toolId工具ID, limit返回条数上限
    // 返回：QJsonArray 在库位置明细
    QJsonArray  findInStockPositions(int toolId, int limit = 10);
    // 统计工具in_stock位置数
    // 入参：toolId工具ID
    // 返回：in_stock位置数量
    int countInStockPositions(int toolId);
    // 查询映射表状态
    // 入参：mappingId位置映射ID
    // 返回：位置状态字符串；不存在时返回空字符串
    QString findMappingStatus(int mappingId);
    // 按位置条件更新映射表状态（条件更新，防止并发覆盖）
    // 入参：toolId/cabinetId/layer/position定位, newStatus新状态, expectedStatus期望原状态
    // 返回：更新成功返回true
    bool updateMappingStatus(int mappingId, const QString& status);
    // 按位置条件更新映射表状态（预期状态匹配才更新）
    // 入参：toolId/cabinetId/layer/position定位, newStatus新状态, expectedStatus期望原状态
    // 返回：更新成功返回true
    bool updateMappingByPosition(int toolId, int cabinetId, const QString& layer,
                                 const QString& position, const QString& newStatus,
                                 const QString& expectedStatus);

    // ═══════════════════════════════════════════════
    // 入库页查询
    // ═══════════════════════════════════════════════
    // 查找待入库工具（映射表中有status='pending'空闲位置的工具）
    // 入参：categoryId分类(-1=全部), machineGroupId机组
    // 返回：QJsonArray 每项含toolId/toolCode/toolName/status/freePosCount
    QJsonArray  findPendingTools(int categoryId, int machineGroupId);
    // 查找工具基本信息（tool_name, tool_code, spec）
    // 入参：toolId工具ID
    // 返回：QJsonObject含toolName/toolCode/spec
    QJsonObject findToolBasicInfo(int toolId);
    // 查找工具的待入库位置（映射表status='pending'）
    // 入参：toolId工具ID
    // 返回：QJsonArray 待入库位置明细
    QJsonArray  findPendingPositions(int toolId);

    // ═══════════════════════════════════════════════
    // 维护域数据接口：任务配置/工具维护/位置对照
    // ═══════════════════════════════════════════════
    // 所有启用的任务类型
    // 返回：QJsonArray 每项{typeId, typeName}
    QJsonArray  allTaskTypes();
    // 任务类型关联的工具列表
    // 入参：typeId任务类型ID
    // 返回：QJsonArray 关联工具列表
    QJsonArray  findTaskTypeTools(int typeId);
    // 更新任务类型关联工具数量
    // 入参：typeId任务类型ID, toolId工具ID, qty数量
    // 返回：更新成功返回true
    bool        updateTaskTypeToolQty(int typeId, int toolId, int qty);
    // 检查任务类型与工具是否已在关联表
    // 入参：typeId任务类型ID, toolId工具ID
    // 返回：已关联返回true
    bool        checkTaskTypeToolExists(int typeId, int toolId);
    // 新增任务类型关联工具
    // 入参：typeId任务类型ID, toolId工具ID, qty数量
    // 返回：新增成功返回true
    bool        addTaskTypeTool(int typeId, int toolId, int qty);
    // 删除任务类型关联工具
    // 入参：typeId任务类型ID, toolId工具ID
    // 返回：删除成功返回true
    bool        deleteTaskTypeTool(int typeId, int toolId);

    // ═══════════════════════════════════════════════
    // 位置对照关系查询
    // ═══════════════════════════════════════════════
    // 工具下拉列表
    // 返回：QJsonArray 每项{toolId,toolCode,toolName}
    // 入参：categoryId 大于 0 时只返回该分类下的工具，否则返回全部（向后兼容默认参数）
    QJsonArray  allToolsSimple(int categoryId = 0);
    // 检查位置对照关系是否存在
    // 入参：cabinetId/layer/position定位
    // 返回：QJsonObject含exists(bool)与已有信息
    QJsonObject checkPositionMappingExists(int cabinetId, const QString& layer, const QString& position);
    // 插入位置对照关系
    // 入参：toolId/cabinetId/layer/position定位, status初始状态（默认SC::TOOL_PENDING）
    // 返回：插入成功返回true
    bool        insertPositionMapping(int toolId, int cabinetId, const QString& layer, const QString& position, const QString& status = SC::TOOL_PENDING);
    // 在库位置明细（出库下拉用）
    // 入参：machineGroupId机组
    // 返回：QJsonArray 每项{mappingId,toolId,toolCode,toolName,cabinetName,layer,position,posDisplay}
    QJsonArray  findAllInStockPositions(int machineGroupId);
    // 已借出未归还的位置明细（出库下拉用；借出工具转出库不需要开箱）
    // 入参：machineGroupId机组（<=0 表示不限机组）
    // 返回：QJsonArray 字段与 findAllInStockPositions 一致
    QJsonArray  findBorrowedOutPositions(int machineGroupId);

    // 从查询结果构造ToolInfo实体
    // 入参：q已定位到某行的查询对象
    // 返回：ToolInfo实体
    static ToolInfo fromQuery(const QSqlQuery& q);
};

} // namespace db
