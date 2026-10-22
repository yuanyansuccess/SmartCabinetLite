/**
 * @file FaceCameraWidget.cpp
 * @brief 摄像头预览控件实现：视频取帧、本地人脸框检测绘制与距离过远提示
 * @author 袁燕
 */
#include "FaceCameraWidget.h"
#include "utils/StyleHelper.h"
#include "common/Constants.h"
#include "CameraCapture.h"
#include "DeepFaceExtractor.h"  // 深度学习人脸特征提取
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QDebug>
#include <QPropertyAnimation>
#include <QtMath>
#include <QVector>
#include <QPair>
#include <algorithm>

// ════════════════════════════════════════
// 构造与析构
// ════════════════════════════════════════

FaceCameraWidget::FaceCameraWidget(QWidget* parent) : QWidget(parent)
{
    setupUI();

    m_cameraCapture = new CameraCapture(this);
    connect(m_cameraCapture, &CameraCapture::frameReady,
            this, &FaceCameraWidget::onNewFrame);
    connect(m_cameraCapture, &CameraCapture::cameraError, this,
            [this](const QString& msg) {
        qWarning() << "[FaceCamera] Camera error:" << msg;
        m_cameraAvailable = false;
        emit errorOccurred(msg);
        emit cameraError(msg);
    });

    m_detectTimer = new QTimer(this);
    m_detectTimer->setInterval(33);
    connect(m_detectTimer, &QTimer::timeout, this, &FaceCameraWidget::onDetectTick);

    m_glowAnim = new QPropertyAnimation(this, "borderGlow");
    m_glowAnim->setDuration(1200);
    m_glowAnim->setStartValue(0);
    m_glowAnim->setEndValue(12);
    m_glowAnim->setLoopCount(-1);
}

FaceCameraWidget::~FaceCameraWidget() { stopCamera(); }

// ════════════════════════════════════════
// UI构建
// ════════════════════════════════════════

/**
 * @brief 构建摄像头预览控件
 */
void FaceCameraWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->setAlignment(Qt::AlignCenter);

    // 摄像头圆形显示区域容器 - 增大到320x260确保录像框+状态提示完整显示
    QWidget* cameraContainer = new QWidget(this);
    cameraContainer->setFixedSize(260, 260);
    cameraContainer->setStyleSheet("background:transparent;");

    m_videoLabel = new QLabel(cameraContainer);
    m_videoLabel->setFixedSize(220, 220);
    m_videoLabel->move(20, 8);
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setScaledContents(false);
    m_videoLabel->setStyleSheet(
        "background:#1a1a2e; border:4px dashed #d0d0d0; border-radius:110px;");
    m_videoLabel->hide();

    m_faceOverlay = new QLabel(cameraContainer);
    m_faceOverlay->setFixedSize(220, 220);
    m_faceOverlay->move(20, 8);
    m_faceOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_faceOverlay->setStyleSheet("background:transparent;");
    m_faceOverlay->hide();

    // 状态提示标签 - 移到录像框下方，增大尺寸确保文字完整一行显示
    m_statusHint = new QLabel(cameraContainer);
    m_statusHint->setFixedHeight(StyleHelper::Token::ControlHeight);       // 44 防文字裁剪，触屏优化
    m_statusHint->setMinimumWidth(260);     // 260 确保长文字不换行截断
    m_statusHint->setMaximumWidth(260);
    m_statusHint->setAlignment(Qt::AlignCenter);
    m_statusHint->setWordWrap(false);       // 强制不换行
    m_statusHint->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_statusHint->setStyleSheet(
        "background:rgba(255,77,79,0.75); color:#ffffff; " + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + " "
        "font-weight:600; border-radius:22px; padding:6px 16px;");
    m_statusHint->setText(QString::fromUtf8("\xF0\x9F\x91\xA4" "请将正脸对准摄像头"));
    // 位置：录像框(220x220在20,8)下方，居中 (260宽居中于20+220范围内)
    m_statusHint->move(0, 228);
    m_statusHint->hide();

    m_displayLabel = new QLabel(cameraContainer);
    m_displayLabel->setFixedSize(220, 220);
    m_displayLabel->move(20, 8);
    m_displayLabel->setAlignment(Qt::AlignCenter);
    m_displayLabel->setStyleSheet(
        "background:#1a1a2e; color:#8899aa; " + StyleHelper::fontSize(StyleHelper::Token::FontInput) + " "
        "border:4px dashed #d0d0d0; border-radius:110px;");
    m_displayLabel->setText(QString::fromUtf8("\xe6\x91\x84\xe5\x83\x8f\xe5\xa4\xb4"));
    m_displayLabel->show();

    mainLayout->addWidget(cameraContainer, 0, Qt::AlignCenter);

    // 状态指示灯
    QWidget* statusRow = new QWidget(this);
    statusRow->setStyleSheet("background:transparent;");
    QHBoxLayout* statusLayout = new QHBoxLayout(statusRow);
    statusLayout->setContentsMargins(0, 6, 0, 0);
    statusLayout->setSpacing(8);
    statusLayout->setAlignment(Qt::AlignCenter);

    m_statusDot = new QLabel(statusRow);
    m_statusDot->setFixedSize(8, 8);
    m_statusDot->setStyleSheet(StyleHelper::statusDot());
    statusLayout->addWidget(m_statusDot);

    mainLayout->addWidget(statusRow, 0, Qt::AlignCenter);
}

