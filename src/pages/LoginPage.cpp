/**
 * @file LoginPage.cpp
 * @brief 登录页面实现 - 双栏布局+刷脸登录+密码降级 (1:1复刻Vue版Login.vue)
 * @author 袁燕
 *
 * 完善BS端复刻：接入状态圆点、陌生人卡片、密码表单提示
 * 人脸识别改走后端API：发送face image→后端face-api.js提取特征→余弦比对
 *           修复本地纹理哈希与Web版face-api.js特征不兼容导致刷脸进不去的致命问题
 */
#include "LoginPage.h"
#include "components/FaceCameraWidget.h"
#include "components/NumKeypad.h"
#include "utils/StyleHelper.h"
#include "services/AuthService.h"
#include "services/FaceRecognitionService.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QSpacerItem>
#include <QDialog>
#include <QFrame>
#include <QFile>
#include <QMouseEvent>
#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>  // 工号纯数字校验
#include <QPixmap>
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QApplication>  // qApp->quit()退出系统
#include "common/Constants.h"

LoginPage::LoginPage(QWidget* parent) : QWidget(parent),
    m_faceCamera(nullptr) {
    // 暗蓝渐变背景 (复刻Vue版) stop0.4对齐Vue版40%断点
    setStyleSheet("background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #0d1b2a,stop:0.4 #162d45,stop:1 #1e3f5e);");
    setupUI();

    // 初始化数字键盘
    // 账号为纯数字工号：用户名/密码统一由NumKeypad输入，移除字母软键盘
    m_numKeypad = new NumKeypad(this);
    connect(m_numKeypad, &NumKeypad::confirmed, this, [this]() {
        m_numKeypad->hide();
        if (m_activeField == "username") {
            // 工号确认 → 切换到密码输入
            m_passwordEdit->clear();
            m_activeField = "password";
            QTimer::singleShot(150, this, &LoginPage::onPasswordFieldClicked);
        } else if (m_passwordEdit->text().length() >= 6) {
            // 注意：不能在软键盘 mouseReleaseEvent 事件派发过程中
            // 同步执行 onPasswordLogin()。该函数会发出 loginSuccess → MainWindow::onLoginSuccess，
            // 后者内部还有模态对话框的嵌套事件循环；而此刻键盘面板窗口正在派发鼠标事件，
            // 叠加 hide() 的窗口拆装，Qt 内部状态不一致 → onLoginSuccess 内第一处 Qt 调用
            // （m_topBar->refreshVersionLabel()）崩溃（0xC0000005 读取 0xFFFFFFFFFFFFFFFF）。
            // 事件派发结束后的下一轮执行，登录流程在干净的窗口状态下运行。
            QTimer::singleShot(0, this, &LoginPage::onPasswordLogin);
        }
    });
    connect(m_numKeypad, &NumKeypad::cancelled, this, [this]() {
        m_numKeypad->hide();
    });

    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    m_autoJumpTimer = new QTimer(this);
    m_autoJumpTimer->setSingleShot(true);
    connect(m_autoJumpTimer, &QTimer::timeout, this, [this]() {
        if (m_destroying) return;  // 析构保护
        if (!m_pendingUser.isEmpty())
            emit loginSuccess(m_pendingUser);
    });
}

LoginPage::~LoginPage() {
    // 析构期间必须标记+断开所有信号，防止Qt递归删除子对象时信号触发访问半销毁的this
    m_destroying = true;

    // 1. 停止所有定时器
    m_timeoutTimer->stop();
    m_autoJumpTimer->stop();
    if (m_dotBlinkTimer) {
        m_dotBlinkTimer->stop();
    }

    // 2. 断开摄像头和软键盘的信号（防止析构期间回调触发）
    if (m_faceCamera) {
        m_faceCamera->disconnect();
        m_faceCamera->stopCamera();
    }
    if (m_numKeypad) {
        m_numKeypad->disconnect();
    }

    // 3. 断开LoginPage自身所有信号连接（阻断lambda回调）
    disconnect();

    // 4. 清理闪烁定时器（已经stopped + disconnected，安全delete）
    delete m_dotBlinkTimer;
    m_dotBlinkTimer = nullptr;
}


// ==============================================================
// 【④ 账号密码登录
//   工号/密码输入、数字键盘、登录校验
// ==============================================================
/**
 * @brief 账号输入框获得焦点事件处理：弹出软键盘
 */
void LoginPage::onUsernameFieldClicked() {
    m_activeField = "username";
    // 工号改纯数字：统一使用数字键盘（不随机打乱、明文显示）
    if (m_numKeypad) {
        m_numKeypad->setShuffle(false);
        m_numKeypad->setShowPassword(true);
        m_numKeypad->attach(m_usernameEdit);
        m_numKeypad->show();
    }
}

