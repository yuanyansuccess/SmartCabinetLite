/**
 * @file EdgeService.cpp
 * @brief 与边缘计算机通信接口的业务层实现：请求登记表 + 双扫描器 + 信号出口
 * @author 袁燕
 *
 * 协议依据：《智能工具柜配置项通信协议》
 *   3.2/3.3  recognize 请求/回应字段
 *   3.6-①    3秒未收到则重发，最多2次（共3次发送：1首发+2重发）
 *   3.6-②    request_id 认不出或 type 不对 → 丢弃并计数
 *   3.6-③    同一 request_id 多次收到 → 只认第一次（命中即移除登记表）
 *   3.6-④/6  周期帧连续15秒（连丢5条）判离线
 *   第5章③  重发次数用尽记通信异常，请求保留
 *   第5章④  链路恢复后按原 request_id 补发一次，再失败转人工
 *   第5章⑤  离线禁止下发识别请求
 *   第6章    链路判活（离线扫描器）与业务超时（请求扫描器）独立判定
 *
 * 说明：超时/补发逻辑只在本文件的扫描器中实现，其他类不重复实现。
 */
#include "EdgeService.h"
#include "EdgeLink.h"

#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>

namespace {
inline qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

/**
 * @brief 从 recognize_ack 的 items 数组解析识别结果清单（协议3.3字段表）
 * @param arr items 数组（JSON）
 * @return 识别结果清单；字段缺失时取默认值（空串/0），不抛异常
 */
QList<RecognizeItem> parseItems(const QJsonArray& arr)
{
    QList<RecognizeItem> out;
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        RecognizeItem it;
        it.position   = o["position"].toString();
        it.toolId     = o["tool_id"].toString();
        it.classId    = o["class_id"].toString();
        it.confidence = o["confidence"].toDouble();
        it.alertType  = o["alert_type"].toString(QStringLiteral("none"));
        it.alertLevel = o["alert_level"].toInt();
        out.append(it);
    }
    return out;
}

/**
 * @brief 从 periodic 帧解析边缘机健康状态（协议3.4）
 * @param frame periodic 帧对象
 * @return 周期帧内容；camera_status 自检依赖硬件选型，字段可缺席（为空）
 */
PeriodicInfo parsePeriodic(const QJsonObject& frame)
{
    PeriodicInfo info;
    info.uptimeSec = static_cast<qint64>(frame["uptime_sec"].toDouble());
    const QJsonArray cams = frame["camera_status"].toArray();
    for (const QJsonValue& v : cams) {
        const QJsonObject o = v.toObject();
        CameraStatusItem c;
        c.cameraId = o["camera_id"].toString();
        c.status   = o["status"].toString();
        info.cameras.append(c);
    }
    return info;
}
}  // namespace

EdgeService::EdgeService(QObject* parent)
    : QObject(parent)
{
    // 双扫描器（第6章：链路判活与业务超时独立判定）
    connect(&m_scanTimer, &QTimer::timeout, this, &EdgeService::onScanTimeout);
    connect(&m_offlineTimer, &QTimer::timeout, this, &EdgeService::onOfflineScan);
}

EdgeService::~EdgeService()
{
    stop();
}

/**
 * @brief 启动服务：建链路层、连信号、启动双扫描器
 * @param cfg 通信配置，构造注入（本类不读文件，测试时可塞内存配置）
 * @return 无。监听失败不致命（边缘机功能置灰，其余功能正常），原因看日志
 * @note  重复调用安全：会先自动停止上一次的服务再重新启动，不会出现两套监听
 */
