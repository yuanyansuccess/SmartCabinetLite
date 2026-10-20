/**
 * @file SoftKeyboard.h
 * @brief 触屏软键盘：字母/数字/符号输入 + 中文拼音输入，将输入结果回填到指定输入框
 * @author 袁燕
 */
#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVector>
#include <QStringList>
#include "utils/PinyinDict.h"
#include "utils/PinyinInput.h"

class SoftKeyboard : public QWidget {
    Q_OBJECT
public:
    // 键盘模式（英文/符号 + 中文拼音）
    enum KeyMode { ModeEn = 1, ModeCn = 2 };

    explicit SoftKeyboard(QWidget* parent = nullptr);
    ~SoftKeyboard();  // 显式析构清理独立顶层窗口m_panel
    void attach(QLineEdit* target);
    void show(QLineEdit* target, const QPoint& pos);
    void show();                     // 版本B兼容：无参数show
    void hide();
    void setMode(KeyMode mode);      // 切换键盘模式（英文/中文）
    void setConfirmText(const QString& text);  // 动态确认按钮文本 (Web: confirmText)
    void setSurnameMode(bool on);    // 姓名输入：开启姓氏候选前置

signals:
    void keyPressed(const QString& key);
    void enterPressed();
    void backspacePressed();
    void confirmed();         // 版本B兼容：确认信号
    void cancelled();         // 版本B兼容：取消信号
    void inputCancelled();    // 版本B兼容：输入取消信号

public:
    QString currentText() const;  // 版本B兼容：获取当前输入文本

private slots:
    void onPrevPage();     // 候选上一页
    void onNextPage();     // 候选下一页

private:
    KeyMode m_keyMode = ModeEn;

    void ensurePanel();
    void setupUI();
    void rebuildKeys();
    void updateDisplay();  // 刷新显示区
    void appendChar(const QString& ch);
    void doBackspace();

    // ── 中文拼音输入（逻辑交由 PinyinInput 引擎，本类只负责渲染）──
    pinyin::Engine m_cn;                      // 拼音输入引擎（纯逻辑，与界面解耦）
    void appendPinyin(const QString& ch);     // 追加一个拼音字母（交给引擎维护缓冲与页码）
    void updateCandidates();                  // 刷新候选条（含翻页与词组联想）
    void commitCandidate(int idx);            // 上屏当前页第 idx 个候选（0基，可为单字或词组）
    void backspaceCn();                       // 中文退格：先删拼音，再删已上屏字

    /// 遮罩点击事件拦截
    bool eventFilter(QObject* obj, QEvent* event) override;

    // ── 核心引用 ──
    QLineEdit* m_target = nullptr;
    QWidget* m_panel = nullptr;
    QWidget* m_overlay = nullptr;  // 全屏半透明遮罩，彻底杜绝穿透

    // ── 模式开关 ──
    QString m_confirmText;  // 自定义确认按钮文本 (Web: confirmText)

    // ── UI组件 ──
    QLabel* m_displayLabel = nullptr;    // 输入显示区
    QLabel* m_displayCount = nullptr;    // 输入长度计数
    QWidget* m_displayArea = nullptr;    // 显示区容器
    QWidget* m_keysArea = nullptr;       // 按键区域容器
    QWidget* m_footerArea = nullptr;     // 底部确认区容器
    QVBoxLayout* m_mainLayout = nullptr;
    QPushButton* m_confirmBtn = nullptr;  // 确认按钮引用

    // ── 中文候选条 ──
    QWidget* m_candBar = nullptr;        // 候选条容器
    QLabel* m_pinyinLabel = nullptr;     // 当前拼音/提示/页码
    QVector<QPushButton*> m_candBtns;    // 候选按钮（最多9个）
    QPushButton* m_prevPageBtn = nullptr;  // 候选上一页
    QPushButton* m_nextPageBtn = nullptr;  // 候选下一页
};
