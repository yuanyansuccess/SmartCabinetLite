/**
 * @file ForgotPasswordDialog.cpp
 * @brief 忘记密码对话框实现（刷脸验证 → 设置新密码）
 * @author 袁燕
 */
#include "ForgotPasswordDialog.h"
#include "components/FaceCameraWidget.h"
#include "components/NumKeypad.h"
#include "components/MessageDialog.h"
#include "services/FaceRecognitionService.h"
#include "controller/UserController.h"
#include "common/Constants.h"
#include "common/UserInputValidator.h"  // 密码规则与提示文案
#include "utils/StyleHelper.h"
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QPushButton>
#include <QShowEvent>
#include <QHideEvent>
#include <QDebug>

// 人脸检测信号每帧触发，这里按固定间隔取一次特征做比对
static const int kFacePollIntervalMs = 1000;

/**
 * @brief 构造函数，初始化 ForgotPasswordDialog 对象并连接信号槽
 * @param parent 父对象指针，作为 Qt 对象树父节点接管所有权
 */
ForgotPasswordDialog::ForgotPasswordDialog(QWidget* parent)
    : BaseDialog(parent, 560) {
    setDialogTitle(QStringLiteral("忘记密码"));
    setupContent();  // 基类不自动调用，需子类显式构建内容
}

ForgotPasswordDialog::~ForgotPasswordDialog() {
    if (m_camera) m_camera->stopCamera();
}

/**
 * @brief 构建对话框内容：验证页与设密页、安全键盘、人脸轮询定时器与底部按钮
 */
void ForgotPasswordDialog::setupContent() {
    auto* cl = contentLayout();
    cl->setSpacing(12);

    m_pages = new QStackedWidget();
    m_pages->addWidget(buildVerifyPage());
    m_pages->addWidget(buildResetPage());
    cl->addWidget(m_pages);

    // 安全键盘：随机键位、不显示明文
    m_keypad = new NumKeypad(this);
    m_keypad->setShuffle(true);
    m_keypad->setShowPassword(false);
    connect(m_keypad, &NumKeypad::confirmed, this, [this]() { m_keypad->hide(); });
    connect(m_keypad, &NumKeypad::cancelled, this, [this]() { m_keypad->hide(); });

    // 人脸轮询定时器：检测到人脸后按间隔取特征比对
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kFacePollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &ForgotPasswordDialog::onPollFace);

    // 底部按钮：两页共用（验证页为取消，设密页为确认重置）
    auto* bl = buttonLayout();
    while (bl->count() > 0) {
        QLayoutItem* item = bl->takeAt(0);
        delete item;
    }
    bl->addStretch();
}

/**
 * @brief 构建身份验证页：摄像头预览 + 状态提示 + 取消按钮
 * @return 页面控件
 */
QWidget* ForgotPasswordDialog::buildVerifyPage() {
    auto* page = new QWidget();
    auto* vl = new QVBoxLayout(page);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(12);
    vl->setAlignment(Qt::AlignHCenter);

    m_camera = new FaceCameraWidget();
    m_camera->setFixedSize(300, 300);
    vl->addWidget(m_camera, 0, Qt::AlignCenter);
    connect(m_camera, &FaceCameraWidget::faceDetected, this, [this]() { m_faceSeen = true; });

    m_captureStatus = new QLabel(QStringLiteral("请面对摄像头完成身份验证"));
    m_captureStatus->setAlignment(Qt::AlignCenter);
    m_captureStatus->setMinimumHeight(StyleHelper::Token::ControlHeight);
    m_captureStatus->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontBody, StyleHelper::textSecondary()));
    vl->addWidget(m_captureStatus);

    m_verifyError = new QLabel();
    m_verifyError->setAlignment(Qt::AlignCenter);
    m_verifyError->setWordWrap(true);
    m_verifyError->setVisible(false);
    m_verifyError->setStyleSheet("" + StyleHelper::fontSize(StyleHelper::Token::FontBody) + ";color:#e74c3c;background:transparent;");
    vl->addWidget(m_verifyError);

    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    cancelBtn->setMinimumHeight(StyleHelper::Token::ControlHeightTouch);
    cancelBtn->setMinimumWidth(140);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setStyleSheet(
        "QPushButton{background:#fff;color:#666;border:2px solid #ddd;border-radius:12px;"
        "" + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + ";font-weight:600;}"
        "QPushButton:hover{background:#f5f5f5;}");
    connect(cancelBtn, &QPushButton::clicked, this, &ForgotPasswordDialog::onCancel);
    vl->addWidget(cancelBtn);

    return page;
}

