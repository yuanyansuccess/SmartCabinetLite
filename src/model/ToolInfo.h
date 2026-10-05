/**
 * @file ToolInfo.h
 * @brief 工具实体，字段对齐 tool_info，含位置映射与识别方式等扩展信息
 * @author 袁燕
 */
#pragma once
#include <QString>
#include <QDateTime>
#include "common/Constants.h"

struct ToolInfo {
    int     toolId       = 0;
    int     mappingId    = 0;  // 位置映射ID（位置唯一标识）
    QString toolCode;
    QString toolName;
    QString spec;
    int     categoryId   = 0;
    int     cabinetId    = 0;
    int     machineGroupId = 0;  // 所属机组ID
    QString layer;
    QString position;
    int     totalQty     = 0;
    int     currentQty   = 0;
    int     activeBorrows = 0;  // 活跃借用数（borrowing+overdue）
    QString visionTag;
    QString status       = SC::TOOL_IN_STOCK;
    QString checkoutReason;
    int     isRecommended = 0;
    // 识别方式统一为视觉识别 + 工具文档本地路径
    QString recognitionMethod = SC::RECOGNITION_VISION;  // 默认视觉识别
    QString documentPath;                // 工具文档本地路径(doc/docx/pdf)
    QDateTime createdAt;
    QDateTime updatedAt;

    // 关联展示字段
    QString categoryName;
    QString cabinetName;
    QString machineGroupName;  // 机组名称（JOIN查询用）

    // 最近操作字段 
    QString latestOpType;       // borrow|checkout|checkin
    QString latestOpTime;       // 最近操作时间
    QString latestOpUser;       // 最近操作人
};