// ════════════════════════════════════════
// 摄像头控制
// ════════════════════════════════════════

/**
 * @brief 替换人脸检测参数
         * @param policy 检测参数集合
         * @note 集中存放阈值类魔数，便于按场景调整而不改动检测逻辑
         */
void FaceCameraWidget::setDetectPolicy(const DetectPolicy& policy)
{
    m_policy = policy;
}

/**
 * @brief 启动摄像头
 */
void FaceCameraWidget::startCamera()
{
    m_lastFrame = QImage();
    m_lastFaceRect = QRect();
    m_faceDetected = false;
    m_stableCount = 0;
    m_capturing = false;
    m_cameraAvailable = m_cameraCapture ? CameraCapture::hasCamera() : false;

    if (m_cameraCapture && m_cameraAvailable)
    {
        bool ok = m_cameraCapture->start(640, 480, 30);
        if (ok)
        {
            qDebug() << "[FaceCamera] Camera started: 640x480@30fps";
            m_displayLabel->hide();
            m_videoLabel->setStyleSheet(
                "background:#0a1628; border:4px dashed #d0d0d0; border-radius:110px;");
            m_videoLabel->show();
            m_faceOverlay->show();
            m_statusHint->setText(QString::fromUtf8("\xF0\x9F\x91\xA4" "请将正脸对准摄像头"));
            m_statusHint->setStyleSheet(
                "background:rgba(255,77,79,0.75); color:#ffffff; " + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + " "
                "font-weight:600; border-radius:22px; padding:6px 16px;");
            m_statusHint->hide();
            m_statusDot->setStyleSheet(StyleHelper::statusDot());
            m_detectTimer->start();
            m_active = true;
            emit stateChanged(1);
            return;
        }
    }

    qWarning() << "[FaceCamera] No camera available";
    m_cameraAvailable = false;
    m_videoLabel->hide();
    m_faceOverlay->hide();
    m_statusHint->hide();
    m_displayLabel->setText(QString::fromUtf8("\xF0\x9F\x93\xB7 摄像头运行中..."));
    m_displayLabel->setStyleSheet(
        "background:#0a1628; color:#aaccee; " + StyleHelper::fontSize(StyleHelper::Token::FontInput) + " "
        "border:4px solid #52c41a; border-radius:110px;");
    m_displayLabel->show();
    m_detectTimer->start();
    m_active = true;
    emit stateChanged(1);
}

/**
 * @brief 停止摄像头
 */
void FaceCameraWidget::stopCamera()
{
    m_glowAnim->stop();
    m_borderGlow = 0;
    if (m_cameraCapture) m_cameraCapture->stop();
    m_detectTimer->stop();
    m_videoLabel->hide();
    m_faceOverlay->hide();
    m_statusHint->hide();
    m_displayLabel->setText(QString::fromUtf8("\xe6\x91\x84\xe5\x83\x8f\xe5\xa4\xb4"));
    m_displayLabel->setStyleSheet(
        "background:#1a1a2e; color:#8899aa; " + StyleHelper::fontSize(StyleHelper::Token::FontInput) + " "
        "border:4px dashed #d0d0d0; border-radius:110px;");
    m_displayLabel->show();
    m_statusDot->setStyleSheet(
        "background:#cccccc; border-radius:4px; min-width:8px; min-height:8px;");
    m_active = false;
    m_faceDetected = false;
    m_stableCount = 0;
    m_lastFrame = QImage();
    m_lastFaceRect = QRect();
    emit stateChanged(0);
}

/**
 * @brief 判断人脸检测是否正在运行
         * @return true=检测中
         */
bool FaceCameraWidget::isActive() const { return m_active; }

/**
 * @brief 采集当前帧的人脸特征
         * @param base64 输出参数，图像的 Base64 文本
         * @return true=采集成功
         */
