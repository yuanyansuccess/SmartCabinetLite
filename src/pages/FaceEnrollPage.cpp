/**
 * @file FaceEnrollPage.cpp
 * @brief 人脸录入页面实现 - 1:1复刻Vue版FaceEnroll.vue
 * @author 袁燕
 *
 * [V2.16 2026-07-06] 5方位引导采集重构：实时检测人脸方位，匹配目标方位后才采集
 *   方位检测基于face-api.js 68关键点几何分析（鼻尖相对两眼中心位置）
 *   提示词字体放大到28px，方位标签36px醒目显示
 */
#include "FaceEnrollPage.h"
#include "components/FaceCameraWidget.h"
#include "components/DeepFaceExtractor.h"
#include "services/FaceRecognitionService.h"
#include "controller/UserController.h"
// [V1.00.9.1 架构修复] 移除db/UserDAO直接引用，改为通过UserController访问数据 —— 作者：袁燕
#include "utils/StyleHelper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include "components/MessageDialog.h"
#include <QGroupBox>
#include <QFrame>
#include <QTimer>
#include <QDateTime>
#include <QDebug>

FaceEnrollPage::FaceEnrollPage(QWidget* parent) : QWidget(parent) {
    // [修正] 完全匹配Web版背景渐变
    setStyleSheet(QString(
        "background:qlineargradient(x1:0,y1:0,x2:0.35,y2:1,stop:0 #f5f7fa,stop:1 #e4e8ed);"
    ));
    // [V8.2] 自动连续采集定时器
    m_autoCaptureTimer = new QTimer(this);
    m_autoCaptureTimer->setSingleShot(true);
    connect(m_autoCaptureTimer, &QTimer::timeout, this, &FaceEnrollPage::onCapture);
    // [V2.16] 方位检测定时器
    m_postureTimer = new QTimer(this);
    m_postureTimer->setInterval(POSTURE_CHECK_INTERVAL_MS);
    connect(m_postureTimer, &QTimer::timeout, this, &FaceEnrollPage::onPostureCheck);
    // [V2.17] 简单模式自动采集定时器（posture API不可用时的fallback）
    m_simpleCaptureTimer = new QTimer(this);
    m_simpleCaptureTimer->setSingleShot(true);
    connect(m_simpleCaptureTimer, &QTimer::timeout, this, &FaceEnrollPage::onSimpleCapture);
    setupUI();
}

FaceEnrollPage::~FaceEnrollPage() {
    if (m_camera) m_camera->stopCamera();
}

void FaceEnrollPage::setCurrentUser(const QJsonObject& user) {
    m_currentUser = user;
}

// ==================== UI布局 ====================