/**
 * @brief 处理口令输入框点击事件
 */
void LoginPage::onPasswordFieldClicked() {
    m_activeField = "password";
    // NumKeypad作为顶层Popup弹窗显示，不受布局约束
    if (m_numKeypad) {
        m_numKeypad->setShuffle(true);
        m_numKeypad->setShowPassword(false);
        m_numKeypad->attach(m_passwordEdit);
        m_numKeypad->show();
    }
}

/**
 * @brief 处理口令
 */
void LoginPage::onPasswordLogin() {
    QString username = m_usernameEdit->text().trimmed();
    QString password = m_passwordEdit->text();

    // [完善] 前端校验
    if (username.isEmpty()) {
        m_errorLabel->setText(QStringLiteral("请输入工号"));
        m_errorLabel->setVisible(true);
        return;
    }
    // 工号为纯数字（与数字键盘输入、DB存储格式统一）
    static const QRegularExpression workNoRe(QStringLiteral("^\\d{1,32}$"));
    if (!workNoRe.match(username).hasMatch()) {
        m_errorLabel->setText(QStringLiteral("工号应为1-32位纯数字"));
        m_errorLabel->setVisible(true);
        return;
    }
    if (password.isEmpty()) {
        m_errorLabel->setText(QStringLiteral("请输入密码"));
        m_errorLabel->setVisible(true);
        return;
    }
    if (password.length() < 6) {
        m_errorLabel->setText(QStringLiteral("密码长度不能少于6位"));
        m_errorLabel->setVisible(true);
        return;
    }

    m_errorLabel->setVisible(false);

    // 调用认证服务
    AuthService svc;
    auto r = svc.login(username, password);
    if (r.success) {
        emit loginSuccess(r.user);
    } else {
        // 登录失败：清空密码+显示错误
        m_errorLabel->setText(r.message.isEmpty() ? QStringLiteral("用户名或密码错误，请重试") : r.message);
        m_errorLabel->setVisible(true);
        m_passwordEdit->clear();
        // 自动弹出密码键盘便于重试
        // Linux/麒麟上Popup窗口可能不显示，延迟更长并确保键盘可见
        QTimer::singleShot(SC::UI_LOGIN_FAIL_KEYBOARD_DELAY_MS, this, [this]() {
            if (m_numKeypad) {
                m_numKeypad->hide();
            }
            onPasswordFieldClicked();
        });
    }
}


// ==============================================================
// 【⑤ 登录模式切换与页面状态重置
//   人脸 ⇄ 密码 切换、失败后复位、重新尝试
// ==============================================================
/// 重置所有登录状态——退出登录/登出后清除上一用户的所有残留信息
/// @details 解决致命Bug：退出登录后LoginPage仍显示"身份验证通过"、张三识别信息、
/// 自动跳转定时器未停等状态残留，导致界面混乱。
/// @author 袁燕 - 2026-06-21
void LoginPage::resetPageState() {
    // 1. 停止所有定时器（防止退出后autoJumpTimer触发登录）
    m_autoJumpTimer->stop();
    m_timeoutTimer->stop();
    stopDotBlink();

    // 2. 停止人脸识别和摄像头
    stopFaceRecognition();

    // 3. 清空待登录用户（防止下次自动跳转）
    m_pendingUser = QJsonObject();
    m_isVerifying = false;

    // 4. 隐藏所有结果面板（成功/陌生人/密码表单/错误提示）
    m_successBox->setVisible(false);
    m_strangerBox->setVisible(false);
    m_passwordForm->setVisible(false);
    m_errorLabel->setVisible(false);
    m_tryFaceBtn->setVisible(false);

    // 5. 恢复人脸摄像头区域可见+隐藏所有状态圆圈+文字
    if (m_cameraWrap) m_cameraWrap->setVisible(true);
    m_faceCamera->setVisible(true);
    if (m_statusCircleSuccess) m_statusCircleSuccess->setVisible(false);
    if (m_statusCircleFail) m_statusCircleFail->setVisible(false);
    if (m_statusCircleStranger) m_statusCircleStranger->setVisible(false);
    if (m_successStatusText) m_successStatusText->setVisible(false);
    if (m_failStatusText) m_failStatusText->setVisible(false);
    if (m_strangerStatusText) m_strangerStatusText->setVisible(false);
    m_altLoginHint->setVisible(true);
    m_altLoginHint->setText(QStringLiteral("🔑 使用账号密码登录"));

    // 6. 重置状态文字（恢复初始文案）
    m_subtitleLabel->setText(QStringLiteral("请面向摄像头完成身份验证"));
    m_welcomeLabel->setText(QStringLiteral("欢迎使用"));
    setScanStatus(QStringLiteral("请对准摄像头"), 0);
    m_captureProgress->setVisible(false);

    // 7. 重置状态圆点为蓝色
    setStatusDot("background:#4da3ff;");

    // 8. 清空所有输入和采集数据
    clearAllForms();

    // 9. 隐藏数字键盘（如果正在显示）
    if (m_numKeypad) {
        m_numKeypad->hide();
    }
    m_activeField.clear();

    // 10. 延迟重新启动人脸识别（对齐构造函数中的延迟）
    QTimer::singleShot(SC::FACE_RETRY_DELAY_MS, this, &LoginPage::startFaceRecognition);
}

