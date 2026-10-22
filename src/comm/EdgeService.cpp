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
    m_sim = cfg.simulate;
    m_online = false;
    m_activeClient = 0;
    m_lastPeriodicMs = 0;
    m_pending.clear();
    m_lastCamStatus.clear();   // 摄像头状态基线重建：首帧只建基线，不把旧连接的状态当变化上报

    if (!m_sim) {
        m_link = new EdgeLink(this);
        connect(m_link, &EdgeLink::clientConnected, this, &EdgeService::onClientConnected);
        connect(m_link, &EdgeLink::clientDisconnected, this, &EdgeService::onClientDisconnected);
        connect(m_link, &EdgeLink::frameReceived, this, &EdgeService::onFrameReceived);
        m_link->start(cfg);   // 失败仅边缘机功能不可用，服务继续跑
    } else {
        // 仿真模式：内置假边缘机，自动上线 + 周期帧 + 自动回应开箱/识别，无需真实硬件/模拟器
        m_activeClient = 1;   // 假活跃连接，使在线/请求前置校验通过
        connect(&m_simTimer, &QTimer::timeout, this, &EdgeService::onSimPeriodic);
        m_simTimer.start(3000);                       // 每3秒一帧 periodic
        QTimer::singleShot(300, this, [this] { onSimPeriodic(); });  // 首帧：模拟接入后先上线
        qInfo() << "[EdgeService] 仿真模式已开启（内置假边缘机，无需真实硬件/模拟器）";
    }

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
    m_simTimer.stop();
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
 * @brief 查询链路当前是否在线（周期帧驱动，供界面显示链路状态）
 * @return true=在线（最近15秒内收到过周期帧）；false=离线或从未连接
 */
bool EdgeService::isOnline() const
{
    return m_online;
}

/**
 * @brief 查询当前边缘机 8 路摄像头自检是否全部正常（开柜前置安全闸门）
 * @return true=全部正常或周期帧未上报自检（兼容未启用自检的硬件）；false=存在异常摄像头
 * @note  摄像头状态缺席（camera_status 为空数组）不拦开柜，与真实边缘机"自检可选"一致
 */
