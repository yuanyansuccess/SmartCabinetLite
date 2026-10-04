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

void LoginPage::setupUI() {
    auto* outer = new QVBoxLayout(this);
    outer->setAlignment(Qt::AlignCenter);

    // 主容器 - min-height:600 可撑高 (复刻Vue版 login-container) [触屏优化]
    auto* card = new QWidget();
    card->setFixedWidth(1000);
    // card最小高度750：摄像头区(~300)+状态行(~40)+间距(20)+密码表单(~280)+底部按钮+版权(~50)+余量
    // 原620不够导致底部"重新扫脸"按钮和版权文字被裁剪
    card->setMinimumHeight(750);
    // WA_StyledBackground启用后border-radius才能裁剪背景
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setStyleSheet(QString(
        "background:%1; border-radius:20px;"
    ).arg(StyleHelper::whiteColor()));
    // 去除登录卡片外阴影

    auto* cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    // 左侧品牌区 (1:1复刻Vue版 login-left: flex:1, 454px)
    auto* leftWidget = new QWidget();
    // 移除fixedWidth(454)，使用stretch比例动态计算
    // card总宽1000px, stretch 454:546 = Web版 flex:1 vs flex:1.2
    leftWidget->setMinimumWidth(300);  // 防挤压下限
    leftWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    // WA_StyledBackground启用border-radius背景裁剪
    leftWidget->setAttribute(Qt::WA_StyledBackground, true);
    leftWidget->setStyleSheet(
        "background:qlineargradient(x1:0,y1:0,x2:0.34,y2:1,stop:0 #132940,stop:1 #1a3f60);"
        "border-top-left-radius:20px; border-bottom-left-radius:20px;");
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(40, 60, 40, 40);  // 上边距60让中间内容视觉下沉更均衡，下边距40配合stretch布局
    leftLayout->setAlignment(Qt::AlignCenter);
    leftLayout->setSpacing(18);  // 间距18紧凑化
    setupLeftPanel(leftLayout);

    // 右侧认证区 (复刻Vue版 login-right)
    auto* rightWidget = new QWidget();
    // WA_StyledBackground启用圆角背景裁剪
    rightWidget->setAttribute(Qt::WA_StyledBackground, true);
    rightWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    rightWidget->setStyleSheet(QString("background:%1; border-top-right-radius:20px; border-bottom-right-radius:20px;")
        .arg(StyleHelper::whiteColor()));
    auto* rightLayout = new QVBoxLayout(rightWidget);
    // 缩小padding为内容腾空间：30(top/bottom)，36(left/right)
    rightLayout->setContentsMargins(36, 30, 36, 30);
    rightLayout->setSpacing(12);
    setupRightPanel(rightLayout);

    // Web版left:flex:1, right:flex:1.2 → 比例 454:546 (1000*1/2.2=454)
    // 使用stretch精确控制，leftWidget去除fixedWidth让stretch决定实际宽度
    cardLayout->addWidget(leftWidget, 454);
    cardLayout->addWidget(rightWidget, 546);
    outer->addWidget(card, 0, Qt::AlignCenter);

    // 启动延迟100ms，摄像头初始化足够
    QTimer::singleShot(100, this, &LoginPage::startFaceRecognition);
}

void LoginPage::setupLeftPanel(QVBoxLayout* layout) {
    // Logo区 - 显示公司logo图片 (复刻Vue版 .logo-wrap)
    auto* logoWrap = new QWidget();
    logoWrap->setFixedSize(200, 80);
    logoWrap->setStyleSheet(
        "background:rgba(255,255,255,0.12); border:1px solid rgba(255,255,255,0.15);"
        "border-radius:20px;");
    auto* ll = new QVBoxLayout(logoWrap);
    ll->setContentsMargins(18, 18, 18, 18);  // top/bottom 18 对齐Vue版 padding:18px 28px
    ll->setAlignment(Qt::AlignCenter);

    // 加载logo图片 - 使用Qt资源系统 (跨平台兼容)
    auto* logoImg = new QLabel();
    logoImg->setAlignment(Qt::AlignCenter);
    logoImg->setStyleSheet("background:transparent;");

    QPixmap pix(":/resources/logo.png");
    if (!pix.isNull()) {
        logoImg->setPixmap(pix.scaledToHeight(50, Qt::SmoothTransformation));
    } else {
        // fallback: 尝试从文件系统加载
        QString logoPath = QCoreApplication::applicationDirPath() + "/resources/logo.png";
        if (!QFile::exists(logoPath)) {
            logoPath = "D:/CFDZ/smartCabinet/trunk/QtSmartCabinet/resources/logo.png";
        }
        if (QFile::exists(logoPath)) {
            QPixmap pix2(logoPath);
            if (!pix2.isNull()) {
                logoImg->setPixmap(pix2.scaledToHeight(50, Qt::SmoothTransformation));
            } else {
                logoImg->setText(QStringLiteral("成飞电子"));
                logoImg->setStyleSheet("color:white; font-size:17px; font-weight:bold; background:transparent; letter-spacing:2px;");
            }
        } else {
            logoImg->setText(QStringLiteral("成飞电子"));
            logoImg->setStyleSheet("color:white; font-size:17px; font-weight:bold; background:transparent; letter-spacing:2px;");
        }
    }
    ll->addWidget(logoImg);
    // 顶部留白，让Logo区域视觉居中偏上
    layout->addStretch(1);

    layout->addWidget(logoWrap, 0, Qt::AlignCenter);
    layout->addSpacing(16);  // 16 紧凑化，Logo和图标间距收紧

    // 品牌图标 (复刻Vue版 .brand-icon)
    auto* icon = new QLabel(QStringLiteral("🛠"));
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet("font-size:68px; background:transparent; margin-bottom:8px;");  // margin-bottom:8 紧凑化，图标和标题收紧
    layout->addWidget(icon);

    // 品牌名称 (复刻Vue版 .brand-name) [触屏优化 2026-06-15]
    auto* name = new QLabel(QStringLiteral("智能工具柜"));
    name->setAlignment(Qt::AlignCenter);
    name->setStyleSheet("color:white; font-size:28px; font-weight:700; letter-spacing:3px; background:transparent; margin-bottom:6px;");  // margin-bottom:6 紧凑化
    layout->addWidget(name);

    // 弹性空间 - 将描述文字推至底部区域，视觉更大气
    layout->addStretch(4);  // 4 加大弹性比例，描述文字更下沉

    // 品牌描述 (复刻Vue版 .brand-desc) [触屏优化 2026-06-15]
    auto* desc = new QLabel(QStringLiteral("智能化工具管理系统\n视觉识别 · 刷脸认证 · 秒级盘点"));
    desc->setAlignment(Qt::AlignCenter);
    desc->setWordWrap(true);
    desc->setStyleSheet("color:rgba(255,255,255,0.8); font-size:16px; background:transparent; line-height:1.8; margin-bottom:12px;");  // 字号16，行高1.8，视觉下沉更沉稳
    layout->addWidget(desc);

    layout->addStretch(1);
}

