/**
 * @file EdgeSimulator.cpp
 * @brief 边缘计算机模拟器实现：按协议逐接口覆盖的按钮测试
 * @author 袁燕
 *
 * 协议依据：《智能工具柜配置项通信协议》
 *   3.3  recognize_ack 场景回应与告警字段
 *   3.4  periodic 周期帧与摄像头自检5态
 *   3.5  version_query / version_ack
 *   3.6  配对/重发/通信异常规则（坏帧、乱序id、未知type）
 *   第5章 停发周期帧验证离线判定与恢复补发
 *   第2章 一行JSON分帧（Compact+'\n'，粘包可靠切分）
 */
#include "EdgeSimulator.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTextEdit>
#include <QTime>
#include <QVBoxLayout>

namespace {
constexpr int kFrameMaxBytes = 1024 * 1024;  // 单帧上限（协议约定）

// 识别回应场景枚举（3.3节全部场景，alert_type 全枚举覆盖）
enum Scene {
    SceneNormal = 0,     // 正常清单（2条样例）
    SceneBorrowReturn,   // 有借有还（3.3-①）
    SceneNoChange,       // 无变化/空清单（3.3-②）
    SceneMisplaced,      // 错放告警 alert_type=misplaced（3.3-③）
    SceneUnknown,        // 未知物告警 alert_type=unknown
    SceneLowConf,        // 置信度不足 alert_type=low_confidence
    SceneMissing,        // 缺失告警 alert_type=missing
};

/// 相机自检"空"选项标记：选中时 camera_status 发空数组（协议3.4：自检可缺席）
const QString kCamEmpty = QStringLiteral("(空-不报自检)");

/**
 * @brief 构造周期帧（协议3.4）
 * @param cam1Status CAM1 自检状态（ok/offline/low_light/dirty/comm_error/空）
 * @param cam2Status CAM2 自检状态（同上）
 * @param uptimeSec  边缘机运行秒数
 * @return periodic 帧对象；某相机选"空"时该相机不出现在 camera_status，
 *         两个都选空则 camera_status 为空数组（验证上位机容错）
 */
QJsonObject makePeriodic(const QString& cam1Status, const QString& cam2Status, int uptimeSec)
{
    QJsonObject p;
    p["type"] = "periodic";
    p["uptime_sec"] = uptimeSec;
    QJsonArray cams;
    if (cam1Status != kCamEmpty) {
        QJsonObject c1; c1["camera_id"] = "CAM1"; c1["status"] = cam1Status; cams.append(c1);
    }
    if (cam2Status != kCamEmpty) {
        QJsonObject c2; c2["camera_id"] = "CAM2"; c2["status"] = cam2Status; cams.append(c2);
    }
    p["camera_status"] = cams;
    return p;
}

/**
 * @brief 按场景构造 recognize_ack 回应帧（协议3.3全部场景）
 * @param scene    场景枚举
 * @param request 收到的recognize请求（取request_id回带）
 * @return recognize_ack 帧对象
 */
QJsonObject sceneAckFrame(int scene, const QJsonObject& request)
{
    QJsonObject ack;
    ack["type"] = "recognize_ack";
    ack["request_id"] = request["request_id"];
    QJsonArray items;

    switch (scene) {
    case SceneNormal: {  // 正常：2条样例
        QJsonObject it1;
        it1["position"] = "A-01-05"; it1["tool_id"] = "C01-T0007";
        it1["class_id"] = "torx";    it1["confidence"] = 0.93;
        items.append(it1);
        QJsonObject it2;
        it2["position"] = "A-02-01"; it2["tool_id"] = "C01-T0012";
        it2["class_id"] = "socket";  it2["confidence"] = 0.88;
        items.append(it2);
        break;
    }
    case SceneBorrowReturn: {  // 3.3-① 有借有还：A-01-05被借走，A-01-06放回套筒
        QJsonObject it;
        it["position"] = "A-01-06"; it["tool_id"] = "C01-T0012";
        it["class_id"] = "socket";  it["confidence"] = 0.91;
        items.append(it);
        break;
    }
    case SceneNoChange:        // 3.3-② 无变化：空清单
        break;
    case SceneMisplaced: {     // 3.3-③ 错放：套筒出现在A-02-01
        QJsonObject it;
        it["position"] = "A-02-01"; it["tool_id"] = "C01-T0012";
        it["class_id"] = "socket";  it["confidence"] = 0.88;
        it["alert_type"] = "misplaced"; it["alert_level"] = 2;
        items.append(it);
        break;
    }
    case SceneUnknown: {       // 告警表：位置上是未知物
        QJsonObject it;
        it["position"] = "A-01-06"; it["tool_id"] = "";
        it["class_id"] = "unknown"; it["confidence"] = 0.71;
        it["alert_type"] = "unknown"; it["alert_level"] = 3;
        items.append(it);
        break;
    }
    case SceneLowConf: {       // 告警表：置信度不足
        QJsonObject it;
        it["position"] = "A-01-05"; it["tool_id"] = "C01-T0007";
        it["class_id"] = "torx";    it["confidence"] = 0.55;
        it["alert_type"] = "low_confidence"; it["alert_level"] = 1;
        items.append(it);
        break;
    }
    case SceneMissing: {       // alert_type=missing：协议字段枚举边界值（台账有、识别为空）
        QJsonObject it;
        it["position"] = "A-01-05"; it["tool_id"] = "C01-T0007";
        it["class_id"] = "torx";    it["confidence"] = 0.0;
        it["alert_type"] = "missing"; it["alert_level"] = 2;
        items.append(it);
        break;
    }
    default:
        break;
    }
    ack["items"] = items;
    return ack;
}
}  // namespace

