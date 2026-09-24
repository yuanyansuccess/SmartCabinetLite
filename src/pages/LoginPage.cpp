/**
 * @file LoginPage.cpp
 * @brief 登录页面实现 - 双栏布局+刷脸登录+密码降级 (1:1复刻Vue版Login.vue)
 * @author 袁燕
 *
 * [2026-06-14] 完善BS端复刻：接入状态圆点、陌生人卡片、密码表单提示
 * [2026-06-21v3] 人脸识别改走后端API：发送face image→后端face-api.js提取特征→余弦比对
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
#include <QRegularExpression>  // [2026-09-23] 工号纯数字校验
#include <QPixmap>
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QApplication>  // [V2.03u] qApp->quit()退出系统

LoginPage::LoginPage(QWidget* parent) : QWidget(parent),
    m_faceCamera(nullptr) {
    // 暗蓝渐变背景 (复刻Vue版) [2026-06-21] stop0.5→0.4对齐Vue版40%断点
    setStyleSheet("background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #0d1b2a,stop:0.4 #162d45,stop:1 #1e3f5e);");
    setupUI();

    // 初始化数字键盘
    // [2026-09-23] 账号改为纯数字工号：用户名/密码统一由NumKeypad输入，移除字母软键盘
    m_numKeypad = new NumKeypad(this);
    connect(m_numKeypad, &NumKeypad::confirmed, this, [this]() {
        m_numKeypad->hide();
        if (m_activeField == "username") {
            // 工号确认 → 切换到密码输入
            m_passwordEdit->clear();
            m_activeField = "password";
            QTimer::singleShot(150, this, &LoginPage::onPasswordFieldClicked);
        } else if (m_passwordEdit->text().length() >= 6) {
            // 崩溃修复：不能在软键盘 mouseReleaseEvent 事件派发过程中
            // 同步执行 onPasswordLogin()。该函数会发出 loginSuccess → MainWindow::onLoginSuccess，
            // 后者内部还有模态对话框的嵌套事件循环；而此刻键盘面板窗口正在派发鼠标事件，
            // 叠加 hide() 的窗口拆装，Qt 内部状态不一致 → onLoginSuccess 内第一处 Qt 调用
            // （m_topBar->refreshVersionLabel()）崩溃（0xC0000005 读取 0xFFFFFFFFFFFFFFFF）。
            // 改为事件派发结束后的下一轮执行，登录流程在干净的窗口状态下运行。
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
        if (m_destroying) return;  // [v4] 析构保护
        if (!m_pendingUser.isEmpty())
            emit loginSuccess(m_pendingUser);
    });
}

LoginPage::~LoginPage() {
    // [v4] 析构期间必须标记+断开所有信号，防止Qt递归删除子对象时信号触发访问半销毁的this
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
    // [2026-06-23修复] card最小高度750：摄像头区(~300)+状态行(~40)+间距(20)+密码表单(~280)+底部按钮+版权(~50)+余量
    // 原620不够导致底部"重新扫脸"按钮和版权文字被裁剪
    card->setMinimumHeight(750);
    // [v4.7修复] WA_StyledBackground启用后border-radius才能裁剪背景
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setStyleSheet(QString(
        "background:%1; border-radius:20px;"
    ).arg(StyleHelper::whiteColor()));
    // [V8.2 2026-06-25] 去除登录卡片外阴影

    auto* cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    // 左侧品牌区 (1:1复刻Vue版 login-left: flex:1, 454px)
    auto* leftWidget = new QWidget();
    // [v4.7修复] 移除fixedWidth(454)，使用stretch比例动态计算
    // card总宽1000px, stretch 454:546 = Web版 flex:1 vs flex:1.2
    leftWidget->setMinimumWidth(300);  // 防挤压下限
    leftWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    // [v4.7修复] WA_StyledBackground启用border-radius背景裁剪
    leftWidget->setAttribute(Qt::WA_StyledBackground, true);
    leftWidget->setStyleSheet(
        "background:qlineargradient(x1:0,y1:0,x2:0.34,y2:1,stop:0 #132940,stop:1 #1a3f60);"
        "border-top-left-radius:20px; border-bottom-left-radius:20px;");
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(40, 60, 40, 40);  // [2026-06-26] 上边距50→60让中间内容视觉下沉更均衡，下边距50→40配合stretch布局
    leftLayout->setAlignment(Qt::AlignCenter);
    leftLayout->setSpacing(18);  // [2026-06-26] 间距20→18紧凑化
    setupLeftPanel(leftLayout);

    // 右侧认证区 (复刻Vue版 login-right)
    auto* rightWidget = new QWidget();
    // [v4.7修复] WA_StyledBackground启用圆角背景裁剪
    rightWidget->setAttribute(Qt::WA_StyledBackground, true);
    rightWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    rightWidget->setStyleSheet(QString("background:%1; border-top-right-radius:20px; border-bottom-right-radius:20px;")
        .arg(StyleHelper::whiteColor()));
    auto* rightLayout = new QVBoxLayout(rightWidget);
    // [2026-06-23修复] 缩小padding为内容腾空间：50→30(top/bottom)，48→36(left/right)
    rightLayout->setContentsMargins(36, 30, 36, 30);
    rightLayout->setSpacing(12);
    setupRightPanel(rightLayout);

    // [v4.7修复] Web版left:flex:1, right:flex:1.2 → 比例 454:546 (1000*1/2.2=454)
    // 使用stretch精确控制，leftWidget去除fixedWidth让stretch决定实际宽度
    cardLayout->addWidget(leftWidget, 454);
    cardLayout->addWidget(rightWidget, 546);
    outer->addWidget(card, 0, Qt::AlignCenter);

    // [2026-06-23 速度优化] 启动延迟500→100ms，摄像头初始化足够
    QTimer::singleShot(100, this, &LoginPage::startFaceRecognition);
}

void LoginPage::setupLeftPanel(QVBoxLayout* layout) {
    // [2026-06-14] Logo区 - 显示公司logo图片 (复刻Vue版 .logo-wrap)
    auto* logoWrap = new QWidget();
    logoWrap->setFixedSize(200, 80);
    logoWrap->setStyleSheet(
        "background:rgba(255,255,255,0.12); border:1px solid rgba(255,255,255,0.15);"
        "border-radius:20px;");
    auto* ll = new QVBoxLayout(logoWrap);
    ll->setContentsMargins(18, 18, 18, 18);  // [2026-06-21] top/bottom 12→18 对齐Vue版 padding:18px 28px
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
    // [2026-06-26v2] 顶部留白，让Logo区域视觉居中偏上
    layout->addStretch(1);

    layout->addWidget(logoWrap, 0, Qt::AlignCenter);
    layout->addSpacing(16);  // [2026-06-26v2] 24→16 紧凑化，Logo和图标间距收紧

    // 品牌图标 (复刻Vue版 .brand-icon)
    auto* icon = new QLabel(QStringLiteral("🛠"));
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet("font-size:68px; background:transparent; margin-bottom:8px;");  // [2026-06-26v2] margin-bottom:16→8 紧凑化，图标和标题收紧
    layout->addWidget(icon);

    // 品牌名称 (复刻Vue版 .brand-name) [触屏优化 2026-06-15]
    auto* name = new QLabel(QStringLiteral("智能工具柜"));
    name->setAlignment(Qt::AlignCenter);
    name->setStyleSheet("color:white; font-size:28px; font-weight:700; letter-spacing:3px; background:transparent; margin-bottom:6px;");  // [2026-06-26v2] margin-bottom:10→6 紧凑化
    layout->addWidget(name);

    // [2026-06-26] 弹性空间 - 将描述文字推至底部区域，视觉更大气
    layout->addStretch(4);  // [2026-06-26v2] 3→4 加大弹性比例，描述文字更下沉

    // 品牌描述 (复刻Vue版 .brand-desc) [触屏优化 2026-06-15]
    auto* desc = new QLabel(QStringLiteral("智能化工具管理系统\n视觉识别 · 刷脸认证 · 秒级盘点"));
    desc->setAlignment(Qt::AlignCenter);
    desc->setWordWrap(true);
    desc->setStyleSheet("color:rgba(255,255,255,0.8); font-size:16px; background:transparent; line-height:1.8; margin-bottom:12px;");  // [2026-06-26] 字号15→16，行高1.7→1.8，视觉下沉更沉稳
    layout->addWidget(desc);

    layout->addStretch(1);
}

void LoginPage::setupRightPanel(QVBoxLayout* layout) {
    // 欢迎文字 (复刻Vue版 .welcome-text) [触屏优化 2026-06-15]
    m_welcomeLabel = new QLabel(QStringLiteral("欢迎使用"));
    m_welcomeLabel->setAlignment(Qt::AlignCenter);
    m_welcomeLabel->setStyleSheet(QString("font-size:27px; font-weight:800; color:%1; background:transparent; margin-bottom:4px;")  // [修正] 对齐Vue版 font-size:27px
        .arg("#1a1a2e"));
    layout->addWidget(m_welcomeLabel);

    m_subtitleLabel = new QLabel(QStringLiteral("请面向摄像头完成身份验证"));
    m_subtitleLabel->setAlignment(Qt::AlignCenter);
    m_subtitleLabel->setWordWrap(true);
    m_subtitleLabel->setStyleSheet(QString("font-size:15px; color:%1; background:transparent; margin-bottom:28px;")  // [v4.2] Web: 15px #999 mb:28px
        .arg("#999999"));
    layout->addWidget(m_subtitleLabel);

    // --- 人脸识别区 ---
    m_cameraWrap = new QWidget();  // [v4] 存为成员变量，切换模式时整体显隐
    m_cameraWrap->setStyleSheet("background:transparent;");
    auto* camLayout = new QVBoxLayout(m_cameraWrap);
    camLayout->setAlignment(Qt::AlignCenter);
    camLayout->setSpacing(12);

    // [2026-06-23] 增大摄像头区域260x300，确保录像框(220)和状态提示完整显示
    m_faceCamera = new FaceCameraWidget();
    m_faceCamera->setFixedSize(260, 300);
    // [2026-06-23 速度优化] 稳定帧20→8(640ms足够确认人脸稳定)，采集延迟500→200ms，检测间隔100→80ms
    m_faceCamera->setAutoCapture(true);
    m_faceCamera->setMinConfidence(0.60);
    // [V2.04 2026-06-28] 识别速度优化：8→3帧(240ms)，延迟200→50ms，间隔80→40ms
    // 原参数：8×80+200=840ms 才开始采集
    // 新参数：3×40+50=170ms 即开始采集，提速约5倍
    //   作者：袁燕
    m_faceCamera->setStableFrames(3);
    m_faceCamera->setCaptureDelay(50);
    m_faceCamera->setDetectInterval(40);
    // Web: border:4px dashed #d0d0d0; border-radius:50%;
    m_faceCamera->setStyleSheet(
        "border:4px dashed #d0d0d0; border-radius:90px;"
        "background:#fafbfc;");
    connect(m_faceCamera, &FaceCameraWidget::faceDetected, this, &LoginPage::onFaceDetected);
    connect(m_faceCamera, &FaceCameraWidget::faceLost, this, &LoginPage::onFaceLost);
    connect(m_faceCamera, &FaceCameraWidget::captureReady, this, &LoginPage::onFaceCaptured);
    connect(m_faceCamera, &FaceCameraWidget::errorOccurred, this, &LoginPage::onCameraError);
    connect(m_faceCamera, &FaceCameraWidget::stateChanged, this, &LoginPage::onFaceStateChanged);

    camLayout->addWidget(m_faceCamera, 0, Qt::AlignCenter);

    // [V6.5] 状态圆圈 - 1:1复刻Web版 .camera-area.success/.fail/.stranger
    // Web设计：180x180圆形，4px solid边框，背景色，内部emoji(50px)+文字(14px)
    // 成功: 绿边框#52c41a 浅绿底#f6ffed ✅ "识别成功"
    // 失败: 红边框#ff4d4f 浅红底#fff2f0 ❌ "识别失败"
    // 陌生人: 橙边框#faad14 浅黄底#fffbe6 ⚠️ "检测到陌生人"

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

    // [2026-06-23] 采集进度文字加大确保触屏清晰可读
    m_captureProgress = new QLabel("");
    m_captureProgress->setAlignment(Qt::AlignCenter);
    m_captureProgress->setStyleSheet(QString("font-size:15px; color:%1; background:transparent; font-weight:600; margin-top:8px;")
        .arg(StyleHelper::primaryColor()));
    m_captureProgress->setVisible(false);
    camLayout->addWidget(m_captureProgress);

    // [V6.4] 密码登录入口移入cameraWrap (Web: .alt-login-hint 在scanning模板内，仅扫描时可见)
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

    // [V6.4] 状态指示行移出cameraWrap (Web: .status-line 在摄像头模板外，始终可见)
    auto* statusRow = new QHBoxLayout();
    statusRow->setAlignment(Qt::AlignCenter);
    statusRow->setSpacing(8);

    m_statusDot = new QLabel();
    m_statusDot->setFixedSize(8, 8);
    m_statusDot->setStyleSheet(
        "background:#4da3ff; border-radius:4px;");
    statusRow->addWidget(m_statusDot);

    m_cameraStatusText = new QLabel(QStringLiteral("正在初始化人脸识别..."));
    m_cameraStatusText->setMinimumWidth(280);   // [2026-06-23] 确保长文字一行显示全
    m_cameraStatusText->setWordWrap(true);
    m_cameraStatusText->setStyleSheet(QString("font-size:14px; font-weight:600; color:%1; background:transparent;")
        .arg("#555555"));
    statusRow->addWidget(m_cameraStatusText);
    layout->addLayout(statusRow);
    layout->addSpacing(20);  // [v4.2] Web: .status-line margin-bottom:20px

    // --- [完善] 密码登录区 (默认隐藏) - 复刻Vue版login-form ---
    m_passwordForm = new QWidget();
    m_passwordForm->setVisible(false);
    m_passwordForm->setStyleSheet("background:transparent;");
    auto* pfLayout = new QVBoxLayout(m_passwordForm);
    pfLayout->setContentsMargins(0, 0, 0, 0);
    pfLayout->setSpacing(16);

    // 人脸识别失败/摄像头不可用 提示条 (Web: .form-error-tip)
    m_errorLabel = new QLabel();
    m_errorLabel->setStyleSheet(QString("color:%1; font-size:15px; font-weight:600; background:transparent; margin-bottom:14px;")  // [v4.2] Web: 15px #ff4d4f mb:14px
        .arg(StyleHelper::dangerColor()));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setVisible(false);
    pfLayout->addWidget(m_errorLabel);

    // 工号标签 (Web: .form-label 14px #555) [2026-09-23] 账号改纯数字工号
    auto* unameLabel = new QLabel(QStringLiteral("工号"));
    unameLabel->setStyleSheet(QString("font-size:14px; font-weight:600; color:#555555; background:transparent; margin-bottom:6px;"));
    pfLayout->addWidget(unameLabel);

    // [v4.2 重构] 用户名输入框+键盘按钮并排 (1:1复刻Web版 .password-input-wrap)
    // Web: input(border-radius:10px 0 0 10px) + button(border-radius:0 10px 10px 0, border-left:none)
    // [2026-06-23v3] 修复边框圆角渲染：添加WA_StyledBackground+固定高度对齐内部控件
    auto* unameWrap = new QFrame();
    unameWrap->setAttribute(Qt::WA_StyledBackground, true);
    unameWrap->setFixedHeight(48);
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

    // [v4.2 重构] 密码输入框+键盘按钮并排 (1:1复刻Web版 .password-input-wrap)
    // [2026-06-23v3] 修复边框圆角渲染：添加WA_StyledBackground+固定高度对齐内部控件
    auto* pwdWrap = new QFrame();
    pwdWrap->setAttribute(Qt::WA_StyledBackground, true);
    pwdWrap->setFixedHeight(48);
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

    // [2026-06-26] 数字键盘占位 - 构造函数中创建，此处添加到布局
    // 初始隐藏，点击密码框⌨按钮时显示

    // 登录按钮 (Web: .login-btn border-radius:10px font-size:17px padding:14px)
    m_loginBtn = new QPushButton(QStringLiteral("登  录"));
    // [v4.8修复] 1:1复刻Web版 login-btn: font-size:17px, font-weight:700, padding:14px
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

    // [V6.4] 密码登录入口已移入cameraWrap内 (对齐Web版在scanning模板内含.alt-login-hint)

    // ===== 成功信息卡片 (Web: .info-box) [2026-06-23重写] =====
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

    // ===== 陌生人警告卡片 (Web: .stranger-box) [触屏优化 2026-06-15] =====
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
    strConfirmBtn->setMinimumHeight(56);
    auto scClicked = static_cast<void(QPushButton::*)(bool)>(&QPushButton::clicked);
    connect(strConfirmBtn, scClicked, this, [this](bool) {
        handleStrangerConfirm();
    });
    strActionLayout->addWidget(strConfirmBtn);

    auto* strRetryBtn = new QPushButton(QStringLiteral("🔄 重新尝试识别"));
    strRetryBtn->setStyleSheet(StyleHelper::buttonOutline());
    strRetryBtn->setCursor(Qt::PointingHandCursor);
    strRetryBtn->setMinimumHeight(56);
    connect(strRetryBtn, scClicked, this, [this](bool) {
        retryFace();
    });
    strActionLayout->addWidget(strRetryBtn);

    strl->addLayout(strActionLayout);
    layout->addWidget(m_strangerBox);

    layout->addStretch();

    // 退出按钮改为右上角悬浮设计
    // 设计理念：右上角半透明圆形按钮，不抢登录画面视觉焦点
    // 暗蓝背景上用半透明白色，hover时微亮，符合系统整体暗蓝风格
    // 48px满足触屏最小点击尺寸
    m_exitBtn = new QPushButton(QStringLiteral("✕"), this);
    m_exitBtn->setFixedSize(48, 48);
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

    // 底部版权 (Web: .login-footer 13px #cccccc)
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
    m_faceResult = "scanning";  // [2026-06-21] 重置状态，防止上次fail状态残留
    m_samples.clear();
    m_captureCount = 0;
    m_isVerifying = false;

    // 重置所有面板可见性
    m_successBox->setVisible(false);
    m_strangerBox->setVisible(false);
    m_passwordForm->setVisible(false);
    m_tryFaceBtn->setVisible(false);
    m_errorLabel->setVisible(false);
    // [V6.5] 恢复摄像头+隐藏所有状态圆圈+文字 (扫描模式：仅显示FaceCamera)
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

    // [v4.2] 设置状态点为蓝色闪烁 (Web: .dot-blue animation:blink 1.2s infinite)
    setStatusDot("background:#4da3ff;");
    // [v4] 清理旧的闪烁定时器，防止重复startFaceRecognition时内存泄漏和信号堆积
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
    m_captureProgress->setVisible(false);

    // [2026-06-26v9] 总超时90s→30s，用户体验优化
    m_timeoutTimer->start(30000);
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_destroying) return;  // [v4] 析构保护
        if (m_faceResult == "scanning" || m_faceResult == "capturing") {
            qDebug() << "[LoginPage] Face recognition timeout, falling back to password";
            stopFaceRecognition();
            setFaceResult("fail");
            // [2026-06-23] 对齐Web版：超时显示"人脸验证未通过"而非"人脸验证超时"
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
    // [新增] 停止闪烁
    if (m_dotBlinkTimer) {
        m_dotBlinkTimer->stop();
        delete m_dotBlinkTimer;
        m_dotBlinkTimer = nullptr;
    }
    if (m_faceCamera) m_faceCamera->stopCamera();
}

void LoginPage::onFaceDetected() {
    m_faceResult = "scanning";
    m_cameraStatusText->setText(QStringLiteral("已检测到人脸，请保持不动..."));
}

void LoginPage::onFaceLost() {
    // [2026-09-24] 人离开摄像头画面 → 清除注销抑制，恢复正常自动刷脸登录
    m_logoutSuppressed = false;
    if (m_faceResult == "scanning") {
        m_cameraStatusText->setText(QStringLiteral("正在检测人脸，请对准摄像头..."));
    }
}

void LoginPage::onFaceCaptured(const QImage& image, double confidence) {
    // [V2.03 2026-06-28] 竞态条件防护：已登录成功或待登录中，拒绝任何后续采集回调
    // 根因：captureNow()异步提取完成后emit captureReady，此时handleFaceSuccess()已调用
    // stopFaceRecognition()但没有等待异步提取完成，导致fail状态覆盖success显示
    //   作者：袁燕
    if (m_faceResult == "success" || !m_pendingUser.isEmpty()) {
        qDebug() << "[LoginPage] onFaceCaptured ignored: already in success/pending state";
        return;
    }

    // [2026-09-24] 注销后抑制自动登录：人未离开画面时不自动识别回登，避免注销被立即弹回
    if (m_logoutSuppressed) {
        m_cameraStatusText->setText(QStringLiteral("已注销，请离开摄像头画面后重新刷脸登录"));
        m_faceCamera->reset();
        m_faceResult = "scanning";
        return;
    }

    // [2026-06-21修复] 作者：袁燕 - 修复描述符为空时静默返回、用户无任何提示的致命Bug
    QString descriptor = m_faceCamera->getLastDescriptor();
    if (descriptor.isEmpty()) {
        qWarning() << "[LoginPage] 人脸特征提取失败(描述符为空), confidence:" << confidence;
        // 已采集足够帧数但没有有效特征 → 直接切换密码登录
        if (m_captureCount >= 2) {
            stopDotBlink();
            setStatusDot("background:#ff4d4f;");
            setFaceResult("fail");
            m_subtitleLabel->setText(QStringLiteral("人脸验证未通过"));
            m_errorLabel->setText(QStringLiteral("⚠️ 人脸特征提取失败，请使用账号密码登录"));
            m_errorLabel->setVisible(true);
            m_cameraStatusText->setText(QStringLiteral("人脸识别失败，请使用账号密码登录"));
        } else {
            // 第一帧就失败 → 重置重试
            m_faceCamera->reset();
            m_faceResult = "scanning";
            m_cameraStatusText->setText(QStringLiteral("正在检测人脸，请对准摄像头..."));
        }
        return;
    }

    m_faceResult = "capturing";
    m_cameraStatusText->setText(QStringLiteral("正在验证身份..."));
    m_captureProgress->setVisible(false);

    // [V8.0 2026-06-28] 增强质量过滤：检查特征维度+置信度+描述符非空
    //   作者：袁燕 - 原#8问题：无姿态/光照/模糊度检查
    int descDim = descriptor.split(",").size();
    double quality = confidence * 0.6 + (descDim >= 128 ? 0.4 : 0.2);
    if (confidence < 0.60 || descDim < 64) {
        m_faceCamera->reset();
        m_faceResult = "scanning";
        return;
    }
    m_samples.append({descriptor, image, confidence, quality});
    m_captureCount = m_samples.size();

    // [V2.04 2026-06-28] 极速优化：第一帧直接验证，不再等待多帧
    // 原逻辑：置信度>0.85且2帧 → 等待时间长
    // 新逻辑：只要特征有效(descDim>=128)直接验证，1帧搞定
    // 提速：2-3s → 1s
    //   作者：袁燕
    if (descDim >= 128 && m_captureCount >= 1) {
        collectBestSample();
        return;
    }

    // 继续采集
    if (m_captureCount < 2) {
        m_captureProgress->setText(QStringLiteral("已采集 %1/2 帧，请保持面部自然...").arg(m_captureCount));
        m_captureProgress->setVisible(true);
        m_faceCamera->reset();
        m_faceResult = "scanning";
        // [V6.1] 对齐Web端：采集期间保持"已检测到人脸"状态文字
        m_cameraStatusText->setText(QStringLiteral("已检测到人脸，请保持不动..."));
    } else {
        collectBestSample();
    }
}

void LoginPage::collectBestSample() {
    // [V2.03] 竞态防护：已登录成功不再采集
    if (m_samples.isEmpty() || m_isVerifying || m_faceResult == "success") return;
    if (!m_pendingUser.isEmpty()) return;
    m_isVerifying = true;
    m_faceResult = "capturing";
    m_cameraStatusText->setText(QStringLiteral("正在验证身份..."));
    m_captureProgress->setVisible(false);

    // 按质量排序选最佳
    std::sort(m_samples.begin(), m_samples.end(),
              [](const FaceSample& a, const FaceSample& b) { return a.quality > b.quality; });

    auto& best = m_samples.first();
    verifyFace(best.descriptor, best.image);
}

/// [V2.05 2026-06-28] 人脸验证 — 直接本地比对，不再走8088后端
/// @author 袁燕 - 架构简化：去掉8088 C++后端依赖，Qt客户端直接连MySQL比对
/// 原方案：HTTP POST 8088/api/auth/face → 后端比对 → 返回结果（异步+降级复杂）
/// 新方案：直接调用 FaceRecognitionService 本地比对（同步，简洁可靠）
/// 现场无Web前端，8088后端不需要部署
void LoginPage::verifyFace(const QString& descriptor, const QImage& image) {
    Q_UNUSED(image);
    // [V2.05] 竞态防护：已登录成功则不重复验证
    if (m_faceResult == "success" || !m_pendingUser.isEmpty()) {
        qDebug() << "[LoginPage] verifyFace ignored: already in success state";
        return;
    }
    // 直接本地比对，无需HTTP请求
    doLocalFaceVerify(descriptor);
}

/// [v4 新增] 本地FaceRecognitionService验证
/// @param descriptor 逗号分隔的128维face-api.js深度学习特征
/// [V2.17 2026-07-06] 阈值对齐FaceRecognitionService默认值(0.94/0.95/0.35/0.15/0.80)
/// 单人脸模式必须95%以上才通过，陌生人绝对不能登录
///   作者：袁燕
void LoginPage::doLocalFaceVerify(const QString& descriptor) {
    FaceRecognitionService svc;
    auto result = svc.matchFace(descriptor);  // 使用默认参数(0.94/0.95/0.35/0.15/0.80)

    if (result.success) {
        QJsonObject resp;
        resp["success"] = true;
        resp["userId"] = result.userId;
        resp["username"] = result.username;
        resp["userName"] = result.realName;
        resp["work_no"] = result.workNo;
        resp["department"] = result.department;
        resp["similarity"] = result.similarity * 100.0;
        resp["role"] = result.role.isEmpty() ? "user" : result.role;  // [V6.3] 从DB读取真实角色，不再硬编码"user"
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

/// [v4 提取] 人脸识别成功处理 (HTTP和本地共用)
void LoginPage::handleFaceSuccess(const QJsonObject& resp) {
    // [V2.03] 双重调用防护：已成功则忽略
    if (m_faceResult == "success" || !m_pendingUser.isEmpty()) {
        qDebug() << "[LoginPage] handleFaceSuccess ignored: already in success state";
        return;
    }
    stopDotBlink();
    // [V7.9] 隐藏采集进度提示，避免识别成功后残留"已采集2/3帧"文字
    m_captureProgress->setVisible(false);
    // [2026-06-23] 识别成功后立即关闭摄像头释放硬件资源
    stopFaceRecognition();
    setStatusDot("background:#52c41a;");
    setFaceResult("success");
    // [V6.5] 显示成功状态圆圈+下方文字 (1:1复刻Web版 .camera-area.success)
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
    m_autoJumpTimer->start(1200);  // [2026-06-26v9] 2s→1.2s，加快成功跳转
}

/// [v4 提取] 陌生人处理 (HTTP和本地共用)
void LoginPage::handleFaceStranger(const QJsonObject& resp) {
    // [V2.03] 竞态防护：已登录成功则忽略陌生人回调
    if (m_faceResult == "success" || !m_pendingUser.isEmpty()) {
        qDebug() << "[LoginPage] handleFaceStranger ignored: already in success state";
        return;
    }
    stopDotBlink();
    // [2026-06-23] 陌生人检测后停止摄像头采集
    stopFaceRecognition();
    setStatusDot("background:#faad14;");
    setFaceResult("stranger");
    // [V6.5] 显示陌生人状态圆圈+下方文字 (1:1复刻Web版 .camera-area.stranger)
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

/// [v3 新增] 处理验证失败，尝试备用帧
/// [v5.1修复] 致命Bug：用户切回密码登录时onPasswordLoginClicked()清空了m_samples，
/// 但之前人脸验证的异步HTTP回调可能尚未到达，导致removeFirst()在空列表上断言崩溃
void LoginPage::handleVerifyFailure(const QString& errMsg) {
    // [V2.03] 竞态防护：已登录成功则忽略验证失败回调
    // 场景：handleFaceSuccess→stopFaceRecognition→异步captureReady到达→验证→失败→此处
    // 此时successBox已显示、autoJumpTimer已启动，禁止fail状态覆盖
    if (m_faceResult == "success" || !m_pendingUser.isEmpty()) {
        qDebug() << "[LoginPage] handleVerifyFailure ignored: already in success state";
        m_isVerifying = false;
        return;
    }
    m_isVerifying = false;
    // [v5.1] 防御：m_samples可能已被异步清空（用户切换登录模式）
    if (m_samples.isEmpty()) {
        setFaceResult("fail");
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
    setFaceResult("fail");
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
    setFaceResult("no-camera");
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
    // [2026-09-23] 工号改纯数字：统一使用数字键盘（不随机打乱、明文显示）
    if (m_numKeypad) {
        m_numKeypad->setShuffle(false);
        m_numKeypad->setShowPassword(true);
        m_numKeypad->attach(m_usernameEdit);
        m_numKeypad->show();
    }
}

void LoginPage::onPasswordFieldClicked() {
    m_activeField = "password";
    // [2026-06-26v2] NumKeypad作为顶层Popup弹窗显示，不受布局约束
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
    // [2026-09-23] 工号改为纯数字（与数字键盘输入、DB存储格式统一）
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

/// [V6.3] 重置所有登录状态——退出登录/登出后清除上一用户的所有残留信息
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

/// [V6.3] 清除所有表单输入和采集数据
void LoginPage::clearAllForms() {
    if (m_usernameEdit) m_usernameEdit->clear();
    if (m_passwordEdit) m_passwordEdit->clear();
    m_samples.clear();
    m_captureCount = 0;
    m_faceResult = "scanning";
}

/** [v2.0] 切换到密码登录 */
void LoginPage::switchToPasswordLogin() {
    stopFaceRecognition();
    setFaceResult("fail");
    stopDotBlink();
    setStatusDot("background:#ff4d4f;");
    m_errorLabel->setText(QStringLiteral("⚠️ 人脸识别失败，请使用账号密码登录"));
    m_errorLabel->setVisible(true);
    m_subtitleLabel->setText(QStringLiteral("请输入账号密码"));
    m_cameraStatusText->setText(QStringLiteral("已切换到账号密码登录"));
    // [V6.4] cameraWrap保持可见(fail圆圈已由setFaceResult管理)，不再额外隐藏
}

