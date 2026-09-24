// 作者：袁燕  智能柜Qt Widget 2.0  FaceCameraWidget实现  
// 日期：2026-06-21 纯C++人脸检测+特征提取
// [2026-06-21] 重写说明：
// 1. 肤色人脸检测（YCrCb色彩空间+形态学开运算+连通域分析）
// 2. 128维纹理特征提取（64x64灰度→8x8网格→均值+方差）
// 3. 人脸框绘制（绿色矩形+蓝色特征点仿真）
// 4. 状态提示叠加层（复刻Web端已检测到人脸/请对准摄像头等）
// 5. 状态指示灯（蓝色扫描/绿色成功/红色错误）
// 6. 摄像头圆框边框动画（灰色虚线→绿色实线→蓝色脉冲）
// 替代之前的随机特征向量生成，实现真正的刷脸登录功能
#include "FaceCameraWidget.h"
#include "CameraCapture.h"
#include "DeepFaceExtractor.h"  // [V8.0] 深度学习人脸特征提取
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

void FaceCameraWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->setAlignment(Qt::AlignCenter);

    // [2026-06-23] 摄像头圆形显示区域容器 - 增大到320x260确保录像框+状态提示完整显示
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

    // [2026-06-23] 状态提示标签 - 移到录像框下方，增大尺寸确保文字完整一行显示
    m_statusHint = new QLabel(cameraContainer);
    m_statusHint->setFixedHeight(44);       // 40→44 防文字裁剪，触屏优化
    m_statusHint->setMinimumWidth(260);     // 220→260 确保长文字不换行截断
    m_statusHint->setMaximumWidth(260);
    m_statusHint->setAlignment(Qt::AlignCenter);
    m_statusHint->setWordWrap(false);       // 强制不换行
    m_statusHint->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_statusHint->setStyleSheet(
        "background:rgba(255,77,79,0.75); color:#ffffff; font-size:15px; "
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
        "background:#1a1a2e; color:#8899aa; font-size:16px; "
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
    m_statusDot->setStyleSheet(
        "background:#4da3ff; border-radius:4px; min-width:8px; min-height:8px;");
    statusLayout->addWidget(m_statusDot);

    mainLayout->addWidget(statusRow, 0, Qt::AlignCenter);
}

// ════════════════════════════════════════
// 摄像头控制
// ════════════════════════════════════════

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
                "background:rgba(255,77,79,0.75); color:#ffffff; font-size:15px; "
                "font-weight:600; border-radius:22px; padding:6px 16px;");
            m_statusHint->hide();
            m_statusDot->setStyleSheet(
                "background:#4da3ff; border-radius:4px; min-width:8px; min-height:8px;");
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
        "background:#0a1628; color:#aaccee; font-size:16px; "
        "border:4px solid #52c41a; border-radius:110px;");
    m_displayLabel->show();
    m_detectTimer->start();
    m_active = true;
    emit stateChanged(1);
}

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
        "background:#1a1a2e; color:#8899aa; font-size:16px; "
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

bool FaceCameraWidget::isActive() const { return m_active; }

QString FaceCameraWidget::captureFaceFeature()
{
    if (!m_lastFaceRect.isNull() && !m_lastFrame.isNull())
        return extractFeature(m_lastFrame, m_lastFaceRect);
    return QString();
}

bool FaceCameraWidget::hasCamera() const { return CameraCapture::hasCamera(); }

// ════════════════════════════════════════
// 版本B兼容API
// ════════════════════════════════════════

void FaceCameraWidget::setAutoCapture(bool enable) { m_autoCapture = enable; }
void FaceCameraWidget::setMinConfidence(double val) { m_minConfidence = val; }
void FaceCameraWidget::setStableFrames(int frames) { m_stableFrames = frames; }
void FaceCameraWidget::setDetectInterval(int ms) { m_detectIntervalMs = ms; }
void FaceCameraWidget::setCaptureDelay(int ms) { m_captureDelay = ms; }

QRect FaceCameraWidget::faceRect() const { return m_lastFaceRect; }

