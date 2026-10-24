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
#include <QShowEvent>

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
    /// 双按钮行（左=次按钮，右=主按钮），点击后关闭并记录动作标识供调用方分支
    /// @param leftText   左侧按钮文字
    /// @param leftStyle  左侧按钮样式
    /// @param rightText  右侧按钮文字
    /// @param rightStyle 右侧按钮样式
    /// @param minWidth   按钮最小宽度
    void addTwoButtonsRow(const QString& leftText, const QString& leftStyle,
                          const QString& rightText, const QString& rightStyle, int minWidth);
    /// 最近一次点击的双按钮动作（"reopen"=重新开柜整理 / "writeNow"=按当前实况写库）；单按钮完成页为空串
    QString clickedAction() const { return m_clickedAction; }
    /// 设置单按钮（完成类）按钮的动作标识：点击时写入 clickedAction，便于调用方区分"用户明确点了确认"与"直接关窗"
    /// @param action 动作标识（调用方自定义，如 "writeNow"）
    void setFinishAction(const QString& action) { m_finishAction = action; }

protected:
    /// 显示时统一居中到父窗口（兼容麒麟窗口管理器）
    void showEvent(QShowEvent* event) override;

private:
    QVBoxLayout* m_layout = nullptr;
    QVBoxLayout* m_body = nullptr;
    QString m_clickedAction;   ///< 双按钮点击动作标识（空=未使用双按钮）
    QString m_finishAction;    ///< 单按钮点击动作标识（空=点了也不记录动作）
};
