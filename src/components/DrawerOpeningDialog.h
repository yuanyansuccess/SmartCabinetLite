/**
 * @file DrawerOpeningDialog.h
 * @brief 抽屉/柜体打开中对话框（借用/归还/入库/出库四流程共用的步骤2骨架）
 * @author 袁燕
 * @说明 统一承载：无边框容器、标题、三点跳动动画、提示文案、取消/下一步按钮。
 *   清单表格由各页面通过 bodyLayout() 注入（各页列配置/样式存在历史差异，不做伪统一）。
 */
#pragma once
#include <QDialog>

class QVBoxLayout;

class DrawerOpeningDialog : public QDialog {
    Q_OBJECT
public:
    /// @param windowTitle 窗口内部标题（如"抽屉打开中"）
    /// @param title       对话框显示标题（如"抽屉已打开"）
    /// @param descHtml    提示文案（支持富文本换行<br/>）
    /// @param contentTopMargin 内容区上边距（借用页历史值28，其余页32）
    /// @param spinnerHeight    动画区高度（借用页历史值48，其余页60）
    explicit DrawerOpeningDialog(const QString& windowTitle, const QString& title,
                                 const QString& descHtml,
                                 int contentTopMargin = 32, int spinnerHeight = 60,
                                 QWidget* parent = nullptr);

    /// 内容槽位（位于提示文案之后、底部按钮之前）——页面注入清单表格等
    QVBoxLayout* bodyLayout() const { return m_body; }

private:
    void buildSpinner();

    QVBoxLayout* m_layout = nullptr;         // 主布局
    QVBoxLayout* m_body = nullptr;           // 页面内容槽位
    int m_spinnerHeight = 60;
    int m_dotIndex = 0;                      // 三点动画当前高亮点
};
