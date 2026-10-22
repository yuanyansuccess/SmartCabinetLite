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
#include <QDateTime>
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
#include <QSettings>
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
    SceneSilent,         // 不回应（测上位机识别超时重发，第5章；作为场景选项而非开关，测完切回即恢复，无状态残留）
};

/// 相机自检"空"选项标记：选中时该相机不出现在 camera_status（协议3.4：自检可缺席）
const QString kCamEmpty = QStringLiteral("(空-不报自检)");

/**
 * @brief 构造周期帧（协议3.4），含 8 路摄像头自检
 * @param cams      8 路摄像头自检状态下拉框（CAM1..CAM8，与硬件一一对应）
 * @param uptimeSec 边缘机运行秒数
 * @return periodic 帧对象；某相机选"空"时不出现在 camera_status，全部选空则 camera_status 为空数组
 * @note  摄像头 id 按位置稳定编为 CAM1..CAM8；任一状态非 ok 即异常（上位机据此禁止开柜）
 */
QJsonObject makePeriodic(const QList<QComboBox*>& cams, int uptimeSec)
{
    QJsonObject p;
    p["type"] = "periodic";
    p["uptime_sec"] = uptimeSec;
    QJsonArray arr;
    int pos = 0;
    for (QComboBox* c : cams) {
        ++pos;
        const QString st = c ? c->currentText() : kCamEmpty;
        if (st != kCamEmpty) {
            QJsonObject o;
            o["camera_id"] = QStringLiteral("CAM%1").arg(pos);
            o["status"] = st;
            arr.append(o);
        }
    }
    p["camera_status"] = arr;
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

    // 右侧：日志工具行（标题+保存按钮）+ 日志区
    auto* rightWrap = new QWidget;
    auto* rightCol = new QVBoxLayout(rightWrap);
    rightCol->setContentsMargins(0, 0, 0, 0);
    auto* logBar = new QHBoxLayout;
    logBar->addWidget(new QLabel(QStringLiteral("通信日志")));
    logBar->addStretch(1);
    m_btnSaveLog = new QPushButton(QStringLiteral("保存日志"));
    m_btnSaveLog->setToolTip(QStringLiteral("把日志区当前内容保存为带时间戳的文本文件（exe 同目录，不可写时自动回退桌面）"));
    connect(m_btnSaveLog, &QPushButton::clicked, this, &EdgeSimulator::onSaveLogClicked);
    logBar->addWidget(m_btnSaveLog);
    rightCol->addLayout(logBar);
    rightCol->addWidget(m_logView, 1);
    root->addWidget(rightWrap, 1);

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
        sendFrame(makePeriodic(m_camList, m_uptime->value()));
    });
    connect(&m_loopTimer, &QTimer::timeout, this, &EdgeSimulator::sendCustomFrame);

    // 全部界面构建完成后，恢复上次的回应配置（连接前即可完成配置，符合"配置好再连"的使用方式）
    loadConfig();
}

EdgeSimulator::~EdgeSimulator()
{
    saveConfig();   // 退出前持久化全部回应配置，下次启动原样恢复
    if (m_sock) {
        m_sock->abort();
    }
}

/**
 * @brief 把全部回应配置持久化到 exe 同目录 simulator.ini
 * @return 无。写失败静默忽略（配置文件不影响主功能）
 * @note  覆盖：8 路摄像头状态/周期帧参数/识别场景与延迟/开箱策略与失败文案/版本号/连接参数；
 *        配置随 exe 部署目录走（绿色部署），测试人员一次配好长期复用
 */