/// 清除所有表单输入和采集数据
void LoginPage::clearAllForms() {
    if (m_usernameEdit) m_usernameEdit->clear();
    if (m_passwordEdit) m_passwordEdit->clear();
    m_samples.clear();
    m_captureCount = 0;
    m_faceResult = FaceResult::Scanning;
    m_verifyBudget = 0;        // 切换登录模式时重置采样预算
    m_verifyStartMs = 0;
}

/** 切换到密码登录 */
void LoginPage::switchToPasswordLogin() {
    stopFaceRecognition();
    setFaceResult(FaceResult::Fail);
    stopDotBlink();
    setStatusDot("background:#ff4d4f;");
    m_errorLabel->setText(QStringLiteral("⚠️ 人脸识别失败，请使用账号密码登录"));
    m_errorLabel->setVisible(true);
    m_subtitleLabel->setText(QStringLiteral("请输入账号密码"));
        setScanStatus(QStringLiteral("已切换密码登录"), 0);
    // cameraWrap保持可见(fail圆圈已由setFaceResult管理)，不额外隐藏
}

/**
 * @brief 处理人脸
 */
void LoginPage::onTryFaceAgain() {
    if (m_faceResult == FaceResult::Fail || m_faceResult == FaceResult::NoCamera) {
        // 当前在密码模式，切换回人脸识别
        retryFace();
    } else {
        // 当前在人脸模式，切到密码模式
        switchToPasswordLogin();
    }
}

/** 重新尝试人脸识别 */
void LoginPage::retryFace() {
    // 用户主动点击重试 → 清除注销抑制，立即恢复识别
    m_logoutSuppressed = false;
    m_logoutSuppressUntilMs = 0;
    m_errorLabel->setVisible(false);
    m_usernameEdit->clear();
    m_passwordEdit->clear();
    m_captureCount = 0;
    m_samples.clear();
    m_isVerifying = false;
    m_verifyBudget = 0;        // 重新尝试时重置采样预算
    m_verifyStartMs = 0;
    m_strangerBox->setVisible(false);
    startFaceRecognition();
}


// ==============================================================
// 【⑥ 页面生命周期事件与辅助
//   窗口显示/尺寸变化、退出系统、状态枚举与结果映射
// ==============================================================
// 退出系统 - 小米风格二次确认弹窗
// 设计理念：触屏设备无窗口关闭按钮，需明确退出入口
// 退出前弹窗确认防止误触（小米极简风格）
// 退出按钮跟随窗口右上角
// 新增showEvent：窗口首次显示时定位退出按钮
// 修复Bug：setupUI时this->width()返回默认值，按钮定位到错误位置
void LoginPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // 回到登录页且识别未运行（注销/自动锁屏后）→ 自动恢复人脸识别，避免摄像头停着不工作
    if (!m_faceRecognitionActive) {
        qInfo() << "[LoginPage] showEvent: face recognition inactive, restarting...";
        startFaceRecognition();
    }
    // 窗口显示时立即定位退出按钮到右上角
    if (m_exitBtn) {
        m_exitBtn->move(this->width() - 60, 12);
        m_exitBtn->raise();
    }
}

/**
 * @brief 窗口尺寸变化事件处理：按新尺寸重排摄像头预览
 * @param event 尺寸变化事件
 */
void LoginPage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_exitBtn) {
        m_exitBtn->move(this->width() - 60, 12);
        m_exitBtn->raise();
    }
}

/**
 * @brief 处理系统
 */