bool EdgeService::allCamerasOk() const
{
    for (const CameraStatusItem& cam : m_lastPeriodic.cameras) {
        if (cam.status != QStringLiteral("ok")) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 取异常摄像头摘要文本（供界面提示与开柜拒绝原因）
 * @return 形如 "CAM3(offline) CAM5(dirty)"；全部正常或自检缺席时返回空串
 */
QString EdgeService::cameraAbnormalText() const
{
    QStringList parts;
    for (const CameraStatusItem& cam : m_lastPeriodic.cameras) {
        if (cam.status != QStringLiteral("ok")) {
            parts.append(QStringLiteral("%1(%2)").arg(cam.cameraId, cam.status));
        }
    }
    return parts.join(QChar(' '));
}

/**
 * @brief 发起一次全柜识别请求（协议3.2/3.3）
 * @return 请求编号（非空=已发出，上层据此进入等待态）；空串=未发出（链路离线或写入失败）
 * @note  超时重发、恢复补发由内部双扫描器统一处理，本方法只负责生成编号、登记、发送；
 *        未发出时会发 recognizeFailed 信号，上层无需再判空
 */
QString EdgeService::requestRecognize()
{
    if (!m_online || m_activeClient == 0) {
        // 协议第5章⑤：离线禁止下发识别请求
        emit recognizeFailed(QString(), QStringLiteral("边缘计算机连接已断开，暂时无法核对柜内工具。\n请检查边缘计算机连接后重试。"));
        return QString();
    }

    // 请求编号：时间戳+进程内自增序号，重连后也不会与历史编号重复
    static int seq = 0;
    ++seq;
    const QString requestId = QStringLiteral("req-%1-%2")
        .arg(QDateTime::currentMSecsSinceEpoch()).arg(seq);

    QJsonObject frame;
    frame["type"] = QStringLiteral("recognize_req");
    frame["request_id"] = requestId;
    frame["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    Pending p;
    p.frame = frame;
    p.sentAtMs = nowMs();
    p.tries = 1;
    m_pending.insert(requestId, p);

    if (!sendToEdge(frame)) {
        // 写入失败：条目保留等待链路恢复补发（第5章③），本次通知上层未发出
        m_pending[requestId].failed = true;
        emit recognizeFailed(requestId, QStringLiteral("识别请求写入失败，已保留待链路恢复补发"));
        return QString();
    }
    return requestId;
}

/**
 * @brief 发起一次开箱（开锁）请求（开箱协议）
 * @return 请求编号（非空=已发出，上层据此进入开柜等待态）；空串=未发出（链路离线或写入失败）
 * @note  超时重发、恢复补发由内部双扫描器统一处理，本方法只负责生成编号、登记、发送；
 *        未发出时会发 openFailed 信号，上层无需再判空
 */
QString EdgeService::requestOpen()
{
    if (!m_online || m_activeClient == 0) {
        // 协议第5章⑤：离线禁止下发开箱请求
        emit openFailed(QString(), QStringLiteral("边缘计算机连接已断开，暂时无法开箱。\n请检查边缘计算机连接后重试。"));
        return QString();
    }

    // 安全闸门：周期帧上报的 8 路摄像头任一异常则禁止开柜（货位识别可信度下降，袁总安全策略）
    if (!allCamerasOk()) {
        emit openFailed(QString(), QStringLiteral("以下摄像头状态异常，禁止开柜：%1").arg(cameraAbnormalText()));
        return QString();
    }

    // 请求编号：时间戳+进程内自增序号（与 recognize 同策略，重连后不重复）
    static int seq = 0;
    ++seq;
    const QString requestId = QStringLiteral("open-%1-%2")
        .arg(QDateTime::currentMSecsSinceEpoch()).arg(seq);

    QJsonObject frame;
    frame["type"] = QStringLiteral("open_req");
    frame["request_id"] = requestId;
    frame["timestamp"] = QDateTime::currentMSecsSinceEpoch();

    Pending p;
    p.frame = frame;
    p.sentAtMs = nowMs();
    p.tries = 1;
    m_pending.insert(requestId, p);

    if (!sendToEdge(frame)) {
        // 写入失败：条目保留等待链路恢复补发（第5章③），本次通知上层未发出
        m_pending[requestId].failed = true;
        emit openFailed(requestId, QStringLiteral("开箱请求写入失败，已保留待链路恢复补发"));
        return QString();
    }
    return requestId;
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
        // 摄像头自检状态变化检测（协议3.4）：相对上一帧有变化且新状态异常（非 ok）才上报，
        // 由上层落告警。恢复正常（→ok）不发送——告警只记异常出现，不记恢复，避免刷屏。
        // 首帧（基线里没有该相机）只建基线不发送：服务重启不应把重启前的状态当"变化"刷告警；
        // 相机从清单缺席不处理（自检可缺席属硬件选型，保持旧基线，回来时若不同仍会发现）
        for (const CameraStatusItem& cam : m_lastPeriodic.cameras) {
            const QString prev = m_lastCamStatus.value(cam.cameraId);
            if (m_lastCamStatus.contains(cam.cameraId) && prev != cam.status
                && cam.status != QStringLiteral("ok")) {
                emit cameraStatusChanged(cam.cameraId, prev, cam.status);
            }
            m_lastCamStatus[cam.cameraId] = cam.status;
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

    if (type == QStringLiteral("open_ack")) {
        const QString id = frame["request_id"].toString();
        if (!m_pending.contains(id)) {
            // 3.6-②/③：重复回应（第一次已移除）或认不出的id → 丢弃并计数
            ++m_dropCount;
            qWarning() << "[EdgeService] 丢弃帧 #" << m_dropCount << ":" << id;
            return;
        }
        m_pending.remove(id);          // 3.6-③：只认第一次
        OpenResult result;
        result.requestId = id;
        result.success = (frame["result"].toInt(0) == 1);
        result.message = frame["message"].toString();
        emit openFinished(id, result);
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
        const bool isOpen = (p.frame["type"].toString() == QStringLiteral("open_req"));
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
                if (isOpen)
                    emit openFailed(it.key(), QStringLiteral("开箱请求超时且重发次数用尽，已保留待链路恢复"));
                else
                    emit recognizeFailed(it.key(), QStringLiteral("识别请求超时且重发次数用尽，已保留待链路恢复"));
                ++it;
            }
        } else if (p.requeued) {
            // 第5章④闭环：恢复补发一次后仍无回应 → 放弃，转人工处理
            const QString id = it.key();
            it = m_pending.erase(it);
            if (isOpen)
                emit openFailed(id, QStringLiteral("链路恢复补发后仍无回应，本次业务转人工处理"));
            else
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

/**
 * @brief 仿真模式周期帧：内置假边缘机持续发送 periodic，维持在线态
 * @note  首帧由 start() 的 singleShot 触发（模拟接入后先上线），之后每3秒一帧
 */
void EdgeService::onSimPeriodic()
{
    QJsonObject frame;
    frame["type"] = QStringLiteral("periodic");
    frame["uptime_sec"] = (QDateTime::currentMSecsSinceEpoch() / 1000) % 100000;
    // 内置假边缘机：8 路摄像头自检全正常（与真实边缘机 8 路一致，使开柜闸门常态生效）
    QJsonArray cams;
    for (int i = 1; i <= 8; ++i) {
        QJsonObject c;
        c["camera_id"] = QStringLiteral("CAM%1").arg(i);
        c["status"] = QStringLiteral("ok");
        cams.append(c);
    }
    frame["camera_status"] = cams;
    onFrameReceived(0, frame);
}

// ─────────────────────── 私有辅助 ───────────────────────

/**
 * @brief 向当前边缘机连接发送一帧
 * @param frame 待发送的JSON帧
 * @return true=已写入socket；false=无连接或写入失败（扫描器会兜底重发）
 */
bool EdgeService::sendToEdge(const QJsonObject& frame)
{
    if (m_sim) {
        // 仿真模式：内置假边缘机，延时回应开箱/识别请求
        const QString type = frame["type"].toString();
        const QString requestId = frame["request_id"].toString();
        if (type == QStringLiteral("open_req")) {
            QTimer::singleShot(200, this, [this, requestId] {
                QJsonObject ack;
                ack["type"] = QStringLiteral("open_ack");
                ack["request_id"] = requestId;
                ack["result"] = 1;                 // 仿真：开锁成功
                ack["message"] = QString();
                onFrameReceived(0, ack);
            });
        } else if (type == QStringLiteral("recognize_req")) {
            QTimer::singleShot(200, this, [this, requestId] {
                QJsonObject ack;
                ack["type"] = QStringLiteral("recognize_ack");
                ack["request_id"] = requestId;
                ack["items"] = QJsonArray();        // 仿真：无变化（空柜）
                onFrameReceived(0, ack);
            });
        }
        return true;
    }

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

