/**
 * @file EdgeLink.cpp
 * @brief 与边缘计算机通信接口的链路层实现：TCP监听、换行分帧、JSON收发
 * @author 袁燕
 *
 * 协议依据：《智能工具柜配置项通信协议》第1章（角色/端口）、第2章（一行JSON分帧）、3.6-②（坏帧丢弃计数）
 * 日志说明：本层使用 Qt 自带 qDebug/qWarning，任何工程单独抽出本文件即可复用。
 */
#include "EdgeLink.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QHostAddress>
#include <QDateTime>

EdgeLink::EdgeLink(QObject* parent)
    : QObject(parent)
{
}

EdgeLink::~EdgeLink()
{
    stop();
}

/**
 * @brief 开始监听端口，等待边缘机接入
 * @param cfg 通信配置，取其中 port（监听端口）与 enabled（总开关）
 * @return true=监听成功；false=监听失败或 enabled=false（此时边缘机功能不可用，程序其余部分正常）
 * @note  重复调用安全：先停掉上一次的监听、释放旧连接，再重新启动
 */
bool EdgeLink::start(const EdgeConfig& cfg)
{
    stop();  // 重复调用安全：先停掉上一次的监听、释放旧连接，再重新启动

    m_cfg = cfg;
    if (!cfg.enabled) {
        qWarning() << "[EdgeLink] enabled=false, not listening";
        return false;
    }

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &EdgeLink::onNewConnection);

    if (!m_server->listen(QHostAddress::Any, static_cast<quint16>(cfg.port))) {
        qWarning() << "[EdgeLink] listen failed on port" << cfg.port
                   << ":" << m_server->errorString();
        m_server->deleteLater();
        m_server = nullptr;
        return false;  // 失败不影响主程序运行，边缘机功能置灰
    }

    qDebug() << "[EdgeLink] listening on port" << cfg.port;
    return true;
}

/**
 * @brief 停止监听并断开所有已接入的边缘机连接
 * @return 无
 */
void EdgeLink::stop()
{
    if (!m_server) {
        return;
    }
    m_server->close();

    // 先断信号再销毁，避免 abort 触发 disconnected 槽二次清理
    const auto sockets = m_clients.values();
    for (QTcpSocket* sock : sockets) {
        disconnect(sock, nullptr, this, nullptr);
        sock->abort();
        sock->deleteLater();
    }
    m_clients.clear();

    m_server->deleteLater();
    m_server = nullptr;

    // 队列里的帧已无接收方，一并丢弃（避免停止后仍上抛过期帧）
    m_frameQueue.clear();
    m_drainScheduled = false;
}

/**
 * @brief 向指定连接发送一帧（紧凑JSON + 换行）
 * @param clientId 目标连接编号（来自 clientConnected 信号）
 * @param frame    待发送的JSON对象
 * @return true=完整写入socket；false=连接不存在/已断开/写入不完整
 */
bool EdgeLink::sendFrame(quintptr clientId, const QJsonObject& frame)
{
    QTcpSocket* sock = m_clients.value(clientId, nullptr);
    if (!sock || sock->state() != QAbstractSocket::ConnectedState) {
        return false;
    }
    // 紧凑序列化是换行分帧成立的前提（协议第2章）：JSON体内换行必被转义
    const QByteArray data = QJsonDocument(frame).toJson(QJsonDocument::Compact) + '\n';
    return sock->write(data) == data.size();
}

/**
 * @brief 查询当前是否正在监听
 * @return true=监听中
 */
bool EdgeLink::isListening() const
{
    return m_server && m_server->isListening();
}

/**
 * @brief 查询最近收到任意一帧的时刻
 * @return 毫秒时间戳；0 表示本连接尚未收到过任何帧
 * @note  该时刻在收帧那一刻刷新，与帧的业务处理进度无关：
 *        链路是否真的断开通，看的是"有没有数据进来"，不是"业务处理到哪了"
 */
qint64 EdgeLink::lastFrameMs() const
{
    return m_lastFrameMs;
}

/**
 * @brief 查询指定连接的对端IP
 * @param clientId 连接编号（clientConnected 信号携带）
 * @return 形如 "127.0.0.1" 的地址串；连接不存在时为空串
 */
QString EdgeLink::peerAddress(quintptr clientId) const
{
    QTcpSocket* sock = m_clients.value(clientId, nullptr);
    return sock ? sock->peerAddress().toString() : QString();
}

