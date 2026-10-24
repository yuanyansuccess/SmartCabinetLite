/**
 * @file ToolController.h
 * @brief 工具控制层：工具/分类/柜体/机组列表查询与详情装配
 * @author 袁燕
 */
#pragma once
#include <QObject>
#include <QDate>
#include "model/ToolInfo.h"
#include "model/ToolCategory.h"
#include "model/ToolCabinet.h"
#include "model/BorrowRecord.h"
#include "db/ToolDAO.h"
#include "db/RecordDAO.h"

// 分层定位：Controller 负责读查询与结果装配；工具的入库/借用/归还/出库等写操作
// 走 services/ 下的 BorrowService / ReturnService / CheckoutService（含事务与业务校验）。
class ToolController : public QObject {
    Q_OBJECT
public:
    explicit ToolController(QObject* parent = nullptr);

    struct PageResult { QList<ToolInfo> list; int total = 0; int page = 1; int pageSize = 20; };

    // 工具CRUD
    PageResult  getToolList(int page, int pageSize, const QString& keyword = "",
                            const QString& category = "", const QString& cabinet = "",
                            const QString& status = "", const QString& machineGroup = "");
    ToolInfo    getToolById(int toolId);
    /// 建档同时落位置映射（位置必填，建档即定位置）
    int         addToolWithPosition(const ToolInfo& tool, int cabinetId,
                                   const QString& layer, const QString& position);
    /// 按柜体名称首字母（如"A"）解析柜体ID；找不到返回-1
    int         resolveCabinetId(const QString& cabLetter);
    bool        updateTool(const ToolInfo& tool);
    bool        deleteTool(int toolId);
    // 详情页上传文档 — 轻量更新文档路径
    bool        updateToolDocument(int toolId, const QString& docPath);

    // 分类/柜体管理
    QList<ToolCategory> getCategories();
    QStringList categoryNames();

    /**
 * @brief 查询工具统计（全系统唯一口径：位置维度计数）
 * @param machineGroupId 机组编号：>0 只统计该机组，<=0 统计全库
 * @return 含 inStockCount/borrowedCount/pendingCount/checkedOutCount/maintenanceCount/totalCount 的对象
 */
QJsonObject getToolStats(int machineGroupId = 0);
    QList<QJsonObject> getMachineGroups();

private:
    db::ToolDAO   m_toolDao;

    bool validateToolCode(const QString& code, int excludeId = 0);
};