void EdgeSimulator::saveConfig()
{
    QSettings s(QCoreApplication::applicationDirPath() + QStringLiteral("/simulator.ini"), QSettings::IniFormat);
    for (int i = 0; i < m_camList.size(); ++i) {
        s.setValue(QString(QStringLiteral("camera/cam%1")).arg(i + 1), m_camList[i]->currentText());
    }
    s.setValue(QStringLiteral("periodic/auto"), m_periodicOn ? m_periodicOn->isChecked() : true);
    s.setValue(QStringLiteral("periodic/intervalMs"), m_periodicMs ? m_periodicMs->value() : 3000);
    s.setValue(QStringLiteral("periodic/uptime"), m_uptime ? m_uptime->value() : 123);
    s.setValue(QStringLiteral("reply/sceneIndex"), m_recScene ? m_recScene->currentIndex() : 0);
    s.setValue(QStringLiteral("reply/delayMs"), m_delayMs ? m_delayMs->value() : 200);
    s.setValue(QStringLiteral("open/policyIndex"), m_openPolicy ? m_openPolicy->currentIndex() : 0);
    s.setValue(QStringLiteral("open/failMsg"), m_openFailMsg ? m_openFailMsg->text() : QStringLiteral("开箱失败（模拟）"));
    s.setValue(QStringLiteral("version/value"), m_version ? m_version->text() : QStringLiteral("1.2.0"));
    s.setValue(QStringLiteral("conn/addr"), m_addr ? m_addr->text() : QStringLiteral("127.0.0.1"));
    s.setValue(QStringLiteral("conn/port"), m_port ? m_port->value() : 9000);
    s.setValue(QStringLiteral("conn/autoReconnect"), m_autoReconn ? m_autoReconn->isChecked() : true);
}

/**
 * @brief 启动时从 exe 同目录 simulator.ini 恢复上次的回应配置
 * @return 无。文件不存在或某项缺失时用界面默认值（与原硬编码默认一致）
 */
