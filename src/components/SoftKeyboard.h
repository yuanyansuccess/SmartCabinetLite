/**
 * @file SoftKeyboard.h
 * @brief 触屏软键盘：字母/数字/符号输入，将输入结果回填到指定输入框
 * @author 袁燕
 */
#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>

class SoftKeyboard : public QWidget {
    Q_OBJECT
public:
    // 键盘模式（仅保留字母/符号模式，数字已由NumKeypad独立负责）
    enum KeyMode { ModeEn = 1, ModeCn = 2 };

    explicit SoftKeyboard(QWidget* parent = nullptr);
    ~SoftKeyboard();  // 显式析构清理独立顶层窗口m_panel
    void attach(QLineEdit* target);
    void show(QLineEdit* target, const QPoint& pos);
    void show();                     // 版本B兼容：无参数show
    void hide();
    void setMode(KeyMode mode);      // 版本B兼容：设置键盘模式
    void setConfirmText(const QString& text);  // 动态确认按钮文本 (Web: confirmText)
    void setConfirmVisible(bool visible);  // 控制底部确认按钮区显隐

signals:
    void keyPressed(const QString& key);
    void enterPressed();
    void backspacePressed();
    void confirmed();         // 版本B兼容：确认信号
    void cancelled();         // 版本B兼容：取消信号
    void inputCancelled();    // 版本B兼容：输入取消信号

public:
    QString currentText() const;  // 版本B兼容：获取当前输入文本
    void triggerShake();  // 错误抖动动画

private:
    KeyMode m_keyMode = ModeEn;

    void ensurePanel();
    void setupUI();
    void rebuildKeys();
    void updateDisplay();  // 刷新显示区
    void appendChar(const QString& ch);
    void doBackspace();
    /// 遮罩点击事件拦截
    bool eventFilter(QObject* obj, QEvent* event) override;

    // ── 核心引用 ──
    QLineEdit* m_target = nullptr;
    QWidget* m_panel = nullptr;
    QWidget* m_overlay = nullptr;  // 全屏半透明遮罩，彻底杜绝穿透

    // ── 模式开关 ──
    QString m_confirmText;  // 自定义确认按钮文本 (Web: confirmText)
    bool m_pendingRebuild = false;  // 面板创建延迟时标记需重建键盘

    // ── UI组件 ──
    QLabel* m_displayLabel = nullptr;    // 输入显示区
    QLabel* m_displayCount = nullptr;    // 输入长度计数
    QWidget* m_displayArea = nullptr;    // 显示区容器
    QWidget* m_keysArea = nullptr;       // 按键区域容器
    QWidget* m_footerArea = nullptr;     // 底部确认区容器
    QVBoxLayout* m_mainLayout = nullptr;
    QPushButton* m_confirmBtn = nullptr;  // 确认按钮引用
};
