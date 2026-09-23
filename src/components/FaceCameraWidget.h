#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0  人脸摄像头组件
// 日期：2026-06-21  功能：摄像头预览 + 人脸检测 + 特征提取
// [2026-06-21] 重写：纯C++肤色人脸检测 + 纹理特征提取(128维)
//   替代之前的随机特征向量，实现真正的刷脸登录
//   Web端1:1复刻：人脸框/特征点/状态提示/指示灯/边框动画
#include <QWidget>
#include <QTimer>
#include <QImage>
#include <QPixmap>
#include <QString>
#include <QVector>
#include <QRect>
#include <QElapsedTimer>

// 前向声明
class CameraCapture;
class QLabel;
class QPropertyAnimation;

class FaceCameraWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int borderGlow READ borderGlow WRITE setBorderGlow)
public:
    explicit FaceCameraWidget(QWidget* parent = nullptr);
    ~FaceCameraWidget();

    /// 启动摄像头采集+人脸检测
    void startCamera();
    /// 停止摄像头和人脸检测
    void stopCamera();
    /// 是否正在运行
    bool isActive() const;
    /// 提取人脸特征（返回逗号分隔的128维浮点字符串）
    QString captureFaceFeature();

    // ── 版本B兼容API ──
    void setAutoCapture(bool enable);
    void setMinConfidence(double val);
    void setStableFrames(int frames);
    void setDetectInterval(int ms);
    void setCaptureDelay(int ms);
    /// 捕获当前帧并发射captureReady(QImage, confidence)信号
    void captureNow();
    /// 返回当前人脸特征描述符（逗号分隔128维浮点数）
    QString getLastDescriptor() const;
    /// 返回当前人脸检测边界框（相对于220x220显示区域）
    QRect faceRect() const;
    /// [V2.16] 获取当前摄像头帧（用于方位检测）
    QImage currentFrame() const { return m_lastFrame; }
    void reset();
    /// 检查摄像头硬件是否可用
    bool hasCamera() const;
    /// [V2.17fix-0706] 本地方位估算（不依赖HTTP，0延迟）
    void estimatePosture(const QRect& faceRect, double& yaw, double& pitch);

    // [2026-06-21] 边框发光动画属性(用于CSS pulse效果)
    int borderGlow() const { return m_borderGlow; }
    void setBorderGlow(int v) { m_borderGlow = v; emit borderGlowChanged(); }

signals:
    // 版本A信号
    void faceDetected(const QString& feature);
    void cameraError(const QString& error);
    // 版本B信号
    void faceLost();
    void captureReady(QImage image, double confidence);
    void stateChanged(int state);   // 0=off 1=scanning 2=detected 3=capturing 4=success 5=error
    void errorOccurred(QString message);
    // [2026-06-21] 新增：人脸检测状态变化
    void faceDetectionChanged(bool detected, double confidence);
    void borderGlowChanged();

private slots:
    /// 摄像头新帧回调
    void onNewFrame(const QImage& frame);
    /// 人脸检测定时器
    void onDetectTick();

private:
    void setupUI();
    void updateOverlay(const QRect& faceRect, double confidence);

    // ── 人脸检测（肤色YCrCb）──
    /// 在图像中检测人脸区域，返回边界框（空rect表示未检测到）
    QRect detectFace(const QImage& rgbImage);

    // ── 特征提取（深度学习）──
    /// 从人脸区域提取128维特征向量（逗号分隔字符串）
    QString extractFeature(const QImage& frame, const QRect& faceRect);
    /// L2归一化
    static void normalizeL2(QVector<double>& v);

    // ── 自动采集逻辑 ──
    void handleFaceDetected(double confidence);
    void handleFaceLost();
    void tryAutoCapture();

    // ── UI组件 ──
    QLabel* m_videoLabel = nullptr;         // 摄像头圆形预览
    QLabel* m_faceOverlay = nullptr;        // 人脸框+特征点叠加层
    QLabel* m_statusHint = nullptr;         // 状态提示文字（"已检测到人脸"等）
    QLabel* m_statusDot = nullptr;          // 状态指示灯
    QLabel* m_displayLabel = nullptr;       // 无摄像头fallback
    QTimer* m_detectTimer = nullptr;        // 人脸检测定时器
    CameraCapture* m_cameraCapture = nullptr;
    QPropertyAnimation* m_glowAnim = nullptr; // 边框脉冲动画

    // ── 状态变量 ──
    QImage m_lastFrame;                     // 最新帧（原始640x480）
    QRect m_lastFaceRect;                   // 最后一次检测到的人脸区域
    QVector<double> m_lastDescriptor;       // 上次提取的特征向量
    bool m_active = false;
    bool m_cameraAvailable = false;
    bool m_faceDetected = false;
    bool m_capturing = false;
    int m_stableCount = 0;
    int m_borderGlow = 0;

    // ── 配置参数（版本B兼容）──
    bool m_autoCapture = false;
    double m_minConfidence = 0.70;          // [V2.17fix-0706] 0.60→0.70 降低陌生人误识
    int m_stableFrames = 3;                 // [V2.17fix-0706] 6→3 更快确认
    int m_captureDelay = 100;               // [V2.17fix-0706] 150→100ms 更快采集
    int m_detectIntervalMs = 40;            // [V2.17fix-0706] 60→40ms 更快检测响应
    int m_detectSkipCounter = 0;            // 跳帧计数（实际检测约每60ms一次）
};
