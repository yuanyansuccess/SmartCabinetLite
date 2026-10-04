/**
 * @file UserManagementPageFaceEnroll.cpp
 * @brief 人员管理-人脸录入全流程（录入对话框/5方位检测/特征保存）
 * @author 袁燕
 *
 * 本文件实现上述功能，成员函数声明见 UserManagementPage.h。
 */

#include "UserManagementPage.h"
#include "ui_UserManagementPage.h"
#include <QTableWidget>  // 用户列表表格
#include "components/PaginationBar.h"
#include "components/SoftKeyboard.h"
#include "components/NumKeypad.h"
#include "components/FaceCameraWidget.h"  // 人脸录入对话框摄像头组件
#include "components/DeepFaceExtractor.h"  // 人脸方位检测
#include "components/SingleSelectFilter.h"  // 通用单选筛选组件
#include "utils/StyleHelper.h"
#include "controller/UserController.h"
#include "controller/AuthController.h"
#include <QtNetwork>
// 重新引入db/UserDAO(仅用于getDistinctDepartments合并数据源)
  // 其他User数据访问仍通过UserController
#include "db/UserDAO.h"
#include "services/AuthService.h"
#include "services/SettingService.h"
#include "services/FaceRecognitionService.h"  // 人脸录入/清除
#include "pages/BatchImportDialog.h"  // 批量导入用户对话框
#include "components/BaseDialog.h"  // 统一圆角对话框基类
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDialog>
#include <QFormLayout>
#include "components/MessageDialog.h"
#include <QDebug>
#include <QFrame>
#include <QPushButton>
#include <QLabel>
#include <QMouseEvent>  // eventFilter
#include <QSet>  // loadDepartments选中状态保留
#include <QTimer>  // 人脸录入成功延迟关闭
#include <QDateTime>  // 方位检测时间戳
#include <functional>  // std::function
#include "common/Constants.h"

// 【⑦ 人脸录入（5方位引导采集）
//   ★核心：录入对话框、方位检测、特征保存；改录入流程看本区
// ==============================================================

namespace {

// 每次录入需要采集的帧数（对应5个方位）
const int MAX_CAPTURES = 5;

// 方位检测服务预检参数：face-server首次加载模型约8秒，预检窗口取20秒
const int POSTURE_PRECHECK_MAX_ATTEMPTS = 40;
const int POSTURE_PRECHECK_INTERVAL_MS = 500;

// 5个方位的目标参数
struct PostureTarget {
    QString name, instruction;
    double yawMin, yawMax, pitchMin, pitchMax;
};
// 方位目标参数（yaw/pitch方向与face-server.js /posture接口一致）
// yaw: 正值=脸偏右(用户左转露右脸)，负值=脸偏左(用户右转露左脸)
// pitch: 正值=低头，负值=抬头
// 归一化基准为人脸框宽高，阈值经实测校准
const PostureTarget POSTURE_TARGETS[5] = {
    { QStringLiteral("居中"), QStringLiteral("请面向摄像头，保持正脸"),          -0.15, 0.15, -0.15, 0.15 },
    { QStringLiteral("左侧"), QStringLiteral("请将头部向右转，露出左侧面部"),   -0.50,-0.15, -0.30, 0.30 },
    { QStringLiteral("右侧"), QStringLiteral("请将头部向左转，露出右侧面部"),    0.15, 0.50, -0.30, 0.30 },
    { QStringLiteral("上偏"), QStringLiteral("请略微抬头，露出面部上方"),       -0.30, 0.30, -0.50,-0.15 },
    { QStringLiteral("下偏"), QStringLiteral("请略微低头，露出面部下方"),       -0.30, 0.30,  0.15, 0.50 },
};

}  // namespace

// 人脸录入会话上下文：聚合本次录入的控件与采集状态
// （.h 中仅前置声明，定义必须与使用它的方法同处一个文件）
struct FaceEnrollCtx {
    // 对话框与控件
    BaseDialog* dlg = nullptr;
    FaceCameraWidget* camera = nullptr;
    QLabel* directionLabel = nullptr;
    QLabel* instructionLabel = nullptr;
    QLabel* statusLabel = nullptr;
    QPushButton* startBtn = nullptr;
    QPushButton* confirmBtn = nullptr;
    QPushButton* cancelBtn = nullptr;
    QTimer* postureTimer = nullptr;   // 方位检测定时器
    QTimer* simpleTimer = nullptr;    // 简易模式定时器
    QNetworkAccessManager* postureNam = nullptr;  // 方位检测HTTP客户端
    // 会话参数
    int userId = 0;
    // 采集状态机状态
    int captureCount = 0;
    QString bestDescriptor;
    double bestConfidence = 0.0;
    bool isCapturing = false;
    bool simpleMode = false;
    int targetIdx = 0;
    int postureMatchCount = 0;
    qint64 postureFirstMatchTime = 0;
    qint64 postureStartTime = 0;
    int postureFailCount = 0;
    bool postureRequestPending = false;  // 是否有HTTP请求在飞行中
    int postureHttpFailCount = 0;        // HTTP连续失败计数
    // 工具函数（connectFaceEnrollFlow中初始化，供状态机方法调用）
    std::function<void()> stopTimers;
    std::function<void()> doCapture;
};

