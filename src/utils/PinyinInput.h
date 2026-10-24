/**
 * @file PinyinInput.h
 * @brief 中文拼音输入引擎（纯逻辑，与界面解耦）
 * @author 袁燕
 *
 * 职责：维护拼音缓冲、翻页、姓氏加权、词组联想，对外只输出"当前页候选"
 * 与"上屏文本"。界面层（SoftKeyboard）只负责渲染与转发按键事件，不再内嵌
 * 拼音状态机，从而降低耦合。
 *
 * 候选构成（同一拼音串下）：单字候选（姓名模式时姓氏前置）+ 词组候选
 * （词组联想）。翻页在界面层按 pageSize 切分。
 */
#pragma once

#include <QString>
#include <QStringList>

namespace pinyin {

class Engine {
public:
    Engine();

    /// 输入一个拼音字母（a-z），清空页码（开始新的一页输入）
    void inputLetter(const QChar& c);
    /// 删除缓冲中最后一个拼音字母；true 表示确实删掉了一个字母，false 表示缓冲本来就是空的
    bool backspace();
    /// 清空拼音缓冲与页码
    void clear();
    /// 姓名输入模式：开启后把常见姓氏候选前置
    void setSurnameMode(bool on);
    /// 当前是否姓名模式
    bool surnameMode() const;

    /// 上一页；返回是否发生翻页
    bool pageUp();
    /// 下一页；返回是否发生翻页
    bool pageDown();
    /// 当前页码（0 基）
    int currentPage() const;
    /// 总页数（候选总数按 pageSize 计）
    int pageCount(int pageSize) const;
    /// 候选总数（单字 + 词组）
    int totalCount() const;
    /// 当前页候选（单字或词组混合），最多 pageSize 个
    QStringList candidatesForPage(int pageSize) const;
    /// 取当前页第 indexInPage 个候选（0 基），返回待上屏文本（单字或整词）；越界返回空
    QString commit(int indexInPage, int pageSize) const;

    /// 当前拼音缓冲（小写无声调）
    QString pinyin() const;

private:
    /// 组合候选：单字（姓氏前置）+ 词组
    QStringList buildFull() const;

    QString m_pinyin;   // 当前拼音缓冲
    int m_page = 0;     // 当前页码
    bool m_surname = false;  // 姓名模式
};

}  // namespace pinyin