QString FaceCameraWidget::captureFaceFeature()
{
    if (!m_lastFaceRect.isNull() && !m_lastFrame.isNull())
        return extractFeature(m_lastFrame, m_lastFaceRect);
    return QString();
}

/**
 * @brief 判断摄像头
 */
bool FaceCameraWidget::hasCamera() const { return CameraCapture::hasCamera(); }

// ════════════════════════════════════════
// 版本B兼容API
// ════════════════════════════════════════

/**
 * @brief 设置是否自动采集
         * @param on true=满足条件后自动采集
         */
void FaceCameraWidget::setAutoCapture(bool enable) { m_autoCapture = enable; }
/**
 * @brief 设置参与比对的最低置信度
         * @param value 置信度下限，低于该值的帧直接丢弃
         */
void FaceCameraWidget::setMinConfidence(double val) { m_minConfidence = val; }
/**
         * @brief 设置自动采集所需的连续稳定帧数
         * @param frames 需要的连续帧数
         */
void FaceCameraWidget::setStableFrames(int frames) { m_stableFrames = frames; }
/**
         * @brief 设置人脸检测的时间间隔
         * @param ms 检测间隔毫秒数
         */
void FaceCameraWidget::setDetectInterval(int ms) { m_detectIntervalMs = ms; }
/**
         * @brief 设置采集成功后的延迟关闭时间
         * @param ms 延迟毫秒数，用于给用户留出取消窗口
         */
void FaceCameraWidget::setCaptureDelay(int ms) { m_captureDelay = ms; }

/**
         * @brief 读取当前帧中的人脸框
         * @return 人脸框矩形；未检出人脸时返回空矩形
         */
QRect FaceCameraWidget::faceRect() const { return m_lastFaceRect; }

/**
         * @brief 判断人脸是否距离过远
         * @return true=人脸框过小，特征质量不足
         * @note 距离过远时特征质量下降易致识别失败，应提示用户靠近而非放宽阈值
         */
bool FaceCameraWidget::isFaceTooFar() const { return m_faceTooFar; }

/**
         * @brief 清除距离过远提示状态
         */
void FaceCameraWidget::clearTooFarHint() {
    if (!m_faceTooFar) return;
    m_faceTooFar = false;
    emit faceTooFarChanged(false);
}

/**
         * @brief 立即触发一次采集
         * @return true=已触发
         */
void FaceCameraWidget::captureNow()
{
    if (m_cameraAvailable && !m_lastFrame.isNull() && !m_lastFaceRect.isNull())
    {
        m_capturing = true;
  // 去掉重复提示，页面层有独立状态标签
        m_glowAnim->start();
        emit stateChanged(3);

        m_videoLabel->setStyleSheet(
            "background:#0a1628; border:4px solid #4da3ff; border-radius:110px;");
        m_glowAnim->start();

        // 同步提取特征 — 通过HTTP调用常驻face-server.js
        // 原方案：每次启动node进程加载模型 → 2-3s/次
        // 新方案：ensureServerRunning()启动常驻服务 → HTTP请求<300ms

        QString feature;
        double deepConfidence = 0;
        QString errMsg;
        DeepFaceExtractor extractor;
        bool ok = extractor.extract(m_lastFrame, feature, deepConfidence, errMsg);

        m_lastDescriptor.clear();
        m_lastExtractError.clear();
        if (ok && !feature.isEmpty())
        {
            QStringList parts = feature.split(",", Qt::SkipEmptyParts);
            for (int i = 0; i < parts.size(); ++i)
            {
                bool convOk = false;
                double dimValue = parts[i].trimmed().toDouble(&convOk);
                if (convOk) m_lastDescriptor.append(dimValue);
            }
        }
        else
        {
            // 保留失败原因：服务端已能识别"距离过远"并回传明确文案，
            // 这里转成距离状态，让页面层提示"请靠近"而不是笼统的"识别失败"
            m_lastExtractError = errMsg;
            // 提取失败是登录体验的关键影响因素，必须可观测（否则只见"识别失败"无从排查）
            qWarning() << "[FaceCamera] 特征提取失败:" << (errMsg.isEmpty() ? QStringLiteral("(无原因)") : errMsg);
            if (errMsg.contains(QStringLiteral("靠近"))) {
                if (!m_faceTooFar) {
                    m_faceTooFar = true;
                    emit faceTooFarChanged(true);
                }
            }
        }

        // 使用face-api.js真实检测置信度，替代面积比估算
        // 深度结果缺失时按面积比估算（系数取自 DetectPolicy，可外部配置）
        const DetectPolicy& policy = m_policy;
        double confidence = deepConfidence > 0 ? deepConfidence : policy.fallbackBaseConfidence;
        if (deepConfidence <= 0) {
            double imgArea = (double)(m_lastFrame.width() * m_lastFrame.height());
            double faceArea = (double)(m_lastFaceRect.width() * m_lastFaceRect.height());
            double areaRatio = imgArea > 0.0 ? faceArea / imgArea : 0.0;
            confidence = qMin(policy.maxConfidence,
                              policy.fallbackBaseConfidence + areaRatio * policy.fallbackAreaWeight);
        }

        m_capturing = false;
        emit captureReady(m_lastFrame, confidence);
    }
    else if (m_cameraAvailable && !m_lastFrame.isNull())
    {
        QImage dummyImg(640, 480, QImage::Format_RGB32);
        dummyImg.fill(QColor(10, 22, 40).rgb());
        QPainter p(&dummyImg);
        p.setPen(Qt::white);
        p.setFont(QFont("Microsoft YaHei", 24));
        p.drawText(dummyImg.rect(), Qt::AlignCenter,
                   QString::fromUtf8("\xe6\x9c\xaa\xe6\xa3\x80\xe6\xb5\x8b\xe5\x88\xb0\xe4\xba\xba\xe8\x84\xb8"));
        p.end();
        emit captureReady(dummyImg, m_policy.fallbackCaptureValue);
    }
    else
    {
        QImage dummyImg(640, 480, QImage::Format_RGB32);
        dummyImg.fill(QColor(10, 22, 40).rgb());
        QPainter p(&dummyImg);
        p.setPen(Qt::white);
        p.setFont(QFont("Microsoft YaHei", 24));
        p.drawText(dummyImg.rect(), Qt::AlignCenter,
                   QString::fromUtf8("\xe6\x97\xa0\xe6\x91\x84\xe5\x83\x8f\xe5\xa4\xb4"));
        p.end();
        emit captureReady(dummyImg, m_policy.dummyCaptureValue);
    }
}

