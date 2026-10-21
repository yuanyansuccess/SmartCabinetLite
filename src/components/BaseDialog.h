/**
 * @file BaseDialog.h
 * @brief 通用对话框基类 — 统一圆角无边框风格
 * @author 袁燕
 * @date 2026-06-26
 * @说明 提供统一的圆角卡片式对话框容器，自动居中、无边框、ESC关闭。
 *   子类有两种用法：
 *     1) 重写 setupContent()，并在子类构造函数中显式调用一次 setupContent()；
 *     2) 不重写，直接在构造函数里往 contentLayout() / buttonLayout() 添加控件。
 *   ⚠ 基类不会自动调用 setupContent()（虚函数在基类构造期间无法派发到子类），
 *     漏调用将得到一个只有标题栏的空白对话框。
 *   用法：
 *     class MyDialog : public BaseDialog {
 *     public:
 *         explicit MyDialog(QWidget* p) : BaseDialog(p, 520) { setupContent(); }
 *     protected:
 *         void setupContent() override { ... }   // 往 contentLayout() 添加控件
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

    /**
     * @brief 将对话框居中到父窗口并钳制在其所在屏幕可用区域内
     * @param dialog 待定位的顶层对话框
     * @note 供同风格对话框复用；对话框超出屏幕时锚定左上角，保证标题可见
     */
    static void centerOnParent(QWidget* dialog);

    /**
     * @brief 显示时居中父窗口，并在窗口映射稳定后按多个时间点重新校准位置
     * @param dialog 待定位的顶层对话框
     *
     * 窗口管理器（尤其麒麟）常在首次映射后重排窗口或延迟定型布局，
     * 仅在 showEvent 中居中一次会在反复打开时越偏越远、底部按钮被屏幕边缘裁掉。
     * 此处先立即居中，再按若干时间点在窗口仍可见时重复居中，保证任何平台都准确。
     * 所有需要居中的弹窗统一调用本方法，避免在多处各写一套居中逻辑。
     */
    static void centerAndKeep(QWidget* dialog);

    /**
     * @brief 显示与隐藏切换：首次显示前先按布局尺寸预约居中位置
     * @param visible true=显示，false=隐藏
     *
     * 窗口管理器只在窗口首次映射时采纳程序指定的位置。若等到 showEvent（映射过程中）
     * 才移动窗口，部分窗口管理器会改按级联规则把窗口放到父窗口右下角，
     * 表现为反复打开时弹窗越偏越远、底部按钮被屏幕边缘裁掉。
     */
    void setVisible(bool visible) override;

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