/**
 * @brief 构建设置新密码页：已验证用户信息 + 两个密码输入行 + 规则提示 + 确认/取消
 * @return 页面控件
 */
QWidget* ForgotPasswordDialog::buildResetPage() {
    auto* page = new QWidget();
    auto* vl = new QVBoxLayout(page);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(12);

    m_userLabel = new QLabel();
    m_userLabel->setAlignment(Qt::AlignCenter);
    m_userLabel->setStyleSheet(
        "font-size:17px;font-weight:700;color:#389e0d;background:#f6ffed;"
        "border:1px solid #b7eb8f;border-radius:10px;padding:10px;");
    vl->addWidget(m_userLabel);

    vl->addWidget(buildPasswordRow(QStringLiteral("新密码"), m_newPwdEdit));
    vl->addWidget(buildPasswordRow(QStringLiteral("确认密码"), m_confirmPwdEdit));

    auto* rule = new QLabel(common::passwordRuleText());
    rule->setStyleSheet("" + StyleHelper::fontSize(StyleHelper::Token::FontSmall) + ";color:#999;background:transparent;");
    vl->addWidget(rule);

    m_resetError = new QLabel();
    m_resetError->setWordWrap(true);
    m_resetError->setVisible(false);
    m_resetError->setStyleSheet("" + StyleHelper::fontSize(StyleHelper::Token::FontBody) + ";color:#e74c3c;background:transparent;padding:4px 0;");
    vl->addWidget(m_resetError);

    auto* okBtn = new QPushButton(QStringLiteral("确认重置密码"));
    okBtn->setMinimumHeight(StyleHelper::Token::ControlHeightTouch);
    okBtn->setCursor(Qt::PointingHandCursor);
    okBtn->setStyleSheet(
        "QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:12px;"
        "" + StyleHelper::fontSize(StyleHelper::Token::FontInput) + ";font-weight:700;}"
        "QPushButton:hover{background:#3d8ae0;}");
    connect(okBtn, &QPushButton::clicked, this, &ForgotPasswordDialog::onResetSubmit);
    vl->addWidget(okBtn);

    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    cancelBtn->setMinimumHeight(StyleHelper::Token::ControlHeightTouch);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setStyleSheet(
        "QPushButton{background:#fff;color:#666;border:2px solid #ddd;border-radius:12px;"
        "" + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + ";font-weight:600;}"
        "QPushButton:hover{background:#f5f5f5;}");
    connect(cancelBtn, &QPushButton::clicked, this, &ForgotPasswordDialog::onCancel);
    vl->addWidget(cancelBtn);

    return page;
}

/**
 * @brief 构建一行密码输入控件（标签 + 只读输入框 + 键盘按钮）
 * @param label 行标题
 * @param edit  [出参] 行内输入框指针
 * @return 整行容器控件
 */