EdgeSimulator::EdgeSimulator(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("边缘计算机模拟器 — 协议接口测试工具"));
    resize(1020, 660);

    auto* left = new QVBoxLayout;
    left->addWidget(buildConnPanel());
    m_tabs = new QTabWidget;
    m_tabs->addTab(buildPeriodicTab(), QStringLiteral("周期帧 3.4"));
    m_tabs->addTab(buildRecognizeTab(), QStringLiteral("识别回应 3.3"));
    m_tabs->addTab(buildFaultTab(), QStringLiteral("版本/异常 3.5-3.6"));
    m_tabs->addTab(buildCustomTab(), QStringLiteral("自定义帧"));
    left->addWidget(m_tabs);

    m_logView = new QTextEdit;
    m_logView->setReadOnly(true);
    m_logView->setFontFamily(QStringLiteral("Consolas"));

    m_stat = new QLabel(QStringLiteral("收帧 0 | 发帧 0 | 坏帧 0"));

    auto* root = new QHBoxLayout(this);
    auto* leftWrap = new QWidget;
    leftWrap->setLayout(left);
    leftWrap->setFixedWidth(430);
    root->addWidget(leftWrap);
    root->addWidget(m_logView, 1);

    // 日志落盘：优先 exe 同目录，失败回退系统临时目录
    QString logPath = QCoreApplication::applicationDirPath() + QStringLiteral("/EdgeSimulator_log.txt");
    m_logFile = new QFile(logPath, this);
    if (!m_logFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        logPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                  + QStringLiteral("/EdgeSimulator_log.txt");
        m_logFile->setFileName(logPath);
        m_logFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate);
    }
    if (m_logFile->isOpen()) {
        m_logFile->write("\xEF\xBB\xBF");
        setWindowTitle(windowTitle() + QStringLiteral("   [日志: %1]").arg(logPath));
    }

    connect(&m_periodicTimer, &QTimer::timeout, this, [this] {
        sendFrame(makePeriodic(m_cam1->currentText(), m_cam2->currentText(), m_uptime->value()));
    });
    connect(&m_loopTimer, &QTimer::timeout, this, &EdgeSimulator::sendCustomFrame);
}

EdgeSimulator::~EdgeSimulator()
{
    if (m_sock) {
        m_sock->abort();
    }
}

/**
 * @brief 一键连接上位机（已连接则不做任何事）
 * @note  供 --auto-connect 命令行与自动化测试调用
 */
void EdgeSimulator::connectToEdge()
{
    if (m_sock && m_sock->state() == QAbstractSocket::ConnectedState) {
        return;
    }
    onConnectToggle();
}

/**
 * @brief 切换到指定页签
 * @param index 页签序号：0=周期帧 1=识别回应 2=版本/异常 3=自定义帧
 * @note  供 --tab 命令行与截图自动化调用
 */
void EdgeSimulator::showTab(int index)
{
    if (m_tabs && index >= 0 && index < m_tabs->count()) {
        m_tabs->setCurrentIndex(index);
    }
}

