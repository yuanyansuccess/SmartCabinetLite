/**
 * @file UserInputValidator.h
 * @brief 用户输入校验规则与提示文案的唯一来源 — 纯字符串处理，无UI依赖，可供页面层共享
 * @author 袁燕
 *
 * 登录页、忘记密码、首次登录强制改密、用户管理四处都要校验"工号"和"密码"，
 * 规则与提示文案集中在此，避免同一规则在多处各写一遍而产生措辞不一致。
 *
 * 两套工号规则的分工：
 * - isValidWorkNo：登录输入防呆，仅要求纯数字且长度在合理范围内，
 *   存量账户工号可能不足6位（如8888），故登录侧不做6-12限制；
 * - isValidNewWorkNo：新建账户的工号规则，6-12位纯数字（闭区间含边界）。
 */
#pragma once

#include <QString>
#include <QRegularExpression>
#include "common/Constants.h"

namespace common {

/// 判断工号输入格式是否合法（登录防呆口径）：1~32 位纯数字
/// @param workNo 待校验的工号文本（调用方应先 trim）
/// @return true=格式合法
/// @note 存量账户存在不足6位的工号，登录校验不套用新建账户的6-12规则
inline bool isValidWorkNo(const QString& workNo)
{
    static const QRegularExpression workNoPattern(QStringLiteral("^\\d{1,32}$"));
    return workNoPattern.match(workNo).hasMatch();
}

/// 判断新建账户的工号是否合法：6~12 位纯数字（闭区间含边界）
/// @param workNo 待校验的工号文本（调用方应先 trim）
/// @return true=格式合法
inline bool isValidNewWorkNo(const QString& workNo)
{
    static const QRegularExpression workNoPattern(QStringLiteral("^\\d{6,12}$"));
    return workNoPattern.match(workNo).hasMatch();
}

/// 工号输入格式非法时的提示文案（登录页使用）
/// @return 面向用户的简短提示
inline QString workNoInvalidText()
{
    return QStringLiteral("工号应为1-32位纯数字");
}

/// 新建账户工号格式非法时的提示文案（人员管理新增用户使用）
/// @return 面向用户的简短提示
inline QString newWorkNoInvalidText()
{
    return QStringLiteral("工号应为6-12位纯数字");
}

/// 密码长度不足时的提示文案
/// @return 面向用户的简短提示，长度下限取自 SC::PASSWORD_MIN_LENGTH
inline QString passwordTooShortText()
{
    return QStringLiteral("密码长度至少%1位").arg(SC::PASSWORD_MIN_LENGTH);
}

/// 密码长度超长时的提示文案
/// @return 面向用户的简短提示，长度上限取自 SC::PASSWORD_MAX_LENGTH
inline QString passwordTooLongText()
{
    return QStringLiteral("密码长度不能超过%1位").arg(SC::PASSWORD_MAX_LENGTH);
}

/// 口令规则的一行式说明（供对话框顶部提示使用）
/// @return 面向用户的简短说明，含长度区间与"不能用初始密码"两条约束
inline QString passwordRuleText()
{
    return QStringLiteral("密码长度为%1-%2位，不能用%3")
        .arg(SC::PASSWORD_MIN_LENGTH)
        .arg(SC::PASSWORD_MAX_LENGTH)
        .arg(SC::DEFAULT_INIT_PASSWORD);
}

} // namespace common