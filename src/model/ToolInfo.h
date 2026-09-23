#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0  工具信息实体
// 日期：2026-06-21  映射表：tool_info
// [V7.0 2026-06-24] 新增machineGroupId/machineGroupName字段，支持工程机组关联
// [V2.01 2026-06-27] 新增recognitionMethod/documentPath字段，支持识别方式选择和工具文档上传
#include <QString>
#include <QDateTime>

struct ToolInfo {
    int     toolId       = 0;
    int     mappingId    = 0;  // [V2.11 2026-07-02 袁燕] 位置映射ID（位置唯一标识）
    QString toolCode;
    QString toolName;
    QString spec;
    int     categoryId   = 0;
    int     cabinetId    = 0;
    int     machineGroupId = 0;  // [V7.0] 所属机组ID
    QString layer;
    QString position;
    int     totalQty     = 0;
    int     currentQty   = 0;
    int     activeBorrows = 0;  // [2026-06-26v17] 活跃借用数（borrowing+overdue）
    QString rfidTag;
    QString status       = "in_stock";
    QString checkoutReason;
    int     isRecommended = 0;
    // [V2.01 2026-06-27] 识别方式(rfid/vision) + 工具文档本地路径
    QString recognitionMethod = "rfid";  // 默认RFID识别
    QString documentPath;                // 工具文档本地路径(doc/docx/pdf)
    QDateTime createdAt;
    QDateTime updatedAt;

    // 关联展示字段
    QString categoryName;
    QString cabinetName;
    QString machineGroupName;  // [V7.0] 机组名称（JOIN查询用）

    // 最近操作字段 [V7.0]
    QString latestOpType;       // borrow|checkout|checkin
    QString latestOpTime;       // 最近操作时间
    QString latestOpUser;       // 最近操作人
};
