/**
 * @file  EdgeService.h
 * @brief 与边缘计算机通信接口的业务层：上层访问边缘机的唯一入口
 * @author 袁燕
 *
 * 职责边界：请求管理（request_id 生成/配对/去重/超时重发/恢复补发）
 *           + 链路状态（在线判定/周期帧缓存/版本缓存）。
 * 不做：识别结果与台账的比对（那是 SessionController 的事）、任何界面逻辑。
 *
 * 线程模型：全部主线程，信号槽驱动（协议流量为 3 秒一条周期帧量级）。
 * 通信对象：唯一持有 EdgeLink；上层不许绕过本类直接碰 EdgeLink。
 * 每个方法的完整注释（功能/输入/输出）见 EdgeService.cpp 对应实现处。
 */
#pragma once
#include <QObject>
#include <QTimer>
#include <QHash>
#include <QJsonObject>
#include "CommTypes.h"

class EdgeLink;

class EdgeService : public QObject {
    Q_OBJECT
public:
    explicit EdgeService(QObject* parent = nullptr);
    ~EdgeService() override;

    void start(const EdgeConfig& cfg);   ///< 启动服务（重复调用安全）
    void stop();                          ///< 停止服务

    bool isOnline() const;                ///< 查询链路当前是否在线（周期帧驱动，供界面显示链路状态）
    QString requestRecognize();           ///< 发起一次全柜识别请求，返回请求编号；空串=未发出（离线或写入失败）
    QString requestOpen();                ///< 发起一次开箱（开锁）请求，返回请求编号；空串=未发出（离线或写入失败）

    /**
     * @brief 查询当前边缘机 8 路摄像头自检是否全部正常（开柜前置安全闸门）
     * @return true=全部正常，或周期帧未上报自检（兼容未启用自检的硬件，不拦开柜）；
     *         false=存在任一状态非 "ok" 的摄像头（offline/low_light/dirty/comm_error）
     * @note  摄像头异常属安全红线：货位识别可信度下降，禁止开锁（袁总安全策略）
     */
    bool allCamerasOk() const;

    /**
     * @brief 取异常摄像头摘要文本（供界面提示与开柜拒绝原因）
     * @return 形如 "CAM3(offline) CAM5(dirty)"；全部正常或自检缺席时返回空串
     */
    QString cameraAbnormalText() const;

signals:
    /**
     * @brief 链路在线状态变化（协议第6章）
     * @param online true=链路恢复（周期帧重新到达）；false=判离线（禁止开锁/识别）
     */
    void onlineChanged(bool online);

    /**
     * @brief 识别请求成功完成（协议3.3）
     * @param requestId 对应 requestRecognize 返回的编号
     * @param result    全柜识别清单（与台账比对由 SessionController 做）
     * @note  同一 request_id 收到多次回应只发一次（协议3.6-③）
     */
    void recognizeFinished(const QString& requestId, const RecognizeResult& result);

    /**
     * @brief 识别请求最终失败（协议第5章③④）
     * @param requestId 失败的请求编号；离线拒绝时为空串
     * @param reason    失败原因（人读，直接可上日志/告警页）
     * @note  记失败的请求仍保留在登记表，链路恢复自动补发一次；补发再失败才最终放弃
     */
    void recognizeFailed(const QString& requestId, const QString& reason);

    /**
     * @brief 开箱请求成功完成（开箱协议）
     * @param requestId 对应 requestOpen 返回的编号
     * @param result    开箱结果（success=true 表示柜体已真实打开）
     */
    void openFinished(const QString& requestId, const OpenResult& result);

    /**
     * @brief 开箱请求最终失败（超时/离线拒发/补发放弃）
     * @param requestId 失败的请求编号；离线拒绝时为空串
     * @param reason    失败原因（人读）
     */
    void openFailed(const QString& requestId, const QString& reason);

    /**
     * @brief 每收到一帧周期帧发一次（协议3.4）
     * @param info 最新周期帧内容（在线状态/摄像头自检）
     */
    void periodicUpdated(const PeriodicInfo& info);

    /**
     * @brief 收到边缘机版本回应（协议3.5）
     * @param version 边缘机软件版本号，验收交付留痕
     */
    void versionReceived(const QString& version);

private slots:
    void onFrameReceived(quintptr clientId, const QJsonObject& frame);  ///< 收到一帧合法JSON
    void onClientConnected(quintptr clientId);   ///< 边缘机接入
    void onClientDisconnected(quintptr clientId); ///< 边缘机断开
    void onScanTimeout();      ///< 请求超时扫描（200ms）
    void onOfflineScan();      ///< 链路离线扫描（1s）
    void onSimPeriodic();      ///< 仿真模式周期帧（内置假边缘机，模拟 periodic）

private:
    /**
     * @brief 在途请求登记表条目：协议第5章主线的全部状态都在这里
     */
    struct Pending {
        QJsonObject frame;        ///< 原始请求帧（重发/补发时原样重发，request_id 不变）
        qint64      sentAtMs = 0; ///< 最近一次发送时刻（毫秒），超时判定基准
        int         tries = 1;    ///< 已发送次数（含首次；1首发→2→3，3.6-①最多2次重发）
        bool        failed = false;   ///< 已记通信异常，条目保留等待链路恢复（第5章③）
        bool        requeued = false; ///< 恢复后已补发过一次；再超时则放弃（第5章④）
    };

    bool sendToEdge(const QJsonObject& frame);      ///< 向当前边缘机发一帧
    void requeueFailedRequests();                   ///< 链路恢复后补发失败请求（协议第5章④）
    EdgeLink*  m_link = nullptr;         ///< 链路层（本类唯一持有）
    EdgeConfig m_cfg;                    ///< 通信配置（start 注入）
    bool        m_online = false;        ///< 链路在线状态
    quintptr    m_activeClient = 0;      ///< 当前边缘机连接编号（0=无连接）
    PeriodicInfo m_lastPeriodic;         ///< 最近一帧周期帧缓存
    QString     m_version;               ///< 最近版本回应缓存
    QHash<QString, Pending> m_pending;   ///< 在途请求登记表，key=request_id
    QTimer      m_scanTimer;             ///< 请求超时扫描器（200ms）
    QTimer      m_offlineTimer;          ///< 链路离线扫描器（1s）
    QTimer      m_simTimer;              ///< 仿真模式周期帧定时器（仿真时启用）
    bool        m_sim = false;           ///< 仿真模式开关（内置假边缘机）
    qint64      m_lastPeriodicMs = 0;    ///< 最近一帧periodic到达时刻（离线判定基准）
    int         m_dropCount = 0;         ///< 异常帧丢弃计数（协议3.6-②）
};