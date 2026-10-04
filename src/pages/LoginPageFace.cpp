/**
 * @file LoginPageFace.cpp
 * @brief 登录页-人脸识别链路（采集→比对→成功/陌生人/失败分派）
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
// 【③ 人脸识别链路（采集 → 比对 → 成功/陌生人/失败）
//   ★核心：识别采样、相似度比对、结果分派都在本区；改识别规则先看这里
// ==============================================================
void LoginPage::startFaceRecognition() {
    m_faceResult = FaceResult::Scanning;  // 重置状态，防止上次fail状态残留
    m_samples.clear();
    m_captureCount = 0;
    m_isVerifying = false;
    m_verifyBudget = 0;        // 新一轮识别，重置采样预算
    m_verifyStartMs = 0;
    m_distanceHintShown = false;
    if (m_faceCamera) m_faceCamera->clearTooFarHint();  // 清除上一轮距离提示

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
            m_statusDot->setStyleSheet(StyleHelper::statusDot());
        } else {
            m_statusDot->setStyleSheet("background:rgba(77,163,255,0.2); border-radius:4px; min-width:8px; min-height:8px;");
        }
    });

    if (m_hasCamera) {
        m_subtitleLabel->setText(QStringLiteral("请面向摄像头完成身份验证"));
        setScanStatus(QStringLiteral("请对准摄像头"), 0);
    } else {
        m_subtitleLabel->setText(QStringLiteral("模拟人脸识别模式 (无摄像头)"));
        setScanStatus(QStringLiteral("请对准摄像头"), 0);
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
            setScanStatus(QStringLiteral("识别超时，请刷脸"), 1);
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
        keepDistanceHint();
        return;
    }
    // 该信号每 33ms 触发一次，若每次都重写样式会与距离提示交替闪烁，
    // 因此仅在文案真正需要变化时设置
    static QString lastText;
    const QString text = QStringLiteral("已检测到人脸，请保持不动...");
    if (lastText != text) {
        lastText = text;
        m_cameraStatusText->setText(text);
    }
}

void LoginPage::onFaceLost() {
    // 人离开摄像头画面 → 清除注销抑制（含截止时间），恢复正常自动刷脸登录
    m_logoutSuppressed = false;
    m_logoutSuppressUntilMs = 0;
    if (m_faceResult == FaceResult::Scanning) {
        setScanStatus(QStringLiteral("请对准摄像头"), 0);
    }
}

/**
 * @brief 距离过远提示
 * @param tooFar true=人脸框过小
 * 仅在扫描阶段改状态文案：距离远时特征像素不足，识别必然失败，
 * 此时引导用户靠近比继续比对更有意义。识别成功后不再覆盖结果提示。
 */
void LoginPage::onFaceTooFarChanged(bool tooFar) {
    if (!tooFar) {
        m_distanceHintShown = false;   // 距离已恢复，允许下次重新提示
        return;
    }
    keepDistanceHint();
}

/**
 * @brief 距离提示持有点：识别过程中距离偏远时，提示不被流程文案覆盖
 */
/**
 * @brief 统一状态提示：所有状态文案走此入口，保证颜色语义一致且不截断
 * @param level 0=常规灰 1=引导橙 2=成功绿 3=失败红
 */
void LoginPage::setScanStatus(const QString& text, int level) {
    if (!m_cameraStatusText) return;
    // 文案超过 12 个字自动降一档字号，避免被控件宽度截断
    const QString t = text.size() > 12 ? QStringLiteral("正在识别，请稍候...")
                                       : text;
    m_cameraStatusText->setText(t);
    static const QStringList kStyles = {
        // 0 常规：浅灰蓝（#888 视觉柔和，避免深色看起来像黑字）
        QStringLiteral("font-size:14px; color:#888888; font-weight:600; background:transparent;"),
        QStringLiteral("font-size:15px; color:#fa8c16; font-weight:700; background:transparent;"),
        QStringLiteral("font-size:15px; color:#389e0d; font-weight:700; background:transparent;"),
        QStringLiteral("font-size:15px; color:#e53935; font-weight:700; background:transparent;"),
    };
    m_cameraStatusText->setStyleSheet(kStyles.value(qBound(0, level, 3)));
}