QWidget* ForgotPasswordDialog::buildPasswordRow(const QString& label, QLineEdit*& edit) {
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
    edit->setReadOnly(true);
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
void ForgotPasswordDialog::openKeypad(QLineEdit* edit) {
    if (!m_keypad || !edit) return;
    m_keypad->setShuffle(true);
    m_keypad->setShowPassword(false);
    m_keypad->attach(edit);
    m_keypad->show();
}

/**
 * @brief 对话框显示时启动摄像头与轮询定时器
 * @param event 显示事件
 */
void ForgotPasswordDialog::showEvent(QShowEvent* event) {
    BaseDialog::showEvent(event);
    if (m_pollTimer && !m_pollTimer->isActive()) m_pollTimer->start();
    if (m_camera) m_camera->startCamera();
}

/**
 * @brief 对话框隐藏时停止摄像头与轮询定时器，释放资源
 * @param event 隐藏事件
 */
void ForgotPasswordDialog::hideEvent(QHideEvent* event) {
    if (m_pollTimer) m_pollTimer->stop();
    if (m_camera) m_camera->stopCamera();
    BaseDialog::hideEvent(event);
}

/**
 * @brief 轮询人脸检测：检测到人脸且未在比对时，取一次特征执行比对
 */
void ForgotPasswordDialog::onPollFace() {
    if (m_verifying || !m_faceSeen) return;
    if (m_pages && m_pages->currentIndex() != 0) return;
    verifyOnce();
}

/**
 * @brief 执行一次人脸比对：成功进入设密页，失败累计次数并提示
 */
void ForgotPasswordDialog::verifyOnce() {
    if (!m_camera) return;
    m_verifying = true;
    m_faceSeen = false;
    if (m_captureStatus) m_captureStatus->setText(QStringLiteral("正在验证身份..."));

    // 特征提取为同步过程，放在定时器回调中执行，避免阻塞信号派发
    const QString descriptor = m_camera->captureFaceFeature();
    if (descriptor.isEmpty()) {
        m_verifying = false;
        if (m_captureStatus) m_captureStatus->setText(QStringLiteral("未识别人脸，请对准摄像头"));
        return;
    }

    FaceRecognitionService faceSvc;
    auto result = faceSvc.matchFace(descriptor);  // 默认阈值与登录识别一致
    m_verifying = false;

    if (result.success && result.userId > 0) {
        m_userId = result.userId;
        if (m_verifyError) {
            m_verifyError->setVisible(false);
        }
        gotoResetPage(result.realName, result.workNo);
        return;
    }

    ++m_attempts;
    const int remain = SC::FACE_VERIFY_MAX_ATTEMPTS - m_attempts;
    if (m_captureStatus) m_captureStatus->setText(QStringLiteral("未识别到本人，请再试一次"));
    if (m_verifyError) {
        if (remain > 0) {
            m_verifyError->setText(QStringLiteral("还可尝试 %1 次").arg(remain));
            m_verifyError->setVisible(true);
        } else {
            // 超过上限：关闭入口，避免反复刷脸试探
            m_verifyError->setText(QStringLiteral("次数已用尽，请联系管理员"));
            m_verifyError->setVisible(true);
            if (m_pollTimer) m_pollTimer->stop();
            if (m_camera) m_camera->stopCamera();
        }
    }
}

/**
 * @brief 切换到设密页并展示已验证用户身份
 * @param realName 用户姓名
 * @param workNo 工号
 */
void ForgotPasswordDialog::gotoResetPage(const QString& realName, const QString& workNo) {
    if (m_camera) m_camera->stopCamera();
    if (m_pollTimer) m_pollTimer->stop();
    if (m_userLabel) {
        m_userLabel->setText(QStringLiteral("✓ 身份验证通过：%1（工号 %2）")
                                 .arg(realName.isEmpty() ? QStringLiteral("本人") : realName)
                                 .arg(workNo));
    }
    if (m_pages) m_pages->setCurrentIndex(1);
}

/**
 * @brief 新密码提交：校验规则并写库，成功关闭对话框
 */
void ForgotPasswordDialog::onResetSubmit() {
    const QString newPwd = m_newPwdEdit ? m_newPwdEdit->text() : QString();
    const QString confirmPwd = m_confirmPwdEdit ? m_confirmPwdEdit->text() : QString();

    auto showError = [this](const QString& msg) {
        if (!m_resetError) return;
        m_resetError->setText(msg);
        m_resetError->setVisible(true);
    };

    if (newPwd.isEmpty()) { showError(QStringLiteral("请输入新密码")); return; }
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

    if (m_resetError) m_resetError->setVisible(false);

    UserController ctrl;
    // mustChange=false：用户当场自设新密码，无需下次登录再强制修改
    if (!ctrl.resetPassword(m_userId, newPwd, false)) {
        showError(QStringLiteral("重置失败，请重试"));
        return;
    }
    m_passwordReset = true;
    MessageDialog::showSuccess(this, QStringLiteral("重置成功"),
                               QStringLiteral("请用新密码登录"));
    accept();
}

/**
 * @brief 取消：关闭对话框，标记未重置
 */
void ForgotPasswordDialog::onCancel() {
    m_passwordReset = false;
    reject();
}