void LoginPage::setupRightPanel(QVBoxLayout* layout) {
    buildWelcomeHeader(layout);
    buildFaceScanArea(layout);
    buildStatusRow(layout);
    buildPasswordForm(layout);
    buildSuccessCard(layout);
    buildStrangerCard(layout);
    layout->addStretch();

    buildExitButton();
    buildCopyright(layout);
}

/** 构建右侧顶部欢迎文字与副标题 */
void LoginPage::buildWelcomeHeader(QVBoxLayout* layout) {
    m_welcomeLabel = new QLabel(QStringLiteral("欢迎使用"));
    m_welcomeLabel->setAlignment(Qt::AlignCenter);
    m_welcomeLabel->setStyleSheet(QString("font-size:27px; font-weight:800; color:%1; background:transparent; margin-bottom:4px;")
        .arg("#1a1a2e"));
    layout->addWidget(m_welcomeLabel);

    m_subtitleLabel = new QLabel(QStringLiteral("请面向摄像头完成身份验证"));
    m_subtitleLabel->setAlignment(Qt::AlignCenter);
    m_subtitleLabel->setWordWrap(true);
    m_subtitleLabel->setStyleSheet(QString("font-size:15px; color:%1; background:transparent; margin-bottom:28px;")
        .arg("#999999"));
    layout->addWidget(m_subtitleLabel);
}

/** 构建人脸扫描区：摄像头、三态状态圆圈、采集进度与密码登录入口 */
void LoginPage::buildFaceScanArea(QVBoxLayout* layout) {
    m_cameraWrap = new QWidget();  // 存为成员变量，切换模式时整体显隐
    m_cameraWrap->setStyleSheet("background:transparent;");
    auto* camLayout = new QVBoxLayout(m_cameraWrap);
    camLayout->setAlignment(Qt::AlignCenter);
    camLayout->setSpacing(12);

    // 摄像头区域260x300，确保录像框和状态提示完整显示
    // 采集参数：3稳定帧×40ms+50ms延迟=170ms即开始采集
    m_faceCamera = new FaceCameraWidget();
    m_faceCamera->setFixedSize(260, 300);
    m_faceCamera->setAutoCapture(true);
    m_faceCamera->setMinConfidence(0.60);
    m_faceCamera->setStableFrames(3);
    m_faceCamera->setCaptureDelay(50);
    m_faceCamera->setDetectInterval(40);
    m_faceCamera->setStyleSheet(
        "border:4px dashed #d0d0d0; border-radius:90px;"
        "background:#fafbfc;");
    connect(m_faceCamera, &FaceCameraWidget::faceDetected, this, &LoginPage::onFaceDetected);
    connect(m_faceCamera, &FaceCameraWidget::faceLost, this, &LoginPage::onFaceLost);
    // 距离过远提示：人脸框过小则特征像素不足，识别必然失败
    connect(m_faceCamera, &FaceCameraWidget::faceTooFarChanged, this, &LoginPage::onFaceTooFarChanged);
    connect(m_faceCamera, &FaceCameraWidget::captureReady, this, &LoginPage::onFaceCaptured);
    connect(m_faceCamera, &FaceCameraWidget::errorOccurred, this, &LoginPage::onCameraError);
    connect(m_faceCamera, &FaceCameraWidget::stateChanged, this, &LoginPage::onFaceStateChanged);

    camLayout->addWidget(m_faceCamera, 0, Qt::AlignCenter);

    // 状态圆圈 - 1:1复刻Web版 .camera-area.success/.fail/.stranger
    // Web设计：180x180圆形，4px solid边框，背景色，内部emoji(50px)+文字(14px)

    // === 成功状态圆圈 (Web: .camera-area.success) ===
    m_statusCircleSuccess = new QLabel();
    m_statusCircleSuccess->setFixedSize(180, 180);
    m_statusCircleSuccess->setAlignment(Qt::AlignCenter);
    m_statusCircleSuccess->setStyleSheet(
        "font-size:50px; border:4px solid #52c41a; border-radius:90px;"
        "background:#f6ffed; color:#52c41a;");
    m_statusCircleSuccess->setText(QStringLiteral("✅"));
    m_statusCircleSuccess->setVisible(false);
    camLayout->addWidget(m_statusCircleSuccess, 0, Qt::AlignCenter);

    // === 成功圆圈下方"识别成功"文字 ===
    m_successStatusText = new QLabel(QStringLiteral("识别成功"));
    m_successStatusText->setAlignment(Qt::AlignCenter);
    m_successStatusText->setStyleSheet("font-size:14px; color:#bbbbbb; font-weight:600; background:transparent;");
    m_successStatusText->setVisible(false);
    camLayout->addWidget(m_successStatusText, 0, Qt::AlignCenter);

    // === 失败状态圆圈 (Web: .camera-area.fail) ===
    m_statusCircleFail = new QLabel();
    m_statusCircleFail->setFixedSize(180, 180);
    m_statusCircleFail->setAlignment(Qt::AlignCenter);
    m_statusCircleFail->setStyleSheet(
        "font-size:50px; border:4px solid #ff4d4f; border-radius:90px;"
        "background:#fff2f0; color:#ff4d4f;");
    m_statusCircleFail->setText(QStringLiteral("❌"));
    m_statusCircleFail->setVisible(false);
    camLayout->addWidget(m_statusCircleFail, 0, Qt::AlignCenter);

    // === 失败圆圈下方"识别失败"文字 (Web: .cam-text) ===
    m_failStatusText = new QLabel(QStringLiteral("识别失败"));
    m_failStatusText->setAlignment(Qt::AlignCenter);
    m_failStatusText->setStyleSheet("font-size:14px; color:#bbbbbb; font-weight:600; background:transparent;");
    m_failStatusText->setVisible(false);
    camLayout->addWidget(m_failStatusText, 0, Qt::AlignCenter);

    // === 陌生人状态圆圈 (Web: .camera-area.stranger) ===
    m_statusCircleStranger = new QLabel();
    m_statusCircleStranger->setFixedSize(180, 180);
    m_statusCircleStranger->setAlignment(Qt::AlignCenter);
    m_statusCircleStranger->setStyleSheet(
        "font-size:50px; border:4px solid #faad14; border-radius:90px;"
        "background:#fffbe6; color:#faad14;");
    m_statusCircleStranger->setText(QStringLiteral("⚠️"));
    m_statusCircleStranger->setVisible(false);
    camLayout->addWidget(m_statusCircleStranger, 0, Qt::AlignCenter);

    // === 陌生人圆圈下方"检测到陌生人"文字 ===
    m_strangerStatusText = new QLabel(QStringLiteral("检测到陌生人"));
    m_strangerStatusText->setAlignment(Qt::AlignCenter);
    m_strangerStatusText->setStyleSheet("font-size:14px; color:#bbbbbb; font-weight:600; background:transparent;");
    m_strangerStatusText->setVisible(false);
    camLayout->addWidget(m_strangerStatusText, 0, Qt::AlignCenter);

    // 采集进度文字加大确保触屏清晰可读
    m_captureProgress = new QLabel("");
    m_captureProgress->setAlignment(Qt::AlignCenter);
    m_captureProgress->setStyleSheet(QString("font-size:15px; color:%1; background:transparent; font-weight:600; margin-top:8px;")
        .arg(StyleHelper::primaryColor()));
    m_captureProgress->setVisible(false);
    camLayout->addWidget(m_captureProgress);

    // 密码登录入口移入cameraWrap (Web: .alt-login-hint 在scanning模板内，仅扫描时可见)
    m_altLoginHint = new QPushButton(QStringLiteral("🔑 使用账号密码登录"));
    m_altLoginHint->setStyleSheet(
        "QPushButton{color:#4da3ff;font-size:14px;border:none;background:transparent;"
        "text-decoration:none;font-weight:600; margin-top:10px;}"
        "QPushButton:hover{color:#3d8ae0; text-decoration:underline;}");
    m_altLoginHint->setCursor(Qt::PointingHandCursor);
    connect(m_altLoginHint, &QPushButton::clicked, this, [this]() {
        switchToPasswordLogin();
    });
    camLayout->addWidget(m_altLoginHint, 0, Qt::AlignCenter);

    layout->addWidget(m_cameraWrap);

}