void UserManagementPage::onFaceEnroll(int userId) {
    UserController ctrl;
    User user = ctrl.getUserById(userId);
    if (user.userId == 0) return;

    auto* dlg = new BaseDialog(this, 520);
    dlg->setDialogTitle(QStringLiteral("人脸信息录入"));
    dlg->setMinimumHeight(580);

    FaceEnrollCtx ctx;
    ctx.dlg = dlg;
    ctx.userId = userId;
    // 方位检测定时器（本地估算0延迟，100ms高频检测）
    ctx.postureTimer = new QTimer(dlg);
    ctx.postureTimer->setInterval(100);
    // 简单模式定时器
    ctx.simpleTimer = new QTimer(dlg);
    ctx.simpleTimer->setSingleShot(true);
    ctx.postureNam = new QNetworkAccessManager(dlg);

    auto* cl = dlg->contentLayout();
    cl->setSpacing(12);
    buildFaceEnrollHeader(user, cl);
    buildFaceEnrollBody(ctx, cl);
    buildFaceEnrollButtons(ctx);

    connectFaceEnrollFlow(ctx);

    dlg->exec();
    dlg->deleteLater();
}

/** 构建录入对话框头部：说明文字与用户信息行 */
void UserManagementPage::buildFaceEnrollHeader(const User& user, QVBoxLayout* cl) {
    // 说明文字
    auto* descLabel = new QLabel(QStringLiteral("请面向摄像头，保持正脸清晰可见"));
    descLabel->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontBody, StyleHelper::textMuted()));
    cl->addWidget(descLabel);

    // 用户信息行
    auto* infoRow = new QHBoxLayout();
    infoRow->setSpacing(12);
    auto* avatar = new QLabel(user.realName.isEmpty() ? "?" : user.realName.left(1));
    avatar->setFixedSize(44, 44);
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setStyleSheet(
        "background:#4da3ff;color:white;border-radius:22px;font-size:20px;font-weight:700;");
    auto* nameCol = new QVBoxLayout();
    nameCol->setSpacing(2);
    auto* nameLbl = new QLabel(user.realName);
    nameLbl->setStyleSheet("font-size:16px;font-weight:700;color:#333;background:transparent;");
    auto* metaLbl = new QLabel(QString("%1  %2").arg(user.workNo, user.department));
    metaLbl->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontSmall, StyleHelper::textMuted()));
    nameCol->addWidget(nameLbl);
    nameCol->addWidget(metaLbl);
    infoRow->addWidget(avatar);
    infoRow->addLayout(nameCol);
    infoRow->addStretch();
    cl->addLayout(infoRow);
}

/** 构建录入对话框主体：摄像头、方位提示、指令提示与状态提示 */
void UserManagementPage::buildFaceEnrollBody(FaceEnrollCtx& ctx, QVBoxLayout* cl) {
    // 摄像头组件：降低阈值+减少稳定帧数，支持偏侧脸自动采集
    auto* camera = new FaceCameraWidget();
    camera->setMinimumSize(280, 260);
    camera->setMaximumSize(360, 300);
    camera->setAutoCapture(false);
    camera->setMinConfidence(0.60);  // 降低阈值，偏侧脸也能检测
    camera->setStableFrames(6);  // 减少稳定帧数，自动采集更快
    camera->setDetectInterval(80);  // 加快检测频率
    cl->addWidget(camera, 0, Qt::AlignCenter);
    ctx.camera = camera;

    // 方位大字提示标签：36px醒目显示，实时方位反馈
    auto* directionLabel = new QLabel(QStringLiteral("选择开始录入"));
    directionLabel->setAlignment(Qt::AlignCenter);
    directionLabel->setMinimumHeight(StyleHelper::Token::ControlHeightLarge);
    directionLabel->setStyleSheet(StyleHelper::postureBadge());
    cl->addWidget(directionLabel);
    ctx.directionLabel = directionLabel;

    // 指令提示
    auto* instructionLabel = new QLabel(QStringLiteral("点击「开始录入」启动摄像头"));
    instructionLabel->setAlignment(Qt::AlignCenter);
    instructionLabel->setWordWrap(true);
    instructionLabel->setStyleSheet("font-size:15px;color:#666;padding:4px 0;background:transparent;");
    cl->addWidget(instructionLabel);
    ctx.instructionLabel = instructionLabel;

    // 状态提示
    auto* statusLabel = new QLabel("");
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setStyleSheet("font-size:14px;color:#4da3ff;background:transparent;");
    cl->addWidget(statusLabel);
    ctx.statusLabel = statusLabel;
}