bool LoginPage::keepDistanceHint() {
    if (!m_faceCamera || !m_faceCamera->isFaceTooFar()) return false;
    if (m_faceResult == FaceResult::Success) return false;
    // 节流：提示已在显示时不再重复重设样式（每帧重设会与其它文案交替闪烁）
    if (m_distanceHintShown) return true;
    m_distanceHintShown = true;
    setScanStatus(QStringLiteral("请靠近"), 1);
    m_subtitleLabel->setText(QStringLiteral("请靠近摄像头"));
    return true;
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
        setScanStatus(QStringLiteral("请重新刷脸"), 1);
        m_faceCamera->reset();
        m_faceResult = FaceResult::Scanning;
        return;
    }

    // 低质量帧直接丢弃：实测置信度约 0.66 的帧提取后无任何候选(相似度 0)，
    // 送入比对只会浪费一次机会。丢弃后继续等待下一帧，不消耗采样预算。
    if (confidence < SC::FACE_MIN_CONFIDENCE) {
        m_faceCamera->reset();
        m_faceResult = FaceResult::Scanning;
        if (!keepDistanceHint()) setScanStatus(QStringLiteral("请保持不动"), 0);
        return;
    }

    // 修复描述符为空时静默返回、用户无任何提示的致命Bug
    QString descriptor = m_faceCamera->getLastDescriptor();
    if (descriptor.isEmpty()) {
        qWarning() << "[LoginPage] 人脸特征提取失败(描述符为空), confidence:" << confidence;

        // 距离过远优先提示：此时特征必然不可用，直接引导靠近而不是让用户
        // 看到"识别失败"（用户会误以为自己没录入）
        if (keepDistanceHint()) {
            m_faceCamera->reset();
            m_faceResult = FaceResult::Scanning;
            m_verifyBudget = 0;        // 距离原因，重置预算避免浪费在无效帧上
            m_verifyStartMs = 0;
            return;
        }

        // 提取失败同样不应立即放弃：服务端偶发"未检测到人脸"占比很高
        // （抓帧时机/画面稳定度），只要仍在采样预算与时限内就继续等待新帧。
        // 判定标准不变——没有任何一帧比对成功仍然不会通过。
        if (m_captureCount >= 2) {
            const qint64 spent = m_verifyStartMs == 0 ? 0
                : QDateTime::currentMSecsSinceEpoch() - m_verifyStartMs;
            if (m_verifyBudget < SC::FACE_MATCH_MAX_FRAMES &&
                spent < SC::FACE_MATCH_MAX_WAIT_MS) {
                if (!keepDistanceHint())
                    setScanStatus(QStringLiteral("请保持不动"), 0);
                m_faceResult = FaceResult::Scanning;
                m_faceCamera->reset();
                return;
            }
            stopDotBlink();
            setStatusDot("background:#ff4d4f;");
            setFaceResult(FaceResult::Fail);
            m_subtitleLabel->setText(QStringLiteral("人脸验证未通过"));
            m_errorLabel->setText(QStringLiteral("⚠️ 人脸特征提取失败，请使用账号密码登录"));
            m_errorLabel->setVisible(true);
            setScanStatus(QStringLiteral("识别失败，请重试"), 3);
        } else {
            // 第一帧就失败 → 重置重试
            m_faceCamera->reset();
            m_faceResult = FaceResult::Scanning;
            setScanStatus(QStringLiteral("请对准摄像头"), 0);
        }
        return;
    }

    m_faceResult = FaceResult::Capturing;
    setScanStatus(QStringLiteral("正在验证..."), 0);
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
        setScanStatus(QStringLiteral("请保持不动"), 0);
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
    if (!keepDistanceHint()) setScanStatus(QStringLiteral("正在验证..."), 0);
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
    setScanStatus(QStringLiteral("识别成功"), 2);

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
        keepDistanceHint();
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
    setScanStatus(QStringLiteral("陌生人，请刷脸"), 1);
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
            if (!keepDistanceHint())
                setScanStatus(QStringLiteral("请保持不动"), 0);
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
    setScanStatus(QStringLiteral("识别失败，请重试"), 3);
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
    setScanStatus(QStringLiteral("摄像头不可用"), 3);
    m_altLoginHint->setVisible(false);
}

void LoginPage::onFaceStateChanged(int state) {}

// ============ 密码登录 (复刻Vue版 + NumKeypad数字键盘) ============


