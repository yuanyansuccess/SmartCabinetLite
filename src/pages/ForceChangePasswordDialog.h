/**
 * @file ForceChangePasswordDialog.h
 * @brief 首次登录强制修改密码对话框：初始密码登录后必须自设新密码才能进入系统
 * @author 袁燕
 */
#pragma once
#include "components/BaseDialog.h"
#include <QLineEdit>
#include <QLabel>

class NumKeypad;
class QPushButton;
class QVBoxLayout;

class ForceChangePasswordDialog : public BaseDialog {
    Q_OBJECT
public:
    /**
     * @brief 构造强制改密对话框
     * @param userId 待改密用户ID
     * @param realName 用户姓名（用于欢迎文案）
     * @param parent 父窗口
     * @param requireOldPassword true=要求填写原口令（用户主动改密场景）；
     *        false=首次登录强制改密，不显示原口令输入框（默认）
     */
    explicit ForceChangePasswordDialog(int userId, const QString& realName,
                                       bool requireOldPassword = false, QWidget* parent = nullptr);
    ~ForceChangePasswordDialog();

protected:
    /// 构建对话框内容（BaseDialog 钩子，show 前自动调用）
    void setupContent() override;

private slots:
    /// 确认修改：校验规则并写库，成功后方可进入系统
    void onSubmit();
    /// 退出登录：放弃本次登录，回到登录页
    void onExitLogin();

private:
    /// 构建单个密码输入行（输入框 + 安全键盘按钮）
    /// @param label 行标题（当前密码/新密码/确认新密码）
    /// @param edit  [出参] 行内输入框指针，供提交时读取
    /// @return 整行控件，调用方加入布局
    QWidget* buildPasswordRow(const QString& label, QLineEdit*& edit);
    /// 弹出安全键盘并绑定到指定输入框
    /// @param edit 目标输入框
    void openKeypad(QLineEdit* edit);

    int         m_userId = 0;
    QString     m_realName;
    /// 是否要求填写原口令：false=首次登录强制改密（不显示原口令框）
    bool        m_requireOldPassword = false;
    QLineEdit*  m_oldPwdEdit = nullptr;
    QLineEdit*  m_newPwdEdit = nullptr;
    QLineEdit*  m_confirmPwdEdit = nullptr;
    QLabel*     m_errorLabel = nullptr;
    NumKeypad*  m_keypad = nullptr;
};