/** 构建录入对话框按钮区：开始录入/确认保存/取消 */
void UserManagementPage::buildFaceEnrollButtons(FaceEnrollCtx& ctx) {
    // 按钮区（通过 BaseDialog::buttonLayout()）
    auto* bl = ctx.dlg->buttonLayout();
    while (bl->count() > 0) {
        QLayoutItem* item = bl->takeAt(0);
        delete item;
    }

    auto* startBtn = new QPushButton(QStringLiteral("开始人脸录入"));
    startBtn->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
    startBtn->setCursor(Qt::PointingHandCursor);
    startBtn->setStyleSheet(
        "QPushButton{background:#4da3ff;color:white;border:none;border-radius:12px;"
        "font-size:15px;font-weight:700;padding:0 28px;}"
        "QPushButton:hover{background:#3d8ae0;}");

    auto* confirmBtn = new QPushButton(QStringLiteral("确认保存"));
    confirmBtn->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
    confirmBtn->setVisible(false);
    confirmBtn->setCursor(Qt::PointingHandCursor);
    confirmBtn->setStyleSheet(
        "QPushButton{background:#27ae60;color:white;border:none;border-radius:12px;"
        "font-size:15px;font-weight:700;padding:0 28px;}"
        "QPushButton:hover{background:#219a52;}");

    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    cancelBtn->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setStyleSheet(
        "QPushButton{background:#f5f5f5;color:#666;border:1px solid #ddd;border-radius:12px;"
        "font-size:15px;font-weight:600;padding:0 24px;}"
        "QPushButton:hover{background:#e8e8e8;}");
    connect(cancelBtn, &QPushButton::clicked, ctx.dlg, &QDialog::reject);

    bl->addWidget(startBtn);
    bl->addWidget(confirmBtn);
    bl->addWidget(cancelBtn);
    bl->addStretch();

    ctx.startBtn = startBtn;
    ctx.confirmBtn = confirmBtn;
    ctx.cancelBtn = cancelBtn;
}

