/**
 * @file StatusValues.h
 * @brief 工具状态取值（状态字符串单一来源，供公共常量与状态展示层共用）
 * @author 袁燕
 */
#pragma once
#include <QString>

namespace SC {
// ── 工具状态 ──
const QString TOOL_IN_STOCK    = "in_stock";
const QString TOOL_BORROWED    = "borrowed";
const QString TOOL_MAINTENANCE = "maintenance";
const QString TOOL_CHECKED_OUT = "checked_out";  // 已出库（区别于借用）
const QString TOOL_PENDING     = "pending";      // 待入库（配置层已建未物理入库）
} // namespace SC