void LoginPage::onExitSystem() {
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("退出确认"));
    dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dlg->setAttribute(Qt::WA_StyledBackground, true);
    dlg->setStyleSheet("background:#ffffff; border-radius:16px;");
    dlg->setFixedSize(380, 220);

    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(32, 28, 32, 24);
    layout->setSpacing(20);

    auto* titleLabel = new QLabel(QStringLiteral("确认退出系统？"));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size:20px; font-weight:700; color:#1a1a2e; background:transparent;");
    layout->addWidget(titleLabel);

    auto* descLabel = new QLabel(QStringLiteral("退出后将关闭整个应用程序"));
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setStyleSheet("font-size:14px; color:#888888; background:transparent;");
    layout->addWidget(descLabel);

    layout->addStretch();

    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(16);

    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    cancelBtn->setMinimumHeight(StyleHelper::Token::ControlHeight);
    cancelBtn->setMinimumWidth(120);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setStyleSheet(
        "QPushButton{font-size:16px; font-weight:500; color:#666666;"
        "background:#f0f0f0; border:none; border-radius:22px; padding:10px 24px;}"
        "QPushButton:hover{background:#e6e6e6;}"
        "QPushButton:pressed{background:#d9d9d9;}");
    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);

    auto* confirmBtn = new QPushButton(QStringLiteral("退出"));
    confirmBtn->setMinimumHeight(StyleHelper::Token::ControlHeight);
    confirmBtn->setMinimumWidth(120);
    confirmBtn->setCursor(Qt::PointingHandCursor);
    confirmBtn->setStyleSheet(
        "QPushButton{font-size:16px; font-weight:600; color:#ffffff;"
        "background:#ff4d4f; border:none; border-radius:22px; padding:10px 24px;}"
        "QPushButton:hover{background:#e84448;}"
        "QPushButton:pressed{background:#d13b3f;}");
    connect(confirmBtn, &QPushButton::clicked, dlg, &QDialog::accept);

    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(confirmBtn);
    layout->addLayout(btnRow);

    if (dlg->exec() == QDialog::Accepted) {
        stopFaceRecognition();
        qApp->quit();
    }
    delete dlg;
}

/** 处理陌生人确认 → 切换密码登录 */
void LoginPage::handleStrangerConfirm() {
    qDebug() << "[LoginPage] Stranger confirmed, switching to password login";
    m_strangerBox->setVisible(false);
    switchToPasswordLogin();
}

/** 停止状态圆点闪烁 */
void LoginPage::stopDotBlink() {
    if (m_dotBlinkTimer) {
        m_dotBlinkTimer->stop();
    }
}

/** [保留] MOC生成代码引用的空方法 */
void LoginPage::retryFaceTimeout() {}

/**
 * @brief 将人脸识别状态转换为可读名称
 * @param result 识别状态
 * @return 状态名称，仅用于日志展示
 */
QString LoginPage::faceResultName(FaceResult result) {
    switch (result) {
    case FaceResult::Idle:      return QStringLiteral("idle");
    case FaceResult::Scanning:  return QStringLiteral("scanning");
    case FaceResult::Capturing: return QStringLiteral("capturing");
    // success/stranger 与识别结果落库值同源，统一取 SC::FACE_RESULT_*；
    // 其余为登录状态机内部值，无对应落库字段，保持字面量
    case FaceResult::Success:   return SC::FACE_RESULT_SUCCESS;
    case FaceResult::Fail:      return QStringLiteral("fail");
    case FaceResult::Stranger:  return SC::FACE_RESULT_STRANGER;
    case FaceResult::NoCamera:  return QStringLiteral("no-camera");
    }
    return QStringLiteral("unknown");
}

/**
 * @brief 设置人脸结果
 */
void LoginPage::setFaceResult(FaceResult state) {
    m_faceResult = state;
    if (state == FaceResult::Fail) {
        // fail状态显示红色❌圆圈+下方文字 (复刻Web版 .camera-area.fail)，cameraWrap保持可见
        m_faceCamera->setVisible(false);
        m_statusCircleSuccess->setVisible(false);
        m_statusCircleFail->setVisible(true);
        m_statusCircleStranger->setVisible(false);
        if (m_successStatusText) m_successStatusText->setVisible(false);
        if (m_failStatusText) m_failStatusText->setVisible(true);
        if (m_strangerStatusText) m_strangerStatusText->setVisible(false);
        m_captureProgress->setVisible(false);
        m_altLoginHint->setVisible(false);
        m_passwordForm->setVisible(true);
        m_tryFaceBtn->setVisible(true);
        // 对齐Web版：失败状态红点 (dot-red)
        setStatusDot("background:#ff4d4f;");
    } else if (state == FaceResult::NoCamera) {
        // no-camera状态隐藏整个摄像头容器 (Web版无camera-area占位)
        if (m_cameraWrap) m_cameraWrap->setVisible(false);
        m_passwordForm->setVisible(true);
        m_tryFaceBtn->setVisible(true);
    }
}

/**
         * 事件过滤器：拦截控件与窗口事件并转交专用处理
         * @param obj 事件来源控件
         * @param event 事件对象
         * @return true=事件已被处理，不再继续传递
         */
bool LoginPage::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        if (obj == m_usernameEdit) {
            onUsernameFieldClicked();
            return true;
        }
        if (obj == m_passwordEdit) {
            onPasswordFieldClicked();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}
