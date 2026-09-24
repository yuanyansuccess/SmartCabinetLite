#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0  工具管理业务控制层
// 日期：2026-06-21 功能：工具CRUD、分类管理、柜体管理、出入库
// [V6.9 2026-06-24] 统一到db/目录namespace db
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
    // [V2.02 2026-06-28] 详情页上传文档 — 轻量更新文档路径
    bool        updateToolDocument(int toolId, const QString& docPath);

    // 借用/归还流转
    struct BorrowResult { bool ok; QString msg; int recordId; };
    BorrowResult borrowTool(int toolId, int userId, int qty, const QString& reason,
                             const QDateTime& expectReturn, const QString& flowNo);
    struct ReturnResult { bool ok; QString msg; };
    ReturnResult returnTool(int borrowId, const QString& condition, const QString& remark);

    // 分类/柜体管理
    QList<ToolCategory> getCategories();
    QList<ToolCabinet>  getCabinets();
    QStringList categoryNames();
    QStringList cabinetNames();

    // [V7.0] 统计与机组管理
    QJsonObject getToolStats();
    QList<QJsonObject> getMachineGroups();
    QJsonObject getMachineGroupById(int groupId);

signals:
    void toolCreated(int toolId);
    void toolUpdated(int toolId);
    void toolBorrowed(int toolId, int userId, int recordId);
    void toolReturned(int toolId, int userId, int recordId, bool overdue);
    void lowStockAlert(int toolId, const QString& toolName, int currentQty);

private:
    db::ToolDAO   m_toolDao;
    db::RecordDAO m_recordDao;

    bool validateToolCode(const QString& code, int excludeId = 0);
};