void EdgeSimulator::loadConfig()
{
    QSettings s(QCoreApplication::applicationDirPath() + QStringLiteral("/simulator.ini"), QSettings::IniFormat);
    for (int i = 0; i < m_camList.size(); ++i) {
        const QString v = s.value(QString(QStringLiteral("camera/cam%1")).arg(i + 1)).toString();
        if (!v.isEmpty()) {
            m_camList[i]->setCurrentText(v);
        }
    }
    m_periodicOn->setChecked(s.value(QStringLiteral("periodic/auto"), true).toBool());
    m_periodicMs->setValue(s.value(QStringLiteral("periodic/intervalMs"), 3000).toInt());
    m_uptime->setValue(s.value(QStringLiteral("periodic/uptime"), 123).toInt());
    m_recScene->setCurrentIndex(s.value(QStringLiteral("reply/sceneIndex"), 0).toInt());
    m_delayMs->setValue(s.value(QStringLiteral("reply/delayMs"), 200).toInt());
    m_openPolicy->setCurrentIndex(s.value(QStringLiteral("open/policyIndex"), 0).toInt());
    m_openFailMsg->setText(s.value(QStringLiteral("open/failMsg"), QStringLiteral("开箱失败（模拟）")).toString());
    m_version->setText(s.value(QStringLiteral("version/value"), QStringLiteral("1.2.0")).toString());
    m_addr->setText(s.value(QStringLiteral("conn/addr"), QStringLiteral("127.0.0.1")).toString());
    m_port->setValue(s.value(QStringLiteral("conn/port"), 9000).toInt());
    m_autoReconn->setChecked(s.value(QStringLiteral("conn/autoReconnect"), true).toBool());
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

    // 8 路摄像头自检状态（CAM1..CAM8，与真实边缘机一一对应）
    const QStringList camStates = {QStringLiteral("ok"), QStringLiteral("offline"),
                                   QStringLiteral("low_light"), QStringLiteral("dirty"),
                                   QStringLiteral("comm_error"), kCamEmpty};
    for (int i = 1; i <= 8; ++i) {
        auto* c = new QComboBox;
        c->addItems(camStates);
        c->setObjectName(QString(QStringLiteral("cam%1")).arg(i));  // 固定名：单测按名查找，避免下拉总数变化导致下标错位
        m_camList.append(c);
        g->addWidget(new QLabel(QStringLiteral("CAM%1 自检状态").arg(i)), i - 1, 0);
        g->addWidget(c, i - 1, 1);
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

    const int rowUptime = 8;     // 8 路相机占 0..7 行
    g->addWidget(new QLabel(QStringLiteral("uptime_sec")), rowUptime, 0);
    g->addWidget(m_uptime, rowUptime, 1);
    g->addWidget(m_btnPeriodic, rowUptime + 1, 0, 1, 2);
    g->addWidget(m_periodicOn, rowUptime + 2, 0);
    g->addWidget(m_periodicMs, rowUptime + 2, 1);
    g->addWidget(new QLabel(QStringLiteral("8 路摄像头（CAM1..CAM8）状态含义：\nok=正常 offline=掉线 low_light=光照不足\n"
                                          "dirty=镜头脏污 comm_error=通信异常\n(空-不报自检)：该相机不出现在清单，全部选空则 camera_status 为空数组\n"
                                          "安全策略：任一状态非 ok → 上位机与模拟器两端都拒绝开柜")),
                 rowUptime + 3, 0, 1, 2);
    g->setRowStretch(rowUptime + 4, 1);
    return w;
}

QWidget* EdgeSimulator::buildRecognizeTab()
{
    auto* w = new QWidget;
    auto* g = new QGridLayout(w);

    // ── 自动回应配置区（连接前配好，连接后即改即生效；下一条请求按当前配置回）──
    auto* cfgTitle = new QLabel(QStringLiteral("识别自动回应配置（协议3.3 · 连接前后可随时切换，下一条请求即生效）"));
    cfgTitle->setStyleSheet(QStringLiteral("font-weight:bold;color:#333;"));
    g->addWidget(cfgTitle, 0, 0, 1, 2);

    m_recScene = new QComboBox;
    // 下拉选项文字=测试含义，itemData=Scene 枚举值；顺序与协议场景清单一致
    m_recScene->addItem(QStringLiteral("正常清单（2条样例，测消息配对）"),        SceneNormal);
    m_recScene->addItem(QStringLiteral("有借有还（A-01-05空+A-01-06套筒）"),      SceneBorrowReturn);
    m_recScene->addItem(QStringLiteral("无变化（空清单，测归档分支）"),           SceneNoChange);
    m_recScene->addItem(QStringLiteral("错放告警（misplaced L2，测告警提示）"),   SceneMisplaced);
    m_recScene->addItem(QStringLiteral("未知物告警（unknown L3，测严重告警）"),   SceneUnknown);
    m_recScene->addItem(QStringLiteral("置信度不足（low_confidence L1，测不落账）"), SceneLowConf);
    m_recScene->addItem(QStringLiteral("缺失告警（missing L2，测字段枚举边界）"),  SceneMissing);
    m_recScene->addItem(QStringLiteral("不回应（测上位机识别超时重发，第5章）"),   SceneSilent);
    g->addWidget(new QLabel(QStringLiteral("回应场景")), 1, 0);
    g->addWidget(m_recScene, 1, 1);

    m_delayMs = new QSpinBox;
    m_delayMs->setRange(0, 30000);
    m_delayMs->setValue(200);
    m_delayMs->setSuffix(QStringLiteral(" ms"));
    m_delayMs->setToolTip(QStringLiteral("0=立即回应；设 >3000（上位机3秒超时）可测超时重发"));
    g->addWidget(new QLabel(QStringLiteral("回应延迟")), 2, 0);
    g->addWidget(m_delayMs, 2, 1);

    // ── 手动注入区（一次性动作，补充自动配置覆盖不到的协议异常）──
    auto* manTitle = new QLabel(QStringLiteral("手动注入（一次性动作）"));
    manTitle->setStyleSheet(QStringLiteral("font-weight:bold;color:#333;margin-top:8px;"));
    g->addWidget(manTitle, 3, 0, 1, 2);
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
            const QByteArray raw = QJsonDocument(makePeriodic(m_camList, m_uptime->value()))
                                       .toJson(QJsonDocument::Compact)
                                   + QJsonDocument(ack).toJson(QJsonDocument::Compact) + '\n';
            m_sock->write(raw);
            ++m_tx;
            log("#3d63d6", QString(QStringLiteral("[TX粘包] 两帧拼一行，共%1字节")).arg(raw.size()));
            refreshStat();
        }
    });
    g->addWidget(btnDup, 4, 0, 1, 2);
    g->addWidget(btnFlood, 5, 0, 1, 2);

    // ── 链路离线模拟区（第5章）──
    auto* linkTitle = new QLabel(QStringLiteral("链路离线模拟（第5章）"));
    linkTitle->setStyleSheet(QStringLiteral("font-weight:bold;color:#333;margin-top:8px;"));
    g->addWidget(linkTitle, 6, 0, 1, 2);
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
    g->addWidget(btnDrop, 7, 0, 1, 2);
    g->addWidget(m_silenceAt, 8, 0);
    g->addWidget(m_silenceFor, 8, 1);

    g->addWidget(m_stat, 9, 0, 1, 2);
    g->setRowStretch(10, 1);
    return w;
}