/** 构建状态指示行：状态圆点+文字（始终可见，Web: .status-line） */
void LoginPage::buildStatusRow(QVBoxLayout* layout) {
    auto* statusRow = new QHBoxLayout();
    statusRow->setAlignment(Qt::AlignCenter);
    statusRow->setSpacing(8);

    m_statusDot = new QLabel();
    m_statusDot->setFixedSize(8, 8);
    m_statusDot->setStyleSheet(
        "background:#4da3ff; border-radius:4px;");
    statusRow->addWidget(m_statusDot);

    m_cameraStatusText = new QLabel(QStringLiteral("正在初始化人脸识别..."));
    m_cameraStatusText->setMinimumWidth(280);  // 确保长文字一行显示全
    m_cameraStatusText->setWordWrap(true);
    m_cameraStatusText->setStyleSheet(QString("font-size:14px; font-weight:600; color:%1; background:transparent;")
        .arg("#555555"));
    statusRow->addWidget(m_cameraStatusText);
    layout->addLayout(statusRow);
    layout->addSpacing(20);  // Web: .status-line margin-bottom:20px
}

/** 构建密码登录区（默认隐藏，复刻Vue版login-form） */
void LoginPage::buildPasswordForm(QVBoxLayout* layout) {
    m_passwordForm = new QWidget();
    m_passwordForm->setVisible(false);
    m_passwordForm->setStyleSheet("background:transparent;");
    auto* pfLayout = new QVBoxLayout(m_passwordForm);
    pfLayout->setContentsMargins(0, 0, 0, 0);
    pfLayout->setSpacing(16);

    // 人脸识别失败/摄像头不可用 提示条 (Web: .form-error-tip)
    m_errorLabel = new QLabel();
    m_errorLabel->setStyleSheet(QString("color:%1; font-size:15px; font-weight:600; background:transparent; margin-bottom:14px;")
        .arg(StyleHelper::dangerColor()));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setVisible(false);
    pfLayout->addWidget(m_errorLabel);

    // 工号标签 (Web: .form-label 14px #555)
    auto* unameLabel = new QLabel(QStringLiteral("工号"));
    unameLabel->setStyleSheet(QString("font-size:14px; font-weight:600; color:#555555; background:transparent; margin-bottom:6px;"));
    pfLayout->addWidget(unameLabel);

    // 用户名输入框+键盘按钮并排 (1:1复刻Web版 .password-input-wrap)
    // WA_StyledBackground修复边框圆角渲染，固定高度对齐内部控件
    auto* unameWrap = new QFrame();
    unameWrap->setAttribute(Qt::WA_StyledBackground, true);
    unameWrap->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
    unameWrap->setStyleSheet(
        "QFrame{border:2px solid #e0e0e0;border-radius:12px;background:#fff;}"
        "QFrame:focus-within{border-color:#4da3ff;}");
    auto* unameWrapLayout = new QHBoxLayout(unameWrap);
    unameWrapLayout->setContentsMargins(0, 0, 0, 0);
    unameWrapLayout->setSpacing(0);

    m_usernameEdit = new QLineEdit();
    m_usernameEdit->setPlaceholderText(QStringLiteral("请输入工号（纯数字）"));
    m_usernameEdit->setReadOnly(true);
    m_usernameEdit->setCursor(Qt::PointingHandCursor);
    m_usernameEdit->setMinimumHeight(46);
    m_usernameEdit->setStyleSheet(
        "QLineEdit{border:none;padding:0 16px;font-size:16px;color:#1a1a2e;background:transparent;}");
    m_usernameEdit->installEventFilter(this);
    unameWrapLayout->addWidget(m_usernameEdit, 1);

    auto* skbBtn1 = new QPushButton(QStringLiteral("⌨"));
    skbBtn1->setFixedSize(48, 46);
    skbBtn1->setCursor(Qt::PointingHandCursor);
    skbBtn1->setStyleSheet(
        "QPushButton{border:none;border-radius:0 10px 10px 0;"
        "background:#f5f6f8;font-size:22px;color:#666;}"
        "QPushButton:hover{background:#e6f0ff;color:#4da3ff;}");
    connect(skbBtn1, &QPushButton::clicked, this, &LoginPage::onUsernameFieldClicked);
    unameWrapLayout->addWidget(skbBtn1);

    pfLayout->addWidget(unameWrap);

    // 密码标签 (Web: .form-label 14px #555)
    auto* pwdLabel = new QLabel(QStringLiteral("密码"));
    pwdLabel->setStyleSheet(QString("font-size:14px; font-weight:600; color:#555555; background:transparent; margin-bottom:6px;"));
    pfLayout->addWidget(pwdLabel);

    // 密码输入框+键盘按钮并排 (1:1复刻Web版 .password-input-wrap)
    auto* pwdWrap = new QFrame();
    pwdWrap->setAttribute(Qt::WA_StyledBackground, true);
    pwdWrap->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
    pwdWrap->setStyleSheet(
        "QFrame{border:2px solid #e0e0e0;border-radius:12px;background:#fff;}"
        "QFrame:focus-within{border-color:#4da3ff;}");
    auto* pwdWrapLayout = new QHBoxLayout(pwdWrap);
    pwdWrapLayout->setContentsMargins(0, 0, 0, 0);
    pwdWrapLayout->setSpacing(0);

    m_passwordEdit = new QLineEdit();
    m_passwordEdit->setPlaceholderText(QStringLiteral("请输入密码"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setReadOnly(true);
    m_passwordEdit->setCursor(Qt::PointingHandCursor);
    m_passwordEdit->setMinimumHeight(46);
    m_passwordEdit->setStyleSheet(
        "QLineEdit{border:none;padding:0 16px;font-size:16px;color:#1a1a2e;background:transparent;}");
    m_passwordEdit->installEventFilter(this);
    pwdWrapLayout->addWidget(m_passwordEdit, 1);

    auto* skbBtn2 = new QPushButton(QStringLiteral("⌨"));
    skbBtn2->setFixedSize(48, 46);
    skbBtn2->setCursor(Qt::PointingHandCursor);
    skbBtn2->setStyleSheet(
        "QPushButton{border:none;border-radius:0 10px 10px 0;"
        "background:#f5f6f8;font-size:22px;color:#666;}"
        "QPushButton:hover{background:#e6f0ff;color:#4da3ff;}");
    connect(skbBtn2, &QPushButton::clicked, this, &LoginPage::onPasswordFieldClicked);
    pwdWrapLayout->addWidget(skbBtn2);

    pfLayout->addWidget(pwdWrap);

    // 数字键盘占位 - 构造函数中创建，此处添加到布局
    // 初始隐藏，点击密码框⌨按钮时显示

    // 登录按钮 (Web: .login-btn border-radius:10px font-size:17px padding:14px)
    m_loginBtn = new QPushButton(QStringLiteral("登  录"));
    m_loginBtn->setCursor(Qt::PointingHandCursor);
    m_loginBtn->setStyleSheet(
        "QPushButton{ background:#4da3ff; color:white; border:none; border-radius:10px;"
        "font-size:17px; font-weight:700; padding:14px 0; }"
        "QPushButton:hover{ background:#3d8ae0; }"
        "QPushButton:pressed{ background:#2d7ad0; }"
        "QPushButton:disabled{ background:#a0c4ff; }");
    connect(m_loginBtn, &QPushButton::clicked, this, &LoginPage::onPasswordLogin);
    pfLayout->addWidget(m_loginBtn);

    // 重新人脸识别链接 (Web: .retry-link 14px)
    m_tryFaceBtn = new QPushButton(QStringLiteral("🔄 重新尝试人脸识别"));
    m_tryFaceBtn->setStyleSheet(
        "QPushButton{color:#4da3ff;font-size:14px;border:none;background:transparent;"
        "text-decoration:underline;font-weight:600; margin-top:14px;}"
        "QPushButton:hover{color:#3d8ae0;}");
    m_tryFaceBtn->setCursor(Qt::PointingHandCursor);
    m_tryFaceBtn->setVisible(false);
    connect(m_tryFaceBtn, &QPushButton::clicked, this, &LoginPage::onTryFaceAgain);
    pfLayout->addWidget(m_tryFaceBtn);

    layout->addWidget(m_passwordForm);
}

/** 构建成功信息卡片（Web: .info-box，默认隐藏） */
void LoginPage::buildSuccessCard(QVBoxLayout* layout) {
    m_successBox = new QWidget();
    m_successBox->setVisible(false);
    m_successBox->setStyleSheet(
        "QWidget#successCard{background:#f0fdf4; border:2px solid #86efac; border-radius:14px;}");
    m_successBox->setObjectName("successCard");
    auto* sbl = new QVBoxLayout(m_successBox);
    sbl->setContentsMargins(24, 20, 24, 20);
    sbl->setSpacing(10);

    // 卡片标题
    auto* cardTitle = new QLabel(QStringLiteral("识别信息"));
    cardTitle->setStyleSheet("font-size:17px; font-weight:700; color:#166534; background:transparent;"
                              "padding-bottom:6px; border-bottom:1px solid #bbf7d0;");
    sbl->addWidget(cardTitle);

    // 信息行辅助函数
    auto addInfoRow = [&](const QString& icon, const QString& label, const QString& valueStr, QLabel*& storage) {
        auto* row = new QWidget();
        row->setStyleSheet("background:transparent;");
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 3, 0, 3);
        rowLayout->setSpacing(10);

        auto* labelWidget = new QLabel(QString("%1 %2").arg(icon, label));
        labelWidget->setFixedWidth(110);
        labelWidget->setStyleSheet("font-size:15px; color:#555555; font-weight:600; background:transparent;");

        storage = new QLabel(valueStr);
        storage->setWordWrap(true);
        storage->setStyleSheet("font-size:15px; color:#1a1a2e; font-weight:600; background:transparent;");

        rowLayout->addWidget(labelWidget);
        rowLayout->addWidget(storage, 1);
        sbl->addWidget(row);
    };

    m_successName = new QLabel(); m_successWorkNo = new QLabel();
    m_successDept = new QLabel(); m_successTime = new QLabel(); m_successSimilarity = new QLabel();
    addInfoRow("👤", QStringLiteral("识别人员"), "", m_successName);
    addInfoRow("🆔", QStringLiteral("工号"), "", m_successWorkNo);
    addInfoRow("🏢", QStringLiteral("所属部门"), "", m_successDept);
    addInfoRow("🕐", QStringLiteral("验证时间"), "", m_successTime);

    // 相似度单独一行（次要信息）
    auto* simRow = new QWidget();
    simRow->setStyleSheet("background:transparent;");
    auto* simLayout = new QHBoxLayout(simRow);
    simLayout->setContentsMargins(0, 0, 0, 0);
    simLayout->setSpacing(10);
    auto* simLabel = new QLabel(QStringLiteral("📊 相似度"));
    simLabel->setFixedWidth(110);
    simLabel->setStyleSheet("font-size:14px; color:#888888; font-weight:500; background:transparent;");
    m_successSimilarity->setStyleSheet("font-size:14px; color:#888888; font-weight:500; background:transparent;");
    simLayout->addWidget(simLabel);
    simLayout->addWidget(m_successSimilarity, 1);
    sbl->addWidget(simRow);

    // 分割线 + 成功提示
    auto* sepLine = new QFrame();
    sepLine->setFrameShape(QFrame::HLine);
    sepLine->setStyleSheet("background:#bbf7d0; border:none; max-height:1px;");
    sbl->addWidget(sepLine);

    auto* successMsg = new QLabel(QStringLiteral("✅ 身份验证通过，即将进入系统..."));
    successMsg->setWordWrap(true);
    successMsg->setAlignment(Qt::AlignCenter);
    successMsg->setStyleSheet("font-size:16px; color:#166534; font-weight:700;"
                               "background:transparent; padding-top:6px;");
    sbl->addWidget(successMsg);

    layout->addWidget(m_successBox);
}

/** 构建陌生人警告卡片（Web: .stranger-box，默认隐藏） */
void LoginPage::buildStrangerCard(QVBoxLayout* layout) {
    m_strangerBox = new QWidget();
    m_strangerBox->setVisible(false);
    m_strangerBox->setStyleSheet("background:#fffbe6; border:2px solid #ffe58f; border-radius:12px; padding:16px 20px;");
    auto* strl = new QVBoxLayout(m_strangerBox);
    strl->setSpacing(5);

    // 陌生人检测信息行
    m_strangerTime = new QLabel();
    m_strangerTime->setWordWrap(true);
    m_strangerTime->setStyleSheet("font-size:15px; color:#333; background:transparent; padding:5px 0;");
    strl->addWidget(m_strangerTime);
    // 陌生人ID
    m_strangerIdLabel = new QLabel();
    m_strangerIdLabel->setWordWrap(true);
    m_strangerIdLabel->setStyleSheet("font-size:15px; color:#333; background:transparent; padding:5px 0;");
    strl->addWidget(m_strangerIdLabel);
    // IP地址
    m_strangerIpLabel = new QLabel();
    m_strangerIpLabel->setWordWrap(true);
    m_strangerIpLabel->setStyleSheet("font-size:15px; color:#333; background:transparent; padding:5px 0;");
    strl->addWidget(m_strangerIpLabel);

    // 陌生人警告消息 (Web: .stranger-msg 15px #d48806)
    auto* strMsg = new QLabel(QStringLiteral("⚠️ 检测到未授权人员，请联系管理员录入信息"));
    strMsg->setWordWrap(true);
    strMsg->setStyleSheet("font-size:15px; color:#d48806; font-weight:700;"
                          "padding-top:8px; margin-top:8px; border-top:1px solid #ffd591; background:transparent;");
    strl->addWidget(strMsg);

    // 陌生人操作按钮区 (Web: .stranger-actions flex-direction:column gap:12px)
    auto* strActionLayout = new QVBoxLayout();
    strActionLayout->setSpacing(12);

    auto* strConfirmBtn = new QPushButton(QStringLiteral("确认并继续使用"));
    strConfirmBtn->setStyleSheet(StyleHelper::buttonPrimary());
    strConfirmBtn->setCursor(Qt::PointingHandCursor);
    strConfirmBtn->setMinimumHeight(StyleHelper::Token::ControlHeightLarge);
    auto scClicked = static_cast<void(QPushButton::*)(bool)>(&QPushButton::clicked);
    connect(strConfirmBtn, scClicked, this, [this](bool) {
        handleStrangerConfirm();
    });
    strActionLayout->addWidget(strConfirmBtn);

    auto* strRetryBtn = new QPushButton(QStringLiteral("🔄 重新尝试识别"));
    strRetryBtn->setStyleSheet(StyleHelper::buttonOutline());
    strRetryBtn->setCursor(Qt::PointingHandCursor);
    strRetryBtn->setMinimumHeight(StyleHelper::Token::ControlHeightLarge);
    connect(strRetryBtn, scClicked, this, [this](bool) {
        retryFace();
    });
    strActionLayout->addWidget(strRetryBtn);

    strl->addLayout(strActionLayout);
    layout->addWidget(m_strangerBox);
}

/** 构建右上角悬浮退出按钮（半透明圆形，不抢登录画面视觉焦点） */
void LoginPage::buildExitButton() {
    m_exitBtn = new QPushButton(QStringLiteral("✕"), this);
    m_exitBtn->setFixedSize(48, 48);  // 满足触屏最小点击尺寸
    m_exitBtn->setCursor(Qt::PointingHandCursor);
    m_exitBtn->setStyleSheet(
        "QPushButton{"
        "  font-size:20px; font-weight:700; color:rgba(255,255,255,0.6);"
        "  background:rgba(255,255,255,0.08); border:2px solid rgba(255,255,255,0.2);"
        "  border-radius:24px;"
        "}"
        "QPushButton:hover{"
        "  color:rgba(255,255,255,0.9);"
        "  background:rgba(255,255,255,0.15);"
        "  border-color:rgba(255,255,255,0.4);"
        "}"
        "QPushButton:pressed{"
        "  background:rgba(255,255,255,0.25);"
        "}");
    connect(m_exitBtn, &QPushButton::clicked, this, &LoginPage::onExitSystem);
    // 右上角悬浮定位
    m_exitBtn->move(this->width() - 60, 12);
    m_exitBtn->raise();
}

/** 构建底部版权行（Web: .login-footer 13px #cccccc） */
void LoginPage::buildCopyright(QVBoxLayout* layout) {
    auto* copyright = new QLabel(QStringLiteral("成都成飞电子科技有限公司 © 2026"));
    copyright->setAlignment(Qt::AlignCenter);
    copyright->setStyleSheet("font-size:13px; color:#cccccc; background:transparent; margin-top:16px;");
    layout->addWidget(copyright);
}

// ============ 人脸识别流程 (复刻Vue版) ============

/** 更新状态圆点颜色 */
void LoginPage::setStatusDot(const QString& colorStyle) {
    if (m_statusDot) {
        m_statusDot->setStyleSheet(QString("border-radius:4px; min-width:8px; min-height:8px;") + colorStyle);
    }
}

void LoginPage::startFaceRecognition() {
    m_faceResult = FaceResult::Scanning;  // 重置状态，防止上次fail状态残留
    m_samples.clear();
    m_captureCount = 0;
    m_isVerifying = false;
    m_verifyBudget = 0;        // 新一轮识别，重置采样预算
    m_verifyStartMs = 0;

    // 重置所有面板可见性
    m_successBox->setVisible(false);
    m_strangerBox->setVisible(false);
    m_passwordForm->setVisible(false);
    m_tryFaceBtn->setVisible(false);
    m_errorLabel->setVisible(false);
    // 恢复摄像头+隐藏所有状态圆圈+文字 (扫描模式：仅显示FaceCamera)
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

    m_faceCamera->reset();
    m_hasCamera = m_faceCamera->hasCamera();

    // 设置状态点为蓝色闪烁 (Web: .dot-blue animation:blink 1.2s infinite)
    setStatusDot("background:#4da3ff;");
    // 清理旧的闪烁定时器，防止重复startFaceRecognition时内存泄漏和信号堆积
    if (m_dotBlinkTimer) {
        m_dotBlinkTimer->stop();
        m_dotBlinkTimer->disconnect();
        delete m_dotBlinkTimer;
        m_dotBlinkTimer = nullptr;
    }
    m_dotBlinkTimer = new QTimer(this);
    m_dotBlinkTimer->setInterval(600);  // Web: 1.2s周期 = 600ms亮+600ms暗
    connect(m_dotBlinkTimer, &QTimer::timeout, this, [this]() {
        if (m_destroying) return;
        static bool visible = true;
        visible = !visible;
        if (visible) {
            m_statusDot->setStyleSheet("background:#4da3ff; border-radius:4px; min-width:8px; min-height:8px;");
        } else {
            m_statusDot->setStyleSheet("background:rgba(77,163,255,0.2); border-radius:4px; min-width:8px; min-height:8px;");
        }
    });

    if (m_hasCamera) {
        m_subtitleLabel->setText(QStringLiteral("请面向摄像头完成身份验证"));
        m_cameraStatusText->setText(QStringLiteral("正在初始化人脸识别..."));
    } else {
        m_subtitleLabel->setText(QStringLiteral("模拟人脸识别模式 (无摄像头)"));
        m_cameraStatusText->setText(QStringLiteral("正在生成模拟人脸..."));
    }

    m_faceCamera->startCamera();
    m_faceRecognitionActive = true;
    m_captureProgress->setVisible(false);

    // 总超时90s→30s，用户体验优化
    m_timeoutTimer->start(30000);
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_destroying) return;  // 析构保护
        if (m_faceResult == FaceResult::Scanning || m_faceResult == FaceResult::Capturing) {
            qDebug() << "[LoginPage] Face recognition timeout, falling back to password";
            stopFaceRecognition();
            setFaceResult(FaceResult::Fail);
            // 对齐Web版：超时显示"人脸验证未通过"而非"人脸验证超时"
            m_subtitleLabel->setText(QStringLiteral("人脸验证未通过"));
            m_errorLabel->setText(QStringLiteral("⚠️ 人脸识别超时，请使用账号密码登录"));
            m_errorLabel->setVisible(true);
            m_cameraStatusText->setText(QStringLiteral("人脸识别超时，请使用账号密码登录"));
        }
    }, Qt::SingleShotConnection);
}