/**
         * @brief 读取最近一次采集到的特征向量
         * @return 特征向量；尚未采集时返回空向量
         */
QString FaceCameraWidget::getLastDescriptor() const
{
    if (m_lastDescriptor.isEmpty()) return QString();
    QStringList parts;
    for (int i = 0; i < m_lastDescriptor.size(); ++i)
        parts.append(QString::number(m_lastDescriptor[i], 'f', 8));
    return parts.join(",");
}

/**
 * @brief 重置人脸检测状态
 */
void FaceCameraWidget::reset()
{
    m_lastDescriptor.clear();
    m_lastFrame = QImage();
    m_lastFaceRect = QRect();
    m_faceDetected = false;
    m_stableCount = 0;
    m_capturing = false;
    m_glowAnim->stop();
    m_borderGlow = 0;
    QPixmap blank(220, 220);
    blank.fill(Qt::transparent);
    m_faceOverlay->setPixmap(blank);
}

// ════════════════════════════════════════
// 新帧回调
// ════════════════════════════════════════

/**
 * @brief 处理边框
 */
void FaceCameraWidget::onNewFrame(const QImage& frame)
{
    if (frame.isNull() || !m_active) return;
    m_lastFrame = frame;

    QImage scaled = frame.scaled(220, 220, Qt::KeepAspectRatioByExpanding,
                                  Qt::SmoothTransformation);
    int sx = (scaled.width() - 220) / 2;
    int sy = (scaled.height() - 220) / 2;
    QImage cropped = scaled.copy(sx, sy, 220, 220);

    // 水平镜像（复刻Web端 scaleX(-1)）
    cropped = cropped.mirrored(true, false);

    QPixmap pix(220, 220);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addEllipse(0, 0, 220, 220);
    painter.setClipPath(path);
    painter.drawImage(0, 0, cropped);
    painter.end();

    m_videoLabel->setPixmap(pix);
}

// ════════════════════════════════════════
// 人脸检测定时器
// ════════════════════════════════════════

/**
         * @brief 人脸检测定时器槽：按间隔取帧并执行检测
         */
