#pragma once
// 智能柜Qt Widget 2.0  工具管理业务控制层
// 功能：工具CRUD、分类管理、柜体管理、出入库
// 统一到db/目录namespace db
//
// 分层定位：Controller 负责「读查询 + 结果装配」；写操作（入库/借用/归还/出库）
// 走 services/ 下的 BorrowService/ReturnService/CheckoutService（含事务与业务校验）。
// 权限校验、审计日志、操作埋点等横切关注点请加在本层，不要散到 pages 或 Service。
#include <QObject>
#include <QDate>
#include "model/ToolInfo.h"
#include "model/ToolCategory.h"
#include "model/ToolCabinet.h"
#include "model/BorrowRecord.h"
#include "db/ToolDAO.h"
#include "db/RecordDAO.h"

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
    int         addTool(const ToolInfo& tool);
    bool        updateTool(const ToolInfo& tool);
    bool        deleteTool(int toolId);
    // 详情页上传文档 — 轻量更新文档路径
    bool        updateToolDocument(int toolId, const QString& docPath);

    // 分类/柜体管理
    QList<ToolCategory> getCategories();
    QStringList categoryNames();
    QStringList cabinetNames();

    // 统计与机组管理
    QJsonObject getToolStats();
    QList<QJsonObject> getMachineGroups();
    QJsonObject getMachineGroupById(int groupId);

private:
    db::ToolDAO   m_toolDao;
    db::RecordDAO m_recordDao;

    bool validateToolCode(const QString& code, int excludeId = 0);
};
