/**
 * @file StatusDisplay.h
 * @brief 业务枚举中文展示工具 — 纯字符串处理，无UI依赖
 * @author 袁燕
 *
 * 把各类业务枚举（操作类型 sys_operation_log.operation_type、借用记录状态 borrow_record.status 等）
 * 的英文取值统一映射为中文文案与配色，供各页面共用，避免各处裸写一长串 if-else 与散落的硬编码颜色。
 * 每个枚举配套 toXxx(字符串→枚举) + xxxText(枚举→中文) + xxxColor(枚举→配色) 三件套。
 * 新增同类展示需求时，在本文件追加同名三件套即可，不要另建散落文件。
 */
#pragma once

#include <QString>

#include "common/Constants.h"
#include "utils/StyleHelper.h"

namespace common {

// 操作类型枚举：QString 不能直接用于 switch，先转换为枚举再分发
enum class OperationType {
    CheckIn,       // 入库
    CheckOut,      // 出库
    Borrow,        // 借用
    Return,        // 归还
    CheckInRound,  // 出入库识别轮次日志（入库）
    CheckOutRound, // 出入库识别轮次日志（出库）
    Unknown        // 未识别类型
};

/// 操作类型枚举 → 字符串（唯一事实来源：DB 的 sys_operation_log.operation_type 取值由枚举派生，
/// 全工程不再定义 SC::OP_* 这类静态字符串常量，需要字符串处一律调本函数）
/// @param type 操作类型枚举
/// @return 对应的字符串取值（如 checkin/checkout/borrow/return），Unknown 返回空串
inline QString operationTypeToString(OperationType type)
{
    switch (type) {
    case OperationType::CheckIn:       return QStringLiteral("checkin");
    case OperationType::CheckOut:      return QStringLiteral("checkout");
    case OperationType::Borrow:        return QStringLiteral("borrow");
    case OperationType::Return:        return QStringLiteral("return");
    case OperationType::CheckInRound:  return QStringLiteral("checkin_round");
    case OperationType::CheckOutRound: return QStringLiteral("checkout_round");
    case OperationType::Unknown:
    default:                            return QString();
    }
}

/// 字符串操作类型 → 枚举（switch 前置转换）
/// @param type 操作类型原始值（如 checkin/checkout/borrow/return）
/// @return 对应的 OperationType 枚举，未匹配返回 Unknown
inline OperationType toOperationType(const QString& type)
{
    if (type == operationTypeToString(OperationType::CheckIn))        return OperationType::CheckIn;
    if (type == operationTypeToString(OperationType::CheckOut))       return OperationType::CheckOut;
    if (type == operationTypeToString(OperationType::Borrow))         return OperationType::Borrow;
    if (type == operationTypeToString(OperationType::Return))         return OperationType::Return;
    if (type == operationTypeToString(OperationType::CheckInRound))   return OperationType::CheckInRound;
    if (type == operationTypeToString(OperationType::CheckOutRound))  return OperationType::CheckOutRound;
    return OperationType::Unknown;
}

/// 操作类型中文文案
/// @param type 操作类型原始值
/// @return 中文展示文案（未知类型返回原始值，便于排查）
inline QString operationTypeText(const QString& type)
{
    switch (toOperationType(type)) {
    case OperationType::CheckIn:
    case OperationType::CheckInRound:
        return QStringLiteral("入库");
    case OperationType::CheckOut:
    case OperationType::CheckOutRound:
        return QStringLiteral("出库");
    case OperationType::Borrow:
        return QStringLiteral("借用");
    case OperationType::Return:
        return QStringLiteral("归还");
    default:
        return type;
    }
}

/// 操作类型配色（与中文文案语义一致：入库绿/出库红/借用橙/归还蓝）
/// @param type 操作类型原始值
/// @return 颜色十六进制字符串，未知类型返回次级文本色
inline QString operationTypeColor(const QString& type)
{
    switch (toOperationType(type)) {
    case OperationType::CheckIn:
    case OperationType::CheckInRound:
        return StyleHelper::successColor();
    case OperationType::CheckOut:
    case OperationType::CheckOutRound:
        return StyleHelper::dangerColor();
    case OperationType::Borrow:
        return StyleHelper::warningColor();
    case OperationType::Return:
        return StyleHelper::primaryColor();
    default:
        return StyleHelper::textSecondary();
    }
}

// 借用记录状态枚举：QString 不能直接用于 switch，先转换为枚举再分发
enum class BorrowStatus {
    Borrowing,   // 借用中
    Returned,    // 已归还
    Overdue,     // 已逾期
    Unknown      // 未识别状态
};

/// 字符串借用记录状态 → 枚举（switch 前置转换）
/// @param status 借用记录状态原始值（如 borrowing/returned/overdue）
/// @return 对应的 BorrowStatus 枚举，未匹配返回 Unknown
inline BorrowStatus toBorrowStatus(const QString& status)
{
    if (status == SC::RECORD_BORROWING) return BorrowStatus::Borrowing;
    if (status == SC::RECORD_RETURNED)  return BorrowStatus::Returned;
    if (status == SC::RECORD_OVERDUE)   return BorrowStatus::Overdue;
    return BorrowStatus::Unknown;
}

/// 借用记录状态中文文案
/// @param status 借用记录状态枚举
/// @return 中文展示文案；Unknown 返回空串，由调用方回退到原始值或"--"
inline QString borrowStatusText(BorrowStatus status)
{
    switch (status) {
    case BorrowStatus::Borrowing: return QStringLiteral("借用中");
    case BorrowStatus::Returned:  return QStringLiteral("已归还");
    case BorrowStatus::Overdue:   return QStringLiteral("已逾期");
    case BorrowStatus::Unknown:
    default:                       return QString();
    }
}

/// 借用记录状态配色
/// @param status 借用记录状态枚举
/// @return 颜色十六进制字符串；Unknown 返回次级文本色（灰）
inline QString borrowStatusColor(BorrowStatus status)
{
    switch (status) {
    case BorrowStatus::Borrowing: return QStringLiteral("#fa8c16");
    case BorrowStatus::Returned:  return QStringLiteral("#43a047");
    case BorrowStatus::Overdue:   return QStringLiteral("#e53935");
    case BorrowStatus::Unknown:
    default:                       return QStringLiteral("#999999");
    }
}

} // namespace common