/** 连接人脸录入状态机全部信号（5方位检测引导采集流程） */
void UserManagementPage::connectFaceEnrollFlow(FaceEnrollCtx& ctx) {
    auto* dlg = ctx.dlg;
    auto* camera = ctx.camera;

    // 关闭所有定时器
    ctx.stopTimers = [&ctx]() {
        ctx.postureTimer->stop();
        ctx.simpleTimer->stop();
    };

    // 执行采集
    ctx.doCapture = [&ctx]() {
        ctx.instructionLabel->setText(QStringLiteral("正在采集..."));
        ctx.instructionLabel->setStyleSheet(
            "font-size:15px;font-weight:bold;color:#4da3ff;padding:4px 0;background:transparent;");
        ctx.camera->captureNow();
    };

    // 距离过远提示：采集前引导靠近，避免因特征像素不足导致采集质量差
    connect(ctx.camera, &FaceCameraWidget::faceTooFarChanged, dlg, [this, &ctx](bool tooFar) {
        if (!tooFar || !ctx.isCapturing) return;
        ctx.instructionLabel->setText(QStringLiteral("请靠近"));
        ctx.instructionLabel->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontLabel, StyleHelper::orangeAccent(), 700, "padding:4px 0;"));
    });

    // 方位检测回调 — 异步HTTP模式
    // 定时器触发异步POST /posture，HTTP响应回调中做方位匹配，不阻塞UI
    connect(ctx.postureTimer, &QTimer::timeout, dlg, [this, &ctx]() {
        onFacePostureTick(ctx);
    });

    // 简易模式定时器触发采集
    connect(ctx.simpleTimer, &QTimer::timeout, dlg, [&ctx]() {
        if (!ctx.isCapturing) return;
        if (!ctx.camera->faceRect().isNull()) {
            ctx.doCapture();
        } else {
            ctx.instructionLabel->setText(QStringLiteral("🔍 未检测到人脸，请对准摄像头..."));
            ctx.instructionLabel->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontLabel, StyleHelper::primaryColor(), 400, "padding:4px 0;"));
            ctx.simpleTimer->start(1000);
        }
    });

  // 开始录入按钮 — 启动方位检测流程
    connect(ctx.startBtn, &QPushButton::clicked, camera, [this, &ctx]() {
        ctx.startBtn->setVisible(false);
        ctx.cancelBtn->setVisible(true);
        ctx.captureCount = 0;
        ctx.bestConfidence = 0.0;
        ctx.bestDescriptor.clear();
        ctx.isCapturing = true;
        ctx.simpleMode = false;
        ctx.targetIdx = 0;
        ctx.postureMatchCount = 0;
        ctx.postureFirstMatchTime = 0;
        ctx.postureFailCount = 0;
        ctx.postureRequestPending = false;
        ctx.postureHttpFailCount = 0;

        ctx.instructionLabel->setText(QStringLiteral("正在检测方位识别服务..."));
        ctx.instructionLabel->setStyleSheet(
            "font-size:15px;font-weight:bold;color:#4da3ff;padding:4px 0;background:transparent;");
        ctx.directionLabel->setText(QStringLiteral("初始化"));
        ctx.directionLabel->setStyleSheet(StyleHelper::postureBadge());

        ctx.camera->startCamera();

        // 800ms后预检/posture（等摄像头和face-server就绪）
        // ⚠ 不可动：预检用 QTimer+property 存储状态，改成 std::function 递归会触发 MSVC 栈崩溃
        // ⚠ 不可动：预检窗口=40×500ms=20s，服务冷启动实测5s，调小会误判"服务未连接"退简易模式
        QTimer::singleShot(800, ctx.dlg, [&ctx]() {
            if (!ctx.isCapturing) return;

            QTimer* retryTimer = new QTimer(ctx.dlg);
            retryTimer->setSingleShot(true);
            retryTimer->setInterval(POSTURE_PRECHECK_INTERVAL_MS);
            retryTimer->setProperty("retryLeft", POSTURE_PRECHECK_MAX_ATTEMPTS);

            connect(retryTimer, &QTimer::timeout, ctx.dlg, [&ctx, retryTimer]() {
                if (!ctx.isCapturing) { retryTimer->deleteLater(); return; }

                int retryLeft = retryTimer->property("retryLeft").toInt();
                QImage frame = ctx.camera->currentFrame();
                DeepFaceExtractor ext;
                double y, p; QString e;
                if (!frame.isNull() && ext.detectPosture(frame, y, p, e)) {
                    ctx.simpleMode = false;
                    const auto& t = POSTURE_TARGETS[0];
                    ctx.directionLabel->setText(QStringLiteral("【 %1 】").arg(t.name));
                    ctx.directionLabel->setStyleSheet(StyleHelper::postureBadge());
                    ctx.instructionLabel->setText(QStringLiteral("📸 第 1/5 帧 — 方位检测已就绪\n%1").arg(t.instruction));
                    ctx.instructionLabel->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontLabel, StyleHelper::orangeAccent(), 700, "padding:4px 0;"));
                    ctx.postureStartTime = QDateTime::currentMSecsSinceEpoch();
                    ctx.postureTimer->start();
                    retryTimer->deleteLater();
                } else if (retryLeft > 0) {
                    retryTimer->setProperty("retryLeft", retryLeft - 1);
                    ctx.instructionLabel->setText(QStringLiteral("正在等待人脸识别服务启动... (%1)").arg(retryLeft - 1));
                    retryTimer->start(POSTURE_PRECHECK_INTERVAL_MS);
                } else {
                    ctx.simpleMode = true;
                    const auto& t = POSTURE_TARGETS[0];
                    ctx.directionLabel->setText(QStringLiteral("【 %1 】(简易)").arg(t.name));
                    ctx.directionLabel->setStyleSheet(
                        "font-size:32px; font-weight:900; color:#ffffff; "
                        "background:#fa8c16; border-radius:14px; padding:10px 20px;");
                    ctx.instructionLabel->setText(QStringLiteral("⚠ 方位检测服务未连接\n简易模式：3秒后自动采集\n%1").arg(t.instruction));
                    ctx.instructionLabel->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontLabel, StyleHelper::orangeAccent(), 700, "padding:4px 0;"));
                    ctx.simpleTimer->start(3000);
                    retryTimer->deleteLater();
                }
            });

            retryTimer->start(0);
        });
    });

  // 采集结果回调 — 进入下一个方位或完成
    connect(camera, &FaceCameraWidget::captureReady, camera,
            [this, &ctx](const QImage&, double confidence) {
        onFaceCaptureReady(ctx, confidence);
    });

    connect(ctx.confirmBtn, &QPushButton::clicked, camera, [this, &ctx]() {
        onFaceConfirmSave(ctx);
    });

    connect(dlg, &QDialog::finished, camera, [&ctx]() { ctx.camera->stopCamera(); });
}