void FaceCameraWidget::onDetectTick()
{
    if (!m_active || m_lastFrame.isNull()) return;

    m_detectSkipCounter += 33;
    if (m_detectSkipCounter < m_detectIntervalMs) return;
    m_detectSkipCounter = 0;

    QRect faceR = detectFace(m_lastFrame);

    if (!faceR.isNull())
    {
        m_lastFaceRect = faceR;

        // 距离判定：人脸框过小说明特征像素不足，识别必然失败（不尝试放宽阈值）。
        // 滞回 + 防抖：进入阈值 70 / 恢复阈值 85，中间为滞回区；
        // 且需连续 2 帧结论一致才切换状态。否则人脸框在阈值附近抖动时，
        // 状态会反复翻转，导致"请靠近"与"已检测到人脸"来回闪烁。
        const int curSize = qMin(faceR.width(), faceR.height());
        const int threshold = m_faceTooFar ? SC::FACE_RECOVER_SIZE : SC::FACE_MIN_SIZE;
        bool farThisFrame = curSize < threshold;
        if (farThisFrame != m_tooFarPending) {
            m_tooFarPending = farThisFrame;
            m_tooFarPendingCount = 1;
        } else if (++m_tooFarPendingCount >= 2) {
            if (farThisFrame != m_faceTooFar) {
                m_faceTooFar = farThisFrame;
                emit faceTooFarChanged(farThisFrame);
            }
        }

        double areaRatio = (double)(faceR.width() * faceR.height()) /
                          (double)(m_lastFrame.width() * m_lastFrame.height());
        double confidence = qMin(m_policy.maxConfidence,
                                  m_policy.loopBaseConfidence + areaRatio * m_policy.loopAreaWeight);

        updateOverlay(faceR, confidence);
        handleFaceDetected(confidence);
    }
    else
    {
        m_lastFaceRect = QRect();
        if (m_faceTooFar) {           // 人脸丢失后复位距离提示
            m_faceTooFar = false;
            emit faceTooFarChanged(false);
        }
        m_tooFarPending = false;      // 人脸丢失，防抖状态一并复位
        m_tooFarPendingCount = 0;
        QPixmap blank(220, 220);
        blank.fill(Qt::transparent);
        m_faceOverlay->setPixmap(blank);
        handleFaceLost();
    }
}

// ════════════════════════════════════════
// 人脸检测：肤色YCrCb + 连通域分析
// ════════════════════════════════════════

static bool isSkinPixel(int r, int g, int b)
{
    if (r <= 20 || g <= 20 || b <= 20) return false;
    if (r >= 250 && g >= 250 && b >= 250) return false;

    double y  = 0.299 * (double)r + 0.587 * (double)g + 0.114 * (double)b;
    double cr = ((double)r - y) * 0.713 + 128.0;
    double cb = ((double)b - y) * 0.564 + 128.0;

    return (cr >= 133.0 && cr <= 173.0 && cb >= 77.0 && cb <= 127.0);
}

// 形态学开运算（先腐蚀后膨胀），在降采样掩码上操作
static void morphOpenInPlace(unsigned char* mask, int w, int h, int ksize)
{
    int halfK = ksize / 2;
    // 临时缓冲区
    QVector<unsigned char> tmpBuf(w * h, 0);
    unsigned char* eroded = tmpBuf.data();

    // 腐蚀
    for (int y = halfK; y < h - halfK; ++y)
    {
        for (int x = halfK; x < w - halfK; ++x)
        {
            int idx = y * w + x;
            if (mask[idx] == 0) continue;
            bool allOk = true;
            for (int dy = -halfK; dy <= halfK && allOk; ++dy)
            {
                for (int dx = -halfK; dx <= halfK; ++dx)
                {
                    if (mask[(y + dy) * w + (x + dx)] == 0)
                    {
                        allOk = false;
                        break;
                    }
                }
            }
            if (allOk) eroded[idx] = 255;
        }
    }

    // 膨胀：结果写回mask
    memset(mask, 0, (size_t)(w * h));
    for (int y = halfK; y < h - halfK; ++y)
    {
        for (int x = halfK; x < w - halfK; ++x)
        {
            bool anyOk = false;
            for (int dy = -halfK; dy <= halfK && !anyOk; ++dy)
            {
                for (int dx = -halfK; dx <= halfK; ++dx)
                {
                    if (eroded[(y + dy) * w + (x + dx)] == 255)
                    {
                        anyOk = true;
                        break;
                    }
                }
            }
            if (anyOk) mask[y * w + x] = 255;
        }
    }
}

/**
         * @brief 在当前帧中检测人脸
         * @note 本地检测不联网：基于肤色分割与连通域分析，服务不可用时仍能画出人脸框
         */
