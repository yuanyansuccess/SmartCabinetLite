/**
 * @file CameraCapture.cpp
 * @brief 摄像头采集实现 - Windows MF / Linux Qt Multimedia
 * @author 袁燕  修改: 2026-06-20 添加Linux/麒麟Qt Multimedia支持
 *
 * Windows: 使用Media Foundation原生API
 * Linux/麒麟: 使用Qt Multimedia (QCamera)
 * 接口统一：start/stop/getFrame，输出QImage供FaceCameraWidget消费
 */
#include "CameraCapture.h"
#include <QDebug>
#include <QThread>

#ifdef HAS_QT_MULTIMEDIA
#include <QCamera>
#include <QMediaCaptureSession>
#include <QImageCapture>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#endif

// ==================== 构造/析构 ====================

CameraCapture::CameraCapture(QObject* parent) : QObject(parent) {
    m_timer = new QTimer(this);
    m_timer->setInterval(33); // ~30fps
    connect(m_timer, &QTimer::timeout, this, &CameraCapture::onCaptureTimer);
}

CameraCapture::~CameraCapture() { stop(); }

// ==================== 公开方法 ====================

bool CameraCapture::hasCamera() {
#ifdef _WIN32
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    IMFAttributes* attr = nullptr;
    HRESULT hr = MFCreateAttributes(&attr, 1);
    if (SUCCEEDED(hr)) {
        hr = attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                           MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    }
    if (SUCCEEDED(hr)) {
        hr = MFEnumDeviceSources(attr, &devices, &count);
    }
    if (attr) attr->Release();
    if (devices) {
        for (UINT32 i = 0; i < count; i++) devices[i]->Release();
        CoTaskMemFree(devices);
    }
    return SUCCEEDED(hr) && count > 0;
#else
    return false;
#endif
}

QStringList CameraCapture::availableCameras() {
    QStringList result;
#ifdef _WIN32
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    IMFAttributes* attr = nullptr;
    HRESULT hr = MFCreateAttributes(&attr, 1);
    if (SUCCEEDED(hr))
        attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                      MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (SUCCEEDED(hr))
        hr = MFEnumDeviceSources(attr, &devices, &count);
    if (SUCCEEDED(hr) && devices) {
        for (UINT32 i = 0; i < count; i++) {
            WCHAR* name = nullptr;
            UINT32 len = 0;
            devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &len);
            if (name) {
                result.append(QString::fromWCharArray(name));
                CoTaskMemFree(name);
            }
            devices[i]->Release();
        }
        CoTaskMemFree(devices);
    }
    if (attr) attr->Release();
#endif
    return result;
}

bool CameraCapture::start(int width, int height, int fps) {
    if (m_running) return true;

#ifdef _WIN32
    if (!initMF()) {
        emit cameraError(QStringLiteral("Media Foundation初始化失败"));
        return false;
    }
    if (!startMFCapture(width, height, fps)) {
        emit cameraError(QStringLiteral("无法打开摄像头"));
        return false;
    }
    m_timer->start();
    m_running = true;
    qDebug() << "[CameraCapture] Started" << width << "x" << height << "@" << fps << "fps";
    return true;
#else
    emit cameraError(QStringLiteral("当前平台不支持摄像头"));
    return false;
#endif
}

void CameraCapture::stop() {
    if (!m_running) return;
    m_timer->stop();
    m_running = false;
#ifdef _WIN32
    stopMFCapture();
#endif
}

bool CameraCapture::isRunning() const { return m_running; }

// ==================== 内部实现 ====================

void CameraCapture::onCaptureTimer() {
#ifdef _WIN32
    QImage frame = grabMFFrame();
    if (!frame.isNull()) {
        QMutexLocker lock(&m_mutex);
        emit frameReady(frame);
    }
#endif
}

#ifdef _WIN32