void LoginPage::stopFaceRecognition() {
    m_timeoutTimer->stop();
    m_autoJumpTimer->stop();
    // 停止闪烁
    if (m_dotBlinkTimer) {
        m_dotBlinkTimer->stop();
        delete m_dotBlinkTimer;
        m_dotBlinkTimer = nullptr;
    }
    if (m_faceCamera) m_faceCamera->stopCamera();
    m_faceRecognitionActive = false;
}

void LoginPage::onFaceDetected() {
    m_faceResult = FaceResult::Scanning;
    // 距离过远时保持"请靠近"提示，不被"已检测到人脸"覆盖
    if (m_faceCamera && m_faceCamera->isFaceTooFar()) {
        m_cameraStatusText->setText(QStringLiteral("请靠近"));
        m_cameraStatusText->setStyleSheet(
            "font-size:16px; font-weight:700; color:#fa8c16; background:transparent;");
        return;
    }
    m_cameraStatusText->setText(QStringLiteral("已检测到人脸，请保持不动..."));
}

void LoginPage::onFaceLost() {
    // 人离开摄像头画面 → 清除注销抑制（含截止时间），恢复正常自动刷脸登录
    m_logoutSuppressed = false;
    m_logoutSuppressUntilMs = 0;
    if (m_faceResult == FaceResult::Scanning) {
        m_cameraStatusText->setText(QStringLiteral("正在检测人脸，请对准摄像头..."));
    }
}

