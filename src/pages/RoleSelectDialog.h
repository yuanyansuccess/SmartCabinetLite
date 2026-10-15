/**
 * @file RoleSelectDialog.h
 * @brief 登录身份选择对话框：仅"管理用户"（管理员+普通用户双重身份）登录时弹出
 * @author 袁燕
 */
#pragma once
#include "components/BaseDialog.h"
#include <QElapsedTimer>

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

    /// 屏蔽 Esc 键：本弹窗是登录分流的关键节点，误触 Esc 会被判定为"取消登录"
    /// 直接注销回登录页，操作员看起来就像弹窗一闪而过，必须显式点击按钮才能离开
    void keyPressEvent(QKeyEvent* event) override;

private:
    Choice  m_choice = Choice::Cancelled;
    QString m_realName;
    /// 弹出冷却计时：拦截登录页双击"确认"时第二击落在弹窗上的残留点击
    /// （实测残留点击发生在弹出后 185~217ms，直接误选"普通用户"进错首页）
    QElapsedTimer m_bornTimer;
};
