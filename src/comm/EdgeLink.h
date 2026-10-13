/**
 * @file  EdgeLink.h
 * @brief 与边缘计算机通信接口的链路层：TCP监听、换行分帧、JSON收发
 * @author 袁燕
 *
 * 职责边界（只管“字节进、字节出”，不懂业务）：
 *   收：按行切分 → JSON 解析 → 合法帧发 frameReceived 信号
 *   发：JSON 紧凑序列化 + '\n' 写 socket
 *   附：连接建立/断开管理、坏帧丢弃计数、单帧超长断开保护
 *
 * 分帧约定（协议第2章）：发送方一律紧凑序列化，JSON 字符串内的换行会被
 * 转义成 \ + n 两个字节，报文体中不会出现真实换行，故按换行切分绝对可靠。
 *
 * 依赖规则：本头文件只允许被 EdgeService.cpp 和测试工具包含，业务/界面层禁止使用。
 */
#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>
#include "CommTypes.h"

class EdgeLink : public QObject {
    Q_OBJECT
public:
    explicit EdgeLink(QObject* parent = nullptr);
    ~EdgeLink() override;

    bool start(const EdgeConfig& cfg);   ///< 开始监听；失败不致命，仅本功能不可用
    void stop();                          ///< 停止并断开全部连接

    bool sendFrame(quintptr clientId, const QJsonObject& frame);  ///< 向指定连接发一帧
    QString peerAddress(quintptr clientId) const;  ///< 查询连接对端IP
    bool isListening() const;             ///< 是否正在监听

signals:
    void clientConnected(quintptr clientId);      ///< 边缘机接入（含重连，每次都是新编号）
    void clientDisconnected(quintptr clientId);   ///< 边缘机断开
    /// 解出一条合法JSON帧；坏帧（解析失败/超长）在链路层丢弃并计数，不上抛
    void frameReceived(quintptr clientId, const QJsonObject& frame);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    QTcpServer* m_server = nullptr;
    /// key = socket 指针值（连接存续期唯一，重连即新编号）
    QHash<quintptr, QTcpSocket*> m_clients;
    EdgeConfig m_cfg;
    int m_badFrames = 0;   ///< 累计坏帧数（日志留痕，观察链路质量）
};