// ─────────────────────── 界面构建 ───────────────────────

QWidget* EdgeSimulator::buildConnPanel()
{
    auto* box = new QGroupBox(QStringLiteral("连接上位机"));
    auto* g = new QGridLayout(box);

    m_addr = new QLineEdit(QStringLiteral("127.0.0.1"));
    m_port = new QSpinBox;
    m_port->setRange(1, 65535);
    m_port->setValue(9000);
    m_btnConn = new QPushButton(QStringLiteral("连接"));
    m_state = new QLabel(QStringLiteral("● 未连接"));
    m_state->setStyleSheet(QStringLiteral("color:#888;font-weight:bold;"));
    m_autoReconn = new QCheckBox(QStringLiteral("断开后自动重连（1/2/4/8/16/30秒退避）"));
    m_autoReconn->setChecked(true);
    connect(m_btnConn, &QPushButton::clicked, this, &EdgeSimulator::onConnectToggle);

    g->addWidget(new QLabel(QStringLiteral("上位机IP")), 0, 0);
    g->addWidget(m_addr, 0, 1, 1, 2);
    g->addWidget(new QLabel(QStringLiteral("端口")), 1, 0);
    g->addWidget(m_port, 1, 1);
    g->addWidget(m_btnConn, 2, 0);
    g->addWidget(m_state, 2, 1, 1, 2);
    g->addWidget(m_autoReconn, 3, 0, 1, 3);
    return box;
}

QWidget* EdgeSimulator::buildPeriodicTab()
{
    auto* w = new QWidget;
    auto* g = new QGridLayout(w);

    m_cam1 = new QComboBox;
    m_cam2 = new QComboBox;
    for (QComboBox* c : {m_cam1, m_cam2}) {
        c->addItems({QStringLiteral("ok"), QStringLiteral("offline"),
                     QStringLiteral("low_light"), QStringLiteral("dirty"),
                     QStringLiteral("comm_error"), kCamEmpty});
    }
    m_uptime = new QSpinBox;
    m_uptime->setRange(0, 9999999);
    m_uptime->setValue(123);
    m_btnPeriodic = new QPushButton(QStringLiteral("立即发一帧周期帧"));
    connect(m_btnPeriodic, &QPushButton::clicked, this, &EdgeSimulator::sendPeriodicNow);

    m_periodicOn = new QCheckBox(QStringLiteral("自动发送"));
    m_periodicOn->setChecked(true);
    m_periodicMs = new QSpinBox;
    m_periodicMs->setRange(100, 60000);
    m_periodicMs->setValue(3000);
    m_periodicMs->setSuffix(QStringLiteral(" ms"));
    connect(m_periodicOn, &QCheckBox::toggled, this, &EdgeSimulator::togglePeriodic);

    g->addWidget(new QLabel(QStringLiteral("CAM1 自检状态")), 0, 0);
    g->addWidget(m_cam1, 0, 1);
    g->addWidget(new QLabel(QStringLiteral("CAM2 自检状态")), 1, 0);
    g->addWidget(m_cam2, 1, 1);
    g->addWidget(new QLabel(QStringLiteral("uptime_sec")), 2, 0);
    g->addWidget(m_uptime, 2, 1);
    g->addWidget(m_btnPeriodic, 3, 0, 1, 2);
    g->addWidget(m_periodicOn, 4, 0);
    g->addWidget(m_periodicMs, 4, 1);
    g->addWidget(new QLabel(QStringLiteral("相机状态含义：ok=正常 offline=掉线\nlow_light=光照不足 dirty=镜头脏污\ncomm_error=通信异常（具体以硬件选型为准）\n(空-不报自检)：该相机不出现在清单，\n两个都选空则 camera_status 为空数组\n（验证上位机对自检缺席的容错）")), 5, 0, 1, 2);
    g->setRowStretch(6, 1);
    return w;
}

