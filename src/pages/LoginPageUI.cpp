/**
 * @file LoginPageUI.cpp
 * @brief 登录页-界面构建（左侧品牌区/右侧认证区及其子区块）
 * @author 袁燕
 *
 * 本文件实现上述功能，成员函数声明见 LoginPage.h。
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

// ==============================================================

// 【① 界面构建（左侧品牌区 / 右侧认证区及其子区块）

//   改控件布局、样式、文案 → 本区；右侧认证区已拆为 buildXxx 系列方法

// ==============================================================

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

    m_successStatusText->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontBody, StyleHelper::textDisabled(), 600));

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

    m_failStatusText->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontBody, StyleHelper::textDisabled(), 600));

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

    m_strangerStatusText->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontBody, StyleHelper::textDisabled(), 600));

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

    m_cameraStatusText->setMinimumWidth(300);  // 确保文案一行显示全

    m_cameraStatusText->setWordWrap(true);

    // 初始样式与 setScanStatus 的 level 0 保持一致（浅灰蓝）

    m_cameraStatusText->setStyleSheet(

        QStringLiteral("font-size:14px; color:#888888; font-weight:600; background:transparent;"));

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

    m_strangerTime->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontLabel, StyleHelper::textBody(), 400, "padding:5px 0;"));

    strl->addWidget(m_strangerTime);

    // 陌生人ID

    m_strangerIdLabel = new QLabel();

    m_strangerIdLabel->setWordWrap(true);

    m_strangerIdLabel->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontLabel, StyleHelper::textBody(), 400, "padding:5px 0;"));

    strl->addWidget(m_strangerIdLabel);

    // IP地址

    m_strangerIpLabel = new QLabel();

    m_strangerIpLabel->setWordWrap(true);

    m_strangerIpLabel->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontLabel, StyleHelper::textBody(), 400, "padding:5px 0;"));

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



// ==============================================================

// 【② 状态提示与识别流程控制

//   状态点/提示文案统一走 setScanStatus，改提示色与文案看这里

// ==============================================================

void LoginPage::setStatusDot(const QString& colorStyle) {

    if (m_statusDot) {

        m_statusDot->setStyleSheet(QString("border-radius:4px; min-width:8px; min-height:8px;") + colorStyle);

    }

}