/** 方位检测定时器回调 — 异步POST /posture，HTTP响应交给onFacePostureReply处理 */
void UserManagementPage::onFacePostureTick(FaceEnrollCtx& ctx) {
    if (!ctx.isCapturing) { ctx.postureTimer->stop(); return; }
    if (ctx.simpleMode) { ctx.postureTimer->stop(); return; }
    // 上一个请求还在飞行中，跳过本次（避免请求堆积）
    if (ctx.postureRequestPending) return;

    qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - ctx.postureStartTime;
    if (elapsed > 30000) {
        ctx.instructionLabel->setText(QStringLiteral("⚠ 方位超时，将采集当前帧作为兜底"));
        ctx.instructionLabel->setStyleSheet(
            "font-size:15px;font-weight:bold;color:#ff4d4f;padding:4px 0;background:transparent;");
        ctx.postureTimer->stop();
        QTimer::singleShot(100, ctx.dlg, [&ctx]() { ctx.doCapture(); });
        return;
    }

    QImage frame = ctx.camera->currentFrame();
    QRect faceRect = ctx.camera->faceRect();
    if (frame.isNull() || faceRect.isNull()) {
        ctx.postureMatchCount = 0;
        ctx.postureFirstMatchTime = 0;
        ctx.instructionLabel->setText(QStringLiteral("🔍 未检测到人脸，请对准摄像头..."));
        ctx.instructionLabel->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontLabel, StyleHelper::primaryColor(), 400, "padding:4px 0;"));
        return;
    }

    // 发起异步HTTP请求到 face-server.js /posture
    ctx.postureRequestPending = true;
    QByteArray base64Data = DeepFaceExtractor::imageToBase64Jpeg(frame);
    QJsonObject bodyObj;
    bodyObj["image"] = QString::fromUtf8(base64Data);
    QByteArray body = QJsonDocument(bodyObj).toJson(QJsonDocument::Compact);

    QNetworkRequest req;
    req.setUrl(QUrl(QString("http://%1:%2/posture")
        .arg(SC::FACE_SERVER_HOST).arg(SC::FACE_SERVER_PORT)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(1500);
    QNetworkReply* reply = ctx.postureNam->post(req, body);

    QObject::connect(reply, &QNetworkReply::finished, ctx.dlg,
        [this, &ctx, reply]() { onFacePostureReply(ctx, reply); });
}

/** 方位检测HTTP响应处理 — 方位匹配判断与界面引导 */
void UserManagementPage::onFacePostureReply(FaceEnrollCtx& ctx, QNetworkReply* reply) {
    ctx.postureRequestPending = false;
    reply->deleteLater();

    if (!ctx.isCapturing || ctx.simpleMode) return;

    // 解析HTTP响应
    double yaw = 0, pitch = 0;
    bool postureOk = false;
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray respData = reply->readAll();
        QJsonParseError parseErr;
        QJsonDocument doc = QJsonDocument::fromJson(respData, &parseErr);
        if (parseErr.error == QJsonParseError::NoError) {
            QJsonObject obj = doc.object();
            if (obj["success"].toBool(false)) {
                yaw = obj["yaw"].toDouble(0);
                pitch = obj["pitch"].toDouble(0);
                postureOk = true;
            }
        }
    }

    if (!postureOk) {
        ctx.postureHttpFailCount++;
        // 不因HTTP失败切简易模式，本地估算兜底继续工作
        // 只有连续15次失败才切简易模式（服务真的挂了）
        if (ctx.postureHttpFailCount >= 15) {
            ctx.instructionLabel->setText(QStringLiteral("⚠ 方位识别服务异常，切换简易模式..."));
            ctx.instructionLabel->setStyleSheet(
                "font-size:15px;color:#fa8c16;padding:4px 0;background:transparent;");
            ctx.simpleMode = true;
            ctx.postureTimer->stop();
            ctx.simpleTimer->start(1000);
            return;
        }
        // HTTP失败，用本地估算兜底
        QRect fr = ctx.camera->faceRect();
        ctx.camera->estimatePosture(fr, yaw, pitch);
    } else {
        ctx.postureHttpFailCount = 0;
        ctx.postureFailCount = 0;
    }

    // 方位匹配判断
    const PostureTarget& target = POSTURE_TARGETS[ctx.targetIdx];
    bool matched = (yaw >= target.yawMin && yaw <= target.yawMax &&
                    pitch >= target.pitchMin && pitch <= target.pitchMax);

    if (matched) {
        ctx.postureMatchCount++;
        if (ctx.postureFirstMatchTime == 0)
            ctx.postureFirstMatchTime = QDateTime::currentMSecsSinceEpoch();
        qint64 stayMs = QDateTime::currentMSecsSinceEpoch() - ctx.postureFirstMatchTime;
        double remainSec = qMax(0.0, (500.0 - stayMs) / 1000.0);

        ctx.directionLabel->setText(QStringLiteral("【 %1 】 %2s").arg(target.name).arg(remainSec, 0, 'f', 1));
        ctx.directionLabel->setStyleSheet(StyleHelper::postureBadge());
        ctx.instructionLabel->setText(QStringLiteral("✅ 方位匹配 %1/2帧  (yaw=%2 pitch=%3)\n还需保持%4秒...")
            .arg(ctx.postureMatchCount)
            .arg(yaw, 0, 'f', 2).arg(pitch, 0, 'f', 2).arg(remainSec, 0, 'f', 1));
        ctx.instructionLabel->setStyleSheet(
            "font-size:15px;font-weight:bold;color:#389e0d;padding:4px 0;background:transparent;");

        if (ctx.postureMatchCount >= 2 && stayMs >= 500) {
            ctx.postureTimer->stop();
            ctx.directionLabel->setText(QStringLiteral("【 %1 】✓").arg(target.name));
            ctx.doCapture();
        }
    } else {
        ctx.postureMatchCount = 0;
        ctx.postureFirstMatchTime = 0;
        QString hint;
        if (yaw > 0.15) hint = QStringLiteral("当前：右转(露右脸)");
        else if (yaw < -0.15) hint = QStringLiteral("当前：左转(露左脸)");
        else if (pitch > 0.15) hint = QStringLiteral("当前：低头");
        else if (pitch < -0.15) hint = QStringLiteral("当前：抬头");
        else hint = QStringLiteral("当前：居中");
        ctx.directionLabel->setText(QStringLiteral("【 %1 】").arg(target.name));
        ctx.directionLabel->setStyleSheet(
            "font-size:32px; font-weight:900; color:#ffffff; "
            "background:#4da3ff; border-radius:14px; padding:10px 20px;");
        ctx.instructionLabel->setText(QStringLiteral("%1\n请调整到【%2】方位\n%3")
            .arg(hint).arg(target.name).arg(target.instruction));
        ctx.instructionLabel->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontLabel, StyleHelper::orangeAccent(), 700, "padding:4px 0;"));
    }
}