QWidget* EdgeSimulator::buildRecognizeTab()
{
    auto* w = new QWidget;
    auto* g = new QGridLayout(w);

    // 场景按钮（协议3.3全部场景，按钮文字带功能说明）
    struct Item { const char* text; int scene; };
    const Item items[] = {
        {"正常清单：2条样例，测消息配对",            SceneNormal},
        {"有借有还：A-01-05空+A-01-06套筒",          SceneBorrowReturn},
        {"无变化：空清单，测归档分支",                SceneNoChange},
        {"错放告警：misplaced L2，测告警弹窗",        SceneMisplaced},
        {"未知物告警：unknown L3，测严重告警",        SceneUnknown},
        {"置信度不足：low_confidence L1，测不落账",   SceneLowConf},
        {"缺失告警：missing L2，测字段枚举边界",      SceneMissing},
    };
    int row = 0;
    for (const Item& it : items) {
        auto* b = new QPushButton(QString::fromUtf8(it.text));
        b->setProperty("scene", it.scene);
        connect(b, &QPushButton::clicked, this, [this, b] { replyScene(b->property("scene").toInt()); });
        g->addWidget(b, row, 0, 1, 2);
        ++row;
    }

    m_delayMs = new QSpinBox;
    m_delayMs->setRange(100, 30000);
    m_delayMs->setValue(4000);
    m_delayMs->setSuffix(QStringLiteral(" ms"));
    auto* btnDelayed = new QPushButton(QStringLiteral("延迟回应：默认4000ms>3秒，测超时重发"));
    connect(btnDelayed, &QPushButton::clicked, this, &EdgeSimulator::sendDelayedReply);
    auto* btnDup = new QPushButton(QStringLiteral("重复回应×2：测request_id去重（3.6-③）"));
    connect(btnDup, &QPushButton::clicked, this, [this] {
        replyScene(SceneNormal);
        replyScene(SceneNormal);
    });
    auto* btnFlood = new QPushButton(QStringLiteral("两帧拼一行：测粘包分帧（第2章）"));
    connect(btnFlood, &QPushButton::clicked, this, [this] {
        if (m_lastRequest.isEmpty()) {
            logEvent(QStringLiteral("没有待回应的recognize请求"));
            return;
        }
        const QJsonObject ack = sceneAckFrame(SceneNormal, m_lastRequest);
        if (m_sock && m_sock->state() == QAbstractSocket::ConnectedState) {
            const QByteArray raw = QJsonDocument(makePeriodic(m_cam1->currentText(), m_cam2->currentText(), m_uptime->value()))
                                       .toJson(QJsonDocument::Compact)
                                   + QJsonDocument(ack).toJson(QJsonDocument::Compact) + '\n';
            m_sock->write(raw);
            ++m_tx;
            log("#3d63d6", QString(QStringLiteral("[TX粘包] 两帧拼一行，共%1字节")).arg(raw.size()));
            refreshStat();
        }
    });
    auto* btnSilent = new QPushButton(QStringLiteral("不回应：关闭自动回应，测超时重发（第5章）"));
    connect(btnSilent, &QPushButton::clicked, this, [this] {
        m_autoReply = false;
        logEvent(QStringLiteral("自动回应已关闭：之后收到的recognize将不回应（点场景按钮可手动回应）"));
    });

    g->addWidget(btnDelayed, row, 0);
    g->addWidget(m_delayMs, row, 1);
    ++row;
    g->addWidget(btnDup, row, 0, 1, 2); ++row;
    g->addWidget(btnFlood, row, 0, 1, 2); ++row;
    g->addWidget(btnSilent, row, 0, 1, 2); ++row;

    m_silenceAt = new QSpinBox;
    m_silenceAt->setRange(1, 600);
    m_silenceAt->setValue(10);
    m_silenceAt->setSuffix(QStringLiteral(" s"));
    m_silenceFor = new QSpinBox;
    m_silenceFor->setRange(1, 600);
    m_silenceFor->setValue(20);
    m_silenceFor->setSuffix(QStringLiteral(" s"));
    auto* btnDrop = new QPushButton(QStringLiteral("停发周期帧：测15秒判离线+恢复补发（第5章）"));
    connect(btnDrop, &QPushButton::clicked, this, &EdgeSimulator::executeSilenceDrop);
    g->addWidget(btnDrop, row, 0); ++row;
    g->addWidget(m_silenceAt, row, 0);
    g->addWidget(m_silenceFor, row, 1);
    ++row;
    g->addWidget(m_stat, row, 0, 1, 2);
    g->setRowStretch(row + 1, 1);
    return w;
}

