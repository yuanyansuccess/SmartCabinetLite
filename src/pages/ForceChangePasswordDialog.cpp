/**
 * @file ForceChangePasswordDialog.cpp
 * @brief 首次登录强制修改密码对话框实现
 * @author 袁燕
 */
#include "ForceChangePasswordDialog.h"
#include "components/NumKeypad.h"
#include "components/MessageDialog.h"
#include "controller/UserController.h"
#include "common/Constants.h"
#include "common/UserInputValidator.h"  // 密码规则与提示文案
#include "utils/StyleHelper.h"
#include "utils/LayoutHelper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QPushButton>

/**
 * @brief 构造函数，初始化 ForceChangePasswordDialog 对象并连接信号槽
 * @param userId 用户唯一标识 user_id
 * @param realName 用户真实姓名
 * @param requireOldPassword 是否要求先校验旧密码
 * @param parent 父对象指针，作为 Qt 对象树父节点接管所有权
 */
ForceChangePasswordDialog::ForceChangePasswordDialog(int userId, const QString& realName,
                                                     bool requireOldPassword, QWidget* parent)
    : BaseDialog(parent, 520)
    , m_userId(userId)
    , m_realName(realName)
    , m_requireOldPassword(requireOldPassword) {
    setDialogTitle(QStringLiteral("修改初始密码"));
    setMinimumHeight(m_requireOldPassword ? 460 : 400);
    setupContent();  // 基类不自动调用，需子类显式构建内容
}

ForceChangePasswordDialog::~ForceChangePasswordDialog() = default;

/**
 * @brief 构建对话框内容：说明文案、密码输入行、规则提示、安全键盘与底部按钮
 */
void ForceChangePasswordDialog::setupContent() {
    auto* cl = contentLayout();
    cl->setSpacing(12);

    // 顶部说明：告知用户为什么必须改密
    auto* hint = new QLabel(m_requireOldPassword
        ? QStringLiteral("%1，请修改登录密码").arg(m_realName)
        : QStringLiteral("%1，请设置新密码").arg(m_realName));
    hint->setWordWrap(true);
    hint->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontLabel,
                                               StyleHelper::textBody(), 600));
    cl->addWidget(hint);

    m_oldPwdEdit = nullptr;
    m_newPwdEdit = nullptr;
    m_confirmPwdEdit = nullptr;

    // 首次登录强制改密时不显示原口令框：初始口令是公开默认值，校验它没有意义
    if (m_requireOldPassword) {
        cl->addWidget(buildPasswordRow(QStringLiteral("当前密码"), m_oldPwdEdit));
    }
    cl->addWidget(buildPasswordRow(QStringLiteral("新密码"), m_newPwdEdit));
    cl->addWidget(buildPasswordRow(QStringLiteral("确认新密码"), m_confirmPwdEdit));

    // 规则提示：一行说清，规则与文案统一取自 common::UserInputValidator
    auto* rule = new QLabel(common::passwordRuleText());
    rule->setStyleSheet("" + StyleHelper::fontSize(StyleHelper::Token::FontSmall) + ";color:#999;background:transparent;");
    cl->addWidget(rule);

    // 内联错误提示（提交时显示具体原因）
    m_errorLabel = new QLabel();
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setVisible(false);
    m_errorLabel->setStyleSheet("" + StyleHelper::fontSize(StyleHelper::Token::FontBody) + ";color:#e74c3c;background:transparent;padding:4px 0;");
    cl->addWidget(m_errorLabel);

    // 安全键盘：随机键位，隐藏明文
    m_keypad = new NumKeypad(this);
    m_keypad->setShuffle(true);
    m_keypad->setShowPassword(false);
    connect(m_keypad, &NumKeypad::confirmed, this, [this]() { m_keypad->hide(); });
    connect(m_keypad, &NumKeypad::cancelled, this, [this]() { m_keypad->hide(); });

    // 底部按钮
    auto* bl = buttonLayout();
    LayoutHelper::clearItems(bl);
    bl->addStretch();

    auto* exitBtn = new QPushButton(QStringLiteral("退出登录"));
    exitBtn->setMinimumHeight(StyleHelper::Token::ControlHeightTouch);
    exitBtn->setMinimumWidth(120);
    exitBtn->setCursor(Qt::PointingHandCursor);
    exitBtn->setStyleSheet(
        "QPushButton{background:#fff;color:#666;border:2px solid #ddd;border-radius:12px;"
        "" + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + ";font-weight:600;}"
        "QPushButton:hover{background:#f5f5f5;}");
    connect(exitBtn, &QPushButton::clicked, this, &ForceChangePasswordDialog::onExitLogin);

    auto* okBtn = new QPushButton(QStringLiteral("确认修改"));
    okBtn->setMinimumHeight(StyleHelper::Token::ControlHeightTouch);
    okBtn->setMinimumWidth(120);
    okBtn->setCursor(Qt::PointingHandCursor);
    okBtn->setStyleSheet(
        "QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:12px;"
        "" + StyleHelper::fontSize(StyleHelper::Token::FontInput) + ";font-weight:700;}"
        "QPushButton:hover{background:#3d8ae0;}");
    connect(okBtn, &QPushButton::clicked, this, &ForceChangePasswordDialog::onSubmit);

    bl->addWidget(exitBtn);
    bl->addWidget(okBtn);
}