/**
 * @brief 距离过远提示
 * @param tooFar true=人脸框过小
 * 仅在扫描阶段改状态文案：距离远时特征像素不足，识别必然失败，
 * 此时引导用户靠近比继续比对更有意义。识别成功后不再覆盖结果提示。
 */
void LoginPage::onFaceTooFarChanged(bool tooFar) {
    if (!tooFar || m_faceResult != FaceResult::Scanning) return;
    m_cameraStatusText->setText(QStringLiteral("请靠近"));
    m_cameraStatusText->setStyleSheet(
        "font-size:16px; font-weight:700; color:#fa8c16; background:transparent;");
    m_subtitleLabel->setText(QStringLiteral("距离过远，请靠近摄像头"));
}

void LoginPage::onFaceCaptured(const QImage& image, double confidence) {
    // 竞态条件防护：已登录成功或待登录中，拒绝任何后续采集回调
    // 根因：captureNow()异步提取完成后emit captureReady，此时handleFaceSuccess()已调用
    // stopFaceRecognition()但没有等待异步提取完成，导致fail状态覆盖success显示

    if (m_faceResult == FaceResult::Success || !m_pendingUser.isEmpty()) {
        qDebug() << "[LoginPage] onFaceCaptured ignored: already in success/pending state";
        return;
    }

    // 注销后抑制自动登录：仅在抑制窗口内且人未离开画面时不自动识别回登
    if (isLogoutSuppressed()) {
        m_cameraStatusText->setText(QStringLiteral("已注销，请离开摄像头画面后重新刷脸登录"));
        m_faceCamera->reset();
        m_faceResult = FaceResult::Scanning;
        return;
    }

    // 修复描述符为空时静默返回、用户无任何提示的致命Bug
    QString descriptor = m_faceCamera->getLastDescriptor();
    if (descriptor.isEmpty()) {
        qWarning() << "[LoginPage] 人脸特征提取失败(描述符为空), confidence:" << confidence;

        // 距离过远优先提示：此时特征必然不可用，直接引导靠近而不是让用户
        // 看到"识别失败"（用户会误以为自己没录入）
        if (m_faceCamera && m_faceCamera->isFaceTooFar()) {
            m_cameraStatusText->setText(QStringLiteral("请靠近"));
            m_cameraStatusText->setStyleSheet(
                "font-size:16px; font-weight:700; color:#fa8c16; background:transparent;");
            m_subtitleLabel->setText(QStringLiteral("距离过远，请靠近摄像头"));
            m_faceCamera->reset();
            m_faceResult = FaceResult::Scanning;
            m_verifyBudget = 0;        // 距离原因，重置预算避免浪费在无效帧上
            m_verifyStartMs = 0;
            return;
        }

        // 已采集足够帧数但没有有效特征 → 直接切换密码登录
        if (m_captureCount >= 2) {
            stopDotBlink();
            setStatusDot("background:#ff4d4f;");
            setFaceResult(FaceResult::Fail);
            m_subtitleLabel->setText(QStringLiteral("人脸验证未通过"));
            m_errorLabel->setText(QStringLiteral("⚠️ 人脸特征提取失败，请使用账号密码登录"));
            m_errorLabel->setVisible(true);
            m_cameraStatusText->setText(QStringLiteral("人脸识别失败，请使用账号密码登录"));
        } else {
            // 第一帧就失败 → 重置重试
            m_faceCamera->reset();
            m_faceResult = FaceResult::Scanning;
            m_cameraStatusText->setText(QStringLiteral("正在检测人脸，请对准摄像头..."));
        }
        return;
    }

    m_faceResult = FaceResult::Capturing;
    m_cameraStatusText->setText(QStringLiteral("正在验证身份..."));
    m_captureProgress->setVisible(false);

    // 增强质量过滤：检查特征维度+置信度+描述符非空
    //   原#8问题：无姿态/光照/模糊度检查
    int descDim = descriptor.split(",").size();
    double quality = confidence * 0.6 + (descDim >= 128 ? 0.4 : 0.2);
    if (confidence < 0.60 || descDim < 64) {
        m_faceCamera->reset();
        m_faceResult = FaceResult::Scanning;
        return;
    }
    m_samples.append({descriptor, image, confidence, quality});
    m_captureCount = m_samples.size();

    // 极速优化：第一帧直接验证，不等待多帧
    // 原逻辑：置信度>0.85且2帧 → 等待时间长
    // 新逻辑：只要特征有效(descDim>=128)直接验证，1帧搞定
    // 提速：2-3s → 1s

    if (descDim >= 128 && m_captureCount >= 1) {
        collectBestSample();
        return;
    }

    // 继续采集
    if (m_captureCount < 2) {
        m_captureProgress->setText(QStringLiteral("已采集 %1/2 帧，请保持面部自然...").arg(m_captureCount));
        m_captureProgress->setVisible(true);
        m_faceCamera->reset();
        m_faceResult = FaceResult::Scanning;
        // 对齐Web端：采集期间保持"已检测到人脸"状态文字
        m_cameraStatusText->setText(QStringLiteral("已检测到人脸，请保持不动..."));
    } else {
        collectBestSample();
    }
}