QWidget* EdgeSimulator::buildFaultTab()
{
    auto* w = new QWidget;
    auto* g = new QGridLayout(w);

    m_version = new QLineEdit(QStringLiteral("1.2.0"));
    auto* btnVer = new QPushButton(QStringLiteral("立即回 version_ack：版本号取左侧输入框"));
    connect(btnVer, &QPushButton::clicked, this, &EdgeSimulator::sendVersionAckNow);
    g->addWidget(new QLabel(QStringLiteral("版本号")), 0, 0);
    g->addWidget(m_version, 0, 1);
    g->addWidget(btnVer, 1, 0, 1, 2);

    auto* btnBad = new QPushButton(QStringLiteral("注入非法JSON：测坏帧丢弃不断链（3.6-②）"));
    connect(btnBad, &QPushButton::clicked, this, &EdgeSimulator::injectBadFrame);
    auto* btnUnknown = new QPushButton(QStringLiteral("注入未知type：测未知类型丢弃（3.6-②）"));
    connect(btnUnknown, &QPushButton::clicked, this, &EdgeSimulator::injectUnknownType);
    auto* btnOver = new QPushButton(QStringLiteral("注入超长帧>1MB：测断开保护"));
    connect(btnOver, &QPushButton::clicked, this, &EdgeSimulator::injectOversize);
    auto* btnBadId = new QPushButton(QStringLiteral("回应乱序request_id：测丢弃计数（3.6-②）"));
    connect(btnBadId, &QPushButton::clicked, this, &EdgeSimulator::sendBadIdAck);
    auto* btnDisc = new QPushButton(QStringLiteral("断开连接：模拟断链（勾选自动重连会重连）"));
    connect(btnDisc, &QPushButton::clicked, this, [this] {
        if (m_sock) m_sock->disconnectFromHost();
    });
    g->addWidget(btnBad, 2, 0, 1, 2);
    g->addWidget(btnUnknown, 3, 0, 1, 2);
    g->addWidget(btnOver, 4, 0, 1, 2);
    g->addWidget(btnBadId, 5, 0, 1, 2);
    g->addWidget(btnDisc, 6, 0, 1, 2);
    g->setRowStretch(7, 1);
    return w;
}

QWidget* EdgeSimulator::buildCustomTab()
{
    auto* w = new QWidget;
    auto* g = new QGridLayout(w);

    m_custom = new QTextEdit;
    m_custom->setPlainText(QStringLiteral("{\"type\":\"recognize_ack\",\"request_id\":\"r-test\",\"items\":[{\"position\":\"A-01-05\",\"tool_id\":\"C01-T0007\",\"class_id\":\"torx\",\"confidence\":0.9}]}"));
    m_custom->setToolTip(QStringLiteral("每行一帧，点【发送】逐行发出。可构造协议任何消息与异常"));

    m_rawSend = new QCheckBox(QStringLiteral("原文发送（不校验JSON，可发坏帧/任意文本）"));
    m_rawSend->setToolTip(QStringLiteral("勾选后按文本原样发出（含换行拼包），用于坏帧与粘包组合测试"));

    m_loopMs = new QSpinBox;
    m_loopMs->setRange(100, 600000);
    m_loopMs->setValue(3000);
    m_loopMs->setSuffix(QStringLiteral(" ms"));
    m_btnLoop = new QPushButton(QStringLiteral("启动循环发送"));
    connect(m_btnLoop, &QPushButton::clicked, this, [this] { toggleLoopSend(m_loopTimer.isActive() == false); });

    auto* btn = new QPushButton(QStringLiteral("发送（逐行）"));
    connect(btn, &QPushButton::clicked, this, &EdgeSimulator::sendCustomFrame);

    g->addWidget(new QLabel(QStringLiteral("自定义内容（每行一帧）")), 0, 0, 1, 3);
    g->addWidget(m_custom, 1, 0, 1, 3);
    g->addWidget(m_rawSend, 2, 0, 1, 3);
    g->addWidget(btn, 3, 0, 1, 3);
    g->addWidget(new QLabel(QStringLiteral("循环发送间隔")), 4, 0);
    g->addWidget(m_loopMs, 4, 1);
    g->addWidget(m_btnLoop, 4, 2);
    g->addWidget(new QLabel(QStringLiteral("扩展用法：①任意协议消息都可在此构造；\n②勾选原文发送可发坏帧文本/多帧粘包；\n③循环发送可持续注入异常（压力测试）；\n④示例：两行JSON拼成粘包；改request_id测乱序")), 5, 0, 1, 3);
    return w;
}