QRect FaceCameraWidget::detectFace(const QImage& rgbImage)
{
    // 降采样到160x120加速检测
    QImage tinyImg = rgbImage.scaled(160, 120, Qt::KeepAspectRatio, Qt::FastTransformation);
    int tw = tinyImg.width();
    int th = tinyImg.height();

    // 优化：确保格式为RGB32以便直接访问bits，避免pixelColor()逐点调用
    QImage workImg = tinyImg.convertToFormat(QImage::Format_RGB32);
    const uchar* bits = workImg.constBits();
    int bytesPerLine = workImg.bytesPerLine();

    // 1. 肤色掩码（用unsigned char数组，避免QImage格式问题）
    int totalPixels = tw * th;
    QVector<unsigned char> skinMask(totalPixels, 0);
    int skinPixels = 0;

    for (int y = 0; y < th; ++y)
    {
        const QRgb* line = reinterpret_cast<const QRgb*>(bits + y * bytesPerLine);
        for (int x = 0; x < tw; ++x)
        {
            QRgb pixel = line[x];
            int r = qRed(pixel);
            int g = qGreen(pixel);
            int b = qBlue(pixel);
            if (isSkinPixel(r, g, b))
            {
                skinMask[y * tw + x] = 255;
                ++skinPixels;
            }
        }
    }

    double skinRatio = (double)skinPixels / (double)totalPixels;
    if (skinRatio < m_policy.minSkinRatio) return QRect();

    // 2. 形态学开运算
    morphOpenInPlace(skinMask.data(), tw, th, 3);

    // 3. 连通域分析：BFS找最大连通区域
    QVector<bool> visited(totalPixels, false);
    int bestMinX = tw, bestMinY = th, bestMaxX = 0, bestMaxY = 0;
    int bestArea = 0;

    // BFS队列（预分配）
    QVector<int> bfsQueue(totalPixels);

    for (int y = 0; y < th; ++y)
    {
        for (int x = 0; x < tw; ++x)
        {
            int startIdx = y * tw + x;
            if (visited[startIdx] || skinMask[startIdx] == 0) continue;

            // BFS
            int head = 0, tail = 0;
            bfsQueue[tail++] = startIdx;
            visited[startIdx] = true;
            int area = 1;
            int minX = x, minY = y, maxX = x, maxY = y;

            while (head < tail)
            {
                int curIdx = bfsQueue[head++];
                int cx = curIdx % tw;
                int cy = curIdx / tw;

                if (cx < minX) minX = cx;
                if (cy < minY) minY = cy;
                if (cx > maxX) maxX = cx;
                if (cy > maxY) maxY = cy;

                // 4邻域
                static const int dx[4] = {0, 1, 0, -1};
                static const int dy[4] = {-1, 0, 1, 0};
                for (int dirIndex = 0; dirIndex < 4; ++dirIndex)
                {
                    int nx = cx + dx[dirIndex];
                    int ny = cy + dy[dirIndex];
                    int nIdx = ny * tw + nx;
                    if (nx >= 0 && nx < tw && ny >= 0 && ny < th &&
                        !visited[nIdx] && skinMask[nIdx] > 0)
                    {
                        visited[nIdx] = true;
                        bfsQueue[tail++] = nIdx;
                        ++area;
                    }
                }
            }

            if (area > bestArea)
            {
                bestArea = area;
                bestMinX = minX; bestMinY = minY;
                bestMaxX = maxX; bestMaxY = maxY;
            }
        }
    }

    // 4. 验证面积和宽高比
    double faceRatio = (double)bestArea / (double)totalPixels;
    if (faceRatio < m_policy.minFaceRatio) return QRect();

    int fw = bestMaxX - bestMinX + 1;
    int fh = bestMaxY - bestMinY + 1;
    double aspectRatio = (double)fh / (double)fw;
    if (aspectRatio < m_policy.minAspectRatio || aspectRatio > m_policy.maxAspectRatio) return QRect();

    // 5. 映射回640x480坐标（加8%边距，避免框超出人脸太多）
    // 0.08 缩小人脸框，使其紧贴人脸不超出
    double scaleX = (double)rgbImage.width() / (double)tw;
    double scaleY = (double)rgbImage.height() / (double)th;
    int expandX = (int)((double)fw * scaleX * m_policy.boxInsetRatio);
    int expandY = (int)((double)fh * scaleY * m_policy.boxInsetRatio);

    int origX = qMax(0, (int)((double)bestMinX * scaleX) - expandX);
    int origY = qMax(0, (int)((double)bestMinY * scaleY) - expandY);
    int origW = qMin(rgbImage.width() - origX, (int)((double)fw * scaleX) + expandX * 2);
    int origH = qMin(rgbImage.height() - origY, (int)((double)fh * scaleY) + expandY * 2);

    return QRect(origX, origY, origW, origH);
}

// ════════════════════════════════════════
// 特征提取：纹理哈希 128维
// ════════════════════════════════════════

/**
         * @brief 从人脸区域提取特征向量
         * @return 128维特征；提取失败时返回空向量
         */
