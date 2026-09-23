/**
 * @file CameraCapture.h
 * @brief 摄像头采集抽象层 - Windows用MF，Linux用QtMultimedia
 * @author 袁燕  修改: 2026-06-20 添加Linux/麒麟Qt Multimedia支持
 *
 * 设计原则：零外部依赖，Windows用原生Media Foundation，Linux用Qt Multimedia
 * 接口简洁：start/stop/getFrame，输出QImage供FaceCameraWidget消费
 */
#pragma once
#include <QObject>
#include <QImage>
#include <QTimer>
#include <QMutex>
#include <QStringList>

#ifdef _WIN32
#include <windows.h>
#include <initguid.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "uuid.lib")
#endif

#ifdef HAS_QT_MULTIMEDIA
#include <QCamera>
#include <QVideoFrame>
#include <QMediaCaptureSession>
#include <QImageCapture>
#endif

class CameraCapture : public QObject {
    Q_OBJECT
public:
    explicit CameraCapture(QObject* parent = nullptr);
    ~CameraCapture();

    /// 获取可用摄像头列表
    static QStringList availableCameras();

    /// 开始采集 (使用默认摄像头)
    bool start(int width = 640, int height = 480, int fps = 30);
    /// 停止采集
    void stop();
    /// 是否正在采集
    bool isRunning() const;
    /// 是否有真实摄像头可用
    static bool hasCamera();

signals:
    void frameReady(const QImage& frame);
    void cameraError(const QString& message);

private slots:
    void onCaptureTimer();
#ifdef HAS_QT_MULTIMEDIA
    void onFrameCaptured(const QVideoFrame& frame);
#endif

private:
#ifdef _WIN32
    bool initMF();
    bool startMFCapture(int width, int height, int fps);
    void stopMFCapture();
    QImage grabMFFrame();
    /// NV12 → RGB24 转换
    static QImage nv12ToRgb(const unsigned char* data, int width, int height);
    /// YUY2 → RGB24 转换
    static QImage yuy2ToRgb(const unsigned char* data, int width, int height);

    IMFMediaSource* m_mediaSource = nullptr;
    IMFSourceReader* m_sourceReader = nullptr;
    GUID m_videoSubtype = {0};  // 实际协商的视频子类型
    int m_capWidth = 640;
    int m_capHeight = 480;
#endif

#ifdef HAS_QT_MULTIMEDIA
    bool startQtMultimediaCapture(int width, int height, int fps);
    void stopQtMultimediaCapture();
    QImage grabQtMultimediaFrame();
    
    QCamera* m_camera = nullptr;
    QMediaCaptureSession* m_captureSession = nullptr;
    QImageCapture* m_imageCapture = nullptr;
    QVideoFrame m_lastFrame;
    bool m_frameReady = false;
#endif

    QTimer* m_timer = nullptr;
    QMutex m_mutex;
    bool m_running = false;
    QImage m_currentFrame;
};