void FaceCameraWidget::captureNow()
{
    if (m_cameraAvailable && !m_lastFrame.isNull() && !m_lastFaceRect.isNull())
    {
        m_capturing = true;
        // [V2.17fix-0706 袁燕] 去掉重复提示，页面层有独立状态标签
        m_glowAnim->start();
        emit stateChanged(3);

        m_videoLabel->setStyleSheet(
            "background:#0a1628; border:4px solid #4da3ff; border-radius:110px;");
        m_glowAnim->start();

        // [V2.03 2026-06-28] 同步提取特征 — 通过HTTP调用常驻face-server.js
        // 原方案：每次启动node进程加载模型 → 2-3s/次
        // 新方案：ensureServerRunning()启动常驻服务 → HTTP请求<300ms
        //   作者：袁燕
        QString feature;
        double deepConfidence = 0;
        QString errMsg;
        DeepFaceExtractor extractor;
        bool ok = extractor.extract(m_lastFrame, feature, deepConfidence, errMsg);

        m_lastDescriptor.clear();
        if (ok && !feature.isEmpty())
        {
            QStringList parts = feature.split(",", Qt::SkipEmptyParts);
            for (int i = 0; i < parts.size(); ++i)
            {
                bool convOk = false;
                double v = parts[i].trimmed().toDouble(&convOk);
                if (convOk) m_lastDescriptor.append(v);
            }
        }

        // [V2.03] 使用face-api.js真实检测置信度，替代面积比估算
        // 原逻辑：confidence = 0.5 + areaRatio*2.0（面积比，不反映检测质量）
        // 新逻辑：优先用深度学习返回的confidence，面积比仅作兜底
        double confidence = deepConfidence > 0 ? deepConfidence : 0.5;
        if (deepConfidence <= 0) {
            double imgArea = (double)(m_lastFrame.width() * m_lastFrame.height());
            double faceArea = (double)(m_lastFaceRect.width() * m_lastFaceRect.height());
            double areaRatio = imgArea > 0.0 ? faceArea / imgArea : 0.0;
            confidence = qMin(0.95, 0.5 + areaRatio * 2.0);
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
        emit captureReady(dummyImg, 0.3);
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
        emit captureReady(dummyImg, 0.5);
    }
}

QString FaceCameraWidget::getLastDescriptor() const
{
    if (m_lastDescriptor.isEmpty()) return QString();
    QStringList parts;
    for (int i = 0; i < m_lastDescriptor.size(); ++i)
        parts.append(QString::number(m_lastDescriptor[i], 'f', 8));
    return parts.join(",");
}

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

        double areaRatio = (double)(faceR.width() * faceR.height()) /
                          (double)(m_lastFrame.width() * m_lastFrame.height());
        double confidence = qMin(0.95, 0.45 + areaRatio * 3.0);

        updateOverlay(faceR, confidence);
        handleFaceDetected(confidence);
    }
    else
    {
        m_lastFaceRect = QRect();
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

QRect FaceCameraWidget::detectFace(const QImage& rgbImage)
{
    // 降采样到160x120加速检测
    QImage tinyImg = rgbImage.scaled(160, 120, Qt::KeepAspectRatio, Qt::FastTransformation);
    int tw = tinyImg.width();
    int th = tinyImg.height();

    // [2026-06-23] 优化：确保格式为RGB32以便直接访问bits，避免pixelColor()逐点调用
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
    if (skinRatio < 0.02) return QRect();

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
                for (int d = 0; d < 4; ++d)
                {
                    int nx = cx + dx[d];
                    int ny = cy + dy[d];
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
    if (faceRatio < 0.03) return QRect();

    int fw = bestMaxX - bestMinX + 1;
    int fh = bestMaxY - bestMinY + 1;
    double aspectRatio = (double)fh / (double)fw;
    if (aspectRatio < 0.6 || aspectRatio > 2.2) return QRect();

    // 5. 映射回640x480坐标（加8%边距，避免框超出人脸太多）
    // [2026-06-23] 0.20→0.08 缩小人脸框，使其紧贴人脸不超出
    double scaleX = (double)rgbImage.width() / (double)tw;
    double scaleY = (double)rgbImage.height() / (double)th;
    int expandX = (int)((double)fw * scaleX * 0.08);
    int expandY = (int)((double)fh * scaleY * 0.08);

    int origX = qMax(0, (int)((double)bestMinX * scaleX) - expandX);
    int origY = qMax(0, (int)((double)bestMinY * scaleY) - expandY);
    int origW = qMin(rgbImage.width() - origX, (int)((double)fw * scaleX) + expandX * 2);
    int origH = qMin(rgbImage.height() - origY, (int)((double)fh * scaleY) + expandY * 2);

    return QRect(origX, origY, origW, origH);
}

// ════════════════════════════════════════
// 特征提取：纹理哈希 128维
// ════════════════════════════════════════

QString FaceCameraWidget::extractFeature(const QImage& frame, const QRect& faceRect)
{
    Q_UNUSED(faceRect);  // 深度学习模式不使用faceRect，face-api.js自己检测
    if (frame.isNull()) return QString();

    // [V2.02 2026-06-28] 深度学习特征提取（唯一方案）
    // 使用face-api.js的128维深度特征，替代8x8网格纹理哈希
    // 深度学习特征个体辨识力强，解决陌生人泛化误识问题
    //   作者：袁燕 — 纹理哈希辨识力不足，不能用于身份验证，已彻底移除
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
        // [V2.02] 深度学习不可用 → 返回空特征，LoginPage会提示使用密码登录
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
    // 深度学习提取失败 → 返回空，不再降级到纹理哈希
    qWarning() << "[FaceCamera] 深度学习提取失败:" << message;
    return QString();
}

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
    // [2026-06-23] 2.5→2.0px细线+圆角，更精致不臃肿
    p.setPen(QPen(QColor("#52c41a"), 2.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(displayX, displayY, displayW, displayH, 6, 6);

    // 特征点：蓝色小圆点 (复刻Web端 #4da3ff)
    // [2026-06-23] 1.8→1.5 缩小特征点，更精致
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
    // [2026-06-23] 缩小标签尺寸，置于框内右上角
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

void FaceCameraWidget::handleFaceDetected(double confidence)
{
    Q_UNUSED(confidence);
    m_stableCount++;

    if (!m_faceDetected)
    {
        m_faceDetected = true;
        m_videoLabel->setStyleSheet(
            "background:#0a1628; border:4px solid #52c41a; border-radius:110px;");
        // [V2.17fix-0706 袁燕] 去掉重复提示，各页面有自己的状态标签
        m_statusHint->hide();
        m_statusDot->setStyleSheet(
            "background:#52c41a; border-radius:4px; min-width:8px; min-height:8px;");

        emit faceDetectionChanged(true, 0.8);
        emit faceDetected(QString());
        emit stateChanged(2);
    }

    tryAutoCapture();
}

void FaceCameraWidget::handleFaceLost()
{
    if (m_faceDetected)
    {
        m_faceDetected = false;
        m_stableCount = 0;
        m_videoLabel->setStyleSheet(
            "background:#0a1628; border:4px dashed #d0d0d0; border-radius:110px;");
        // [V2.17fix-0706 袁燕] 去掉重复提示，各页面有独立状态标签
        m_statusHint->hide();
        m_statusDot->setStyleSheet(
            "background:#4da3ff; border-radius:4px; min-width:8px; min-height:8px;");

        emit faceLost();
        emit stateChanged(1);
    }
}

void FaceCameraWidget::tryAutoCapture()
{
    if (!m_autoCapture || m_capturing || !m_faceDetected) return;
    if (m_stableCount < m_stableFrames) return;

    m_capturing = true;
    // [V2.17fix-0706 袁燕] 去掉重复提示，页面层有独立状态标签
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
    // pitch不反转：抬头dy<0→pitch负值，低头dy>0→pitch正值（与face-server.js一致）
    // 放大2倍提高上下偏灵敏度
    pitch = qBound(-1.0, dy * 2.0, 1.0);
}