// ─────────────────────── 连接管理 ───────────────────────

void EdgeSimulator::onConnectToggle()
{
    if (m_sock && m_sock->state() == QAbstractSocket::ConnectedState) {
        m_userDisconnect = true;   // 用户主动断开：不触发自动重连
        m_sock->disconnectFromHost();
        return;
    }
    m_sock = new QTcpSocket(this);
    connect(m_sock, &QTcpSocket::connected, this, &EdgeSimulator::onSocketConnected);
    connect(m_sock, &QTcpSocket::disconnected, this, &EdgeSimulator::onSocketDisconnected);
    connect(m_sock, &QTcpSocket::readyRead, this, &EdgeSimulator::onReadyRead);
    m_sock->connectToHost(m_addr->text(), static_cast<quint16>(m_port->value()));
    logEvent(QString(QStringLiteral("正在连接 %1:%2 …")).arg(m_addr->text()).arg(m_port->value()));
}

void EdgeSimulator::onSocketConnected()
{
    m_reconnTry = 0;   // 连接成功，退避计数归零
    m_state->setText(QString(QStringLiteral("● 已连接 %1:%2")).arg(m_addr->text()).arg(m_port->value()));
    m_state->setStyleSheet(QStringLiteral("color:#2a9d52;font-weight:bold;"));
    m_btnConn->setText(QStringLiteral("断开"));
    logEvent(QStringLiteral("已连接上位机"));
    if (m_periodicOn->isChecked()) {
        m_periodicTimer.start(m_periodicMs->value());
    }
}

void EdgeSimulator::onSocketDisconnected()
{
    m_periodicTimer.stop();
    m_state->setText(QStringLiteral("● 未连接"));
    m_state->setStyleSheet(QStringLiteral("color:#c2571f;font-weight:bold;"));
    m_btnConn->setText(QStringLiteral("连接"));
    logEvent(QStringLiteral("与上位机断开"));
    if (m_sock) {
        m_sock->deleteLater();
        m_sock = nullptr;
    }
    // 模拟真实边缘机行为：按 1/2/4/8/16/30 秒退避自动重连（上限30秒）
    // 用户主动断开（m_userDisconnect）不重连
    if (m_autoReconn->isChecked() && !m_userDisconnect) {
        static const int kBackoff[] = {1, 2, 4, 8, 16, 30};
        const int idx = qMin(m_reconnTry, 5);
        const int sec = kBackoff[idx];
        ++m_reconnTry;
        logEvent(QString(QStringLiteral("%1秒后自动重连（第%2次退避）")).arg(sec).arg(m_reconnTry));
        QTimer::singleShot(sec * 1000, this, [this] {
            if (!m_sock) {
                onConnectToggle();
            }
        });
    }
    m_userDisconnect = false;
}

// ─────────────────────── 收帧 ───────────────────────

void EdgeSimulator::onReadyRead()
{
    auto* sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) {
        return;
    }
    while (sock->canReadLine()) {
        const QByteArray line = sock->readLine();
        if (line.size() > kFrameMaxBytes) {
            ++m_bad;
            sock->abort();
            return;
        }
        processLine(line);
    }
}

void EdgeSimulator::processLine(const QByteArray& line)
{
    const QJsonDocument doc = QJsonDocument::fromJson(line.trimmed());
    if (doc.isNull() || !doc.isObject()) {
        ++m_bad;
        log("#c2571f", QString(QStringLiteral("[RX坏帧 #%1] %2")).arg(m_bad).arg(QString::fromUtf8(line.left(120))));
        refreshStat();
        return;
    }
    ++m_rx;
    const QJsonObject frame = doc.object();
    logFrame("RX", frame);
    const QString type = frame["type"].toString();

    if (type == QStringLiteral("recognize")) {
        m_lastRequest = frame;
        if (m_autoReply) {
            QTimer::singleShot(200, this, [this] { replyScene(SceneNormal); });
        }
    } else if (type == QStringLiteral("version_query")) {
        // 3.5：收到版本查询自动回应
        QTimer::singleShot(200, this, [this] { sendVersionAckNow(); });
    }
    refreshStat();
}

// ─────────────────────── 协议3.4 周期帧 ───────────────────────