void FaceEnrollPage::setupUI() {
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(32, 24, 32, 24);
    outerLayout->setSpacing(20);
    outerLayout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    // --- 标题区 ---
    auto* headerWidget = new QWidget();
    headerWidget->setMaximumWidth(720);
    headerWidget->setStyleSheet("background:transparent;");
    auto* headerLayout = new QVBoxLayout(headerWidget);
    headerLayout->setSpacing(6);

    auto* titleLabel = new QLabel(QStringLiteral("👤 人脸信息录入"));
    titleLabel->setStyleSheet("font-size:20px; font-weight:700; color:#1a1a2e; background:transparent;");
    auto* descLabel = new QLabel(QStringLiteral("请面向摄像头，保持正脸清晰可见，系统将采集人脸特征"));
    descLabel->setStyleSheet("font-size:16px; color:#888; background:transparent;");
    // [V2.17fix-0706 袁燕] 版本标签 — 红色醒目标记，确认运行的exe是更新后的版本
    auto* versionLabel = new QLabel(QStringLiteral("V2.17fix-0706"));
    versionLabel->setStyleSheet(
        "font-size:14px; font-weight:bold; color:#ffffff; "
        "background:#e53935; border-radius:6px; padding:2px 10px;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addWidget(descLabel);
    headerLayout->addWidget(versionLabel);
    outerLayout->addWidget(headerWidget, 0, Qt::AlignHCenter);

    // --- 主卡片容器 ---
    auto* card = new QWidget();
    card->setMaximumWidth(720);
    card->setStyleSheet("background:white; border-radius:12px;");
    // [V8.2 2026-06-25] 去除人脸录入卡片外阴影
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(40, 32, 40, 32);
    cardLayout->setSpacing(24);

    // --- 摄像头区域 ---
    // [2026-06-23] 适配FaceCameraWidget新尺寸(260x300含状态气泡)，确保录像框+提示完整
    m_camera = new FaceCameraWidget();
    m_camera->setMinimumSize(260, 300);
    m_camera->setMaximumSize(320, 360);
    m_camera->setAutoCapture(false);  // 手动采集模式
    m_camera->setMinConfidence(0.60);  // [V2.03l] 0.65→0.60 降低阈值，偏侧脸也能检测
    m_camera->setStableFrames(6);      // [V2.03l] 20→6 减少稳定帧数，自动采集更快
    m_camera->setDetectInterval(80);   // [V2.03l] 120→80 加快检测频率
    // [V2.16fix] faceDetected/faceLost不再覆盖instructionLabel！
    // Capturing状态下方位检测独占提示权，faceDetected/faceLost只更新状态指示灯
    connect(m_camera, &FaceCameraWidget::faceDetected, this, [this](){
        if (m_state == Idle) return;
        // [V2.16fix] Capturing状态不覆盖instructionLabel（方位检测在控制提示）
        // 只在非Capturing状态（如Confirm）时更新提示
        if (m_state != Capturing) {
            m_instructionLabel->setText(QStringLiteral("✅ 已检测到人脸"));
            m_instructionLabel->setStyleSheet("font-size:18px; font-weight:bold; color:#389e0d; background:transparent;");
        }
    });
    connect(m_camera, &FaceCameraWidget::faceLost, this, [this](){
        if (m_state == Idle) return;
        // [V2.16fix] Capturing状态不覆盖instructionLabel（方位检测在控制提示）
        if (m_state != Capturing) {
            m_instructionLabel->setText(QStringLiteral("🔍 正在检测人脸，请对准摄像头..."));
            m_instructionLabel->setStyleSheet("font-size:18px; color:#4da3ff; background:transparent;");
        }
    });
    connect(m_camera, &FaceCameraWidget::captureReady, this, &FaceEnrollPage::onFaceCaptured);
    connect(m_camera, &FaceCameraWidget::errorOccurred, this, &FaceEnrollPage::onCameraError);

    cardLayout->addWidget(m_camera, 0, Qt::AlignCenter);

    // 方位大字提示标签：左侧/右侧/上偏/下偏/居中
    // 字体36px醒目显示，背景高亮，触屏远距离可见
    // [V2.17fix-0706] 始终可见，默认显示"选择用户后点击开始录入"
    m_directionLabel = new QLabel(QStringLiteral("选择用户后点击「开始录入」"));
    m_directionLabel->setAlignment(Qt::AlignCenter);
    m_directionLabel->setMinimumHeight(64);
    m_directionLabel->setStyleSheet(
        "font-size:24px; font-weight:700; color:#ffffff; "
        "background:#4da3ff; border-radius:16px; padding:12px 24px;");
    cardLayout->addWidget(m_directionLabel);

    // 提示文字
    m_instructionLabel = new QLabel(QStringLiteral("请选择用户后点击「开始录入」"));
    m_instructionLabel->setAlignment(Qt::AlignCenter);
    m_instructionLabel->setWordWrap(true);
    m_instructionLabel->setStyleSheet("font-size:16px; color:#666; background:transparent;");
    cardLayout->addWidget(m_instructionLabel);

    // --- 结果框(成功/失败) ---
    m_resultBox = new QWidget();
    m_resultBox->setVisible(false);
    auto* rbLayout = new QVBoxLayout(m_resultBox);
    rbLayout->setSpacing(6);
    rbLayout->setAlignment(Qt::AlignCenter);
    m_resultIcon = new QLabel();
    m_resultIcon->setAlignment(Qt::AlignCenter);
    m_resultIcon->setStyleSheet("font-size:48px; background:transparent;");
    m_resultText = new QLabel();
    m_resultText->setAlignment(Qt::AlignCenter);
    m_resultText->setWordWrap(true);
    rbLayout->addWidget(m_resultIcon);
    rbLayout->addWidget(m_resultText);
    cardLayout->addWidget(m_resultBox);

    // --- 用户选择区(仅在idle/success/fail阶段显示) ---
    m_enrollPanel = new QWidget();
    m_enrollPanel->setStyleSheet("background:transparent;");
    auto* epLayout = new QVBoxLayout(m_enrollPanel);
    epLayout->setContentsMargins(0,0,0,0);
    epLayout->setSpacing(12);

    auto* selLabel = new QLabel(QStringLiteral("选择要录入人脸的用户："));
    selLabel->setStyleSheet("font-size:17px; font-weight:700; color:#555; background:transparent;");
    epLayout->addWidget(selLabel);

    m_userCombo = new QComboBox();
    m_userCombo->setStyleSheet(StyleHelper::comboBox());
    m_userCombo->setMinimumHeight(52);
    connect(m_userCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FaceEnrollPage::onUserSelected);
    epLayout->addWidget(m_userCombo);

    m_statusLabel = new QLabel("");
    m_statusLabel->setStyleSheet("font-size:16px; color:#fa8c16; background:transparent; padding:6px;");  // [触屏优化] 字体16px，内边距增加
    epLayout->addWidget(m_statusLabel);

    cardLayout->addWidget(m_enrollPanel);

    // --- 操作按钮区 ---
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(12);

    m_startBtn = new QPushButton(QStringLiteral("请先选择用户"));
    m_startBtn->setEnabled(false);
    m_startBtn->setCursor(Qt::PointingHandCursor);
    m_startBtn->setStyleSheet(
        "QPushButton{background:#4da3ff;color:white;border:none;border-radius:10px;"
        "font-size:16px;font-weight:bold;min-height:52px;padding:12px 32px;}"
        "QPushButton:hover:!disabled{background:#3d8ae0;}"
        "QPushButton:disabled{background:#a0c4ff;color:white;}");
    connect(m_startBtn, &QPushButton::clicked, this, &FaceEnrollPage::onStartEnroll);

    m_confirmBtn = new QPushButton(QStringLiteral("✅ 确认保存"));
    m_confirmBtn->setVisible(false);
    m_confirmBtn->setCursor(Qt::PointingHandCursor);
    m_confirmBtn->setStyleSheet(StyleHelper::buttonSuccess());
    connect(m_confirmBtn, &QPushButton::clicked, this, &FaceEnrollPage::onEnrollConfirm);

    m_cancelBtn = new QPushButton(QStringLiteral("取消"));
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setStyleSheet(StyleHelper::buttonOutline());
    connect(m_cancelBtn, &QPushButton::clicked, this, &FaceEnrollPage::onCancel);

    btnLayout->addWidget(m_startBtn);
    btnLayout->addWidget(m_confirmBtn);
    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addStretch();
    cardLayout->addLayout(btnLayout);

    // 清除人脸按钮
    auto* clearBtn = new QPushButton(QStringLiteral("清除人脸信息"));
    clearBtn->setCursor(Qt::PointingHandCursor);
    clearBtn->setStyleSheet(
        "QPushButton{background:#ff6b6b;color:white;border:none;border-radius:10px;"
        "font-size:14px;font-weight:700;min-height:44px;padding:10px 28px;}"
        "QPushButton:hover{background:#ee5a5a;}"
        "QPushButton:pressed{background:#e74c3c;}");
    connect(clearBtn, &QPushButton::clicked, this, [this](){
        if (m_selectedUserId <= 0) {
            m_statusLabel->setText(QStringLiteral("请先选择用户"));
            return;
        }
        if (MessageDialog::showQuestion(this, QStringLiteral("确认清除"),
            QStringLiteral("确定要清除该用户的人脸信息吗？清除后用户将无法使用人脸识别登录。"))) {
            FaceRecognitionService svc;
            if (svc.deleteFace(m_selectedUserId)) {
                m_statusLabel->setText(QStringLiteral("人脸信息已清除"));
                m_statusLabel->setStyleSheet("font-size:16px; color:#389e0d; background:transparent; padding:6px;");  // [触屏优化] 字体16px
                resetEnroll();
            } else {
                m_statusLabel->setText(QStringLiteral("清除失败"));
                m_statusLabel->setStyleSheet("font-size:16px; color:#e53935; background:transparent; padding:6px;");  // [触屏优化] 字体16px
            }
        }
    });
    cardLayout->addWidget(clearBtn, 0, Qt::AlignLeft);

    outerLayout->addWidget(card, 1, Qt::AlignHCenter);
    outerLayout->addStretch();

    // 初始加载用户列表
    loadUsers();
}

// ==================== 用户列表 ====================

void FaceEnrollPage::loadUsers() {
    m_userCombo->clear();
    m_userCombo->addItem(QStringLiteral("-- 请选择用户 --"), -1);

    // [V1.00.9.1 架构修复] 通过UserController替代直接调用db/UserDAO —— 作者：袁燕
    UserController ctrl;
    auto pageResult = ctrl.getUserList(1, 999);
    QJsonArray users;
    for (const auto& user : pageResult.list) {
        QJsonObject u;
        u["userId"] = user.userId;
        u["realName"] = user.realName;
        u["workNo"] = user.workNo;
        u["faceEnrolled"] = !user.faceFeature.isEmpty();
        users.append(u);
    }
    m_userList = users;

    FaceRecognitionService svc;
    for (int i = 0; i < users.size(); i++) {
        QJsonObject u = users[i].toObject();
        int userId = u["userId"].toInt();
        QString realName = u["realName"].toString();
        QString workNo = u["workNo"].toString();
        bool enrolled = u["faceEnrolled"].toBool();

        QString label = QString("%1  %2 (%3) %4")
            .arg(realName.isEmpty() ? "?" : realName.left(1))
            .arg(realName)
            .arg(workNo)
            .arg(enrolled ? QStringLiteral("✅已录入") : QStringLiteral("⚠未录入"));
        m_userCombo->addItem(label, userId);
    }
}

void FaceEnrollPage::onUserSelected(int index) {
    if (index <= 0) {
        m_selectedUserId = -1;
        m_selectedRealName = "";
        m_startBtn->setText(QStringLiteral("请先选择用户"));
        m_startBtn->setEnabled(false);
        m_statusLabel->setText("");
        return;
    }

    m_selectedUserId = m_userCombo->itemData(index).toInt();
    m_selectedRealName = m_userCombo->itemText(index);
    m_startBtn->setText(QStringLiteral("开始人脸录入"));
    m_startBtn->setEnabled(true);

    // 检查是否已录入
    FaceRecognitionService svc;
    if (svc.hasFaceEnrolled(m_selectedUserId)) {
        m_statusLabel->setText(QStringLiteral("该用户已录入人脸，可选择重新录入或清除"));
        m_statusLabel->setStyleSheet("font-size:16px; color:#fa8c16; background:transparent; padding:6px;");  // [触屏优化] 字体16px
    } else {
        m_statusLabel->setText(QStringLiteral("该用户尚未录入人脸"));
        m_statusLabel->setStyleSheet("font-size:16px; color:#4da3ff; background:transparent; padding:6px;");  // [触屏优化] 字体16px
    }
}

// ==================== 录入流程 ====================

// 5方位定义：正面/左侧/右侧/偏上/偏下
// 方位判断基于face-api.js 68关键点几何：yaw=左右偏转，pitch=上下偏转
struct PostureTarget {
    QString name;        // 方位名称（显示用）
    QString instruction; // 引导文字
    double yawMin;       // yaw范围
    double yawMax;
    double pitchMin;     // pitch范围
    double pitchMax;
};

// 5个方位的目标参数（yaw/pitch阈值由face-server.js计算）
// yaw >0.10 = 鼻子偏图像右（用户左转脸），< -0.10 = 偏图像左（用户右转脸）
// pitch >0.10 = 低头，< -0.10 = 抬头
static const PostureTarget POSTURE_TARGETS[5] = {
    { QStringLiteral("居中"), QStringLiteral("请面向摄像头，保持正脸"),          -0.10, 0.10, -0.10, 0.10 },
    { QStringLiteral("左侧"), QStringLiteral("请将头部缓慢向右转，露出左侧面部"),  0.12, 1.00, -0.15, 0.15 },
    { QStringLiteral("右侧"), QStringLiteral("请将头部缓慢向左转，露出右侧面部"), -1.00,-0.12, -0.15, 0.15 },
    { QStringLiteral("上偏"), QStringLiteral("请略微抬头，露出下巴和面部上方"),   -0.15, 0.15, -1.00,-0.12 },
    { QStringLiteral("下偏"), QStringLiteral("请略微低头，露出额头和面部下方"),   -0.15, 0.15,  0.12, 1.00 },
};

void FaceEnrollPage::onStartEnroll() {
    if (m_selectedUserId <= 0) return;

    m_state = Capturing;
    m_captureCount = 0;
    m_targetDirection = 0;
    m_postureMatchCount = 0;
    m_postureTimeoutCount = 0;
    m_postureFailCount = 0;
    m_bestConfidence = 0.0;
    m_bestDescriptor.clear();
    m_bestImage = QImage();
    m_resultBox->setVisible(false);

    m_enrollPanel->setVisible(false);
    m_startBtn->setVisible(false);
    m_confirmBtn->setVisible(false);
    m_cancelBtn->setVisible(true);
    m_camera->setVisible(true);

    m_camera->startCamera();

    // [V2.17fix-0706] 简化启动：等待face-server.js加载完成后再预检/posture
    // face-server.js首次启动需2-3秒加载模型，500ms太短导致误判不可用
    m_instructionLabel->setText(QStringLiteral("正在初始化人脸识别服务..."));
    m_instructionLabel->setStyleSheet("font-size:18px; color:#4da3ff; background:transparent;");
    m_directionLabel->setText(QStringLiteral("服务初始化"));
    m_directionLabel->setStyleSheet(
        "font-size:36px; font-weight:900; color:#ffffff; "
        "background:#4da3ff; border-radius:16px; padding:12px 24px;");

    // 延迟后检查posture服务（等待摄像头初始化+face-server模型加载）
    QTimer::singleShot(800, this, [this]() {
        checkPostureServiceAndStart();
    });
}

/// [V2.17fix-0706 袁燕] 检查face-server.js /posture端点是否可用，选择采集模式
/// 增加重试机制：face-server.js首次启动需2-3秒加载模型，
/// 每次重试间隔500ms，最多重试REM次，确保不会误判为不可用
void FaceEnrollPage::checkPostureServiceAndStart() {
    // 重试计数器（静态局部变量保持状态跨多次QTimer回调）
    static int retryCount = 0;
    static const int MAX_RETRIES = 10;  // 最多重试10次（约5秒）

    QImage testFrame = m_camera->currentFrame();
    if (testFrame.isNull()) {
        // 摄像头还没准备好，等待后重试
        if (retryCount < MAX_RETRIES) {
            retryCount++;
            m_instructionLabel->setText(QStringLiteral("等待摄像头就绪... %1/%2")
                .arg(retryCount).arg(MAX_RETRIES));
            QTimer::singleShot(500, this, [this]() { checkPostureServiceAndStart(); });
            return;
        }
        qWarning() << "[FaceEnroll] 摄像头始终未就绪 → 简单模式";
        startSimpleMode();
        retryCount = 0;
        return;
    }

    DeepFaceExtractor extractor;
    double testYaw = 0, testPitch = 0;
    QString testErr;
    bool postureOk = extractor.detectPosture(testFrame, testYaw, testPitch, testErr);

    if (postureOk) {
        // /posture可用 → 进入深度方位检测模式
        retryCount = 0;
        m_simpleMode = false;
        qDebug() << "[FaceEnroll] /posture可用(yaw=" << testYaw << "pitch=" << testPitch
                 << ") → 深度方位检测模式";
        const PostureTarget& target = POSTURE_TARGETS[0];
        m_directionLabel->setText(QStringLiteral("【 %1 】").arg(target.name));
        m_directionLabel->setStyleSheet(
            "font-size:36px; font-weight:900; color:#ffffff; "
            "background:#4da3ff; border-radius:16px; padding:12px 24px;");
        m_instructionLabel->setText(QStringLiteral("📸 第 1/5 帧采集 —— 方位检测已就绪\n%1").arg(target.instruction));
        m_instructionLabel->setStyleSheet("font-size:18px; font-weight:bold; color:#fa8c16; background:transparent;");
        m_postureStartTime = QDateTime::currentMSecsSinceEpoch();
        m_postureTimeoutCount = 0;
        startPostureCheck();
    } else {
        // /posture不可用 → 重试或fallback
        if (retryCount < MAX_RETRIES) {
            retryCount++;
            qDebug() << "[FaceEnroll] /posture重试 #" << retryCount << ": " << testErr;
            m_instructionLabel->setText(QStringLiteral("正在连接人脸识别服务... %1/%2\n%3")
                .arg(retryCount).arg(MAX_RETRIES).arg(testErr));
            QTimer::singleShot(500, this, [this]() { checkPostureServiceAndStart(); });
            return;
        }
        qWarning() << "[FaceEnroll] /posture不可用(重试" << MAX_RETRIES << "次后放弃): "
                   << testErr << " → 简单模式";
        startSimpleMode();
        retryCount = 0;
    }
}

/// [V2.17fix-0706 袁燕] 简单模式启动：/posture API不可用时的fallback
/// 方向标签用橙色区分，明确告知用户当前是简易模式
void FaceEnrollPage::startSimpleMode() {
    m_simpleMode = true;
    const PostureTarget& target = POSTURE_TARGETS[0];
    m_directionLabel->setText(QStringLiteral("【 %1 】(简易模式)").arg(target.name));
    m_directionLabel->setStyleSheet(
        "font-size:36px; font-weight:900; color:#ffffff; "
        "background:#fa8c16; border-radius:16px; padding:12px 24px;");
    m_instructionLabel->setText(QStringLiteral("⚠ 方位检测服务未连接，简易采集模式\n第 1/5 帧\n%1\n（请按提示调整方向）")
        .arg(target.instruction));
    m_instructionLabel->setStyleSheet("font-size:18px; font-weight:bold; color:#fa8c16; background:transparent;");
    m_simpleCaptureTimer->start(SIMPLE_CAPTURE_DELAY_MS);
}

void FaceEnrollPage::startPostureCheck() {
    m_postureMatchCount = 0;
    m_postureFirstMatchTime = 0;  // [V2.17fix] 重置首次匹配时间
    m_postureTimer->start();
}

void FaceEnrollPage::stopPostureCheck() {
    m_postureTimer->stop();
    m_postureMatchCount = 0;
}

// [V2.17 袁燕] 方位检测回调：实时检测当前人脸方位
// 每400ms调用face-server.js /posture接口检测yaw/pitch
// 连续失败POSTURE_FAIL_FALLBACK次(10次≈4秒)→自动切换简单模式
void FaceEnrollPage::onPostureCheck() {
    if (m_state != Capturing) {
        qDebug() << "[FaceEnroll] onPostureCheck: state != Capturing, stopping";
        stopPostureCheck();
        return;
    }

    // [V2.17] 已切换到简单模式时不再做posture检测
    if (m_simpleMode) {
        stopPostureCheck();
        return;
    }

    // 超时检查：60秒未匹配目标方位
    qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_postureStartTime;
    if (elapsed > POSTURE_TIMEOUT_MS) {
        m_postureTimeoutCount++;
        qDebug() << "[FaceEnroll] 方位超时 #" << m_postureTimeoutCount
                 << "目标:" << POSTURE_TARGETS[m_targetDirection].name;
        if (m_postureTimeoutCount >= 3) {
            m_instructionLabel->setText(QStringLiteral("⚠ 多次超时，将采集当前帧作为兜底"));
            m_instructionLabel->setStyleSheet("font-size:18px; color:#ff4d4f; background:transparent;");
            stopPostureCheck();
            QTimer::singleShot(100, this, &FaceEnrollPage::onCapture);
            return;
        }
        m_postureStartTime = QDateTime::currentMSecsSinceEpoch();
        const PostureTarget& target = POSTURE_TARGETS[m_targetDirection];
        m_instructionLabel->setText(QStringLiteral("⚠ 请调整头部到【%1】方位！\n%2\n（已超时%3次，请按提示调整）")
            .arg(target.name).arg(target.instruction).arg(m_postureTimeoutCount));
        m_instructionLabel->setStyleSheet("font-size:18px; font-weight:bold; color:#ff4d4f; background:transparent;");
        m_directionLabel->setStyleSheet(
            "font-size:36px; font-weight:900; color:#ffffff; "
            "background:#ff6b6b; border-radius:16px; padding:12px 24px;");
        return;
    }

    // 获取当前摄像头帧
    QRect faceR = m_camera->faceRect();
    if (faceR.isNull()) {
        m_instructionLabel->setText(QStringLiteral("🔍 未检测到人脸，请对准摄像头...\n%1")
            .arg(POSTURE_TARGETS[m_targetDirection].instruction));
        m_postureMatchCount = 0;
        return;
    }

    // 调用face-server.js /posture检测方位
    DeepFaceExtractor extractor;
    double yaw = 0, pitch = 0;
    QString errMsg;

    QImage frame = m_camera->currentFrame();
    if (frame.isNull()) {
        m_postureMatchCount = 0;
        return;
    }

    if (!extractor.detectPosture(frame, yaw, pitch, errMsg)) {
        // [V2.17] detectPosture连续失败计数 → 超过上限自动切换简单模式
        m_postureFailCount++;
        m_postureMatchCount = 0;
        qWarning() << "[FaceEnroll] detectPosture失败 #" << m_postureFailCount << ":" << errMsg;

        if (m_postureFailCount >= POSTURE_FAIL_FALLBACK) {
            // 连续失败10次(约4秒) → 切换简单模式，不再浪费时间
            qWarning() << "[FaceEnroll] detectPosture连续失败" << m_postureFailCount
                       << "次 → 自动切换简单模式";
            m_simpleMode = true;
            stopPostureCheck();
            const PostureTarget& target = POSTURE_TARGETS[m_targetDirection];
            m_directionLabel->setStyleSheet(
                "font-size:36px; font-weight:900; color:#ffffff; "
                "background:#fa8c16; border-radius:16px; padding:12px 24px;");
            m_instructionLabel->setText(QStringLiteral("⚠ 方位检测服务不可用，采用简单模式\n请按提示调整方向后等待采集\n%1").arg(target.instruction));
            m_instructionLabel->setStyleSheet("font-size:18px; font-weight:bold; color:#fa8c16; background:transparent;");
            // 简单模式：3秒后采集当前帧
            m_simpleCaptureTimer->start(SIMPLE_CAPTURE_DELAY_MS);
            return;
        }

        // 失败但未达上限 → 显示提示继续重试
        m_instructionLabel->setText(QStringLiteral("⚠ 方位检测失败(%1次): %2\n正在重试...")
            .arg(m_postureFailCount).arg(errMsg));
        m_instructionLabel->setStyleSheet("font-size:18px; color:#fa8c16; background:transparent;");
        return;
    }

    // detectPosture成功 → 重置失败计数
    m_postureFailCount = 0;

    qDebug() << "[FaceEnroll] detectPosture成功: yaw=" << yaw << "pitch=" << pitch
             << "目标:" << POSTURE_TARGETS[m_targetDirection].name;

    // [V2.17fix] 判断当前方位是否匹配目标方位（非居中方向用严格阈值）
    const PostureTarget& target = POSTURE_TARGETS[m_targetDirection];
    bool matched = false;
    if (m_targetDirection == 0) {
        // 居中：宽松阈值（正脸不动就是居中）
        matched = (yaw >= target.yawMin && yaw <= target.yawMax &&
                    pitch >= target.pitchMin && pitch <= target.pitchMax);
    } else if (m_targetDirection == 1) {
        // 左侧：必须明显左偏(yaw>0.15)
        matched = (yaw > 0.15 && pitch >= -0.15 && pitch <= 0.15);
    } else if (m_targetDirection == 2) {
        // 右侧：必须明显右偏(yaw<-0.15)
        matched = (yaw < -0.15 && pitch >= -0.15 && pitch <= 0.15);
    } else if (m_targetDirection == 3) {
        // 上偏(抬头)：必须明显抬头(pitch<-0.15)
        matched = (pitch < -0.15 && yaw >= -0.15 && yaw <= 0.15);
    } else if (m_targetDirection == 4) {
        // 下偏(低头)：必须明显低头(pitch>0.15)
        matched = (pitch > 0.15 && yaw >= -0.15 && yaw <= 0.15);
    }

    if (matched) {
        m_postureMatchCount++;
        // [V2.17fix] 记录首次匹配时间
        if (m_postureFirstMatchTime == 0) {
            m_postureFirstMatchTime = QDateTime::currentMSecsSinceEpoch();
        }
        // 计算已停留时间和剩余等待时间
        qint64 stayMs = QDateTime::currentMSecsSinceEpoch() - m_postureFirstMatchTime;
        int remainSec = qMax(0, (MIN_STAY_MS - stayMs + 999) / 1000);

        QString currentDir = QString("yaw=%1 pitch=%2").arg(yaw, 0, 'f', 2).arg(pitch, 0, 'f', 2);

        // [V2.17fix] 方位标签显示倒计时——让袁总看到变化！
        m_directionLabel->setText(QStringLiteral("【 %1 】 %2s")
            .arg(target.name).arg(remainSec));
        m_directionLabel->setStyleSheet(
            "font-size:36px; font-weight:900; color:#ffffff; "
            "background:#52c41a; border-radius:16px; padding:12px 24px;");

        // 显示匹配进度和yaw/pitch值
        m_instructionLabel->setText(QStringLiteral("✅ 方位匹配 %1/%2帧\n%3\n还需等待%4秒...")
            .arg(m_postureMatchCount).arg(POSTURE_MATCH_REQUIRED)
            .arg(currentDir).arg(remainSec));
        m_instructionLabel->setStyleSheet("font-size:18px; font-weight:bold; color:#389e0d; background:transparent;");

        // [V2.17fix] 双重条件：连续帧数足够 AND 最小停留时间已过
        if (m_postureMatchCount >= POSTURE_MATCH_REQUIRED && stayMs >= MIN_STAY_MS) {
            stopPostureCheck();
            m_directionLabel->setText(QStringLiteral("【 %1 】✓").arg(target.name));
            m_instructionLabel->setText(QStringLiteral("📸 方位匹配成功，正在采集..."));
            onCapture();
        }
    } else {
        m_postureMatchCount = 0;
        QString hint;
        if (yaw > 0.12) hint = QStringLiteral("当前：左偏");
        else if (yaw < -0.12) hint = QStringLiteral("当前：右偏");
        else if (pitch < -0.12) hint = QStringLiteral("当前：抬头");
        else if (pitch > 0.12) hint = QStringLiteral("当前：低头");
        else hint = QStringLiteral("当前：居中");

        m_instructionLabel->setText(QStringLiteral("%1\n请调整到【%2】方位\n%3")
            .arg(hint).arg(target.name).arg(target.instruction));
        m_instructionLabel->setStyleSheet("font-size:18px; font-weight:bold; color:#fa8c16; background:transparent;");
        m_directionLabel->setStyleSheet(
            "font-size:36px; font-weight:900; color:#ffffff; "
            "background:#4da3ff; border-radius:16px; padding:12px 24px;");
    }
}

void FaceEnrollPage::scheduleNextCapture() {
    if (m_state != Capturing) return;
}

/// [V2.17 袁燕] 简单模式自动采集回调（posture API不可用时的fallback）
/// 每个方向等待SIMPLE_CAPTURE_DELAY_MS(3秒)后采集，给用户看方向提示的时间
void FaceEnrollPage::onSimpleCapture() {
    if (m_state != Capturing) return;

    // 简单模式下，检测到人脸才采集
    if (m_camera->faceRect().isNull()) {
        m_instructionLabel->setText(QStringLiteral("🔍 未检测到人脸，请对准摄像头..."));
        m_instructionLabel->setStyleSheet("font-size:18px; color:#4da3ff; background:transparent;");
        // 1秒后重试
        m_simpleCaptureTimer->start(1000);
        return;
    }

    // 检测到人脸，开始采集
    qDebug() << "[FaceEnroll] 简单模式采集: 方位" << m_targetDirection
             << POSTURE_TARGETS[m_targetDirection].name;
    onCapture();
}

void FaceEnrollPage::onCapture() {
    if (m_state != Capturing) return;
    m_camera->captureNow();
}

void FaceEnrollPage::onFaceCaptured(const QImage& image, double confidence) {
    if (m_state != Capturing) return;

    QString descriptor = m_camera->getLastDescriptor();
    if (descriptor.isEmpty()) {
        m_instructionLabel->setText(QStringLiteral("⚠ 特征提取失败，稍后自动重试..."));
        m_instructionLabel->setStyleSheet("font-size:18px; color:#fa8c16; background:transparent;");
        m_camera->reset();
        // 重新启动采集（根据当前模式）
        if (m_simpleMode) {
            m_simpleCaptureTimer->start(1000);
        } else {
            m_postureStartTime = QDateTime::currentMSecsSinceEpoch();
            m_postureTimeoutCount = 0;
            startPostureCheck();
        }
        return;
    }

    m_captureCount++;

    // 评分：综合置信度和描述符维度
    int dimCount = descriptor.split(",").size();
    double score = confidence * 0.7 + (dimCount >= 128 ? 0.3 : 0.1);

    if (score > m_bestConfidence) {
        m_bestConfidence = score;
        m_bestDescriptor = descriptor;
        m_bestImage = image;
    }

    if (m_captureCount < MAX_ENROLL_CAPTURES) {
        // [V2.17] 进入下一个方位的检测，兼容两种模式
        m_targetDirection = m_captureCount;
        const PostureTarget& nextTarget = POSTURE_TARGETS[m_targetDirection];
        m_directionLabel->setText(QStringLiteral("【 %1 】").arg(nextTarget.name));

        if (m_simpleMode) {
            // 简单模式：方向标签用橙色，4秒后自动采集
            m_directionLabel->setStyleSheet(
                "font-size:36px; font-weight:900; color:#ffffff; "
                "background:#fa8c16; border-radius:16px; padding:12px 24px;");
            m_instructionLabel->setText(QStringLiteral("✅ 第 %1/5 帧采集成功！\n下一个方位【%2】\n%3\n（等待4秒...）")
                .arg(m_captureCount).arg(nextTarget.name).arg(nextTarget.instruction));
            m_instructionLabel->setStyleSheet("font-size:18px; color:#389e0d; background:transparent;");
            m_camera->reset();
            m_simpleCaptureTimer->start(SIMPLE_CAPTURE_DELAY_MS);
        } else {
            // 深度检测模式：方向标签用蓝色，启动posture检测
            m_directionLabel->setStyleSheet(
                "font-size:36px; font-weight:900; color:#ffffff; "
                "background:#4da3ff; border-radius:16px; padding:12px 24px;");
            m_instructionLabel->setText(QStringLiteral("✅ 第 %1/5 帧采集成功！\n请准备下一个方位【%2】\n%3")
                .arg(m_captureCount).arg(nextTarget.name).arg(nextTarget.instruction));
            m_instructionLabel->setStyleSheet("font-size:18px; color:#389e0d; background:transparent;");
            m_camera->reset();
            m_postureStartTime = QDateTime::currentMSecsSinceEpoch();
            m_postureTimeoutCount = 0;
            startPostureCheck();
        }
    } else {
        // 采集完毕，进入确认状态
        m_state = Confirm;
        m_autoCaptureTimer->stop();
        stopPostureCheck();
        // [V2.17fix-0706] 不隐藏标签，改为显示完成信息
        m_directionLabel->setText(QStringLiteral("5方位采集完成"));
        m_directionLabel->setStyleSheet(
            "font-size:30px; font-weight:900; color:#ffffff; "
            "background:#52c41a; border-radius:16px; padding:12px 24px;");
        m_confirmBtn->setVisible(true);
        m_instructionLabel->setText(QStringLiteral("🎉 5方位采集完成！最佳置信度: %1%%\n请点击「确认保存」或「取消」重新采集")
            .arg(QString::number(m_bestConfidence * 100, 'f', 1)));
        m_instructionLabel->setStyleSheet("font-size:18px; font-weight:bold; color:#389e0d; background:transparent;");
    }
}

void FaceEnrollPage::onEnrollConfirm() {
    if (m_bestDescriptor.isEmpty() || m_selectedUserId <= 0) return;

    m_state = Capturing; // 保存中
    m_instructionLabel->setText(QStringLiteral("正在保存人脸特征..."));
    m_instructionLabel->setStyleSheet("font-size:16px; color:#4da3ff; background:transparent;");
    m_confirmBtn->setEnabled(false);

    saveFaceData(m_selectedUserId, m_bestDescriptor);
}

void FaceEnrollPage::saveFaceData(int userId, const QString& descriptor) {
    FaceRecognitionService svc;
    bool ok = svc.enrollFace(userId, descriptor);

    if (ok) {
        m_state = Success;
        m_resultBox->setVisible(true);
        m_resultBox->setStyleSheet("background:#f6ffed; border:2px solid #b7eb8f; border-radius:16px; padding:24px;");
        m_resultIcon->setText(QStringLiteral("✅"));
        m_resultText->setText(QStringLiteral("人脸特征采集成功！\n已录入用户：%1").arg(m_selectedRealName));
        m_resultText->setStyleSheet("font-size:16px; font-weight:700; color:#389e0d; background:transparent;");
        m_camera->stopCamera();
        m_camera->setVisible(false);
        m_confirmBtn->setVisible(false);
        m_startBtn->setVisible(true);
        m_startBtn->setText(QStringLiteral("重新录入"));
        m_startBtn->setEnabled(true);
        m_enrollPanel->setVisible(true);

        emit enrollSuccess(userId, m_selectedRealName);
    } else {
        m_state = Failed;
        m_resultBox->setVisible(true);
        m_resultBox->setStyleSheet("background:#fff2f0; border:2px solid #ffa39e; border-radius:16px; padding:24px;");
        m_resultIcon->setText(QStringLiteral("❌"));
        m_resultText->setText(QStringLiteral("人脸录入失败\n后端保存异常，请重试"));
        m_resultText->setStyleSheet("font-size:16px; font-weight:700; color:#ff4d4f; background:transparent;");
        m_confirmBtn->setEnabled(true);
    }
}

// ==================== 取消/重置 ====================

void FaceEnrollPage::onCancel() {
    m_autoCaptureTimer->stop();
    stopPostureCheck();
    if (m_simpleCaptureTimer) m_simpleCaptureTimer->stop();
    if (m_camera) {
        m_camera->stopCamera();
        m_camera->reset();
    }
    resetEnroll();
}

void FaceEnrollPage::onCameraError(const QString& msg) {
    m_autoCaptureTimer->stop();
    m_state = Failed;
    m_instructionLabel->setText(QStringLiteral("摄像头异常: %1").arg(msg));
    m_instructionLabel->setStyleSheet("font-size:16px; color:#ff4d4f; background:transparent;");
    m_resultBox->setVisible(true);
    m_resultBox->setStyleSheet("background:#fff2f0; border:2px solid #ffa39e; border-radius:16px; padding:24px;");
    m_resultIcon->setText(QStringLiteral("📷"));
    m_resultText->setText(QStringLiteral("摄像头不可用\n%1").arg(msg));
    m_resultText->setStyleSheet("font-size:16px; color:#ff4d4f; background:transparent;");
}

void FaceEnrollPage::resetEnroll() {
    m_state = Idle;
    m_captureCount = 0;
    m_targetDirection = 0;
    m_postureMatchCount = 0;
    m_postureTimeoutCount = 0;
    m_postureFailCount = 0;
    m_postureFirstMatchTime = 0;
    m_simpleMode = false;
    m_bestConfidence = 0.0;
    m_bestDescriptor.clear();
    m_bestImage = QImage();
    m_resultBox->setVisible(false);

    m_enrollPanel->setVisible(true);
    m_startBtn->setVisible(true);
    m_startBtn->setEnabled(m_selectedUserId > 0);
    m_startBtn->setText(m_selectedUserId > 0 ? QStringLiteral("开始人脸录入") : QStringLiteral("请先选择用户"));
    m_confirmBtn->setVisible(false);
    m_cancelBtn->setVisible(true);
    m_camera->setVisible(true);
    // [V2.17fix-0706] directionLabel始终可见，重置为默认文字
    m_directionLabel->setText(QStringLiteral("选择用户后点击「开始录入」"));
    m_directionLabel->setStyleSheet(
        "font-size:24px; font-weight:700; color:#ffffff; "
        "background:#4da3ff; border-radius:16px; padding:12px 24px;");

    m_instructionLabel->setText(QStringLiteral("请选择用户后点击「开始录入」"));
    m_instructionLabel->setStyleSheet("font-size:16px; color:#666; background:transparent;");

    loadUsers();
}
