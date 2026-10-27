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
/**
 * @brief 启动人脸识别
 * @param relogin true=注销回登场景（状态文案"请重新刷脸"），false=首次登录
 */
void LoginPage::startFaceRecognition(bool relogin) {
    m_passwordMode = false;  // 回到人脸模式：恢复回调写状态的资格与状态行显示
    if (m_statusDot) m_statusDot->show();
    if (m_cameraStatusText) m_cameraStatusText->show();
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
        m_subtitleLabel->setText(QStringLiteral("请面向摄像头"));
        // 注销/锁屏回登时提示"请重新刷脸"，首次登录提示"请对准摄像头"
        setScanStatus(relogin ? QStringLiteral("请重新刷脸") : QStringLiteral("请对准摄像头"), 0);
    } else {
        m_subtitleLabel->setText(QStringLiteral("模拟人脸识别模式 (无摄像头)"));
        setScanStatus(relogin ? QStringLiteral("请重新刷脸") : QStringLiteral("请对准摄像头"), 0);
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
            // 失败提示只说"原因 + 下一步"两件事：不再重复告知"可用账号密码登录"
            // （页面上"使用账号密码登录"按钮本身已是常驻引导）
            m_subtitleLabel->setText(QStringLiteral("人脸验证未通过"));
            m_errorLabel->setText(QStringLiteral("识别超时，请重试"));
            m_errorLabel->setVisible(true);
            setScanStatus(QStringLiteral("请重试"), 1);
        }
    }, Qt::SingleShotConnection);
}

/**
 * @brief 停止人脸识别
 */
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

/**
 * @brief 处理人脸
 */
void LoginPage::onFaceDetected() {
    if (m_passwordMode) return;  // 密码登录模式不响应人脸检测回调
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

/**
 * @brief 处理人脸
 */
void LoginPage::onFaceLost() {
    if (m_passwordMode) return;  // 密码登录模式不响应
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
    if (m_passwordMode) return;  // 密码登录模式不显示距离提示
    if (!tooFar) {
        m_distanceHintShown = false;   // 距离已恢复，允许下次重新提示
        return;
    }
    keepDistanceHint();
}

/**
 * @brief 统一状态提示：所有状态文案走此入口，保证颜色语义一致
 * @param text 提示文案，由调用方保证简短（一行可容纳），本方法不再替换文案
 * @param level 0=常规灰 1=引导橙 2=成功绿 3=失败红
 */
void LoginPage::setScanStatus(const QString& text, int level) {
    if (!m_cameraStatusText) return;
    if (m_passwordMode) return;  // 密码登录模式整行已隐藏，禁止写入任何人脸状态文案
    m_cameraStatusText->setText(text);
    static const QStringList kStyles = {
        // 0 常规：浅灰蓝（#888 视觉柔和，避免深色看起来像黑字）
        ("" + StyleHelper::fontSize(StyleHelper::Token::FontBody) + " color:#888888; font-weight:600; background:transparent;"),
        ("" + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + " color:#fa8c16; font-weight:700; background:transparent;"),
        ("" + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + " color:#389e0d; font-weight:700; background:transparent;"),
        ("" + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + " color:#e53935; font-weight:700; background:transparent;"),
    };
    m_cameraStatusText->setStyleSheet(kStyles.value(qBound(0, level, 3)));
}

/**
 * @brief 更新"请靠近"距离提示：人脸过远时提示，过近后自动消除
 * @return true=本次新显示了"请靠近"提示（用于节流，避免每帧重设样式），false=不显示或已显示过
 */
bool LoginPage::keepDistanceHint() {
    if (m_passwordMode) return false;  // 密码登录模式不显示距离提示
    if (!m_faceCamera || !m_faceCamera->isFaceTooFar()) return false;
    if (m_faceResult == FaceResult::Success) return false;
    // 节流：提示已在显示时不再重复重设样式（每帧重设会与其它文案交替闪烁）
    if (m_distanceHintShown) return true;
    m_distanceHintShown = true;
    setScanStatus(QStringLiteral("请靠近"), 1);
    m_subtitleLabel->setText(QStringLiteral("请靠近摄像头"));
    return true;
}

/**
 * @brief 处理摄像头采集到的人脸帧，触发本地比对验证
 * @param image 摄像头采集的人脸帧图像
 * @param confidence 人脸检测置信度
 */
void LoginPage::onFaceCaptured(const QImage& image, double confidence) {
    // 密码登录模式或识别已停止：切换/停止后异步在途的采集回调仍会到达，必须整体丢弃。
    // 不拦截会把状态拉回Scanning并在密码页或失败页写下"请保持不动"等人脸提示
    if (m_passwordMode || !m_faceRecognitionActive) return;

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

    // 描述符为空时给出明确提示而非静默返回，避免用户得不到任何反馈
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
            m_errorLabel->setText(QStringLiteral("人脸提取失败，请重试"));
            m_errorLabel->setVisible(true);
            setScanStatus(QStringLiteral("请重试"), 3);
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

    // 自适应早退：质量合格的帧立即比对(第1帧即验)。
    //   - 成功(sim>=0.965 且非双高候选)：直接通过，恢复"1帧"速度，体感不再慢；
    //   - 失败/陌生人(临界帧 0.94~0.965 或双高候选)：不立即判失败，继续采集第2帧
    //     择优保底，保留"多帧取最优"对未识别率的改善。
    // 阈值0.965保持不变（张育诚误识红线不受影响）；好帧sim多在0.97+可1帧通过。
    if (descDim >= 128) {
        collectBestSample();
        return;
    }

    // 质量不足(维度<128)：继续采集下一帧，不消耗比对机会
    m_captureProgress->setText(QStringLiteral("已采集 %1 帧，请保持面部自然...").arg(m_captureCount));
    m_captureProgress->setVisible(true);
    m_faceCamera->reset();
    m_faceResult = FaceResult::Scanning;
    setScanStatus(QStringLiteral("请保持不动"), 0);
}

/**
 * @brief 收集最佳样本
 */
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
    verifyFace(best.descriptor, best.image, best.quality);
}

