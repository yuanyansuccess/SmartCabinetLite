/**
 * @file MessageDialog.h
 * @brief 自定义消息对话框组件（替代QMessageBox）
 * @author 袁燕
 * @date 2026-06-25
 * @说明 美观的模态对话框，支持4种类型：成功(Success)、失败(Error)、警告(Warning)、询问(Question)
 *   - 触屏友好：按钮48px，字体16px
 *   - 圆角卡片风格，绿色/红色/橙色/蓝色图标+标题
 *   - 支持单按钮(alert)和双按钮(confirm)模式
 */
#ifndef MESSAGEDIALOG_H
#define MESSAGEDIALOG_H

#include <QDialog>
#include <QString>
#include <QShowEvent>

class MessageDialog : public QDialog {
    Q_OBJECT

public:
    enum DialogType {
        Success,   // 操作成功 - 绿色
        Error,     // 操作失败 - 红色
        Warning,   // 警告提示 - 橙色
        Question   // 确认询问 - 蓝色
    };

    /**
     * @brief 显示信息/成功对话框（单按钮"确定"）
     * @param parent 父窗口
     * @param title 标题文字
     * @param message 消息内容（支持\n换行）
     */
    static void showSuccess(QWidget* parent, const QString& title, const QString& message = QString());

    /**
     * @brief 显示错误/失败对话框（单按钮"确定"）
     */
    static void showError(QWidget* parent, const QString& title, const QString& message = QString());

    /**
     * @brief 显示警告对话框（单按钮"确定"）
     */
    static void showWarning(QWidget* parent, const QString& title, const QString& message = QString());

    /**
     * @brief 显示询问/确认对话框（双按钮"确定"+"取消"）
     * @return true=用户点击确定, false=取消或关闭
     */
    static bool showQuestion(QWidget* parent, const QString& title, const QString& message = QString());

    /**
     * @brief 显示脏数据保存确认对话框（双按钮"保存"+"不保存"，无取消）
     * @param parent 父窗口
     * @param title 标题
     * @param message 消息内容
     * @param saveText "保存"按钮文字，默认"保存"
     * @param discardText "不保存"按钮文字，默认"不保存"
     * @return 0=不保存, 1=保存
     */
    static int showDirtyConfirm(QWidget* parent, const QString& title, const QString& message,
                                 const QString& saveText = QString(),
                                 const QString& discardText = QString());

protected:
    void showEvent(QShowEvent* event) override;   // [2026-06-25] 每次弹出自动居中

private:
    MessageDialog(DialogType type, QWidget* parent);
    void setupUI(DialogType type, const QString& title, const QString& message);
    static void showSingle(DialogType type, QWidget* parent, const QString& title, const QString& message);

    bool m_confirmed = false;
};

#endif // MESSAGEDIALOG_H