void LoginPage::onTryFaceAgain() {
    if (m_faceResult == "fail" || m_faceResult == "no-camera") {
        // 当前在密码模式，切换回人脸识别
        retryFace();
    } else {
        // 当前在人脸模式，切到密码模式
        switchToPasswordLogin();
    }
}

/** [复刻Vue版] 重新尝试人脸识别 */
void LoginPage::retryFace() {
    m_errorLabel->setVisible(false);
    m_usernameEdit->clear();
    m_passwordEdit->clear();
    m_captureCount = 0;
    m_samples.clear();
    m_isVerifying = false;
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
    // [V2.05] 窗口显示时立即定位退出按钮到右上角
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
    cancelBtn->setMinimumHeight(44);
    cancelBtn->setMinimumWidth(120);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setStyleSheet(
        "QPushButton{font-size:16px; font-weight:500; color:#666666;"
        "background:#f0f0f0; border:none; border-radius:22px; padding:10px 24px;}"
        "QPushButton:hover{background:#e6e6e6;}"
        "QPushButton:pressed{background:#d9d9d9;}");
    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);

    auto* confirmBtn = new QPushButton(QStringLiteral("退出"));
    confirmBtn->setMinimumHeight(44);
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

/** [复刻Vue版] 处理陌生人确认 → 切换密码登录 */
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

void LoginPage::setFaceResult(const QString& state) {
    m_faceResult = state;
    if (state == "fail") {
        // [V6.5] fail状态显示红色❌圆圈+下方文字 (复刻Web版 .camera-area.fail)，cameraWrap保持可见
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
        // [2026-06-23] 对齐Web版：失败状态红点 (dot-red)
        setStatusDot("background:#ff4d4f;");
    } else if (state == "no-camera") {
        // [V6.4] no-camera状态隐藏整个摄像头容器 (Web版无camera-area占位)
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