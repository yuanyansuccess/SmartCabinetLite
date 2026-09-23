/**
 * @file BaseDialog.h
 * @brief 通用对话框基类 — 统一圆角无边框风格
 * @author 袁燕
 * @date 2026-06-26
 * @说明 提供统一的圆角卡片式对话框容器，自动居中、无边框、ESC关闭。
 *   子类只需调用 setupCard() 获取内容布局即可添加业务控件。
 *   用法：
 *     class MyDialog : public BaseDialog {
 *         void setupContent(QVBoxLayout* contentLayout) override { ... }
 *     };
 */
#ifndef BASEDIALOG_H
#define BASEDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QFrame>
#include <QShowEvent>
#include <QWidget>
#include <QHBoxLayout>
#include <QLabel>

class BaseDialog : public QDialog {
    Q_OBJECT
public:
    /**
     * @brief 构造函数
     * @param parent 父窗口
     * @param cardWidth 卡片宽度（默认480px）
     */
    explicit BaseDialog(QWidget* parent = nullptr, int cardWidth = 480);

    /**
     * @brief 设置标题（显示在卡片顶部）
     * @param title 标题文字
     */
    void setDialogTitle(const QString& title);

    /**
     * @brief 获取卡片内容布局（子类在此添加业务控件）
     */
    QVBoxLayout* contentLayout() { return m_contentLayout; }

    /**
     * @brief 获取底部按钮布局（子类在此添加按钮）
     */
    QHBoxLayout* buttonLayout() { return m_buttonLayout; }

    /**
     * @brief 设置底部区域是否可见
     */
    void setButtonAreaVisible(bool visible);

    /**
     * @brief 获取卡片控件指针
     */
    QFrame* card() { return m_card; }

protected:
    /** 子类重写：在此方法中向 contentLayout 添加业务控件 */
    virtual void setupContent() {}

    /** 每次show时自动居中到父窗口 */
    void showEvent(QShowEvent* event) override;

    /** ESC键关闭 */
    void keyPressEvent(QKeyEvent* event) override;

private:
    void buildBaseUI(int cardWidth);

    QFrame*      m_card;
    QVBoxLayout* m_contentLayout;
    QHBoxLayout* m_buttonLayout;
    QWidget*     m_buttonArea;
    QLabel*      m_titleLabel;
};

#endif // BASEDIALOG_H
