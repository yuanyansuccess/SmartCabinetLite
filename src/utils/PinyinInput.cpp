/**
 * @file PinyinInput.cpp
 * @brief 中文拼音输入引擎实现（纯逻辑，与界面解耦）
 * @author 袁燕
 */
#include "utils/PinyinInput.h"
#include "utils/PinyinDict.h"

namespace pinyin {

Engine::Engine() = default;

/**
 * @brief 输入一个拼音字母，追加到拼音缓冲并回到第一页
 * @param c 输入字符，只接受 a-z（内部统一转小写）；非字母直接忽略不报错
 */
void Engine::inputLetter(const QChar& c) {
    QChar lower = c.toLower();
    if (lower < 'a' || lower > 'z') return;  // 仅接受字母
    m_pinyin.append(lower);
    m_page = 0;  // 新输入回到第一页
}

/**
 * @brief 删掉拼音缓冲的最后一个字母，并回到第一页
 * @return true=确实删掉了一个字母；false=缓冲本来就是空的，没有可删的
 */
bool Engine::backspace() {
    if (m_pinyin.isEmpty()) return false;
    m_pinyin.chop(1);
    m_page = 0;
    return true;
}

/**
 * @brief 清空拼音缓冲并回到第一页，用于上屏或取消输入后复位
 */
void Engine::clear() {
    m_pinyin.clear();
    m_page = 0;
}

/**
 * @brief 打开或关闭姓名模式
 * @param on true=姓名模式，常见姓氏候选排到最前；false=按普通字频排序
 */
void Engine::setSurnameMode(bool on) {
    m_surname = on;
}

/**
 * @brief 查询当前是否为姓名模式
 * @return true=姓名模式；false=普通模式
 */
bool Engine::surnameMode() const {
    return m_surname;
}

/**
 * @brief 候选列表翻到上一页
 * @return true=翻页成功；false=已经在第一页，翻不动
 */
bool Engine::pageUp() {
    if (m_page <= 0) return false;
    --m_page;
    return true;
}

/**
 * @brief 候选列表翻到下一页
 * @return true=翻页成功；false=没有候选，或已经在最后一页
 */
bool Engine::pageDown() {
    int pages = pageCount(9);
    if (pages <= 0 || m_page >= pages - 1) return false;
    ++m_page;
    return true;
}

/**
 * @brief 查询当前页码
 * @return 页码，第一页为 0
 */
int Engine::currentPage() const {
    return m_page;
}

/**
 * @brief 按每页条数计算总页数，最后一页不足也按一页算
 * @param pageSize 每页显示几个候选，必须大于 0
 * @return 总页数；没有候选或传了非法页大小时返回 0
 */
int Engine::pageCount(int pageSize) const {
    int total = totalCount();
    if (total <= 0 || pageSize <= 0) return 0;
    return (total + pageSize - 1) / pageSize;
}

/**
 * @brief 查询当前拼音对应的候选总数
 * @return 候选个数；没有候选时为 0
 */
int Engine::totalCount() const {
    return buildFull().size();
}

/**
 * @brief 取出当前这一页要显示的候选
 * @param pageSize 每页显示几个候选，必须大于 0
 * @return 当前页的候选列表；当前页码超出范围时返回空列表
 */
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

/**
 * @brief 选中当前页的第几个候选，返回要上屏的汉字
 * @param indexInPage 候选在本页中的位置，从 0 开始
 * @param pageSize 每页显示几个候选，用于把页内位置换算成总列表下标
 * @return 选中的汉字或词组；下标越界时返回空串
 */
QString Engine::commit(int indexInPage, int pageSize) const {
    QStringList full = buildFull();
    int idx = m_page * pageSize + indexInPage;
    if (idx < 0 || idx >= full.size()) return QString();
    return full[idx];
}

/**
 * @brief 查询当前已输入的拼音串
 * @return 拼音字母串；还没输入时为空串
 */
QString Engine::pinyin() const {
    return m_pinyin;
}

/**
 * @brief 拼出完整候选列表：单字在前（按字频排序），词组在后
 * @return 完整候选列表；姓名模式下常见姓氏会被挪到最前面
 */
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
