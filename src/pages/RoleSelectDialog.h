/**
 * @file RoleSelectDialog.h
 * @brief 登录身份选择对话框：仅"管理用户"（管理员+普通用户双重身份）登录时弹出
 * @author 袁燕
 */
#pragma once
#include "components/BaseDialog.h"

class QPushButton;

class RoleSelectDialog : public BaseDialog {
    Q_OBJECT
public:
    /// 用户选择结果：以管理员身份进入 / 以普通用户身份进入 / 取消（退出登录）
    enum class Choice { Admin, User, Cancelled };

    /**
     * @brief 构造身份选择对话框
     * @param realName 用户姓名（标题欢迎语用）
     * @param parent 父窗口
     */
    explicit RoleSelectDialog(const QString& realName, QWidget* parent = nullptr);
    ~RoleSelectDialog();

    /// 读取用户选择（exec 返回后调用）
    Choice choice() const { return m_choice; }

protected:
    /// 构建对话框内容（BaseDialog 钩子）
    void setupContent() override;

private:
    Choice  m_choice = Choice::Cancelled;
    QString m_realName;
};
