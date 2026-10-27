/**
 * @file DrawerOpeningDialog.h
 * @brief 抽屉/柜体打开中对话框（借用/归还/入库/出库四流程共用的步骤2骨架）
 * @author 袁燕
 * @说明 统一承载：无边框容器、标题、三点跳动动画、提示文案、取消/下一步按钮。
 *   清单表格由各页面通过 bodyLayout() 注入（各页列配置/样式存在历史差异，不做伪统一）。
 */
#pragma once
#include <QDialog>
#include <QShowEvent>

class QVBoxLayout;
class QLabel;
class QPushButton;

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

    /// 修改"下一步"按钮文案（默认"下一步 →"，页面可按场景定制，如"我已关柜，开始核对"）
    /// @note 按钮宽度按文案自适应且只增不减：长文案不会被裁切，切换文案时宽度不跳动
    void setNextButtonText(const QString& text);
    /// 动态修改提示文案（等待态切换用，如切到"正在核对柜内工具…"）
    void setDescHtml(const QString& html);
    /// 动态修改标题文案（同一页面切换状态用，如"正在打开柜体"切到"柜体已打开"）
    void setTitleText(const QString& text);
    /// 启用/禁用"下一步"按钮（等待态禁用，防止重复提交）
    void setNextEnabled(bool enabled);

    /// 切换到"失败态"：把下一步按钮从正常流程（如"我已关柜"）改为"重新核对"，
    /// 点击时发出 retryRequested 信号，由页面决定重试动作（如重新下发识别请求），
    /// 避免弹出顶层问答框打断流程。标题与提示文案同步替换为失败信息。
    /// @param title 失败态标题（如"核对失败"）
    /// @param desc 失败原因文案（人读，直接提示用户）
    /// @param retryText 下一步按钮文案（如"重新核对"）
    void showFailureState(const QString& title, const QString& desc, const QString& retryText);

signals:
    /// 失败态下用户点击"重新核对"按钮时发出，页面需 connect 到具体重试动作
    void retryRequested();

private:
    void buildSpinner();

protected:
    /// 显示时统一居中到父窗口（兼容麒麟窗口管理器）
    void showEvent(QShowEvent* event) override;

    QVBoxLayout* m_layout = nullptr;         // 主布局
    QVBoxLayout* m_body = nullptr;           // 页面内容槽位
    QLabel* m_titleLabel = nullptr;          // 标题文案（setTitleText 动态切换用）
    QLabel* m_descLabel = nullptr;           // 提示文案（setDescHtml 动态切换用）
    QPushButton* m_nextBtn = nullptr;        // 下一步按钮（文案/启用态动态控制用）
    int m_spinnerHeight = 60;
    int m_dotIndex = 0;                      // 三点动画当前高亮点
};
