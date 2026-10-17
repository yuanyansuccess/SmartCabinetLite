/**
 * @file PinyinInput.cpp
 * @brief 中文拼音输入引擎实现（纯逻辑，与界面解耦）
 * @author 袁燕
 */
#include "utils/PinyinInput.h"
#include "utils/PinyinDict.h"

namespace pinyin {

Engine::Engine() = default;

void Engine::inputLetter(const QChar& c) {
    QChar lower = c.toLower();
    if (lower < 'a' || lower > 'z') return;  // 仅接受字母
    m_pinyin.append(lower);
    m_page = 0;  // 新输入回到第一页
}

bool Engine::backspace() {
    if (m_pinyin.isEmpty()) return false;
    m_pinyin.chop(1);
    m_page = 0;
    return true;
}

void Engine::clear() {
    m_pinyin.clear();
    m_page = 0;
}

void Engine::setSurnameMode(bool on) {
    m_surname = on;
}

bool Engine::surnameMode() const {
    return m_surname;
}

bool Engine::pageUp() {
    if (m_page <= 0) return false;
    --m_page;
    return true;
}

bool Engine::pageDown() {
    int pages = pageCount(9);
    if (pages <= 0 || m_page >= pages - 1) return false;
    ++m_page;
    return true;
}

int Engine::currentPage() const {
    return m_page;
}

int Engine::pageCount(int pageSize) const {
    int total = totalCount();
    if (total <= 0 || pageSize <= 0) return 0;
    return (total + pageSize - 1) / pageSize;
}

int Engine::totalCount() const {
    return buildFull().size();
}

QStringList Engine::candidatesForPage(int pageSize) const {
    QStringList full = buildFull();
    int start = m_page * pageSize;
    if (start < 0 || start >= full.size()) return QStringList();
    int count = full.size() - start;
    if (count > pageSize) count = pageSize;
    QStringList result;
    for (int i = start; i < start + count; ++i) result.append(full[i]);
    return result;
}

QString Engine::commit(int indexInPage, int pageSize) const {
    QStringList full = buildFull();
    int idx = m_page * pageSize + indexInPage;
    if (idx < 0 || idx >= full.size()) return QString();
    return full[idx];
}

QString Engine::pinyin() const {
    return m_pinyin;
}

QStringList Engine::buildFull() const {
    // 单字候选（已按字频排序）
    QStringList chars = PinyinDict::candidates(m_pinyin);

    // 姓名模式：把常见姓氏候选前置，其余保持原序
    if (m_surname && !chars.isEmpty()) {
        QStringList sur;
        QStringList other;
        for (const QString& s : chars) {
            if (s.size() == 1 && PinyinDict::isSurname(s.at(0))) {
                sur.append(s);
            } else {
                other.append(s);
            }
        }
        chars = sur + other;
    }

    // 词组联想（多音节时才有结果，单音节为空）
    QStringList phraseList = PinyinDict::phrases(m_pinyin);

    return chars + phraseList;
}

}  // namespace pinyin