/** 采集结果回调 — 记录最佳特征并进入下一个方位或完成 */
void UserManagementPage::onFaceCaptureReady(FaceEnrollCtx& ctx, double confidence) {
    if (!ctx.isCapturing) return;
    QString descriptor = ctx.camera->getLastDescriptor();
    if (descriptor.isEmpty()) {
        ctx.instructionLabel->setText(QStringLiteral("⚠ 特征提取失败，稍后自动重试..."));
        ctx.instructionLabel->setStyleSheet(
            "font-size:15px;color:#fa8c16;padding:4px 0;background:transparent;");
        ctx.camera->reset();
        if (ctx.simpleMode) {
            ctx.simpleTimer->start(1500);
        } else {
            ctx.postureStartTime = QDateTime::currentMSecsSinceEpoch();
            ctx.postureMatchCount = 0;
            ctx.postureFirstMatchTime = 0;
            ctx.postureFailCount = 0;
            ctx.postureRequestPending = false;
            ctx.postureHttpFailCount = 0;
            ctx.postureTimer->start();
        }
        return;
    }
    ctx.captureCount++;
    int dimCount = descriptor.split(",").size();
    double score = confidence * 0.7 + (dimCount >= 128 ? 0.3 : 0.1);
    if (score > ctx.bestConfidence) { ctx.bestConfidence = score; ctx.bestDescriptor = descriptor; }

    if (ctx.captureCount < MAX_CAPTURES) {
        // 进入下一个方位
        ctx.targetIdx = ctx.captureCount;
        const auto& next = POSTURE_TARGETS[ctx.targetIdx];
        ctx.camera->reset();
        if (ctx.simpleMode) {
            ctx.directionLabel->setText(QStringLiteral("【 %1 】(简易)").arg(next.name));
            ctx.directionLabel->setStyleSheet(
                "font-size:32px; font-weight:900; color:#ffffff; "
                "background:#fa8c16; border-radius:14px; padding:10px 20px;");
            ctx.instructionLabel->setText(QStringLiteral("✅ 第 %1/5 帧采集成功\n下一个【%2】— 3秒后自动采集\n%3")
                .arg(ctx.captureCount).arg(next.name).arg(next.instruction));
            ctx.instructionLabel->setStyleSheet(
                "font-size:15px;color:#389e0d;padding:4px 0;background:transparent;");
            ctx.simpleTimer->start(3000);
        } else {
            ctx.directionLabel->setText(QStringLiteral("【 %1 】").arg(next.name));
            ctx.directionLabel->setStyleSheet(StyleHelper::postureBadge());
            ctx.instructionLabel->setText(QStringLiteral("✅ 第 %1/5 帧采集成功\n请准备【%2】\n%3")
                .arg(ctx.captureCount).arg(next.name).arg(next.instruction));
            ctx.instructionLabel->setStyleSheet(
                "font-size:15px;color:#389e0d;padding:4px 0;background:transparent;");
            ctx.postureStartTime = QDateTime::currentMSecsSinceEpoch();
            ctx.postureMatchCount = 0;
            ctx.postureFirstMatchTime = 0;
            ctx.postureFailCount = 0;
            ctx.postureRequestPending = false;
            ctx.postureHttpFailCount = 0;
            ctx.postureTimer->start();
        }
    } else {
        // 采集完毕
        ctx.isCapturing = false;
        ctx.stopTimers();
        ctx.directionLabel->setText(QStringLiteral("5方位采集完成"));
        ctx.directionLabel->setStyleSheet(StyleHelper::postureBadge());
        ctx.confirmBtn->setVisible(true);
        ctx.instructionLabel->setText(QStringLiteral("🎉 采集完成！最佳置信度: %1%\n请点击「确认保存」")
            .arg(QString::number(ctx.bestConfidence * 100, 'f', 1)));
        ctx.instructionLabel->setStyleSheet(
            "font-size:15px;font-weight:600;color:#389e0d;padding:4px 0;background:transparent;");
    }
}