/// 人脸验证 — 直接本地比对，不走独立后端服务
/// 由 FaceRecognitionService 完成本地特征比对（同步，简洁可靠）
/// @param descriptor 人脸特征向量字符串
/// @param image 采集到的人脸帧（当前实现未使用）
/// @param quality 图像质量评分，低于阈值时比对会被拒绝
void LoginPage::verifyFace(const QString& descriptor, const QImage& image, double quality) {
    Q_UNUSED(image);
    // 竞态防护：已登录成功则不重复验证
    if (m_faceResult == FaceResult::Success || !m_pendingUser.isEmpty()) {
        qDebug() << "[LoginPage] verifyFace ignored: already in success state";
        return;
    }
    // 直接本地比对，无需HTTP请求
    doLocalFaceVerify(descriptor, quality);
}

/// 本地FaceRecognitionService验证
/// @param descriptor 逗号分隔的128维face-api.js深度学习特征
/// 使用 FaceRecognitionService 默认阈值，与登录识别共用同一套判定
void LoginPage::doLocalFaceVerify(const QString& descriptor, double quality) {
    FaceRecognitionService svc;
    auto result = svc.matchFace(descriptor, 0.965, 0.965, 0.35, 0.15, 0.80, quality);

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
        resp["mustChangePwd"] = result.mustChangePwd;  // 强制改密标志，刷脸与账密登录保持一致
        resp["token"] = QString::number(QDateTime::currentSecsSinceEpoch());
        handleFaceSuccess(resp);
    } else if (result.isStranger) {
        // 自适应早退：第1帧被判陌生人多为本人临界帧(0.94~0.965)被当作全局最高不足，
        // 不直接判陌生人，继续采第2帧择优，避免误伤本人、抬高未识别率。
        if (m_captureCount < 2) {
            m_isVerifying = false;
            m_faceCamera->reset();
            m_faceResult = FaceResult::Scanning;
            if (!keepDistanceHint()) setScanStatus(QStringLiteral("请保持不动"), 0);
            return;
        }
        QJsonObject resp;
        resp["isStranger"] = true;
        resp["timestamp"] = QDateTime::currentDateTime().toString(SC::TIMESTAMP_FORMAT);
        resp["strangerId"] = "UNKNOWN";
        resp["clientIp"] = QStringLiteral("本地终端");
        handleFaceStranger(resp);
    } else {
        // 自适应早退：仅第1帧(尚不足2帧)比对失败时，不立即判失败，
        // 继续采集第2帧择优保底，避免临界帧(0.94~0.965)被单帧误拒抬高未识别率。
        if (m_captureCount < 2) {
            m_isVerifying = false;
            m_faceCamera->reset();
            m_faceResult = FaceResult::Scanning;
            if (!keepDistanceHint()) setScanStatus(QStringLiteral("请保持不动"), 0);
            return;
        }
        handleVerifyFailure(result.message.isEmpty()
            ? QStringLiteral("人脸验证失败") : result.message);
    }
}