/**
 * @brief 构建一行密码输入控件（标签 + 只读输入框 + 键盘按钮）
 * @param label 行标题（当前密码/新密码/确认新密码）
 * @param edit  [出参] 行内输入框指针，供提交时读取文本
 * @return 整行容器控件
 */
QWidget* ForceChangePasswordDialog::buildPasswordRow(const QString& label, QLineEdit*& edit) {
    auto* row = new QWidget();
    row->setStyleSheet("background:transparent;");
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(12);

    auto* lbl = new QLabel(label);
    lbl->setStyleSheet(StyleHelper::labelText());
    lbl->setFixedWidth(88);
    lbl->setMinimumHeight(StyleHelper::Token::ControlHeight);
    lbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto* frame = new QFrame();
    frame->setAttribute(Qt::WA_StyledBackground, true);
    frame->setStyleSheet(
        "QFrame{border:2px solid #e0e0e0;border-radius:10px;background:#fff;min-height:44px;}");
    auto* fl = new QHBoxLayout(frame);
    fl->setContentsMargins(14, 0, 4, 0);
    fl->setSpacing(0);

    edit = new QLineEdit();
    edit->setEchoMode(QLineEdit::Password);
    edit->setReadOnly(true);  // 只允许安全键盘输入，防止物理键盘窥视
    edit->setCursor(Qt::PointingHandCursor);
    edit->setStyleSheet(
        "QLineEdit{border:none;padding:0;" + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + ";color:#1a1a2e;background:transparent;}");
    fl->addWidget(edit, 1);

    auto* kbBtn = new QPushButton(QStringLiteral("⌨"));
    kbBtn->setFixedSize(40, 40);
    kbBtn->setCursor(Qt::PointingHandCursor);
    kbBtn->setStyleSheet(
        "QPushButton{border:none;border-radius:10px;" + StyleHelper::fontSize(StyleHelper::Token::FontHeading) + ";background:transparent;color:#666;}"
        "QPushButton:hover{background:#e6f0ff;color:#4da3ff;}");
    connect(kbBtn, &QPushButton::clicked, this, [this, edit]() { openKeypad(edit); });
    fl->addWidget(kbBtn);

    rl->addWidget(lbl);
    rl->addWidget(frame, 1);
    return row;
}

/**
 * @brief 弹出安全键盘并绑定到指定输入框
 * @param edit 目标输入框
 */
void ForceChangePasswordDialog::openKeypad(QLineEdit* edit) {
    if (!m_keypad || !edit) return;
    m_keypad->setShuffle(true);
    m_keypad->setShowPassword(false);
    m_keypad->attach(edit);
    m_keypad->show();
}

/**
 * @brief 提交：校验密码规则并写库，成功则关闭对话框进入系统
 */
void ForceChangePasswordDialog::onSubmit() {
    const QString oldPwd = m_oldPwdEdit ? m_oldPwdEdit->text() : QString();
    const QString newPwd = m_newPwdEdit ? m_newPwdEdit->text() : QString();
    const QString confirmPwd = m_confirmPwdEdit ? m_confirmPwdEdit->text() : QString();

    auto showError = [this](const QString& msg) {
        if (!m_errorLabel) return;
        m_errorLabel->setText(msg);
        m_errorLabel->setVisible(true);
    };

    // 仅"要求原口令"模式才校验原口令输入
    if (m_requireOldPassword && oldPwd.isEmpty()) {
        showError(QStringLiteral("请输入当前密码"));
        return;
    }
    if (newPwd.length() < SC::PASSWORD_MIN_LENGTH) {
        showError(common::passwordTooShortText());
        return;
    }
    if (newPwd.length() > SC::PASSWORD_MAX_LENGTH) {
        showError(common::passwordTooLongText());
        return;
    }
    if (newPwd == SC::DEFAULT_INIT_PASSWORD) {
        showError(QStringLiteral("不能使用初始密码"));
        return;
    }
    if (newPwd != confirmPwd) { showError(QStringLiteral("两次输入不一致")); return; }

    if (m_errorLabel) m_errorLabel->setVisible(false);

    UserController ctrl;
    // 首次登录强制改密走"免原口令"接口；主动改密走带原口令校验的接口
    const bool ok = m_requireOldPassword
                  ? ctrl.changePassword(m_userId, oldPwd, newPwd)
                  : ctrl.forceChangePassword(m_userId, newPwd);
    if (!ok) {
        // 规则已在前端校验，此处失败只可能是原口令不正确（仅主动改密模式可能出现）
        showError(m_requireOldPassword ? QStringLiteral("当前密码不正确，请重新输入")
                                       : QStringLiteral("密码修改失败，请稍后重试"));
        return;
    }
    MessageDialog::showSuccess(this, QStringLiteral("修改成功"),
                               QStringLiteral("请用新密码登录"));
    accept();
}

/**
 * @brief 退出登录：拒绝对话框，由主窗口收到 reject 后执行注销回到登录页
 */
void ForceChangePasswordDialog::onExitLogin() {
    // 拒绝登录：由主窗口收到 reject 后执行注销回到登录页
    reject();
}