void EdgeService::start(const EdgeConfig& cfg)
{
    stop();  // 重复调用安全：先停干净上一次的资源再启动

    m_cfg = cfg;
    m_online = false;
    m_activeClient = 0;
    m_lastPeriodicMs = 0;
    m_pending.clear();

    m_link = new EdgeLink(this);
    connect(m_link, &EdgeLink::clientConnected, this, &EdgeService::onClientConnected);
    connect(m_link, &EdgeLink::clientDisconnected, this, &EdgeService::onClientDisconnected);
    connect(m_link, &EdgeLink::frameReceived, this, &EdgeService::onFrameReceived);
    m_link->start(cfg);   // 失败仅边缘机功能不可用，服务继续跑

    m_scanTimer.start(200);      // 请求超时/重发扫描
    m_offlineTimer.start(1000);  // 链路离线扫描（周期3秒×5条=15秒口径）
}

/**
 * @brief 停止服务：停扫描器、断链路、清登记表
 * @return 无。停止时若处于在线状态，先通知界面收敛（否则订阅方状态悬空）
 */
void EdgeService::stop()
{
    // 在线状态下先通知界面收敛（否则订阅方状态悬空）
    if (m_online) {
        m_online = false;
        emit onlineChanged(false);
    }
    m_scanTimer.stop();
    m_offlineTimer.stop();
    if (m_link) {
        m_link->deleteLater();
        m_link = nullptr;
    }
    m_pending.clear();
    m_activeClient = 0;
}



/**
 * @brief 发起边缘机软件版本查询（验收留痕/运维，协议3.5）
 * @return 请求编号，含义同 requestRecognize；离线时返回空串且不发送（日志留痕）
 * @note  异步：结果通过 versionReceived 信号返回
 */

/**
 * @brief 取当前边缘机的IP地址（界面显示/日志定位用）
 * @return 形如 "127.0.0.1" 的地址串；无连接时为空串
 */

/**
 * @brief 边缘机TCP接入（含重连，每次接入都是新编号）
 * @param clientId 新连接编号，成为当前活跃边缘机
 * @note  在线判定只认周期帧，连接本身不触发上线（协议第6章）
 */
void EdgeService::onClientConnected(quintptr clientId)
{
    m_activeClient = clientId;
}

/**
 * @brief 边缘机连接断开
 * @param clientId 断开的连接编号；非当前活跃连接则忽略
 * @note  TCP已断，15秒口径必然超——立即判离线（等价且更快，协议第6章）
 */
void EdgeService::onClientDisconnected(quintptr clientId)
{
    if (clientId != m_activeClient) {
        return;
    }
    m_activeClient = 0;
    if (m_online) {
        m_online = false;
        emit onlineChanged(false);
    }
}

/**
 * @brief 收到边缘机一帧合法JSON（EdgeLink 转发）
 * @param clientId 来源连接编号
 * @param frame    已解析的JSON对象，type 为 periodic/recognize_ack/version_ack
 * @note  按消息类型分发；认不出的类型丢弃并计数（协议3.6-②）
 */
void EdgeService::onFrameReceived(quintptr clientId, const QJsonObject& frame)
{
    Q_UNUSED(clientId);
    const QString type = frame["type"].toString();

    if (type == QStringLiteral("periodic")) {
        // 协议第6章：周期帧管"链路通不通"
        m_lastPeriodic = parsePeriodic(frame);
        m_lastPeriodicMs = nowMs();
        if (!m_online) {
            m_online = true;
            emit onlineChanged(true);
            requeueFailedRequests();   // 第5章④：链路恢复（周期帧重新到达）→ 补发
        }
        emit periodicUpdated(m_lastPeriodic);
        return;
    }

    if (type == QStringLiteral("recognize_ack")) {
        const QString id = frame["request_id"].toString();
        if (!m_pending.contains(id)) {
            // 3.6-②/③：重复回应（第一次已移除）或认不出的id → 丢弃并计数
            ++m_dropCount;
            qWarning() << "[EdgeService] 丢弃帧 #" << m_dropCount << ":" << id;
            return;
        }
        m_pending.remove(id);          // 3.6-③：只认第一次
        RecognizeResult result;
        result.requestId = id;
        result.items = parseItems(frame["items"].toArray());
        emit recognizeFinished(id, result);
        return;
    }

    if (type == QStringLiteral("version_ack")) {
        // 协议3.5：version_ack 不带 request_id，单边缘机按"唯一在途请求"配对
        m_version = frame["software_version"].toString();
        for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
            if (it->frame["type"].toString() == QStringLiteral("version_query")) {
                m_pending.erase(it);
                break;
            }
        }
        emit versionReceived(m_version);
        return;
    }

    // 3.6-②：type 不对 → 丢弃并计数
    ++m_dropCount;
    qWarning() << "[EdgeService] 丢弃帧 #" << m_dropCount << "未知type:" << type;
}

