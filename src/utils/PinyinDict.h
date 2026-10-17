/**
 * @file PinyinDict.h
 * @brief 内置全量拼音静态词库（汉字 + 词组，零依赖、编进 exe）
 * @author 袁燕
 *
 * 设计：
 *   - 候选汉字按真实字频/常用度排序（生成脚本内置 FREQ_SEED 频率序，生僻字沉底），
 *     保证 "li"->"里" 等常用字排在候选首位。
 *   - 另含【词组联想】表（PinyinPhraseData.h，拼音->常用词组）与【姓氏】表
 *     （kSurnameChars，百家姓），支撑中文软键盘的联想与姓名输入加权。
 *   - 数据由 tools/gen_pinyin_dict.py 生成 PinyinDictData.h / PinyinPhraseData.h
 *     （纯数据，不入库版本控制），本文件只负责查询逻辑。
 *   - 纯 C++/Qt 静态表，不读外部文件、不联网、不引入第三方库，
 *     麒麟 ARM64 交叉编译零新增依赖风险。
 */
#pragma once

#include <QString>
#include <QStringList>

namespace PinyinDict {

/**
 * @brief 查询某全拼对应的候选汉字（按字频/常用度排序，生僻字沉底）
 * @param pinyin 全拼，不区分大小写，可带可不带声调数字（自动归一为小写无声调）
 * @return 候选汉字列表；无匹配返回空列表
 * @example candidates("zhang") -> ["张","章","彰",...]
 */
QStringList candidates(const QString& pinyin);

/**
 * @brief 查询某全拼对应的候选词组（词组联想，拼音由生成脚本自动推导）
 * @param pinyin 词组全拼，小写无声调（如 "zhongguo"）
 * @return 候选词组列表；无匹配返回空列表
 * @example phrases("zhongguo") -> ["中国"]
 */
QStringList phrases(const QString& pinyin);

/**
 * @brief 判断某汉字是否为常见姓氏（百家姓），用于姓名输入时把姓氏候选前置
 * @param hanzi 单个汉字
 * @return true 表示该字是常见姓氏
 */
bool isSurname(const QChar& hanzi);

}  // namespace PinyinDict