bool CameraCapture::initMF() {
    static bool s_initialized = false;
    if (!s_initialized) {
        HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
        if (FAILED(hr)) {
            qWarning() << "[CameraCapture] MFStartup failed:" << hr;
            return false;
        }
        s_initialized = true;
    }
    return true;
}

bool CameraCapture::startMFCapture(int width, int height, int fps) {
    m_capWidth = width;
    m_capHeight = height;
    memset(&m_videoSubtype, 0, sizeof(m_videoSubtype));

    // 获取默认摄像头
    IMFAttributes* attr = nullptr;
    HRESULT hr = MFCreateAttributes(&attr, 1);
    if (SUCCEEDED(hr))
        hr = attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                           MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    if (SUCCEEDED(hr))
        hr = MFEnumDeviceSources(attr, &devices, &count);
    if (attr) attr->Release();

    if (FAILED(hr) || count == 0) {
        qWarning() << "[CameraCapture] No camera found";
        if (devices) CoTaskMemFree(devices);
        return false;
    }

    // 使用第一个摄像头
    hr = devices[0]->ActivateObject(IID_PPV_ARGS(&m_mediaSource));
    for (UINT32 i = 0; i < count; i++) devices[i]->Release();
    CoTaskMemFree(devices);

    if (FAILED(hr)) {
        qWarning() << "[CameraCapture] ActivateObject failed:" << hr;
        return false;
    }

    // 创建SourceReader
    IMFAttributes* readerAttr = nullptr;
    MFCreateAttributes(&readerAttr, 1);
    readerAttr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

    hr = MFCreateSourceReaderFromMediaSource(m_mediaSource, readerAttr, &m_sourceReader);
    if (readerAttr) readerAttr->Release();

    if (FAILED(hr)) {
        qWarning() << "[CameraCapture] CreateSourceReader failed:" << hr;
        return false;
    }

    // 设置视频格式 — 优先MJPG，降级NV12/YUY2
    IMFMediaType* mediaType = nullptr;
    hr = MFCreateMediaType(&mediaType);
    if (SUCCEEDED(hr)) {
        mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        mediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_MJPG);
        MFSetAttributeSize(mediaType, MF_MT_FRAME_SIZE, width, height);
        MFSetAttributeRatio(mediaType, MF_MT_FRAME_RATE, fps, 1);

        hr = m_sourceReader->SetCurrentMediaType(
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, mediaType);
        mediaType->Release();

        if (SUCCEEDED(hr)) {
            m_videoSubtype = MFVideoFormat_MJPG;
            qDebug() << "[CameraCapture] Using MJPG format";
        }
    }

    // MJPG失败 → 获取摄像头原生格式
    if (FAILED(hr)) {
        qDebug() << "[CameraCapture] MJPG not supported, reading native format...";
        IMFMediaType* nativeType = nullptr;
        hr = m_sourceReader->GetCurrentMediaType(
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &nativeType);
        if (SUCCEEDED(hr) && nativeType) {
            GUID subtype = {0};
            nativeType->GetGUID(MF_MT_SUBTYPE, &subtype);
            m_videoSubtype = subtype;

            UINT32 w = 0, h = 0;
            MFGetAttributeSize(nativeType, MF_MT_FRAME_SIZE, &w, &h);
            if (w > 0 && h > 0) { m_capWidth = w; m_capHeight = h; }

            if (subtype == MFVideoFormat_NV12)
                qDebug() << "[CameraCapture] Using NV12 format" << w << "x" << h;
            else if (subtype == MFVideoFormat_YUY2)
                qDebug() << "[CameraCapture] Using YUY2 format" << w << "x" << h;
            else
                qDebug() << "[CameraCapture] Using unknown format" << w << "x" << h;

            nativeType->Release();
        } else {
            qWarning() << "[CameraCapture] Cannot get native media type";
        }
    }

    qDebug() << "[CameraCapture] Camera started successfully";
    return true;
}

