/**
 * @file ResultDialog.h
 * @brief 流程结果对话框骨架（借用/归还/入库/出库四流程共用的步骤4外壳）
 * @author 袁燕
 * @说明 统一承载：无边框容器、结果图标（✅/❌）、大标题（绿/红）、详情槽位、
 *   可选提示行、完成按钮。详情内容差异大（数量行/清单/RichText/失败原因），
 *   由各页面通过 bodyLayout() 注入；DB写入等业务保留在页面。
 */
#pragma once
#include <QDialog>

class QVBoxLayout;
class QHBoxLayout;

class ResultDialog : public QDialog {
    Q_OBJECT
public:
    /// @param icon     结果图标（"✅"成功 / "❌"失败）
    /// @param title    大标题（如"借用成功！"）
    /// @param titleColor 标题色（成功#43a047 / 失败#e53935）
    /// @param spacing  主布局间距（借还页16，入出页14）
    explicit ResultDialog(const QString& windowTitle, const QString& icon,
                          const QString& title, const QString& titleColor,
                          int spacing, QWidget* parent = nullptr);

    /// 详情槽位（位于大标题之后、提示/按钮之前）
    QVBoxLayout* bodyLayout() const { return m_body; }

    /// 可选提示行（如借用页"请在预计归还时间前归还…"，14px灰字）
    void setHint(const QString& text);

    /// 居中完成按钮（借用页样式：绿色自定义样式，layout居中）
    void addFinishButtonCentered(const QString& text, const QString& style);
    /// 按钮行完成按钮（归还/入库/出库样式：buttonPrimary + stretch居中）
    void addFinishButtonRow(const QString& text, const QString& style, int minWidth);

private:
    QVBoxLayout* m_layout = nullptr;
    QVBoxLayout* m_body = nullptr;
};