QWidget* EdgeSimulator::buildFaultTab()
{
    auto* w = new QWidget;
    auto* g = new QGridLayout(w);

    // ── 开箱回应配置区（协议3.7：连接前配好，连接后即改即生效）──
    auto* openTitle = new QLabel(QStringLiteral("开箱回应配置（协议3.7 · 下一条 open_req 即生效）"));
    openTitle->setStyleSheet(QStringLiteral("font-weight:bold;color:#333;"));
    g->addWidget(openTitle, 0, 0, 1, 2);

    m_openPolicy = new QComboBox;
    m_openPolicy->addItem(QStringLiteral("自动成功（摄像头异常时联动拒绝，与真实边缘机一致）"), 0);
    m_openPolicy->addItem(QStringLiteral("固定失败（回 result=0，测上位机开箱失败分支）"), 1);
    m_openPolicy->addItem(QStringLiteral("不回应（测上位机开箱超时重发，第5章）"), 2);
    g->addWidget(new QLabel(QStringLiteral("回应策略")), 1, 0);
    g->addWidget(m_openPolicy, 1, 1);

    m_openFailMsg = new QLineEdit(QStringLiteral("开箱失败（模拟）"));
    m_openFailMsg->setToolTip(QStringLiteral("策略为“固定失败”时 open_ack 携带的原因文案"));
    g->addWidget(new QLabel(QStringLiteral("失败原因文案")), 2, 0);
    g->addWidget(m_openFailMsg, 2, 1);

    // ── 版本回应配置（协议3.5：自动回应+手动立即回）──
    auto* verTitle = new QLabel(QStringLiteral("版本回应（协议3.5）"));
    verTitle->setStyleSheet(QStringLiteral("font-weight:bold;color:#333;margin-top:8px;"));
    g->addWidget(verTitle, 3, 0, 1, 2);
    m_version = new QLineEdit(QStringLiteral("1.2.0"));
    auto* btnVer = new QPushButton(QStringLiteral("立即回 version_ack：版本号取左侧输入框"));
    connect(btnVer, &QPushButton::clicked, this, &EdgeSimulator::sendVersionAckNow);
    g->addWidget(new QLabel(QStringLiteral("版本号")), 4, 0);
    g->addWidget(m_version, 4, 1);
    g->addWidget(btnVer, 5, 0, 1, 2);

    // ── 异常注入区（协议3.6，一次性动作）──
    auto* injTitle = new QLabel(QStringLiteral("异常注入（协议3.6 · 一次性动作）"));
    injTitle->setStyleSheet(QStringLiteral("font-weight:bold;color:#333;margin-top:8px;"));
    g->addWidget(injTitle, 6, 0, 1, 2);
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
    g->addWidget(btnBad, 7, 0, 1, 2);
    g->addWidget(btnUnknown, 8, 0, 1, 2);
    g->addWidget(btnOver, 9, 0, 1, 2);
    g->addWidget(btnBadId, 10, 0, 1, 2);
    g->addWidget(btnDisc, 11, 0, 1, 2);
    g->setRowStretch(12, 1);
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
    // 连接失败（上位机未启动/端口未监听/连接被拒）也必须接：Qt 对"从未建立过的连接"
    // 失败时只发 errorOccurred、不发 disconnected，不接就会让退避重连链条无声断裂
    connect(m_sock, &QTcpSocket::errorOccurred, this, &EdgeSimulator::onSocketError);
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
    // 防重入：连接失败(errorOccurred)与断开(disconnected)可能先后触发同一socket，
    // 先到者已把 m_sock 置空并安排过退避，后到者直接跳过，避免退避翻倍与日志重复
    if (!m_sock) return;
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

/**
 * @brief 连接失败处理：上位机未启动/端口未监听/连接被拒
 * @param err 套接字错误类型（仅用于定位，具体人读信息取 errorString）
 * @note  Qt 对"从未成功建立过的连接"失败时只发 errorOccurred、不发 disconnected。
 *       若此处不处理，退避重连链条会在这里无声断裂——表现为"模拟器再也不重连、
 *       上位机永远收不到心跳"，而界面上只有一句"正在连接"便再无下文，极易误判为
 *       "模拟器一直在发心跳"。这里显式复用 onSocketDisconnected 的清理与退避逻辑，
 *       达到两个目的：①给出可见的失败原因（不再无声卡死）
 *       ②退避重连持续进行，上位机一旦起来就自动接上
 */
void EdgeSimulator::onSocketError(QAbstractSocket::SocketError err)
{
    if (!m_sock) return;   // 已由 onSocketDisconnected 处理过，防重入
    Q_UNUSED(err)
    logEvent(QString(QStringLiteral("连接失败：%1（将继续按退避自动重连）"))
                 .arg(m_sock->errorString()));
    onSocketDisconnected();
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

    if (type == QStringLiteral("recognize_req")) {   // 协议3.3：上位机下发的识别请求帧类型
        m_lastRequest = frame;
        // 按配置回应：场景取下拉当前项（含"不回应"），延迟取数字框（0=立即）；
        // 连接前后改配置都即时生效，测完"不回应"切回其他场景即可，无状态残留
        const int scene = m_recScene ? m_recScene->currentData().toInt() : static_cast<int>(SceneNormal);
        const int delay = m_delayMs ? m_delayMs->value() : 0;
        if (scene == SceneSilent) {
            logEvent(QStringLiteral("收到recognize_req（场景配置为不回应，不回应以测上位机超时重发）"));
        } else {
            const QString sceneText = m_recScene ? m_recScene->currentText() : QString();
            logEvent(QString(QStringLiteral("收到recognize_req → 按配置延迟%1ms回： %2")).arg(delay).arg(sceneText));
            QTimer::singleShot(delay, this, [this, scene] { replyScene(scene); });
        }
    } else if (type == QStringLiteral("version_query")) {
        // 3.5：收到版本查询自动回应
        QTimer::singleShot(200, this, [this] { sendVersionAckNow(); });
    } else if (type == QStringLiteral("open_req")) {
        // 开箱协议（3.7）：按回应策略配置回应——
        // 自动成功=模拟真实边缘机（摄像头异常时联动拒绝）；固定失败=纯粹回 result=0（测上位机失败分支）；
        // 不回应=静默（测上位机开箱超时重发）
        m_lastOpenRequest = frame;
        const int policy = m_openPolicy ? m_openPolicy->currentData().toInt() : 0;
        if (policy == 2) {
            logEvent(QStringLiteral("收到open_req（策略为不回应，静默以测上位机开箱超时重发）"));
        } else if (policy == 1) {
            const QString msg = m_openFailMsg ? m_openFailMsg->text() : QStringLiteral("开箱失败（模拟）");
            logEvent(QString(QStringLiteral("收到open_req → 按配置固定失败（result=0）：%1")).arg(msg));
            QTimer::singleShot(200, this, [this, msg] { sendOpenAckNow(false, msg); });
        } else {
            // 自动成功：安全策略——8 路摄像头任一异常则模拟真实边缘机拒绝开柜（回 result=0），与上位机闸门一致
            const QString abnormal = cameraAbnormalText();
            if (abnormal.isEmpty()) {
                logEvent(QStringLiteral("收到open_req → 按配置自动成功（摄像头全部正常）"));
                QTimer::singleShot(200, this, [this] { sendOpenAckNow(true, QStringLiteral("")); });
            } else {
                const QString msg = QStringLiteral("以下摄像头状态异常，禁止开柜：%1").arg(abnormal);
                logEvent(msg);
                QTimer::singleShot(200, this, [this, msg] { sendOpenAckNow(false, msg); });
            }
        }
    }
    refreshStat();
}

// ─────────────────────── 协议3.4 周期帧 ───────────────────────

void EdgeSimulator::sendPeriodicNow()
{
    sendFrame(makePeriodic(m_camList, m_uptime->value()));
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

/**
 * @brief 取当前 8 路摄像头中状态异常的摘要（非 ok 且非"不报自检"）
 * @return 形如 "CAM3(offline) CAM5(dirty)"；全部正常或均为空时返回空串
 */
QString EdgeSimulator::cameraAbnormalText() const
{
    QStringList parts;
    int pos = 0;
    for (QComboBox* c : m_camList) {
        ++pos;
        const QString st = c ? c->currentText() : kCamEmpty;
        if (st != kCamEmpty && st != QStringLiteral("ok")) {
            parts.append(QStringLiteral("CAM%1(%2)").arg(pos).arg(st));
        }
    }
    return parts.join(QChar(' '));
}

void EdgeSimulator::sendVersionAckNow()
{
    QJsonObject v;
    v["type"] = "version_ack";
    v["software_version"] = m_version->text();
    sendFrame(v);
}

void EdgeSimulator::sendOpenAckNow(bool success, const QString& message)
{
    if (!m_sock || m_sock->state() != QAbstractSocket::ConnectedState) {
        logEvent(QStringLiteral("发送open_ack失败：未连接"));
        return;
    }
    if (m_lastOpenRequest.isEmpty()) {
        logEvent(QStringLiteral("没有待回应的open_req（先等上位机发一次开箱请求）"));
        return;
    }
    QJsonObject a;
    a["type"] = "open_ack";
    a["request_id"] = m_lastOpenRequest["request_id"].toString();
    a["result"] = success ? 1 : 0;
    a["message"] = message;
    sendFrame(a);
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

/**
 * @brief 保存日志：把日志区当前内容一键导出为文本文件
 * @note  保存位置：exe 同目录优先（与启动时自动落盘一致，测试人员就地取档）；
 *        目录不可写时依次回退桌面、系统临时目录。文件名带导出时间戳，
 *        不会覆盖自动落盘的 EdgeSimulator_log.txt，多轮测试可分别留档。
 *        导出为 UTF-8 带 BOM 的纯文本，头部附软件名与导出时间；
 *        保存结果（成功含路径、失败含原因）写回日志区。
 * @return 无（日志区为空时仅提示，不产生文件）
 */
void EdgeSimulator::onSaveLogClicked()
{
    const QString text = m_logView->toPlainText();
    if (text.isEmpty()) {
        logEvent(QStringLiteral("保存失败：当前没有日志内容"));
        return;
    }
    // 目录可写探测：写一个临时探针文件再删除，避免把错误路径当可用
    auto dirWritable = [](const QString& dir) {
        if (dir.isEmpty()) {
            return false;
        }
        QFile probe(dir + QStringLiteral("/.log_save_probe"));
        if (!probe.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        probe.close();
        probe.remove();
        return true;
    };
    QString dir = QCoreApplication::applicationDirPath();
    if (!dirWritable(dir)) {
        dir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    }
    if (!dirWritable(dir)) {
        dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    const QString fileName = QString(QStringLiteral("EdgeSimulator_log_%1.txt"))
                                 .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString path = dir + QChar('/') + fileName;

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        logEvent(QString(QStringLiteral("保存日志失败：无法写入 %1（%2）")).arg(path, out.errorString()));
        return;
    }
    const QString header = QString(QStringLiteral("=== 边缘计算机模拟器 通信日志 导出时间 %1 ===\n"))
                               .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    out.write("\xEF\xBB\xBF");   // UTF-8 BOM：Windows 记事本直接打开中文不乱码
    out.write(header.toUtf8());
    out.write(text.toUtf8());
    out.close();
    logEvent(QString(QStringLiteral("日志已保存：%1")).arg(path));
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