QString FaceCameraWidget::extractFeature(const QImage& frame, const QRect& faceRect)
{
    Q_UNUSED(faceRect);  // 深度学习模式不使用faceRect，face-api.js自己检测
    if (frame.isNull()) return QString();

    // 深度学习特征提取（唯一方案）
    // 使用face-api.js的128维深度特征，替代8x8网格纹理哈希
    // 深度学习特征个体辨识力强，解决陌生人泛化误识问题
    //   纹理哈希辨识力不足，不能用于身份验证，已彻底移除
    static bool deepFaceChecked = false;
    static bool deepFaceAvailable = false;
    if (!deepFaceChecked) {
        deepFaceAvailable = DeepFaceExtractor::isAvailable();
        deepFaceChecked = true;
        if (deepFaceAvailable) {
            qInfo() << "[FaceCamera] 深度学习人脸识别已启用 (face-api.js)";
        } else {
            qCritical() << "[FaceCamera] 深度学习环境不可用！人脸识别功能无法使用，请检查Node.js和face-api.js安装。"
                        << "请确认: 1)Node.js已安装并在PATH中 2)tools/face-recognition/extract-feature.js存在"
                        << "3)tools/face-recognition/models/模型文件存在 4)node_modules/@vladmandic/face-api已安装";
        }
    }

    if (!deepFaceAvailable) {
        // 深度学习不可用 → 返回空特征，LoginPage会提示使用密码登录
        // 纹理哈希辨识力不足，两个不同人可能sim>0.85，不能用于身份验证
        return QString();
    }

    // 深度学习提取：发送完整帧（不裁剪），让face-api.js自己检测人脸
    QString feature;
    double confidence = 0;
    QString message;
    DeepFaceExtractor extractor;
    if (extractor.extract(frame, feature, confidence, message)) {
        return feature;
    }
    // 深度学习提取失败 → 返回空，不降级到纹理哈希
    qWarning() << "[FaceCamera] 深度学习提取失败:" << message;
    return QString();
}

/**
         * @brief 对特征向量做L2归一化，消除光照与表情带来的幅度差异
         * @param v 待归一化向量（原地修改）
         */
void FaceCameraWidget::normalizeL2(QVector<double>& v)
{
    double sum = 0.0;
    for (int i = 0; i < v.size(); ++i)
        sum += v[i] * v[i];
    double len = qSqrt(sum);
    if (len > 1e-10)
    {
        for (int i = 0; i < v.size(); ++i)
            v[i] /= len;
    }
}

// ════════════════════════════════════════
// 叠加层绘制：人脸框 + 特征点
// ════════════════════════════════════════

/**
         * @brief 更新预览画面上的状态叠加层
         */
