/**
 * @file DeepFaceExtractor.h
 * @brief 深度学习人脸特征提取器 — 通过HTTP调用常驻face-server.js服务
 * @author 袁燕
 *
 * 性能优化：从"每次启动node进程"改为"常驻HTTP服务"
 *   原方案：每次captureNow()启动node进程加载模型 → 2-3s/次 × 3帧 = 9s
 *   新方案：Qt启动时拉起face-server.js常驻 → 模型只加载一次 → 每次请求<300ms
 *   提速：9s → 1s（约9倍）
 *
 * 新增detectPosture方法：只检测方位不提取特征，用于录入页方位引导
 *
 * 工作流程：
 *   1. ensureServerRunning() 检查face-server.js是否在运行，未运行则启动
 *   2. QImage → base64 JPEG → POST http://127.0.0.1:8089/extract 或 /posture
 *   3. 解析JSON响应 → 返回特征/方位数据
 */
#pragma once
#include <QObject>
#include <QString>
#include <QImage>
#include <QVector>
#include <QPointF>
#include <QtNetwork>

class DeepFaceExtractor : public QObject {
    Q_OBJECT
public:
    explicit DeepFaceExtractor(QObject* parent = nullptr);

    /**
     * @brief 从QImage提取128维深度学习人脸特征
     * @param image  摄像头采集的人脸图片
     * @param outFeature  输出：逗号分隔的128维特征字符串
     * @param outConfidence  输出：人脸检测置信度(0-1)
     * @param outMessage  输出：错误消息（成功时为空）
     * @return true 提取成功
     *
     * 通过HTTP调用常驻face-server.js，同步等待响应
     * 超时5秒（服务已预热，正常<300ms）
     */
    bool extract(const QImage& image,
                 QString& outFeature,
                 double& outConfidence,
                 QString& outMessage);

    /**
     * @brief 人脸方位检测（轻量，只检测landmarks不提取特征）
     * @param image  摄像头当前帧
     * @param outYaw  输出：左右偏转比（>0.15左偏，<-0.15右偏，|yaw|<0.10居中）
     * @param outPitch 输出：上下偏转比（>0.15低头，<-0.15抬头，|pitch|<0.10居中）
     * @param outMessage 输出：错误消息（成功时为空）
     * @return true 检测成功
     *
     * 调用face-server.js的/posture接口，比/extract快（不提取128维特征）
     * 用于人脸录入页实时方位引导
     */
    bool detectPosture(const QImage& image,
                       double& outYaw,
                       double& outPitch,
                       QString& outMessage);

    /**
     * @brief 检查深度学习环境是否可用
     * @return true Node.js和模型文件都存在
     */
    static bool isAvailable();

    /**
     * @brief 确保face-server.js常驻服务在运行
     * @return true 服务已就绪可接收请求
     */
    static bool ensureServerRunning();

    /**
     * @brief 主程序启动时预拉起人脸识别服务（异步，不阻塞主界面）
     *
     * 全程在主线程完成：启动face-server.js子进程后由定时器轮询就绪状态，
     * 不阻塞程序启动；服务进程由应用对象托管，主程序退出时自动终止。
     */
    static void prestartAsync();

    /**
     * @brief 停止本程序拉起的人脸识别服务（主程序退出时调用）
     *
     * 仅终止由prestartAsync拉起的进程；外部手动启动的服务不受影响。
     */
    static void shutdownServer();

    /**
     * @brief QImage转base64 JPEG（extract和detectPosture共用）
     *        改为public，供UserManagementPage异步调用
     */
    static QByteArray imageToBase64Jpeg(const QImage& image);

private:
    /// 查找node可执行文件路径
    static QString findNodePath();

    /// 获取脚本目录绝对路径
    static QString scriptDir();

    /// 检查服务健康状态
    static bool checkServerHealth();

    /// 由本程序拉起服务子进程（主线程专用，进程指针由qApp托管）
    static bool startServerProcess();

    /// 异步等待服务就绪（定时器轮询，不阻塞主线程）
    static void waitReadyAsync(int triedTimes);

    /// HTTP同步请求（阻塞等待响应）
    QString httpPostSync(const QString& url, const QByteArray& body, int timeoutMs);

    /// 超时时间（毫秒）— 30s→5s，常驻服务已预热
    static constexpr int TIMEOUT_MS = 5000;

    /// 方位检测超时（毫秒）— 真人脸推理需1~2s，预留3s保证不误判超时
    /// ⚠ 不可动：实测真人脸 /posture RTT≈590ms，调小会导致全部请求超时→退简易模式
    static constexpr int POSTURE_TIMEOUT_MS = 3000;

    /// face-server.js服务端口
    static constexpr int SERVER_PORT = 8089;
};
