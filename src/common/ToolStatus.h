/**
 * @file ToolStatus.h
 * @brief 工具状态中文展示工具 — 纯字符串处理，无UI依赖
 * @author 袁燕
 *
 * 把 tool_info.status 的英文枚举（SC::TOOL_*）统一映射为中文文案与配色，
 * 供工具列表、工具详情等页面共用，避免各处裸写一长串 if-else 与散落的硬编码颜色。
 * 配色语义：在库绿 / 已借出橙 / 已出库红 / 维护中灰 / 待入库蓝。
 * borrowed 存在历史显示差异：工具管理页沿用旧口径"已借用"，其余页统一"已借出"，
 * 由 keepLegacyBorrowText 参数控制，默认新口径。
 */
#pragma once

#include <QString>

#include "common/Constants.h"
#include "utils/StyleHelper.h"

namespace common {

// 工具状态枚举：QString 不能直接用于 switch，先转换为枚举再分发
enum class ToolStatus {
    InStock,      // 在库
    Borrowed,     // 已借出（工具管理页历史口径为"已借用"）
    CheckedOut,   // 已出库（区别于借用）
    Maintenance,  // 维护中
    Pending,      // 待入库（配置层已建未物理入库）
    Unknown       // 未识别状态
};

/// 字符串工具状态 → 枚举（switch 前置转换）
/// @param status 工具状态原始值（如 in_stock/borrowed/checked_out）
/// @return 对应的 ToolStatus 枚举，未匹配返回 Unknown
inline ToolStatus toToolStatus(const QString& status)
{
    if (status == SC::TOOL_IN_STOCK)    return ToolStatus::InStock;
    if (status == SC::TOOL_BORROWED)    return ToolStatus::Borrowed;
    if (status == SC::TOOL_CHECKED_OUT) return ToolStatus::CheckedOut;
    if (status == SC::TOOL_MAINTENANCE) return ToolStatus::Maintenance;
    if (status == SC::TOOL_PENDING)     return ToolStatus::Pending;
    return ToolStatus::Unknown;
}

/// 工具状态中文文案
/// @param status 工具状态枚举
/// @param keepLegacyBorrowText 工具管理页历史口径开关，true 时 borrowed 显示"已借用"，默认 false 显示"已借出"
/// @return 中文展示文案；Unknown 返回空串，由调用方回退到原始值
inline QString toolStatusText(ToolStatus status, bool keepLegacyBorrowText = false)
{
    switch (status) {
    case ToolStatus::InStock:
        return QStringLiteral("在库");
    case ToolStatus::Borrowed:
        return keepLegacyBorrowText ? QStringLiteral("已借用") : QStringLiteral("已借出");
    case ToolStatus::CheckedOut:
        return QStringLiteral("已出库");
    case ToolStatus::Maintenance:
        return QStringLiteral("维护中");
    case ToolStatus::Pending:
        return QStringLiteral("待入库");
    case ToolStatus::Unknown:
    default:
        return QString();
    }
}

/// 工具状态配色（与中文文案语义一致：在库绿/已借出橙/已出库红/维护中灰/待入库蓝）
/// @param status 工具状态枚举
/// @return 颜色十六进制字符串；Unknown 返回次级文本色
inline QString toolStatusColor(ToolStatus status)
{
    switch (status) {
    case ToolStatus::InStock:
        return StyleHelper::successColor();
    case ToolStatus::Borrowed:
        return StyleHelper::warningColor();
    case ToolStatus::CheckedOut:
        return StyleHelper::dangerColor();
    case ToolStatus::Maintenance:
        return StyleHelper::textMuted();
    case ToolStatus::Pending:
        return StyleHelper::primaryColor();
    case ToolStatus::Unknown:
    default:
        return StyleHelper::textMuted();
    }
}

} // namespace common