void FaceCameraWidget::updateOverlay(const QRect& faceRect, double confidence)
{
    if (faceRect.isNull() || m_lastFrame.isNull()) return;

    // 将640x480坐标映射到220x220显示区域（考虑镜像翻转）
    double scaleX = 220.0 / (double)m_lastFrame.width();
    double scaleY = 220.0 / (double)m_lastFrame.height();
    double scale = qMin(scaleX, scaleY);

    int displayX = (int)((double)(m_lastFrame.width() - faceRect.x() - faceRect.width()) * scale);
    int displayY = (int)((double)faceRect.y() * scale);
    int displayW = (int)((double)faceRect.width() * scale);
    int displayH = (int)((double)faceRect.height() * scale);

    displayX = qMax(0, qMin(displayX, 218));
    displayY = qMax(0, qMin(displayY, 218));
    displayW = qMin(displayW, 220 - displayX);
    displayH = qMin(displayH, 220 - displayY);

    QPixmap overlay(220, 220);
    overlay.fill(Qt::transparent);
    QPainter p(&overlay);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 人脸框：绿色圆角细线 (复刻Web端 #52c41a)
    // 2.0px细线+圆角，更精致不臃肿
    p.setPen(QPen(QColor("#52c41a"), 2.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(displayX, displayY, displayW, displayH, 6, 6);

    // 特征点：蓝色小圆点 (复刻Web端 #4da3ff)
    // 1.5 缩小特征点，更精致
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#4da3ff"));

    // 简化版11个关键点位置比例
    struct Pt { double rx, ry; };
    static const Pt lmPts[11] = {
        {0.25, 0.35}, {0.35, 0.35},
        {0.65, 0.35}, {0.75, 0.35},
        {0.45, 0.55}, {0.55, 0.55},
        {0.35, 0.75}, {0.50, 0.80}, {0.65, 0.75},
        {0.20, 0.50}, {0.80, 0.50},
    };

    for (int i = 0; i < 11; ++i)
    {
        int px = displayX + (int)((double)displayW * lmPts[i].rx);
        int py = displayY + (int)((double)displayH * lmPts[i].ry);
        p.drawEllipse(QPointF((double)px, (double)py), 1.5, 1.5);
    }

    // 置信度标签 - 更小巧精致
    // 缩小标签尺寸，置于框内右上角
    int confW = 60;
    int confH = 14;
    int confX = displayX + displayW - confW - 2;
    int confY = displayY + 2;
    QRect confBg(confX, confY, confW, confH);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(82, 196, 26, 200));
    p.drawRoundedRect(confBg, 3, 3);
    p.setPen(QColor("#ffffff"));
    p.setFont(QFont("Microsoft YaHei", 7));
    p.drawText(confBg, Qt::AlignCenter,
               QString("face %1%").arg((int)(confidence * 100.0)));

    p.end();
    m_faceOverlay->setPixmap(overlay);
}

// ════════════════════════════════════════
// 人脸检测状态处理
// ════════════════════════════════════════

/**
         * @brief 检出人脸时的处理：绘制人脸框并判断是否自动采集
         */
void FaceCameraWidget::handleFaceDetected(double confidence)
{
    Q_UNUSED(confidence);
    m_stableCount++;

    if (!m_faceDetected)
    {
        m_faceDetected = true;
        m_videoLabel->setStyleSheet(
            "background:#0a1628; border:4px solid #52c41a; border-radius:110px;");
  // 去掉重复提示，各页面有自己的状态标签
        m_statusHint->hide();
        m_statusDot->setStyleSheet(
            "background:#52c41a; border-radius:4px; min-width:8px; min-height:8px;");

        emit faceDetectionChanged(true, 0.8);
        emit faceDetected(QString());
        emit stateChanged(2);
    }

    tryAutoCapture();
}

/**
         * @brief 丢失人脸时的处理：清除人脸框与距离提示
         */
void FaceCameraWidget::handleFaceLost()
{
    if (m_faceDetected)
    {
        m_faceDetected = false;
        m_stableCount = 0;
        m_videoLabel->setStyleSheet(
            "background:#0a1628; border:4px dashed #d0d0d0; border-radius:110px;");
  // 去掉重复提示，各页面有独立状态标签
        m_statusHint->hide();
        m_statusDot->setStyleSheet(StyleHelper::statusDot());

        emit faceLost();
        emit stateChanged(1);
    }
}

/**
         * @brief 在满足置信度与稳定帧条件时自动触发采集
         * @return true=已触发采集
         */
void FaceCameraWidget::tryAutoCapture()
{
    if (!m_autoCapture || m_capturing || !m_faceDetected) return;
    if (m_stableCount < m_stableFrames) return;

    m_capturing = true;
  // 去掉重复提示，页面层有独立状态标签
    m_glowAnim->start();
    emit stateChanged(3);
    m_glowAnim->start();
    emit stateChanged(3);

    QTimer::singleShot(m_captureDelay, this, [this]() {
        if (!m_active || !m_faceDetected)
        {
            m_capturing = false;
            return;
        }
        captureNow();
        m_capturing = false;
    });
}

// 本地方位估算：根据人脸框位置计算yaw/pitch
// yaw: 人脸框中心X偏离画面中心 / 人脸框宽度 → [-1, 1]
// 正值=脸偏右(用户左转露右脸)，负值=脸偏左(用户右转露左脸)
// pitch: 人脸框中心Y偏离画面中心 / 人脸框高度 × 2 → [-1, 1]
// 正值=低头(脸下移)，负值=抬头(脸上移)
// 用人脸框宽高归一化（比画面宽度更稳定，不受分辨率影响）
// pitch放大2倍：上下偏移量天然较小，需放大提高灵敏度
void FaceCameraWidget::estimatePosture(const QRect& faceRect, double& yaw, double& pitch) {
    if (faceRect.isEmpty() || faceRect.width() < 10) {
        yaw = 0; pitch = 0;
        return;
    }
    QImage frame = currentFrame();
    double imgW = frame.isNull() ? 220.0 : frame.width();
    double imgH = frame.isNull() ? 220.0 : frame.height();
    double centerX = imgW / 2.0;
    double centerY = imgH / 2.0;

    QPoint faceCenter = faceRect.center();
    double faceW = qMax(1.0, (double)faceRect.width());
    double faceH = qMax(1.0, (double)faceRect.height());

    // 用人脸框宽高归一化（不是画面宽度），更稳定
    double dx = (faceCenter.x() - centerX) / faceW;
    double dy = (faceCenter.y() - centerY) / faceH;

    yaw = qBound(-1.0, dx, 1.0);
    // pitch不反转：抬头dy<pitch负值，低头dy>pitch正值（与face-server.js一致）
    // 放大2倍提高上下偏灵敏度
    pitch = qBound(-1.0, dy * 2.0, 1.0);
}