/** 确认保存 — 将最佳人脸特征写入数据库 */
void UserManagementPage::onFaceConfirmSave(FaceEnrollCtx& ctx) {
    if (ctx.bestDescriptor.isEmpty() || ctx.userId <= 0) return;
    ctx.confirmBtn->setEnabled(false);
    ctx.isCapturing = false;
    ctx.instructionLabel->setText(QStringLiteral("正在保存人脸特征..."));
    ctx.instructionLabel->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontLabel, StyleHelper::primaryColor(), 400, "padding:4px 0;"));
    FaceRecognitionService svc;
    if (svc.enrollFace(ctx.userId, ctx.bestDescriptor)) {
        ctx.statusLabel->setText(QStringLiteral("人脸特征采集成功！"));
        ctx.statusLabel->setStyleSheet("font-size:15px;font-weight:700;color:#389e0d;background:transparent;");
        ctx.camera->stopCamera();
        refresh();
        QTimer::singleShot(800, ctx.dlg, &QDialog::accept);
    } else {
        ctx.statusLabel->setText(QStringLiteral("人脸录入失败，请重试"));
        ctx.statusLabel->setStyleSheet("font-size:15px;font-weight:700;color:#ff4d4f;background:transparent;");
        ctx.confirmBtn->setEnabled(true);
    }
}

/** 已录入用户点击 → 弹出查看/清除人脸对话框 — 统一BaseDialog圆角风格 */