/// 人脸识别成功处理 (HTTP和本地共用)
/// @param resp 比对返回的用户结果对象（含 userId/realName/token 等字段）
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
    m_successTime->setText(QDateTime::currentDateTime().toString(SC::TIMESTAMP_FORMAT));
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
    user["mustChangePwd"] = resp["mustChangePwd"].toInt();
    user["token"] = resp["token"].toString();
    m_pendingUser = user;
    m_autoJumpTimer->start(700);   // 成功提示停留0.7秒后自动跳转，加快登录节奏
}

/// 陌生人处理 (HTTP和本地共用)
/// @param resp 比对返回的结果对象（含匹配到的相似账户提示信息）
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
    if (m_failStatusText) m_failStatusText->setVisible(false);
    if (m_strangerStatusText) m_strangerStatusText->setVisible(true);
    m_altLoginHint->setVisible(false);
    m_strangerBox->setVisible(true);
    m_strangerTime->setText(QStringLiteral("检测时间: %1")
        .arg(resp["timestamp"].toString(QDateTime::currentDateTime().toString(SC::TIMESTAMP_FORMAT))));
    m_strangerIdLabel->setText(QStringLiteral("陌生人ID: %1").arg(resp["strangerId"].toString()));
    m_strangerIpLabel->setText(QStringLiteral("IP地址: %1").arg(resp["clientIp"].toString("本地终端")));
    m_subtitleLabel->setText(QStringLiteral("陌生人警报"));
    setScanStatus(QStringLiteral("陌生人，请刷脸"), 1);
}

/// 处理验证失败，尝试备用帧
/// 防御说明：用户切回密码登录时可能清空采集样本，而此前人脸验证的异步回调尚未到达，
/// 若在空列表上取帧会断言崩溃，故此处需配合竞态防护跳过已登录态
/// @param errMsg 验证失败原因描述
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
        verifyFace(next.descriptor, next.image, next.quality);
        return;
    }
    // 所有帧都失败 → 降级密码登录
    stopDotBlink();
    stopFaceRecognition();
    setStatusDot("background:#ff4d4f;");
    setFaceResult(FaceResult::Fail);
    m_subtitleLabel->setText(QStringLiteral("人脸验证未通过"));
    // 提示文案由服务层给出（不含算法数值），双高候选时引导改用密码登录
    qDebug() << "[LoginPage] 人脸验证未通过:" << errMsg;
    m_errorLabel->setText(errMsg.isEmpty() ? QStringLiteral("未识别到您，请重试") : errMsg);
    m_errorLabel->setVisible(true);
    setScanStatus(QStringLiteral("请重试"), 3);
    m_altLoginHint->setText(QStringLiteral("🔑 使用账号密码登录"));
}

/**
 * @brief 处理摄像头异常，退化为密码登录
 * @param msg 摄像头故障详情（仅记录日志，不展示给用户）
 */
void LoginPage::onCameraError(const QString& msg) {
    m_hasCamera = false;
    stopDotBlink();
    setStatusDot("background:#ff4d4f;");
    setFaceResult(FaceResult::NoCamera);
    m_faceCamera->setVisible(false);
    // 摄像头故障详情只进日志，界面保持一句话，避免出现半句技术描述
    qDebug() << "[LoginPage] 摄像头不可用:" << msg;
    m_errorLabel->setText(QStringLiteral("摄像头不可用"));
    m_errorLabel->setVisible(true);
    m_subtitleLabel->setText(QStringLiteral("摄像头未就绪"));
    setScanStatus(QStringLiteral("请用密码登录"), 3);
    m_altLoginHint->setVisible(false);
}

/**
 * @brief 人脸检测状态变化回调（预留扩展，当前实现为空）
 * @param state 新的检测状态枚举值
 */
void LoginPage::onFaceStateChanged(int state) {}

// ============ 密码登录 (复刻Vue版 + NumKeypad数字键盘) ============