/**
 * @brief 请求超时扫描（200ms周期）：到3秒的重发，用尽的记失败
 * @note  首发后最多重发2次（3.6-①）；用尽→记异常并保留（第5章③）；
 *        补发后再超时→移除并最终失败（第5章④）
 */
void EdgeService::onScanTimeout()
{
    const qint64 nowAt = nowMs();
    auto it = m_pending.begin();
    while (it != m_pending.end()) {
        Pending& p = it.value();
        if (nowAt - p.sentAtMs < m_cfg.requestTimeoutMs) {
            ++it;
            continue;
        }
        if (!p.failed && !p.requeued) {
            if (p.tries <= m_cfg.maxRetry) {
                // 第5章②：重发用原 request_id（原帧原样重发）
                ++p.tries;
                p.sentAtMs = nowAt;
                sendToEdge(p.frame);
                qWarning() << "[EdgeService] 重发请求" << it.key() << "第" << (p.tries - 1) << "次重发";
                ++it;
            } else {
                // 第5章③：记通信异常，请求保留等待链路恢复
                p.failed = true;
                emit recognizeFailed(it.key(), QStringLiteral("识别请求超时且重发次数用尽，已保留待链路恢复"));
                ++it;
            }
        } else if (p.requeued) {
            // 第5章④闭环：恢复补发一次后仍无回应 → 放弃，转人工处理
            const QString id = it.key();
            it = m_pending.erase(it);
            emit recognizeFailed(id, QStringLiteral("链路恢复补发后仍无回应，本次业务转人工处理"));
        } else {
            // failed && !requeued：等待链路恢复，不动
            ++it;
        }
    }
}

/**
 * @brief 链路离线扫描（1秒周期）：周期帧距上帧超15秒判离线
 * @note  与请求扫描器独立判定（第6章）
 */
void EdgeService::onOfflineScan()
{
    // 3.6-④/第6章：周期帧连续15秒（连丢5条）收不到 → 判离线
    if (m_online && m_lastPeriodicMs > 0
        && nowMs() - m_lastPeriodicMs > m_cfg.offlineTimeoutMs) {
        m_online = false;
        emit onlineChanged(false);
    }
}

// ─────────────────────── 私有辅助 ───────────────────────

/**
 * @brief 向当前边缘机连接发送一帧
 * @param frame 待发送的JSON帧
 * @return true=已写入socket；false=无连接或写入失败（扫描器会兜底重发）
 */
bool EdgeService::sendToEdge(const QJsonObject& frame)
{
    if (m_activeClient == 0) {
        qWarning() << "[EdgeService] 发送失败：无边缘机连接";
        return false;
    }
    const bool ok = m_link->sendFrame(m_activeClient, frame);
    if (!ok) {
        qWarning() << "[EdgeService] 发送失败：连接已断开";
    }
    return ok;
}

/**
 * @brief 链路恢复时补发所有"已记失败且未补发"的请求（协议第5章④）
 * @note  补发条目置 requeued=true，再超时即最终放弃
 */
void EdgeService::requeueFailedRequests()
{
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        Pending& p = it.value();
        if (p.failed && !p.requeued) {
            p.requeued = true;
            p.failed = false;
            p.sentAtMs = nowMs();
            sendToEdge(p.frame);
            qWarning() << "[EdgeService] 链路恢复，补发请求" << it.key();
        }
    }
}

