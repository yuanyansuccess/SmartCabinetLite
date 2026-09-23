/**
 * @file NumKeypad.h
 * @brief 独立数字软键盘控件 - QPainter自绘 + 顶层Popup弹窗，彻底杜绝穿透
 * @author 袁燕
 * @date 2026-06-26
 *
 * 设计原则：
 * - 继承QWidget，QPainter重绘所有按键和UI，不依赖QSS透明度
 * - 显示时创建独立顶层窗口(Qt::Popup)，定位于屏幕底部，不受父布局空间约束
 * - 全屏半透明遮罩 + Popup标志，彻底杜绝鼠标穿透
 * - 支持普通数字键盘和Fisher-Yates密码随机打乱模式
 * - ATM风格布局：789/456/123/.0⌫
 */
#pragma once

#include <QWidget>
#include <QLineEdit>

class NumKeypad : public QWidget {
    Q_OBJECT
public:
    explicit NumKeypad(QWidget* parent = nullptr);
    ~NumKeypad();

    /** 绑定目标输入框 */
    void attach(QLineEdit* target);

    /** 设置是否随机打乱键位（密码模式） */
    void setShuffle(bool enable);
    bool isShuffle() const { return m_shuffle; }

    /** 设置是否显示密码明文 */
    void setShowPassword(bool show);

    /** 获取当前输入文本 */
    QString currentText() const;

    /** 清除输入 */
    void clear();

    /** 显示键盘（创建顶层Popup窗口，定位屏幕底部） */
    void show();

    /** 隐藏键盘 */
    void hide();

signals:
    void confirmed();
    void cancelled();
    void textChanged(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    /** 确保顶层Popup面板已创建 */
    void ensurePanel();

    /** 重新计算按键矩形区域 */
    void recalcKeyRects();

    /** 获取指定行列的按键文本 */
    QString keyText(int row, int col) const;

    /** Fisher-Yates随机打乱 */
    void shuffleKeys();

    void appendChar(const QString& ch);
    void doBackspace();

    // 目标绑定
    QLineEdit* m_target = nullptr;

    // 顶层弹窗（独立于父布局，不受空间约束）
    QWidget* m_panel = nullptr;     // Qt::Popup顶层窗口（实际显示载体）
    QWidget* m_overlay = nullptr;   // 全屏半透明遮罩
    QWidget* m_ownerWidget = nullptr; // 保存原始父窗口（hide时恢复）

    // 模式
    bool m_shuffle = false;
    bool m_showPassword = false;

    // 按键数据
    QStringList m_keys;
    struct KeyRect { QRectF rect; QString text; int row; int col; };
    QVector<KeyRect> m_keyRects;

    // 交互状态
    int m_hoveredRow = -1, m_hoveredCol = -1;
    int m_pressedRow = -1, m_pressedCol = -1;

    // 外观参数
    qreal m_keyH = 56;
    qreal m_keySpacing = 8;
    qreal m_radius = 16;
    qreal m_padLeft = 16, m_padRight = 16;
    qreal m_padTop = 12, m_padBottom = 12;
    qreal m_displayH = 48;
    qreal m_footerH = 56;
    qreal m_toolbarH = 36;

    // 颜色常量
    static constexpr const char* COLOR_BG = "#ffffff";
    static constexpr const char* COLOR_TOOLBAR_BG = "#f8f9fb";
    static constexpr const char* COLOR_DISPLAY_BG = "#f5f6f8";
    static constexpr const char* COLOR_KEY_BG = "#ffffff";
    static constexpr const char* COLOR_KEY_BORDER = "#e0e0e0";
    static constexpr const char* COLOR_KEY_HOVER = "#f0f4ff";
    static constexpr const char* COLOR_KEY_PRESS = "#dce8ff";
    static constexpr const char* COLOR_KEY_TEXT = "#333333";
    static constexpr const char* COLOR_KEY_FN_BG = "#f0f2f5";
    static constexpr const char* COLOR_KEY_FN_TEXT = "#888888";
    static constexpr const char* COLOR_CONFIRM_BG = "#4da3ff";
    static constexpr const char* COLOR_CONFIRM_TEXT = "#ffffff";
    static constexpr const char* COLOR_DOT = "#4da3ff";
    static constexpr const char* COLOR_BORDER = "#e8e8e8";
};