void LoginPage::collectBestSample() {
    // 竞态防护：已登录成功不采集
    if (m_samples.isEmpty() || m_isVerifying || m_faceResult == FaceResult::Success) return;
    if (!m_pendingUser.isEmpty()) return;
    m_isVerifying = true;
    m_faceResult = FaceResult::Capturing;
    m_cameraStatusText->setText(QStringLiteral("正在验证身份..."));
    m_captureProgress->setVisible(false);

    // 按质量排序：从最优帧开始逐帧尝试（失败会自动换下一帧，见 handleVerifyFailure）
    std::sort(m_samples.begin(), m_samples.end(),
              [](const FaceSample& a, const FaceSample& b) { return a.quality > b.quality; });

    if (m_verifyBudget == 0) {
        m_verifyStartMs = QDateTime::currentMSecsSinceEpoch();
    }
    auto& best = m_samples.first();
    verifyFace(best.descriptor, best.image);
}

/// 人脸验证 — 直接本地比对，不走8088后端
/// @author 袁燕 - 架构简化：去掉8088 C++后端依赖，Qt客户端直接连MySQL比对
/// 原方案：HTTP POST 8088/api/auth/face → 后端比对 → 返回结果（异步+降级复杂）
/// 新方案：直接调用 FaceRecognitionService 本地比对（同步，简洁可靠）
/// 现场无Web前端，8088后端不需要部署
void LoginPage::verifyFace(const QString& descriptor, const QImage& image) {
    Q_UNUSED(image);
    // 竞态防护：已登录成功则不重复验证
    if (m_faceResult == FaceResult::Success || !m_pendingUser.isEmpty()) {
        qDebug() << "[LoginPage] verifyFace ignored: already in success state";
        return;
    }
    // 直接本地比对，无需HTTP请求
    doLocalFaceVerify(descriptor);
}

