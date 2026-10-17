/**
 * @file PinyinDict.cpp
 * @brief 内置全量常用字拼音静态词库查询实现
 * @author 袁燕
 *
 * 数据由 tools/gen_pinyin_dict.py（或 gen_pinyin_dict.ps1）生成到
 * PinyinDictData.h，本文件只负责把静态表建索引并提供查询。
 * 纯静态、零依赖、编进 exe，麒麟 ARM64 交叉编译无新增依赖。
 */
#include "utils/PinyinDict.h"
#include "utils/PinyinDictData.h"
#include "utils/PinyinPhraseData.h"
#include <QMap>
#include <QSet>
#include <QRegularExpression>

namespace PinyinDict {

QStringList candidates(const QString& pinyin) {
    QString py = pinyin.trimmed().toLower();
    py.remove(QRegularExpression("[0-9]"));  // 归一：去掉声调数字（zhang1 -> zhang）
    if (py.isEmpty()) return QStringList();

    // 懒构建索引（仅一次）：汉字已按字频排序，直接按序入表
    static QMap<QString, QStringList> cache;
    if (cache.isEmpty()) {
        for (const auto& e : kPinyinEntries) {
            if (!e.py) break;
            QStringList& list = cache[e.py];
            QString hanziStr = QString::fromUtf8(e.hanzi);
            for (QChar c : hanziStr) {
                if (c.isNull()) continue;
                QString ch(c);
                if (!list.contains(ch)) list.append(ch);  // 同拼音内去重
            }
        }
    }

    auto it = cache.find(py);
    return it == cache.end() ? QStringList() : it.value();
}

QStringList phrases(const QString& pinyin) {
    QString py = pinyin.trimmed().toLower();
    py.remove(QRegularExpression("[0-9]"));
    if (py.isEmpty()) return QStringList();

    // 懒构建索引（仅一次）：整词入表，不能拆成单字
    static QMap<QString, QStringList> phraseCache;
    if (phraseCache.isEmpty()) {
        for (const auto& e : kPinyinPhraseEntries) {
            if (!e.py) break;
            QStringList& list = phraseCache[e.py];
            QString phraseStr = QString::fromUtf8(e.phrase);
            if (phraseStr.isEmpty()) continue;
            if (!list.contains(phraseStr)) list.append(phraseStr);  // 同拼音内整词去重
        }
    }

    auto it = phraseCache.find(py);
    return it == phraseCache.end() ? QStringList() : it.value();
}

bool isSurname(const QChar& hanzi) {
    static QSet<QChar> surnameSet;
    if (surnameSet.isEmpty()) {
        for (const char* s : kSurnameChars) {
            if (!s) break;
            QString ch = QString::fromUtf8(s);
            if (!ch.isEmpty()) surnameSet.insert(ch.at(0));
        }
    }
    return surnameSet.contains(hanzi);
}

}  // namespace PinyinDict