void CameraCapture::stopMFCapture() {
    if (m_sourceReader) { m_sourceReader->Release(); m_sourceReader = nullptr; }
    if (m_mediaSource) { m_mediaSource->Shutdown(); m_mediaSource->Release(); m_mediaSource = nullptr; }
}

QImage CameraCapture::grabMFFrame() {
    if (!m_sourceReader) return QImage();

    IMFSample* sample = nullptr;
    DWORD streamIndex, flags;
    LONGLONG timestamp;

    HRESULT hr = m_sourceReader->ReadSample(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
        &streamIndex, &flags, &timestamp, &sample);

    if (FAILED(hr) || !sample) return QImage();

    // 获取buffer
    IMFMediaBuffer* buffer = nullptr;
    hr = sample->ConvertToContiguousBuffer(&buffer);
    if (FAILED(hr) || !buffer) {
        if (sample) sample->Release();
        return QImage();
    }

    BYTE* data = nullptr;
    DWORD maxLen = 0, curLen = 0;
    hr = buffer->Lock(&data, &maxLen, &curLen);
    if (FAILED(hr) || !data) {
        buffer->Release();
        sample->Release();
        return QImage();
    }

    QImage img;
    int w = m_capWidth, h = m_capHeight;

    if (m_videoSubtype == MFVideoFormat_MJPG) {
        // MJPG = JPEG压缩数据，直接用Qt解码
        img = QImage::fromData(data, (int)curLen);
    }
    else if (m_videoSubtype == MFVideoFormat_NV12) {
        img = nv12ToRgb(data, w, h);
    }
    else if (m_videoSubtype == MFVideoFormat_YUY2) {
        img = yuy2ToRgb(data, w, h);
    }
    else {
        // 未知格式 → 先尝试JPEG解码，失败则试NV12
        img = QImage::fromData(data, (int)curLen);
        if (img.isNull()) {
            img = nv12ToRgb(data, w, h);
        }
    }

    buffer->Unlock();
    buffer->Release();
    sample->Release();

    return img;
}

// ── NV12 → RGB24 转换 ──
// NV12布局：Y平面 (W×H) + 交错UV平面 (W×H/2, 每2字节=U,V)
// YUV→RGB公式：R=Y+1.402*(V-128), G=Y-0.344*(U-128)-0.714*(V-128), B=Y+1.772*(U-128)
QImage CameraCapture::nv12ToRgb(const unsigned char* data, int width, int height) {
    QImage rgb(width, height, QImage::Format_RGB32);
    const unsigned char* yPlane = data;
    const unsigned char* uvPlane = data + width * height;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int Y = yPlane[y * width + x];
            int uvIdx = (y / 2) * width + (x & ~1);
            int U = uvPlane[uvIdx];
            int V = uvPlane[uvIdx + 1];

            int C = Y - 16;
            int D = U - 128;
            int E = V - 128;

            int r = qBound(0, (298 * C + 409 * E + 128) >> 8, 255);
            int g = qBound(0, (298 * C - 100 * D - 208 * E + 128) >> 8, 255);
            int b = qBound(0, (298 * C + 516 * D + 128) >> 8, 255);

            rgb.setPixel(x, y, qRgb(r, g, b));
        }
    }
    return rgb;
}

