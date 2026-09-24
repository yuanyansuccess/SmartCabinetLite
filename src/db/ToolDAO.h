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

namespace db {

class ToolDAO : public BaseDAO {
public:
    ToolDAO() = default;

    // ═══════════════════════════════════════════════
    // QJsonObject API（Service层使用）
    // ═══════════════════════════════════════════════
    QJsonObject findAll(const QString& keyword, const QString& category,
                        const QString& status, int cabinetId, int page, int pageSize,
                        int machineGroupId = 0);
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
    QJsonObject findById(int toolId);
    QJsonObject findByCode(const QString& code);
    int         insert(const QJsonObject& toolInfo);
    bool        update(int toolId, const QJsonObject& updates);
    bool        softDelete(int toolId);
    bool        updateStock(int toolId, int delta);
    bool        updateStatus(int toolId, const QString& status);

    // ═══════════════════════════════════════════════
    // 实体类API（Controller层使用）— 从dao/ToolDAO合并
    // ═══════════════════════════════════════════════
    QList<ToolInfo> findAllTools(int page = 1, int pageSize = 20,
                                 const QString& keyword = "", const QString& category = "",
                                 const QString& cabinet = "", const QString& status = "",
                                 const QString& machineGroup = "");
    int             countTools(const QString& keyword = "", const QString& category = "",
                               const QString& cabinet = "", const QString& status = "",
                               const QString& machineGroup = "");
    ToolInfo        findToolById(int toolId);
    ToolInfo        findToolByCode(const QString& toolCode);
    int             insertTool(const ToolInfo& tool);
    bool            updateTool(const ToolInfo& tool);
    bool            deleteToolById(int toolId);
    bool            updateToolStatus(int toolId, const QString& status);
    bool            borrowTool(int toolId, int qty);
    bool            returnTool(int toolId, int qty);
    // [V2.02 2026-06-28] 轻量更新文档路径 — 详情页上传文档专用，避免全字段updateTool
    bool            updateDocumentPath(int toolId, const QString& docPath);

    // 分类/柜体查询
    QList<ToolCategory> allCategories();
    QList<ToolCabinet>  allCabinets();
    QStringList         allCategoryNames();
    QStringList         allCabinetNames();

    // 统计与机组查询
    QJsonObject getToolStats();                          // 工具统计（总数/在库/已借用/维护中）
    QList<QJsonObject> allMachineGroups();               // 工程机组列表
    QJsonObject getMachineGroupById(int groupId);        // 机组详情
    // 通过工具ID查询映射表中in_stock位置的列表，用于借用时自动分配位置
    QJsonArray findInStockPositions(int toolId, int limit = 10);
    // 更新映射表状态（用于出库操作标记checked_out等）
    bool        updateMappingStatus(int mappingId, const QString& status);

    // 按名称查ID（Service层迁移到DAO）
    int findCategoryIdByName(const QString& name);     // 按分类名查category_id，-1=未找到
    int findCabinetIdByName(const QString& name);      // 按柜体名查cabinet_id，-1=未找到
    int countInStockPositions(int toolId);             // 统计工具in_stock位置数
    QString findMappingStatus(int mappingId);          // 查询映射表状态，空字符串=不存在
    // [V2.15] 按位置条件更新映射表状态（条件更新，防止并发覆盖）
    bool updateMappingByPosition(int toolId, int cabinetId, const QString& layer,
                                 const QString& position, const QString& newStatus,
                                 const QString& expectedStatus);

    // 入库页：查找待入库工具（映射表中有status='pending'空闲位置的工具）
    // 入参：categoryId分类(-1=全部), machineGroupId机组
    // 返回：每个工具含toolId/toolCode/toolName/status/freePosCount
    QJsonArray  findPendingTools(int categoryId, int machineGroupId);
    // 入库页：查找工具基本信息（tool_name, tool_code, spec）
    QJsonObject findToolBasicInfo(int toolId);
    // 入库页：查找工具的待入库位置（映射表status='pending'）
    QJsonArray  findPendingPositions(int toolId);

    // ═══════════════════════════════════════════════
    // 系统维护页：任务配置/工具维护/对照关系
    // ═══════════════════════════════════════════════
    // 任务类型
    QJsonArray  allTaskTypes();                            // 所有启用的任务类型 [{typeId, typeName}]
    QJsonArray  findTaskTypeTools(int typeId);              // 任务类型关联的工具列表
    bool        updateTaskTypeToolQty(int typeId, int toolId, int qty);
    bool        checkTaskTypeToolExists(int typeId, int toolId);
    bool        addTaskTypeTool(int typeId, int toolId, int qty);
    bool        deleteTaskTypeTool(int typeId, int toolId);
    // 工具维护
    QJsonArray  allToolsForMaintenance();                   // 工具列表（含分类名）[{toolId,toolCode,toolName,categoryName,spec,unit,status}]
    QJsonObject findToolForEdit(int toolId);                // 编辑对话框用的工具信息
    bool        insertToolFull(const QJsonObject& tool);    // 新建工具（含全部字段）
    bool        updateToolFull(int toolId, const QJsonObject& updates); // 更新工具（含全部字段）
    bool        deleteToolFully(int toolId);                // 删除工具及关联映射记录
    // 位置对照
    QJsonArray  allToolsSimple();                           // 工具下拉列表 [{toolId,toolCode,toolName}]
    QJsonObject checkPositionMappingExists(int cabinetId, const QString& layer, const QString& position);
    bool        insertPositionMapping(int toolId, int cabinetId, const QString& layer, const QString& position, const QString& status = "pending");
    QJsonObject findPositionMappingDetail(int mappingId);   // 映射详情（含工具名）
    bool        isPositionMappingOccupied(int mappingId);   // 该位置是否已有工具在库/借用
    bool        deletePositionMapping(int mappingId);
    QJsonArray  findAllPositionMappings();                  // 所有对照关系 [{mappingId,toolName,toolCode,cabinetName,layer,position,positionStatus}]

    static ToolInfo fromQuery(const QSqlQuery& q);
};

} // namespace db