void EdgeSimulator::sendPeriodicNow()
{
    sendFrame(makePeriodic(m_cam1->currentText(), m_cam2->currentText(), m_uptime->value()));
}

void EdgeSimulator::togglePeriodic(bool on)
{
    if (on && m_sock && m_sock->state() == QAbstractSocket::ConnectedState) {
        m_periodicTimer.start(m_periodicMs->value());
        logEvent(QString(QStringLiteral("周期帧自动发送启动，间隔%1ms")).arg(m_periodicMs->value()));
    } else {
        m_periodicTimer.stop();
        if (!on) {
            logEvent(QStringLiteral("周期帧自动发送停止（上位机应15秒判离线）"));
        }
    }
}

// ─────────────────────── 协议3.3 识别回应 ───────────────────────

void EdgeSimulator::replyScene(int scene)
{
    if (!m_sock || m_lastRequest.isEmpty()) {
        logEvent(QStringLiteral("没有待回应的recognize请求（先等上位机发一次请求）"));
        return;
    }
    sendFrame(sceneAckFrame(scene, m_lastRequest));
}

void EdgeSimulator::sendDelayedReply()
{
    if (m_lastRequest.isEmpty()) {
        logEvent(QStringLiteral("没有待回应的recognize请求"));
        return;
    }
    logEvent(QString(QStringLiteral("延迟%1ms后回应")).arg(m_delayMs->value()));
    QTimer::singleShot(m_delayMs->value(), this, [this] { replyScene(SceneNormal); });
}

void EdgeSimulator::executeSilenceDrop()
{
    logEvent(QString(QStringLiteral("%1秒后停发周期帧%2秒（模拟断链）"))
                 .arg(m_silenceAt->value()).arg(m_silenceFor->value()));
    QTimer::singleShot(m_silenceAt->value() * 1000, this, [this] {
        m_periodicTimer.stop();
        logEvent(QStringLiteral("*** 周期帧已停发，等待上位机判离线"));
        QTimer::singleShot(m_silenceFor->value() * 1000, this, [this] {
            if (m_periodicOn->isChecked() && m_sock && m_sock->state() == QAbstractSocket::ConnectedState) {
                m_periodicTimer.start(m_periodicMs->value());
                logEvent(QStringLiteral("*** 周期帧恢复发送（上位机应补发未回应请求）"));
            }
        });
    });
}

// ─────────────────────── 协议3.5 版本 ───────────────────────

void EdgeSimulator::sendVersionAckNow()
{
    QJsonObject v;
    v["type"] = "version_ack";
    v["software_version"] = m_version->text();
    sendFrame(v);
}

// ─────────────────────── 协议3.6 异常注入 ───────────────────────

void EdgeSimulator::injectBadFrame()
{
    if (!m_sock || m_sock->state() != QAbstractSocket::ConnectedState) {
        logEvent(QStringLiteral("发送失败：未连接"));
        return;
    }
    m_sock->write(QStringLiteral("这不是合法JSON\n").toUtf8());
    logEvent(QStringLiteral("已注入1条非法帧（上位机应丢弃计数、链路不断）"));
}

void EdgeSimulator::injectUnknownType()
{
    QJsonObject u;
    u["type"] = QStringLiteral("unknown_type_test");
    u["note"] = QStringLiteral("测试未知type丢弃");
    sendFrame(u);
}

void EdgeSimulator::injectOversize()
{
    if (!m_sock || m_sock->state() != QAbstractSocket::ConnectedState) {
        logEvent(QStringLiteral("发送失败：未连接"));
        return;
    }
    // 构造一个超过1MB的JSON行（填充字段）
    const QString big(kFrameMaxBytes + 1024, QChar('x'));
    QJsonObject o;
    o["type"] = QStringLiteral("periodic");
    o["uptime_sec"] = 1;
    o["pad"] = big;
    m_sock->write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n');
    logEvent(QStringLiteral("已注入超长帧>1MB（上位机应断开保护）"));
}

void EdgeSimulator::sendBadIdAck()
{
    if (m_lastRequest.isEmpty()) {
        logEvent(QStringLiteral("没有待回应的recognize请求"));
        return;
    }
    QJsonObject ack = sceneAckFrame(SceneNormal, m_lastRequest);
    ack["request_id"] = QStringLiteral("r-INVALID-999");   // 故意用乱序id
    sendFrame(ack);
    logEvent(QStringLiteral("已发乱序request_id的ack（上位机应丢弃计数 3.6-②）"));
}