// ==============================================================
// 【⑧ 已录入用户的人脸查看与清除
//   查看/重新录入/清除人脸
// ==============================================================
void UserManagementPage::onFaceEnrolledClick(int userId, const QString& realName, const QString& workNo) {
    UserController ctrl;
    User user = ctrl.getUserById(userId);
    if (user.userId == 0) return;

    auto* dlg = new BaseDialog(this, 440);
    dlg->setDialogTitle(QStringLiteral("人脸信息"));
    dlg->setMinimumHeight(360);

    auto* cl = dlg->contentLayout();
    cl->setSpacing(16);

    // 用户信息行（头像 + 姓名/工号）
    auto* infoRow = new QHBoxLayout();
    infoRow->setSpacing(14);

    auto* avatar = new QLabel(realName.isEmpty() ? "?" : realName.left(1));
    avatar->setFixedSize(56, 56);
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setStyleSheet(
        "background:#4da3ff;color:white;border-radius:28px;font-size:24px;font-weight:700;");

    auto* nameCol = new QVBoxLayout();
    nameCol->setSpacing(4);
    auto* nameLbl = new QLabel(realName);
    nameLbl->setStyleSheet("font-size:18px;font-weight:700;color:#333;background:transparent;");
    auto* metaLbl = new QLabel(QString("%1  %2").arg(workNo, user.department));
    metaLbl->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontBody, StyleHelper::textMuted()));
    nameCol->addWidget(nameLbl);
    nameCol->addWidget(metaLbl);

    infoRow->addWidget(avatar);
    infoRow->addLayout(nameCol);
    infoRow->addStretch();
    cl->addLayout(infoRow);

    // 人脸状态区
    auto* statusBox = new QFrame();
    statusBox->setStyleSheet(
        "QFrame{background:#f6ffed;border:none;border-radius:12px;}");
    auto* statusLayout = new QVBoxLayout(statusBox);
    statusLayout->setContentsMargins(24, 20, 24, 20);
    statusLayout->setSpacing(8);
    statusLayout->setAlignment(Qt::AlignCenter);

    auto* statusIcon = new QLabel(QStringLiteral("✓"));
    statusIcon->setAlignment(Qt::AlignCenter);
    statusIcon->setStyleSheet("font-size:36px;color:#52c41a;font-weight:bold;background:transparent;");
    auto* statusText = new QLabel(QStringLiteral("人脸特征已录入"));
    statusText->setAlignment(Qt::AlignCenter);
    statusText->setStyleSheet("font-size:16px;font-weight:600;color:#389e0d;background:transparent;");
    auto* statusHint = new QLabel(QStringLiteral("该用户可使用人脸识别登录智能柜系统"));
    statusHint->setAlignment(Qt::AlignCenter);
    statusHint->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontSmall, StyleHelper::textMuted()));

    statusLayout->addWidget(statusIcon);
    statusLayout->addWidget(statusText);
    statusLayout->addWidget(statusHint);
    cl->addWidget(statusBox);

    // 按钮区（通过 BaseDialog::buttonLayout()）
    auto* bl = dlg->buttonLayout();
    while (bl->count() > 0) {
        QLayoutItem* item = bl->takeAt(0);
        delete item;
    }
    bl->addStretch();

    auto* clearBtn = new QPushButton(QStringLiteral("清除人脸信息"));
    clearBtn->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
    clearBtn->setMinimumWidth(140);
    clearBtn->setCursor(Qt::PointingHandCursor);
    clearBtn->setStyleSheet(
        "QPushButton{background:#ff4d4f;color:white;border:none;border-radius:12px;"
        "font-size:15px;font-weight:600;}"
        "QPushButton:hover{background:#e04343;}");

    auto* closeBtn = new QPushButton(QStringLiteral("关闭"));
    closeBtn->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
    closeBtn->setMinimumWidth(100);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QPushButton{background:#f5f5f5;color:#666;border:1px solid #ddd;border-radius:12px;"
        "font-size:15px;font-weight:600;}"
        "QPushButton:hover{background:#e8e8e8;}");
    connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::reject);

    bl->addWidget(clearBtn);
    bl->addWidget(closeBtn);

    // 清除逻辑
    connect(clearBtn, &QPushButton::clicked, dlg, [dlg, userId, this]() {
        if (MessageDialog::showQuestion(dlg, QStringLiteral("确认清除"),
            QStringLiteral("确定要清除该用户的人脸信息吗？\n清除后用户将无法使用人脸识别登录。"))) {
            FaceRecognitionService svc;
            if (svc.deleteFace(userId)) {
                MessageDialog::showSuccess(dlg, QStringLiteral("成功"), QStringLiteral("人脸信息已清除"));
                dlg->accept();
                refresh();
            } else {
                MessageDialog::showError(dlg, QStringLiteral("失败"), QStringLiteral("清除失败，请重试"));
            }
        }
    });

    dlg->exec();
    dlg->deleteLater();
}
