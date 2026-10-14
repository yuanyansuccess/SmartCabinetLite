/**
 * @file ForgotPasswordDialog.h
 * @brief 忘记密码对话框：刷脸验证身份后自助设置新密码，未录入人脸的用户引导联系管理员
 * @author 袁燕
 */
#pragma once
#include "components/BaseDialog.h"
#include <QLineEdit>
#include <QLabel>
#include <QString>

class FaceCameraWidget;
class NumKeypad;
class QTimer;
class QStackedWidget;
class QLineEdit;
class QPushButton;

class ForgotPasswordDialog : public BaseDialog {
    Q_OBJECT
public:
    /**
     * @brief 构造忘记密码对话框
     * @param parent 父窗口
     */
    explicit ForgotPasswordDialog(QWidget* parent = nullptr);
    ~ForgotPasswordDialog();

    /// 密码是否已重置成功（exec 返回后调用，为 true 则登录页可提示"请用新密码登录"）
    bool passwordReset() const { return m_passwordReset; }

protected:
    /// 构建对话框内容（BaseDialog 钩子）
    void setupContent() override;
    /// 打开时启动摄像头
    void showEvent(QShowEvent* event) override;
    /// 关闭时释放摄像头
    void hideEvent(QHideEvent* event) override;

private slots:
    /// 轮询人脸检测状态：检测到人脸且未在比对时，取特征执行一次比对
    void onPollFace();
    /// 新密码确认提交：校验规则并写库
    void onResetSubmit();
    /// 取消：关闭对话框回到登录页
    void onCancel();

private:
    /// 构建第 1 页：身份验证（摄像头预览 + 提示）
    /// @return 页面控件
    QWidget* buildVerifyPage();
    /// 构建第 2 页：设置新密码
    /// @return 页面控件
    QWidget* buildResetPage();
    /// 构建单个密码输入行（输入框 + 安全键盘按钮）
    /// @param label 行标题
    /// @param edit  [出参] 行内输入框指针
    /// @return 整行控件
    QWidget* buildPasswordRow(const QString& label, QLineEdit*& edit);
    /// 弹出安全键盘并绑定到指定输入框
    /// @param edit 目标输入框
    void openKeypad(QLineEdit* edit);
    /// 执行一次人脸比对：成功进入设密页，失败累计次数
    void verifyOnce();
    /// 切换到设密页并显示已验证用户信息
    /// @param realName 用户姓名
    /// @param workNo 工号
    void gotoResetPage(const QString& realName, const QString& workNo);

    QStackedWidget*   m_pages = nullptr;
    FaceCameraWidget* m_camera = nullptr;
    NumKeypad*        m_keypad = nullptr;
    QTimer*           m_pollTimer = nullptr;
    QLabel*           m_captureStatus = nullptr;  // 摄像头下方状态文字
    QLabel*           m_verifyError = nullptr;   // 验证失败提示
    QLabel*           m_userLabel = nullptr;      // 已验证用户信息
    QLineEdit*        m_newPwdEdit = nullptr;
    QLineEdit*        m_confirmPwdEdit = nullptr;
    QLabel*           m_resetError = nullptr;
    bool  m_faceSeen = false;        // 本轮是否检测到人脸（由 faceDetected 置位）
    bool  m_verifying = false;       // 比对进行中，防止重复触发
    int   m_attempts = 0;            // 已失败次数，上限 SC::FACE_VERIFY_MAX_ATTEMPTS
    int   m_userId = 0;              // 验证通过的用户ID
    bool  m_passwordReset = false;   // 是否已成功重置密码
};
