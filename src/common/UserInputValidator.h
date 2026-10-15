/**
 * @file UserInputValidator.h
 * @brief 用户输入校验规则与提示文案的唯一来源 — 纯字符串处理，无UI依赖，可供页面层共享
 * @author 袁燕
 *
 * 登录页、忘记密码、首次登录强制改密、用户管理四处都要校验"工号"和"密码"，
 * 规则与提示文案集中在此，避免同一规则在多处各写一遍而产生措辞不一致。
 */
#pragma once

#include <QString>
#include <QRegularExpression>
#include "common/Constants.h"

namespace common {

/// 判断工号格式是否合法：1~32 位纯数字
/// @param workNo 待校验的工号文本（调用方应先 trim）
/// @return true=格式合法
inline bool isValidWorkNo(const QString& workNo)
{
    static const QRegularExpression workNoPattern(QStringLiteral("^\\d{1,32}$"));
    return workNoPattern.match(workNo).hasMatch();
}

/// 工号格式非法时的提示文案
/// @return 面向用户的简短提示
inline QString workNoInvalidText()
{
    return QStringLiteral("工号应为1-32位纯数字");
}

/// 密码长度不足时的提示文案
/// @return 面向用户的简短提示，长度下限取自 SC::PASSWORD_MIN_LENGTH
inline QString passwordTooShortText()
{
    return QStringLiteral("新密码至少%1位").arg(SC::PASSWORD_MIN_LENGTH);
}

/// 口令规则的一行式说明（供对话框顶部提示使用）
/// @return 面向用户的简短说明，含长度下限与"不能用初始密码"两条约束
inline QString passwordRuleText()
{
    return QStringLiteral("新密码至少%1位，不能用%2")
        .arg(SC::PASSWORD_MIN_LENGTH)
        .arg(SC::DEFAULT_INIT_PASSWORD);
}

} // namespace common