/// 本地FaceRecognitionService验证
/// @param descriptor 逗号分隔的128维face-api.js深度学习特征
/// 阈值对齐FaceRecognitionService默认值(0.94/0.95/0.35/0.15/0.80)
/// 单人脸模式必须95%以上才通过，陌生人绝对不能登录
void LoginPage::doLocalFaceVerify(const QString& descriptor) {
    FaceRecognitionService svc;
    auto result = svc.matchFace(descriptor);  // 使用默认参数(0.94/0.95/0.35/0.15/0.80)

    // 记录本次相似度：便于现场判断"差多少到阈值"（SC_LOG_SENSITIVE=1 可见）
    ++m_verifyBudget;
    qInfo() << "[LoginPage] 第" << m_verifyBudget << "帧比对 相似度="
            << QString::number(result.similarity, 'f', 4)
            << (result.success ? "通过" : "未通过");

    if (result.success) {
        QJsonObject resp;
        resp["success"] = true;
        resp["userId"] = result.userId;
        resp["username"] = result.username;
        resp["userName"] = result.realName;
        resp["work_no"] = result.workNo;
        resp["department"] = result.department;
        resp["similarity"] = result.similarity * 100.0;
        resp["role"] = result.role.isEmpty() ? SC::ROLE_USER : result.role;  // 从DB读取真实角色，不硬编码SC::ROLE_USER
        resp["token"] = QString::number(QDateTime::currentSecsSinceEpoch());
        handleFaceSuccess(resp);
    } else if (result.isStranger) {
        QJsonObject resp;
        resp["isStranger"] = true;
        resp["timestamp"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        resp["strangerId"] = "UNKNOWN";
        resp["clientIp"] = QStringLiteral("本地终端");
        handleFaceStranger(resp);
    } else {
        handleVerifyFailure(result.message.isEmpty()
            ? QStringLiteral("人脸验证失败") : result.message);
    }
}

/// 人脸识别成功处理 (HTTP和本地共用)
void LoginPage::handleFaceSuccess(const QJsonObject& resp) {
    // 双重调用防护：已成功则忽略
    if (m_faceResult == FaceResult::Success || !m_pendingUser.isEmpty()) {
        qDebug() << "[LoginPage] handleFaceSuccess ignored: already in success state";
        return;
    }
    stopDotBlink();
    // 隐藏采集进度提示，避免识别成功后残留"已采集2/3帧"文字
    m_captureProgress->setVisible(false);
    // 识别成功后立即关闭摄像头释放硬件资源
    stopFaceRecognition();
    setStatusDot("background:#52c41a;");
    setFaceResult(FaceResult::Success);
    // 显示成功状态圆圈+下方文字 (1:1复刻Web版 .camera-area.success)
    m_faceCamera->setVisible(false);
    m_statusCircleSuccess->setVisible(true);
    m_statusCircleFail->setVisible(false);
    m_statusCircleStranger->setVisible(false);
    if (m_successStatusText) m_successStatusText->setVisible(true);
    if (m_failStatusText) m_failStatusText->setVisible(false);
    if (m_strangerStatusText) m_strangerStatusText->setVisible(false);
    m_altLoginHint->setVisible(false);
    m_successBox->setVisible(true);

    QString realName = resp["userName"].toString();
    QString workNo = resp["work_no"].toString();
    QString department = resp["department"].toString();
    double similarityPct = resp["similarity"].toDouble(0);

    m_successName->setText(realName);
    m_successWorkNo->setText(workNo);
    m_successDept->setText(department);
    m_successTime->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    m_successSimilarity->setText(QString("%1%").arg(QString::number(similarityPct, 'f', 1)));
    m_subtitleLabel->setText(QStringLiteral("身份验证通过"));
    m_cameraStatusText->setText(QStringLiteral("人脸识别成功，身份已验证"));

    QJsonObject user;
    user["userId"] = resp["userId"].toInt();
    user["username"] = resp["username"].toString();
    user["realName"] = realName;
    user["workNo"] = workNo;
    user["department"] = department;
    user["role"] = resp["role"].toString();
    user["token"] = resp["token"].toString();
    m_pendingUser = user;
    m_autoJumpTimer->start(1200);  // 2s→1.2s，加快成功跳转
}

/// 陌生人处理 (HTTP和本地共用)
void LoginPage::handleFaceStranger(const QJsonObject& resp) {
    // 竞态防护：已登录成功则忽略陌生人回调
    if (m_faceResult == FaceResult::Success || !m_pendingUser.isEmpty()) {
        qDebug() << "[LoginPage] handleFaceStranger ignored: already in success state";
        return;
    }
    stopDotBlink();
    // 距离过远时不要判定为陌生人：特征质量不足会导致相似度偏低，
    // 此时把人判成"陌生人"会让已录入用户困惑，应引导靠近后重试
    if (m_faceCamera && m_faceCamera->isFaceTooFar()) {
        m_cameraStatusText->setText(QStringLiteral("请靠近"));
        m_cameraStatusText->setStyleSheet(
            "font-size:16px; font-weight:700; color:#fa8c16; background:transparent;");
        m_subtitleLabel->setText(QStringLiteral("距离过远，请靠近摄像头"));
        m_faceResult = FaceResult::Scanning;
        m_faceCamera->reset();
        return;
    }
    // 陌生人检测后停止摄像头采集
    stopFaceRecognition();
    setStatusDot("background:#faad14;");
    setFaceResult(FaceResult::Stranger);
    // 显示陌生人状态圆圈+下方文字 (1:1复刻Web版 .camera-area.stranger)
    m_faceCamera->setVisible(false);
    m_statusCircleSuccess->setVisible(false);
    m_statusCircleFail->setVisible(false);
    m_statusCircleStranger->setVisible(true);
    if (m_successStatusText) m_successStatusText->setVisible(false);
    if (m_failStatusText) m_failStatusText->setVisible(false);
    if (m_strangerStatusText) m_strangerStatusText->setVisible(true);
    m_altLoginHint->setVisible(false);
    m_strangerBox->setVisible(true);
    m_strangerTime->setText(QStringLiteral("检测时间: %1")
        .arg(resp["timestamp"].toString(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"))));
    m_strangerIdLabel->setText(QStringLiteral("陌生人ID: %1").arg(resp["strangerId"].toString()));
    m_strangerIpLabel->setText(QStringLiteral("IP地址: %1").arg(resp["clientIp"].toString("本地终端")));
    m_subtitleLabel->setText(QStringLiteral("陌生人警报"));
    m_cameraStatusText->setText(QStringLiteral("检测到陌生人，该人员不在库中"));
}

/// 处理验证失败，尝试备用帧
/// 致命Bug：用户切回密码登录时onPasswordLoginClicked()清空了m_samples，
/// 但之前人脸验证的异步HTTP回调可能尚未到达，导致removeFirst()在空列表上断言崩溃
void LoginPage::handleVerifyFailure(const QString& errMsg) {
    // 竞态防护：已登录成功则忽略验证失败回调
    // 场景：handleFaceSuccess→stopFaceRecognition→异步captureReady到达→验证→失败→此处
    // 此时successBox已显示、autoJumpTimer已启动，禁止fail状态覆盖
    if (m_faceResult == FaceResult::Success || !m_pendingUser.isEmpty()) {
        qDebug() << "[LoginPage] handleVerifyFailure ignored: already in success state";
        m_isVerifying = false;
        return;
    }
    m_isVerifying = false;
    // 防御：m_samples可能已被异步清空（用户切换登录模式）
    if (m_samples.isEmpty()) {
        // 本轮候选帧用尽：若仍在采样预算内且未超时，继续等待新一帧，
        // 而不是立刻让用户输密码（远距离时特征相似度天然偏低，
        // 多给几次采样机会即可命中，且不降低任何阈值）
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_verifyStartMs;
        const int budgetLeft = SC::FACE_MATCH_MAX_FRAMES - m_verifyBudget;
        if (budgetLeft > 0 && elapsed < SC::FACE_MATCH_MAX_WAIT_MS) {
            m_cameraStatusText->setText(QStringLiteral("请保持不动，正在重试..."));
            m_faceResult = FaceResult::Scanning;
            return;   // 等待 onFaceCaptured 送来下一帧
        }
        setFaceResult(FaceResult::Fail);
        return;
    }
    m_samples.removeFirst();
    if (!m_samples.isEmpty()) {
        // 尝试下一帧 (复刻Web版逐帧重试)
        qDebug() << "[LoginPage] 当前帧失败，尝试备用帧，剩余:" << m_samples.size();
        auto& next = m_samples.first();
        verifyFace(next.descriptor, next.image);
        return;
    }
    // 所有帧都失败 → 降级密码登录
    stopDotBlink();
    stopFaceRecognition();
    setStatusDot("background:#ff4d4f;");
    setFaceResult(FaceResult::Fail);
    m_subtitleLabel->setText(QStringLiteral("人脸验证未通过"));
    m_errorLabel->setText(QStringLiteral("⚠️ %1").arg(errMsg));
    m_errorLabel->setVisible(true);
    m_cameraStatusText->setText(QStringLiteral("人脸识别失败，请使用账号密码登录"));
    m_altLoginHint->setText(QStringLiteral("🔑 使用账号密码登录"));
}

void LoginPage::onCameraError(const QString& msg) {
    m_hasCamera = false;
    stopDotBlink();
    setStatusDot("background:#ff4d4f;");
    setFaceResult(FaceResult::NoCamera);
    m_faceCamera->setVisible(false);
    m_errorLabel->setText(QStringLiteral("⚠️ 摄像头不可用: %1").arg(msg));
    m_errorLabel->setVisible(true);
    m_subtitleLabel->setText(QStringLiteral("摄像头未就绪"));
    m_cameraStatusText->setText(QStringLiteral("摄像头不可用，请使用账号密码登录"));
    m_altLoginHint->setVisible(false);
}

void LoginPage::onFaceStateChanged(int state) {}

// ============ 密码登录 (复刻Vue版 + NumKeypad数字键盘) ============

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
        QTimer::singleShot(500, this, [this]() {
            if (m_numKeypad) {
                m_numKeypad->hide();
            }
            onPasswordFieldClicked();
        });
    }
}

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
    m_cameraStatusText->setText(QStringLiteral("正在初始化人脸识别..."));
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

    // 10. 延迟重新启动人脸识别（对齐构造函数中的500ms延迟）
    QTimer::singleShot(500, this, &LoginPage::startFaceRecognition);
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
    m_cameraStatusText->setText(QStringLiteral("已切换到账号密码登录"));
    // cameraWrap保持可见(fail圆圈已由setFaceResult管理)，不额外隐藏
}

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

void LoginPage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_exitBtn) {
        m_exitBtn->move(this->width() - 60, 12);
        m_exitBtn->raise();
    }
}

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

QString LoginPage::faceResultName(FaceResult result) {
    switch (result) {
    case FaceResult::Idle:      return QStringLiteral("idle");
    case FaceResult::Scanning:  return QStringLiteral("scanning");
    case FaceResult::Capturing: return QStringLiteral("capturing");
    case FaceResult::Success:   return QStringLiteral("success");
    case FaceResult::Fail:      return QStringLiteral("fail");
    case FaceResult::Stranger:  return QStringLiteral("stranger");
    case FaceResult::NoCamera:  return QStringLiteral("no-camera");
    }
    return QStringLiteral("unknown");
}

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