// ── YUY2 → RGB24 转换 ──
// YUY2布局：每2像素4字节 [Y0,U,Y1,V]，macro-pixel=2像素
QImage CameraCapture::yuy2ToRgb(const unsigned char* data, int width, int height) {
    QImage rgb(width, height, QImage::Format_RGB32);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; x += 2) {
            int idx = (y * width + x) * 2;
            int Y0 = data[idx];
            int U  = data[idx + 1];
            int Y1 = data[idx + 2];
            int V  = data[idx + 3];

            int C0 = Y0 - 16, C1 = Y1 - 16;
            int D = U - 128, E = V - 128;

            int r0 = qBound(0, (298 * C0 + 409 * E + 128) >> 8, 255);
            int g0 = qBound(0, (298 * C0 - 100 * D - 208 * E + 128) >> 8, 255);
            int b0 = qBound(0, (298 * C0 + 516 * D + 128) >> 8, 255);

            int r1 = qBound(0, (298 * C1 + 409 * E + 128) >> 8, 255);
            int g1 = qBound(0, (298 * C1 - 100 * D - 208 * E + 128) >> 8, 255);
            int b1 = qBound(0, (298 * C1 + 516 * D + 128) >> 8, 255);

            rgb.setPixel(x, y, qRgb(r0, g0, b0));
            if (x + 1 < width)
                rgb.setPixel(x + 1, y, qRgb(r1, g1, b1));
        }
    }
    return rgb;
}

#endif // _WIN32

#ifdef HAS_QT_MULTIMEDIA

// ==================== Linux/麒麟 Qt Multimedia 实现 ====================

bool CameraCapture::hasCamera() {
    QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    return !cameras.isEmpty();
}

QStringList CameraCapture::availableCameras() {
    QStringList result;
    QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    for (const QCameraDevice &camera : cameras) {
        result.append(camera.description());
    }
    return result;
}

bool CameraCapture::start(int width, int height, int fps) {
    if (m_running) return true;
    
    if (!hasCamera()) {
        emit cameraError(QStringLiteral("未检测到摄像头设备"));
        return false;
    }
    
    return startQtMultimediaCapture(width, height, fps);
}

void CameraCapture::stop() {
    if (!m_running) return;
    stopQtMultimediaCapture();
    m_running = false;
    m_timer->stop();
}

bool CameraCapture::startQtMultimediaCapture(int width, int height, int fps) {
    // 获取默认摄像头
    QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    if (cameras.isEmpty()) {
        emit cameraError(QStringLiteral("未检测到摄像头"));
        return false;
    }
    
    m_camera = new QCamera(cameras.first(), this);
    m_captureSession = new QMediaCaptureSession(this);
    m_captureSession->setCamera(m_camera);
    
    // 连接视频帧
    connect(m_camera, &QCamera::errorOccurred, this, [this](QCamera::Error error, const QString &errorString) {
        emit cameraError(errorString);
    });
    
    // 使用QVideoSink获取帧
    QVideoSink *videoSink = m_captureSession->videoSink();
    if (videoSink) {
        connect(videoSink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
            QMutexLocker lock(&m_mutex);
            m_currentFrame = frame.toImage();
            m_frameReady = true;
        });
    }
    
    m_camera->start();
    m_running = true;
    m_timer->start(1000 / fps); // 设置定时器间隔
    
    qDebug() << "[CameraCapture] Qt Multimedia camera started" << width << "x" << height << "@" << fps << "fps";
    return true;
}

void CameraCapture::stopQtMultimediaCapture() {
    if (m_camera) {
        m_camera->stop();
        delete m_camera;
        m_camera = nullptr;
    }
    if (m_captureSession) {
        delete m_captureSession;
        m_captureSession = nullptr;
    }
    m_frameReady = false;
}

QImage CameraCapture::grabQtMultimediaFrame() {
    QMutexLocker lock(&m_mutex);
    if (m_frameReady && !m_currentFrame.isNull()) {
        m_frameReady = false;
        return m_currentFrame;
    }
    return QImage();
}

void CameraCapture::onCaptureTimer() {
#ifdef _WIN32
    if (m_running) {
        QImage frame = grabMFFrame();
        if (!frame.isNull()) {
            emit frameReady(frame);
        }
    }
#elif defined(HAS_QT_MULTIMEDIA)
    if (m_running && m_frameReady) {
        QImage frame = grabQtMultimediaFrame();
        if (!frame.isNull()) {
            emit frameReady(frame);
        }
    }
#endif
}

#endif // HAS_QT_MULTIMEDIA