/**
 * @brief 新连接到达：登记连接、连接信号、发 clientConnected
 * @note  一次可能到达多个待接入连接（握手队列），用 while 全收
 */
void EdgeLink::onNewConnection()
{
    while (QTcpSocket* sock = m_server->nextPendingConnection()) {
        const quintptr id = reinterpret_cast<quintptr>(sock);
        m_clients.insert(id, sock);
        connect(sock, &QTcpSocket::readyRead, this, &EdgeLink::onReadyRead);
        connect(sock, &QTcpSocket::disconnected, this, &EdgeLink::onDisconnected);
        qDebug() << "[EdgeLink] client connected"
                 << sock->peerAddress().toString() << "id=" << id;
        emit clientConnected(id);
    }
}

/**
 * @brief 连接上有数据到达：按行切分并解析JSON，合法帧入队等待上抛
 * @note  粘包时缓冲区一次到达多帧，while 循环逐行处理，靠换行自然分帧；
 *        本方法只负责收字节与切帧，帧的交付交给 drainFrames 排队处理，
 *        保证上层的写库与模态对话框不会压在本函数的调用栈上
 */
void EdgeLink::onReadyRead()
{
    auto* sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) {
        return;
    }
    const quintptr id = reinterpret_cast<quintptr>(sock);

    while (sock->canReadLine()) {
        const QByteArray line = sock->readLine();

        // 单帧超长：异常数据保护，直接断开该连接并计数
        if (line.size() > m_cfg.frameMaxBytes) {
            qWarning() << "[EdgeLink] frame too large:" << line.size()
                       << "bytes, disconnect id=" << id;
            sock->abort();  // 触发 disconnected → 统一清表
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(line.trimmed());
        if (doc.isNull() || !doc.isObject()) {
            // 协议3.6-②：非法帧丢弃并计数，不报错、不断连
            ++m_badFrames;
            qWarning() << "[EdgeLink] bad frame #" << m_badFrames
                       << "id=" << id
                       << ":" << QString::fromUtf8(line.left(80));
            continue;
        }

        qDebug() << "[EdgeLink] 收到帧 type=" << doc.object().value("type").toString()
                 << "字节=" << line.size() << "id=" << id;
        m_lastFrameMs = QDateTime::currentMSecsSinceEpoch();   // 收帧时刻基准（与业务处理解耦）
        m_frameQueue.enqueue(qMakePair(id, doc.object()));
    }

    scheduleDrain();
}

/**
 * @brief 把已切分出的帧逐个上抛给上层（每次只抛一帧，抛完让出事件循环）
 * @return 无
 * @note  一帧一让出：上抛会触发界面写库与模态对话框，一次只处理一帧可让
 *        定时器（心跳、请求超时扫描）及时得到执行机会
 */
void EdgeLink::drainFrames()
{
    m_drainScheduled = false;

    while (!m_frameQueue.isEmpty()) {
        const auto frame = m_frameQueue.dequeue();
        emit frameReceived(frame.first, frame.second);

        if (!m_frameQueue.isEmpty()) {
            // 队列还有帧：重新排队，下一轮事件循环继续处理
            scheduleDrain();
            return;
        }
    }
}

/**
 * @brief 安排一次帧队列排空（排队派发，保证排空动作发生在事件循环的下一轮）
 * @return 无
 * @note  同一时刻只允许一个排空动作在排队中，防止重复投递
 */
void EdgeLink::scheduleDrain()
{
    if (m_drainScheduled) {
        return;
    }
    m_drainScheduled = true;
    QMetaObject::invokeMethod(this, &EdgeLink::drainFrames, Qt::QueuedConnection);
}

/**
 * @brief 连接断开：从连接表移除，发 clientDisconnected
 * @note  该连接队列中尚未上抛的帧一并剔除：连接已断，这些帧的回应已失效，
 *        若继续上抛会被上层误判成对在途请求的有效回应
 */
void EdgeLink::onDisconnected()
{
    auto* sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) {
        return;
    }
    const quintptr id = reinterpret_cast<quintptr>(sock);
    m_clients.remove(id);
    sock->deleteLater();

    for (int i = m_frameQueue.size() - 1; i >= 0; --i) {
        if (m_frameQueue.at(i).first == id) {
            m_frameQueue.removeAt(i);
        }
    }

    qDebug() << "[EdgeLink] client disconnected id=" << id;
    emit clientDisconnected(id);
}