// ─────────────────────── 自定义帧（扩展接口） ───────────────────────

/**
 * @brief 发送自定义内容：按行逐条发送，支持 JSON 与原文两种模式
 * @note  JSON 模式（默认）：逐行解析校验后发送，格式错误本地提示不发；
 *        原文模式（勾选）：按文本原样发出，可构造坏帧、多帧粘包等任意异常。
 *        这是测试人员自建异常场景的扩展入口——协议任何消息都可用本页签构造。
 */
void EdgeSimulator::sendCustomFrame()
{
    if (!m_sock || m_sock->state() != QAbstractSocket::ConnectedState) {
        logEvent(QStringLiteral("发送失败：连接未建立"));
        return;
    }
    const QString text = m_custom->toPlainText();
    const QStringList lines = text.split(QStringLiteral("\n"), Qt::SkipEmptyParts);
    if (lines.isEmpty()) {
        logEvent(QStringLiteral("自定义内容为空"));
        return;
    }
    int okCount = 0;
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        if (m_rawSend->isChecked()) {
            // 原文模式：原样发出（含换行规则由行本身决定）
            m_sock->write(trimmed.toUtf8() + '\n');
            ++m_tx;
            log("#3d63d6", QString(QStringLiteral("[TX原文] %1")).arg(trimmed.left(200)));
            ++okCount;
        } else {
            // JSON 模式：解析校验后发送
            const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8());
            if (doc.isNull() || !doc.isObject()) {
                logEvent(QString(QStringLiteral("第%1行JSON解析失败，已跳过")).arg(okCount + 1));
                continue;
            }
            sendFrame(doc.object());
            ++okCount;
        }
    }
    refreshStat();
    logEvent(QString(QStringLiteral("自定义发送完成，共%1行")).arg(okCount));
}

/**
 * @brief 循环发送自定义内容（持续异常注入/压力测试）
 * @param on true=启动循环；false=停止
 * @note  按“循环发送间隔”周期性调用 sendCustomFrame
 */
void EdgeSimulator::toggleLoopSend(bool on)
{
    if (on) {
        m_loopTimer.start(m_loopMs->value());
        m_btnLoop->setText(QStringLiteral("停止循环发送"));
        logEvent(QString(QStringLiteral("循环发送启动，每%1ms")).arg(m_loopMs->value()));
    } else {
        m_loopTimer.stop();
        m_btnLoop->setText(QStringLiteral("启动循环发送"));
        logEvent(QStringLiteral("循环发送停止"));
    }
}

// ─────────────────────── 发送与日志 ───────────────────────

bool EdgeSimulator::sendFrame(const QJsonObject& frame)
{
    if (!m_sock || m_sock->state() != QAbstractSocket::ConnectedState) {
        logEvent(QStringLiteral("发送失败：连接未建立"));
        return false;
    }
    const QByteArray data = QJsonDocument(frame).toJson(QJsonDocument::Compact) + '\n';
    if (m_sock->write(data) != data.size()) {
        logEvent(QStringLiteral("发送失败：写入不完整"));
        return false;
    }
    ++m_tx;
    logFrame("TX", frame);
    refreshStat();
    return true;
}

void EdgeSimulator::log(const QString& color, const QString& text)
{
    const QString stamp = QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    m_logView->append(QString(QStringLiteral("<span style='color:%1'>[%2] %3</span>")).arg(color, stamp, text));
    if (m_logFile && m_logFile->isOpen()) {
        m_logFile->write(QString("[%1] %2\n").arg(stamp, text).toUtf8());
        m_logFile->flush();
    }
}

void EdgeSimulator::logFrame(const QString& dir, const QJsonObject& f)
{
    const QString compact = QString::fromUtf8(QJsonDocument(f).toJson(QJsonDocument::Compact));
    log(dir == QStringLiteral("TX") ? QStringLiteral("#3d63d6") : QStringLiteral("#2a9d52"),
        QString(QStringLiteral("[%1] %2")).arg(dir, compact));
}

void EdgeSimulator::refreshStat()
{
    m_stat->setText(QString(QStringLiteral("收帧 %1 | 发帧 %2 | 坏帧 %3")).arg(m_rx).arg(m_tx).arg(m_bad));
}