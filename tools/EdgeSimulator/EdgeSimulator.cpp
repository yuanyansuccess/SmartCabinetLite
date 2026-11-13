/**
 * @file EdgeSimulator.cpp
 * @brief 边缘计算机模拟器实现：按协议逐接口覆盖的按钮测试
 * @author 袁燕
 *
 * 协议依据：《智能工具柜配置项通信协议》
 *   3.3  recognize_ack 场景回应与告警字段
 *   3.4  periodic 周期帧与摄像头自检5态
 *   3.5  （版本合帧：软件版本随 periodic 上报，不再单独 version_query/version_ack）
 *   3.6  配对/重发/通信异常规则（坏帧、乱序id、未知type）
 *   第5章 停发周期帧验证离线判定与恢复补发
 *   第2章 一行JSON分帧（Compact+'\n'，粘包可靠切分）
 */
#include "EdgeSimulator.h"
#include "../../src/common/PositionFormatter.h"   // 位置格式化规则与上位机共用一份，避免清单货位与单据对不上

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QSettings>
#include <QTabWidget>
#include <QTableWidget>
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
    SceneCustomList = 8, // 自定义清单（在“工具清单”页按真实单据逐行配置；追加在末尾不影响已有配置的下标含义）

    // 结构与兼容场景组（合法JSON但字段不全/类型不符，编号独立段避免与上面冲突）
    SceneStructNoItems = 20,      // 整帧缺 items 字段
    SceneStructItemsObject,       // items 是对象而非数组
    SceneStructNoPosition,       // item 缺 position 字段
    SceneStructBadType,           // item 的 position 是数字
    SceneStructNoRequestId,       // 缺 request_id 字段（丢弃计数 3.6-②）
    SceneStructUnknownType,       // type 为未知值（丢弃计数 3.6-②）
    SceneStructNoToolId,          // item 缺 tool_id 字段
    SceneStructToolIdNumber,      // tool_id 是数字
    SceneStructNoConfidence,      // item 缺 confidence 字段
    SceneStructConfidenceString,  // confidence 是字符串
    SceneStructConfidenceRange,   // confidence 超出 0~1
    SceneStructNoAlertType,       // item 缺 alert_type（上位机按 none 兼容）
    SceneStructBadAlertType,      // alert_type 不在协议枚举内
    SceneStructDuplicatePos,      // 同一货位上报两条
    SceneStructItemNotObject,     // items 元素是字符串而非对象
};

/// 相机自检"空"选项标记：选中时该相机不出现在 camera_status（协议3.4：自检可缺席）
const QString kCamEmpty = QStringLiteral("(空-不报自检)");

/// 自定义清单里不写置信度时使用的默认值（0~1）
const double kCustomDefaultConfidence = 0.95;

/// 模拟器随周期帧上报的软件版本（版本合帧：替代原 version_query/version_ack 握手）
const QString kSimVersion = QStringLiteral("1.0.0");


/// 数据库驱动名（QODBC 走 ODBC 连接串，与上位机同口径）
const QString QODBC_DRIVER_NAME = QStringLiteral("QODBC");








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
    p["software_version"] = kSimVersion;
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
 * @brief 往回应场景下拉与注入下拉填入全部结构错误/兼容场景（单一来源）
 * @param combo 目标下拉框
 * @return 无
 * @note  文案直接写出期望结果，测试人员不用查手册也知道该看什么
 */
void addStructScenes(QComboBox* combo)
{
    combo->addItem(QStringLiteral("【结构错】缺 items 字段 → 应弹核对失败、不写库"), SceneStructNoItems);
    combo->addItem(QStringLiteral("【结构错】items 是对象非数组 → 应弹核对失败、不写库"), SceneStructItemsObject);
    combo->addItem(QStringLiteral("【结构错】item 缺 position → 应弹核对失败、不写库"), SceneStructNoPosition);
    combo->addItem(QStringLiteral("【结构错】position 是数字 → 应弹核对失败、不写库"), SceneStructBadType);
    combo->addItem(QStringLiteral("【帧丢弃】缺 request_id → 上位机丢弃、约9秒超时"), SceneStructNoRequestId);
    combo->addItem(QStringLiteral("【帧丢弃】type 未知 → 上位机丢弃、约9秒超时"), SceneStructUnknownType);
    combo->addItem(QStringLiteral("【结构错】缺 tool_id（最危险）→ 应弹核对失败、不写库"), SceneStructNoToolId);
    combo->addItem(QStringLiteral("【结构错】tool_id 是数字 → 应弹核对失败、不写库"), SceneStructToolIdNumber);
    combo->addItem(QStringLiteral("【结构错】缺 confidence → 应弹核对失败、不写库"), SceneStructNoConfidence);
    combo->addItem(QStringLiteral("【结构错】confidence 是字符串 → 应弹核对失败、不写库"), SceneStructConfidenceString);
    combo->addItem(QStringLiteral("【结构错】confidence=99 越界 → 应弹核对失败、不写库"), SceneStructConfidenceRange);
    combo->addItem(QStringLiteral("【兼容】缺 alert_type → 按无告警正常核对"), SceneStructNoAlertType);
    combo->addItem(QStringLiteral("【结构错】alert_type 枚举外 → 应弹核对失败、不写库"), SceneStructBadAlertType);
    combo->addItem(QStringLiteral("【结构错】同一货位重复上报 → 应弹核对失败、不写库"), SceneStructDuplicatePos);
    combo->addItem(QStringLiteral("【结构错】items 元素是字符串 → 应弹核对失败、不写库"), SceneStructItemNotObject);
}

/**
 * @brief 按结构错误/兼容场景构造 recognize_ack 回应帧（协议3.3）
 * @param kind    场景枚举（SceneStruct* 段）
 * @param request 收到的 recognize_req，用于回带 request_id
 * @return 单帧 recognize_ack；缺字段的项不经过字段规整
 * @note  不合规帧应让上位机弹【核对失败】且不写库；缺 alert_type 属兼容帧，应正常核对。
 */
QJsonObject structErrorAck(int kind, const QJsonObject& request)
{
    // 一条字段齐全的合法识别项：各结构错误场景只在它上面破坏一处，便于逐项对照
    const auto baseItem = []() {
        QJsonObject it;
        it["position"] = "A-01-05";
        it["tool_id"] = "C01-T0007";
        it["confidence"] = 0.9;
        it["alert_type"] = "none";
        return it;
    };
    const auto oneItemAck = [&](const QJsonObject& it) {
        QJsonArray arr;
        arr.append(it);
        return arr;
    };
    QJsonObject ack;
    ack["type"] = "recognize_ack";
    switch (kind) {
    case SceneStructNoItems: {        // 整帧缺 items 字段
        ack["request_id"] = request["request_id"];
        // 故意不设 items → 上位机判结构不合规，弹核对失败且不写库
        break;
    }
    case SceneStructItemsObject: {    // items 是对象而非数组
        ack["request_id"] = request["request_id"];
        QJsonObject fake;
        fake["position"] = "A-01-05";
        fake["tool_id"] = "C01-T0007";
        ack["items"] = fake;
        break;
    }
    case SceneStructNoPosition: {     // item 缺 position
        ack["request_id"] = request["request_id"];
        QJsonArray items;
        QJsonObject it;
        it["tool_id"] = "C01-T0007";
        it["confidence"] = 0.9;
        it["alert_type"] = "none";
        items.append(it);
        ack["items"] = items;
        break;
    }
    case SceneStructBadType: {        // position 是数字（类型错）
        ack["request_id"] = request["request_id"];
        QJsonArray items;
        QJsonObject it;
        it["position"] = 12345;       // 数字而非字符串
        it["tool_id"] = "C01-T0007";
        it["confidence"] = 0.9;
        it["alert_type"] = "none";
        items.append(it);
        ack["items"] = items;
        break;
    }
    case SceneStructNoRequestId: {    // 缺 request_id
        QJsonArray items;
        QJsonObject it;
        it["position"] = "A-01-05"; it["tool_id"] = "C01-T0007";
        it["confidence"] = 0.9; it["alert_type"] = "none";
        items.append(it);
        ack["items"] = items;
        // 故意不设 request_id → 上位机 m_pending 查不到，丢弃计数
        break;
    }
    case SceneStructUnknownType: {     // type 未知
        ack["type"] = "weird_type";
        ack["request_id"] = request["request_id"];
        QJsonArray items;
        QJsonObject it;
        it["position"] = "A-01-05"; it["tool_id"] = "C01-T0007";
        it["confidence"] = 0.9; it["alert_type"] = "none";
        items.append(it);
        ack["items"] = items;
        break;
    }
    case SceneStructNoToolId: {        // 缺 tool_id：拒绝整帧，避免把空编号当成"货位已空"
        ack["request_id"] = request["request_id"];
        QJsonObject it = baseItem();
        it.remove("tool_id");
        ack["items"] = oneItemAck(it);
        break;
    }
    case SceneStructToolIdNumber: {    // tool_id 是数字
        ack["request_id"] = request["request_id"];
        QJsonObject it = baseItem();
        it["tool_id"] = 12345;
        ack["items"] = oneItemAck(it);
        break;
    }
    case SceneStructNoConfidence: {    // 缺 confidence
        ack["request_id"] = request["request_id"];
        QJsonObject it = baseItem();
        it.remove("confidence");
        ack["items"] = oneItemAck(it);
        break;
    }
    case SceneStructConfidenceString: { // confidence 是字符串
        ack["request_id"] = request["request_id"];
        QJsonObject it = baseItem();
        it["confidence"] = QStringLiteral("0.9");
        ack["items"] = oneItemAck(it);
        break;
    }
    case SceneStructConfidenceRange: {  // confidence 超出 0~1
        ack["request_id"] = request["request_id"];
        QJsonObject it = baseItem();
        it["confidence"] = 99;
        ack["items"] = oneItemAck(it);
        break;
    }
    case SceneStructNoAlertType: {     // 缺 alert_type：上位机按 none 兼容
        ack["request_id"] = request["request_id"];
        QJsonObject it = baseItem();
        it.remove("alert_type");
        ack["items"] = oneItemAck(it);
        break;
    }
    case SceneStructBadAlertType: {    // alert_type 不在协议枚举内
        ack["request_id"] = request["request_id"];
        QJsonObject it = baseItem();
        it["alert_type"] = QStringLiteral("banana");
        ack["items"] = oneItemAck(it);
        break;
    }
    case SceneStructDuplicatePos: {    // 同一货位上报两条
        ack["request_id"] = request["request_id"];
        QJsonArray arr;
        arr.append(baseItem());
        QJsonObject second = baseItem();
        second["tool_id"] = QStringLiteral("C01-T0099");
        arr.append(second);
        ack["items"] = arr;
        break;
    }
    case SceneStructItemNotObject: {   // items 元素是字符串而非对象
        ack["request_id"] = request["request_id"];
        QJsonArray arr;
        arr.append(QStringLiteral("A-01-05"));
        ack["items"] = arr;
        break;
    }
    default:
        break;
    }
    return ack;
}

/**
 * @brief 按回应场景构造 recognize_ack 帧（协议3.3全部场景）
 * @param scene   场景枚举
 * @param request 收到的 recognize_req（取 request_id 回带）
 * @return recognize_ack 帧对象
 */
QJsonObject sceneAckFrame(int scene, const QJsonObject& request)
{
    // 结构与兼容场景：单独构造后直接返回（绕过下方字段规整）
    if (scene >= SceneStructNoItems && scene <= SceneStructItemNotObject) {
        return structErrorAck(scene, request);
    }
    QJsonObject ack;
    ack["type"] = "recognize_ack";
    ack["request_id"] = request["request_id"];
    QJsonArray items;

    switch (scene) {
    case SceneNormal: {  // 正常：2条样例
        QJsonObject it1;
        it1["position"] = "A-01-05"; it1["tool_id"] = "C01-T0007";
        it1["confidence"] = 0.93;
        items.append(it1);
        QJsonObject it2;
        it2["position"] = "A-02-01"; it2["tool_id"] = "C01-T0012";
        it2["confidence"] = 0.88;
        items.append(it2);
        break;
    }
    case SceneBorrowReturn: {  // 3.3-① 有借有还：A-01-05已取走（空货位照样回报），A-01-06放回套筒
        QJsonObject empty;
        empty["position"] = "A-01-05"; empty["tool_id"] = "";
        empty["confidence"] = 0.91;
        items.append(empty);
        QJsonObject it;
        it["position"] = "A-01-06"; it["tool_id"] = "C01-T0012";
        it["confidence"] = 0.91;
        items.append(it);
        break;
    }
    case SceneNoChange:        // 3.3-② 无变化：空清单
        break;
    case SceneMisplaced: {     // 3.3-③ 错放：套筒出现在A-02-01
        QJsonObject it;
        it["position"] = "A-02-01"; it["tool_id"] = "C01-T0012";
        it["confidence"] = 0.88;
        it["alert_type"] = "misplaced";
        items.append(it);
        break;
    }
    case SceneUnknown: {       // 告警表：位置上是未知物
        QJsonObject it;
        it["position"] = "A-01-06"; it["tool_id"] = "";
        it["confidence"] = 0.71;
        it["alert_type"] = "unknown";
        items.append(it);
        break;
    }
    case SceneLowConf: {       // 告警表：置信度不足
        QJsonObject it;
        it["position"] = "A-01-05"; it["tool_id"] = "C01-T0007";
        it["confidence"] = 0.55;
        it["alert_type"] = "low_confidence";
        items.append(it);
        break;
    }
    case SceneMissing: {       // alert_type=missing：协议字段枚举边界值（台账有、识别为空）
        QJsonObject it;
        it["position"] = "A-01-05"; it["tool_id"] = "C01-T0007";
        it["confidence"] = 0.0;
        it["alert_type"] = "missing";
        items.append(it);
        break;
    }
    default:
        break;
    }
    // 内置场景同样输出字段齐全的清单，与自定义清单保持同一口径，
    // 避免两种回应形态让上位机走两套判定逻辑
    for (int i = 0; i < items.size(); ++i) {
        QJsonObject it = items.at(i).toObject();
        if (!it.contains(QStringLiteral("confidence"))) {
            it[QStringLiteral("confidence")] = kCustomDefaultConfidence;
        }
        if (!it.contains(QStringLiteral("alert_type"))) {
            it[QStringLiteral("alert_type")] = QStringLiteral("none");
        }
        items.replace(i, it);
    }
    ack["items"] = items;
    return ack;
}
}  // namespace

EdgeSimulator::EdgeSimulator(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("边缘计算机模拟器 — 出入库联调工作台"));
    resize(1400, 900);

    // 日志落盘：优先 exe 同目录，失败回退系统临时目录（放在最前面，构造期的提示也能落盘）
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

    // 顶部：上位机连接区 + 一键标准联调
    auto* topBar = new QHBoxLayout;
    topBar->addWidget(buildConnPanel());
    m_btnOneClick = new QPushButton(QStringLiteral("一键标准联调"));
    m_btnOneClick->setToolTip(QStringLiteral("连上位机 + 读库 + 生成本机组全部已登记货位清单 + 切到自定义清单 + 开周期帧"));
    connect(m_btnOneClick, &QPushButton::clicked, this, &EdgeSimulator::onOneClickStart);
    topBar->addWidget(m_btnOneClick);
    topBar->addStretch(1);

    // 左侧步骤导航 + 右侧工作区（每一步一页）
    m_stepNav = new QListWidget;
    m_stepNav->setFixedWidth(170);
    m_stepNav->setToolTip(QStringLiteral("① 连接与数据 ② 单据模拟 ③ 识别回应 ④ 异常注入"));
    connect(m_stepNav, &QListWidget::currentRowChanged, this, &EdgeSimulator::onStepClicked);
    m_stack = new QStackedWidget;
    m_stack->addWidget(buildConnectDataPage());
    m_stack->addWidget(buildOrderPage());
    m_stack->addWidget(buildRespondPage());
    m_stack->addWidget(buildExceptionPage());
    buildStepNav();

    m_logView = new QTextEdit;
    m_logView->setReadOnly(true);
    m_logView->setFontFamily(QStringLiteral("Consolas"));
    m_logView->setMinimumHeight(120);
    m_logView->setMaximumHeight(210);

    m_stat = new QLabel(QStringLiteral("收帧 0 | 发帧 0 | 坏帧 0"));

    auto* root = new QVBoxLayout(this);
    root->addLayout(topBar);
    m_globalHint = new QLabel;
    m_globalHint->setWordWrap(true);
    root->addWidget(m_globalHint);

    auto* middle = new QHBoxLayout;
    middle->addWidget(m_stepNav);
    // 工作区套滚动区：窗口变小或内容变多时页面按内容高度滚动，
    // 不会把清单表压扁、也不会让按钮行和表格叠在一起
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(m_stack);
    middle->addWidget(scroll, 1);
    root->addLayout(middle, 1);

    // 底部：通信日志（横贯窗口，收发分色）
    auto* logBar = new QHBoxLayout;
    logBar->addWidget(new QLabel(QStringLiteral("通信日志")));
    logBar->addWidget(m_stat);
    logBar->addStretch(1);
    m_btnSaveLog = new QPushButton(QStringLiteral("保存日志"));
    m_btnSaveLog->setToolTip(QStringLiteral("把日志区当前内容保存为带时间戳的文本文件（exe 同目录，不可写时自动回退桌面）"));
    connect(m_btnSaveLog, &QPushButton::clicked, this, &EdgeSimulator::onSaveLogClicked);
    logBar->addWidget(m_btnSaveLog);
    root->addLayout(logBar);
    root->addWidget(m_logView);

    connect(&m_periodicTimer, &QTimer::timeout, this, [this] {
        sendFrame(makePeriodic(m_camList, m_uptime->value()));
    });
    connect(&m_loopTimer, &QTimer::timeout, this, &EdgeSimulator::sendCustomFrame);

    // 全部界面构建完成后，恢复上次的回应配置（连接前即可完成配置，符合"配置好再连"的使用方式）
    loadConfig();
    refreshOrderStat();   // 恢复配置后重刷清单统计与生效提示
    updateSceneHint();            // 启动就把"清单是否生效"摆在页顶，不用等到点了才知道
    // 启动即读库：延到事件循环起来后再连，避免界面还没出现就被同步查询卡住
    if (m_dbLoadOnStart) {
        QTimer::singleShot(0, this, &EdgeSimulator::onLoadToolsFromDb);
    }
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
 * @note  覆盖：8 路摄像头状态/周期帧参数/识别场景与反馈响应时间/帧 JSON 文本/开箱策略与失败文案/版本号/连接参数；
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
    s.setValue(QStringLiteral("conn/addr"), m_addr ? m_addr->text() : QStringLiteral("127.0.0.1"));
    s.setValue(QStringLiteral("conn/port"), m_port ? m_port->value() : 9000);
    s.setValue(QStringLiteral("conn/autoReconnect"), m_autoReconn ? m_autoReconn->isChecked() : true);
    // 识别回应序列：有序场景枚举值列表 + 启用开关（连接前配好即可复用）
    s.beginWriteArray(QStringLiteral("reply/seq"));
    for (int i = 0; i < (m_seqList ? m_seqList->count() : 0); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("scene"), m_seqList->item(i)->data(Qt::UserRole).toInt());
    }
    s.endArray();
    s.setValue(QStringLiteral("reply/seqEnabled"), m_seqEnabled ? m_seqEnabled->isChecked() : false);
    // 发送帧 JSON：整段文本一起存，下次启动原样恢复（含手工改过的结构）
    s.setValue(QStringLiteral("reply/customFrame"), m_frameText ? m_frameText->toPlainText() : QString());
    // 清单选数据用的数据库连接参数（测试现场换个库只改这里）
    s.setValue(QStringLiteral("db/host"), m_dbHost ? m_dbHost->text() : QStringLiteral("127.0.0.1"));
    s.setValue(QStringLiteral("db/port"), m_dbPort ? m_dbPort->value() : 3306);
    s.setValue(QStringLiteral("db/name"), m_dbName ? m_dbName->text() : QStringLiteral("smart_cabinet"));
    s.setValue(QStringLiteral("db/user"), m_dbUser ? m_dbUser->text() : QStringLiteral("root"));
    // 口令写入 ini：测试工具连的是本机联调库，口令固定 root/root，
    // 写成明文是刻意取舍——目的是让测试人员拿到工具直接连库，不在界面上手输口令
    s.setValue(QStringLiteral("db/password"), m_dbPass ? m_dbPass->text() : QStringLiteral("root"));
    // 启动即读库并把工具/货位填进选择框：测试人员不用先点一次按钮就能选数据
    s.setValue(QStringLiteral("db/loadOnStart"), m_dbLoadOnStart);
    s.setValue(QStringLiteral("db/groupId"), m_selectedGroupId);
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
    m_addr->setText(s.value(QStringLiteral("conn/addr"), QStringLiteral("127.0.0.1")).toString());
    m_port->setValue(s.value(QStringLiteral("conn/port"), 9000).toInt());
    m_autoReconn->setChecked(s.value(QStringLiteral("conn/autoReconnect"), true).toBool());
    // 识别回应序列：读回有序场景列表与启用开关；游标归零（每次启动从首项开始）
    m_seqList->clear();
    const int seqN = s.beginReadArray(QStringLiteral("reply/seq"));
    for (int i = 0; i < seqN; ++i) {
        s.setArrayIndex(i);
        addSeqScene(s.value(QStringLiteral("scene")).toInt());
    }
    s.endArray();
    m_seqEnabled->setChecked(s.value(QStringLiteral("reply/seqEnabled"), false).toBool());
    m_seqCursor = 0;
    // 发送帧 JSON：读回上次编辑的内容（缺项时保留页内默认帧）
    const QString savedFrame = s.value(QStringLiteral("reply/customFrame")).toString();
    if (!savedFrame.isEmpty()) {
        loadFrameText(savedFrame);
        m_frameEdited = true;   // 恢复的是上次手工改过的帧，启动刷新不得覆盖
    }
    // 数据库连接参数：恢复上次填的，现场换库不必每次重填
    m_dbHost->setText(s.value(QStringLiteral("db/host"), QStringLiteral("127.0.0.1")).toString());
    m_dbPort->setValue(s.value(QStringLiteral("db/port"), 3306).toInt());
    m_dbName->setText(s.value(QStringLiteral("db/name"), QStringLiteral("smart_cabinet")).toString());
    m_dbUser->setText(s.value(QStringLiteral("db/user"), QStringLiteral("root")).toString());
    // 口令：ini 里没有就按本机联调库默认口令 root 填上，测试人员无需手输
    m_dbPass->setText(s.value(QStringLiteral("db/password"), QStringLiteral("root")).toString());
    m_dbLoadOnStart = s.value(QStringLiteral("db/loadOnStart"), true).toBool();
    if (m_chkLoadOnStart) {
        m_chkLoadOnStart->setChecked(m_dbLoadOnStart);
    }
    // 默认按本机机组（与上位机 system.ini 的 Local/group_id 默认 1 对齐）推送识别清单，
    // 避免模拟器全量推清单、而上位机只按本机组核对时，别的机组货位被误报成"来源不明"。
    // 测试人员仍可在下拉里选"全部机组"或其他机组做全量/跨机组联调
    m_selectedGroupId = s.value(QStringLiteral("db/groupId"), 1).toInt();
}

/**
 * @brief 指定要连接的上位机地址（命令行 --host/--port 用，优先于配置文件）
 * @param host 上位机 IP 或主机名
 * @param port 上位机监听端口
 * @return 无。界面上的地址与端口同步更新
 * @note  命令行参数不生效会让跨电脑联调连错机器，这里显式覆盖配置值
 */
void EdgeSimulator::setPeerAddress(const QString& host, int port)
{
    if (m_addr && !host.isEmpty()) {
        m_addr->setText(host);
    }
    if (m_port && port > 0 && port <= 65535) {
        m_port->setValue(port);
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
 * @param index 步骤页序号：0=连接与数据 1=单据模拟 2=识别回应 3=异常注入
 * @note  供 --tab 命令行与截图自动化调用
 */
void EdgeSimulator::showTab(int index)
{
    logEvent(QStringLiteral("切换步骤页：请求 %1，当前页数 %2").arg(index)
                 .arg(m_stack ? m_stack->count() : 0));
    if (m_stack && index >= 0 && index < m_stack->count()) {
        m_stack->setCurrentIndex(index);
    }
    if (m_stepNav && index >= 0 && index < m_stepNav->count()) {
        m_stepNav->setCurrentRow(index);
    }
}

// ─────────────────────── 界面构建 ───────────────────────

/// 构建"连接上位机"配置面板：包含地址/端口/库名/账号/口令输入框与"连接并读取工具与货位"按钮
/// @return 组装完成的面板控件（调用方负责显示与其生命周期）
QWidget* EdgeSimulator::buildConnPanel()
{
    auto* box = new QGroupBox(QStringLiteral("连接上位机"));
    auto* g = new QHBoxLayout(box);

    m_addr = new QLineEdit(QStringLiteral("127.0.0.1"));
    m_addr->setMaximumWidth(150);
    m_port = new QSpinBox;
    m_port->setRange(1, 65535);
    m_port->setValue(9000);
    m_port->setMaximumWidth(90);
    m_btnConn = new QPushButton(QStringLiteral("连接"));
    m_state = new QLabel(QStringLiteral("● 未连接"));
    m_state->setStyleSheet(QStringLiteral("color:#888;font-weight:bold;"));
    m_autoReconn = new QCheckBox(QStringLiteral("断开后自动重连（1/2/4/8/16/30秒退避）"));
    m_autoReconn->setChecked(true);
    connect(m_btnConn, &QPushButton::clicked, this, &EdgeSimulator::onConnectToggle);

    g->addWidget(new QLabel(QStringLiteral("IP")));
    g->addWidget(m_addr);
    g->addWidget(new QLabel(QStringLiteral("端口")));
    g->addWidget(m_port);
    g->addWidget(m_btnConn);
    g->addWidget(m_state);
    g->addWidget(m_autoReconn);
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
    auto* camTip = new QLabel(QStringLiteral("8 路摄像头（CAM1..CAM8）状态含义：\nok=正常 offline=掉线 low_light=光照不足\n"
                                             "dirty=镜头脏污 comm_error=通信异常\n(空-不报自检)：该相机不出现在清单，全部选空则 camera_status 为空数组\n"
                                             "安全策略：任一状态非 ok → 上位机与模拟器两端都拒绝开柜"));
    camTip->setWordWrap(true);
    camTip->setMaximumWidth(460);
    g->addWidget(camTip, rowUptime + 3, 1, 1, 1);
    g->setRowStretch(rowUptime + 4, 1);
    return w;
}

QWidget* EdgeSimulator::buildRecognizeTab()
{
    auto* w = new QWidget;
    auto* g = new QGridLayout(w);

    // ── 自动回应配置区（连接前配好，连接后即改即生效；下一条请求按当前配置回）──
    auto* cfgTitle = new QLabel(QStringLiteral(
        "识别自动回应配置（协议3.3 · 连接前后可随时切换，下一条请求即生效）\n"
        "内置场景是协议样例帧，不读库；要回报本机组全部已登记货位请用②页【按本单生成清单】或【一键标准联调】"));
    cfgTitle->setWordWrap(true);
    cfgTitle->setStyleSheet(QStringLiteral("font-weight:bold;color:#333;"));
    g->addWidget(cfgTitle, 0, 1, 1, 1);

    m_recScene = new QComboBox;
    // 下拉项文字较长：显式限定宽度上限，避免把整页撑宽导致右侧控件被裁掉
    m_recScene->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_recScene->setMinimumContentsLength(16);
    m_recScene->setMinimumWidth(260);
    m_recScene->setMaximumWidth(460);
    // 下拉选项文字=测试含义，itemData=Scene 枚举值；顺序与协议场景清单一致
    m_recScene->addItem(QStringLiteral("正常清单（2条样例，测消息配对）"),        SceneNormal);
    m_recScene->addItem(QStringLiteral("有借有还（A-01-05空+A-01-06套筒）"),      SceneBorrowReturn);
    m_recScene->addItem(QStringLiteral("无变化（空清单，测归档分支）"),           SceneNoChange);
    m_recScene->addItem(QStringLiteral("错放告警（misplaced L2，测告警提示）"),   SceneMisplaced);
    m_recScene->addItem(QStringLiteral("未知物告警（unknown L3，测严重告警）"),   SceneUnknown);
    m_recScene->addItem(QStringLiteral("置信度不足（low_confidence L1，测不落账）"), SceneLowConf);
    m_recScene->addItem(QStringLiteral("缺失告警（missing，测字段枚举边界）"),  SceneMissing);
    m_recScene->addItem(QStringLiteral("不回应（测上位机识别超时重发，第5章）"),   SceneSilent);
    m_recScene->addItem(QStringLiteral("自定义清单（在“工具清单”页按单据逐行配置）"), SceneCustomList);
    // 结构与兼容场景：选一项后，上位机发起识别核对时自动回一帧
    addStructScenes(m_recScene);
    g->addWidget(new QLabel(QStringLiteral("回应场景")), 1, 0);
    g->addWidget(m_recScene, 1, 1);

    // 响应时间与清单同页设置（②页），这里只给入口指引
    auto* delayHint = new QLabel(QStringLiteral("识别清单反馈响应时间在②页设置（只作用于识别清单回应）"));
    delayHint->setWordWrap(true);
    g->addWidget(delayHint, 2, 1);

    // ── 手动注入区（一次性动作，补充自动配置覆盖不到的协议异常）──
    auto* manTitle = new QLabel(QStringLiteral("手动注入（一次性动作）"));
    manTitle->setStyleSheet(QStringLiteral("font-weight:bold;color:#333;margin-top:8px;"));
    g->addWidget(manTitle, 3, 1, 1, 1);
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
    g->addWidget(btnDup, 4, 1, 1, 1);
    g->addWidget(btnFlood, 5, 1, 1, 1);

    // ── 链路离线模拟区（第5章）──
    auto* linkTitle = new QLabel(QStringLiteral("链路离线模拟（第5章）"));
    linkTitle->setStyleSheet(QStringLiteral("font-weight:bold;color:#333;margin-top:8px;"));
    g->addWidget(linkTitle, 6, 1, 1, 1);
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
    g->addWidget(btnDrop, 7, 1, 1, 1);
    g->addWidget(m_silenceAt, 8, 0);
    g->addWidget(m_silenceFor, 8, 1);
    // 链路离线模拟当前不在界面开放：逻辑保留（停发计时仍生效），四个控件统一隐藏，
    // 需要恢复界面时删掉下面四行 hide 即可
    linkTitle->hide();
    btnDrop->hide();
    m_silenceAt->hide();
    m_silenceFor->hide();

    // ── 识别回应序列（多轮流程测试：按序回放，播完回落单场景默认）──
    auto* seqBox = new QGroupBox(QStringLiteral("识别回应序列（多轮流程测试：按序回放，播完回落单场景默认）"));
    auto* seqV = new QVBoxLayout(seqBox);
    m_seqEnabled = new QCheckBox(QStringLiteral("启用序列"));
    m_seqEnabled->setToolTip(QStringLiteral("每条 recognize_req 依次取序列下一项，播完后回落单场景默认"));
    m_seqEnabled->setMaximumWidth(140);
    m_seqList = new QListWidget;
    m_seqList->setSelectionMode(QListWidget::SingleSelection);
    m_seqList->setToolTip(QStringLiteral("序列按顺序逐条回放：第1次识别请求用第1项，第2次用第2项…播完回单场景默认。配合『三轮重开柜』等流程可一键闭环"));
    auto* btnAdd  = new QPushButton(QStringLiteral("添加当前场景"));
    auto* btnUp   = new QPushButton(QStringLiteral("上移"));
    auto* btnDown = new QPushButton(QStringLiteral("下移"));
    auto* btnDel  = new QPushButton(QStringLiteral("删除"));
    auto* btnClear= new QPushButton(QStringLiteral("清空"));
    connect(btnAdd, &QPushButton::clicked, this, [this] {
        if (m_recScene) addSeqScene(m_recScene->currentData().toInt());
    });
    connect(btnUp, &QPushButton::clicked, this, [this] {
        const int r = m_seqList->currentRow();
        if (r > 0) { m_seqList->insertItem(r - 1, m_seqList->takeItem(r)); m_seqList->setCurrentRow(r - 1); }
    });
    connect(btnDown, &QPushButton::clicked, this, [this] {
        const int r = m_seqList->currentRow();
        if (r >= 0 && r < m_seqList->count() - 1) { m_seqList->insertItem(r + 1, m_seqList->takeItem(r)); m_seqList->setCurrentRow(r + 1); }
    });
    connect(btnDel, &QPushButton::clicked, this, [this] {
        const int r = m_seqList->currentRow();
        if (r >= 0) delete m_seqList->takeItem(r);
    });
    connect(btnClear, &QPushButton::clicked, this, [this] { m_seqList->clear(); m_seqCursor = 0; });
    // 重新启用序列时游标归零，保证每次从首项开始回放
    connect(m_seqEnabled, &QCheckBox::toggled, this, [this](bool on) { if (on) m_seqCursor = 0; });
    auto* seqBtnRow = new QHBoxLayout;
    seqBtnRow->addWidget(btnAdd); seqBtnRow->addWidget(btnUp);
    seqBtnRow->addWidget(btnDown); seqBtnRow->addWidget(btnDel); seqBtnRow->addWidget(btnClear);
    seqV->addWidget(m_seqEnabled);
    seqV->addWidget(m_seqList);
    seqV->addLayout(seqBtnRow);
    g->addWidget(seqBox, 9, 1, 1, 1);
    g->addWidget(m_stat, 10, 1, 1, 1);
    g->setRowStretch(11, 1);
    return w;
}

/**
 * @brief 构建"开箱回应"面板（协议3.7）
 * @return 组装完成的面板控件
 * @note  放在"识别回应"页顶部：上位机发 open_req、边缘机按配置回应的配置项，
 *        与识别回应放在一起，测试人员不用翻到异常页去找。
 *        另有两枚立即发送按钮，不依赖下一条 open_req，直接按最近一次请求编号回 open_ack。
 *        注：原协议3.5版本查询已取消，软件版本随 periodic 周期帧上报，不再单独回 version_ack。
 */
QWidget* EdgeSimulator::buildOpenPanel()
{
    auto* box = new QGroupBox(QStringLiteral("开箱回应 3.7（按配置自动回应，或点按钮立即回应）"));
    auto* g = new QGridLayout(box);

    // ── 开箱回应（3.7）──
    auto* openTitle = new QLabel(QStringLiteral("开箱回应（协议3.7：下一条 open_req 按策略回应，或点下方按钮立即回应）"));
    openTitle->setStyleSheet(QStringLiteral("font-weight:bold;color:#333;"));
    g->addWidget(openTitle, 0, 1, 1, 1);
    m_openPolicy = new QComboBox;
    m_openPolicy->addItem(QStringLiteral("自动成功（摄像头异常时联动拒绝，与真实边缘机一致）"), 0);
    m_openPolicy->addItem(QStringLiteral("固定失败（回 result=0，测上位机开箱失败分支）"), 1);
    m_openPolicy->addItem(QStringLiteral("不回应（测上位机开箱超时重发，第5章）"), 2);
    m_openPolicy->setToolTip(QStringLiteral("上位机点确认后会下发开箱请求，这里决定模拟器怎么回；"
                                            "下方两枚立即发送按钮不受本策略约束"));
    // 下拉项文字较长：显式限定宽度上限，避免把整页撑宽导致右侧按钮被裁掉
    m_openPolicy->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_openPolicy->setMinimumContentsLength(16);
    m_openPolicy->setMinimumWidth(260);
    m_openPolicy->setMaximumWidth(420);
    g->addWidget(new QLabel(QStringLiteral("回应策略")), 1, 0);
    g->addWidget(m_openPolicy, 1, 1, 1, 1);
    m_openFailMsg = new QLineEdit(QStringLiteral("开箱失败（模拟）"));
    m_openFailMsg->setToolTip(QStringLiteral("策略为“固定失败”或点【回开柜失败】时 open_ack 携带的原因文案"));
    g->addWidget(new QLabel(QStringLiteral("失败原因文案")), 2, 0);
    g->addWidget(m_openFailMsg, 2, 1, 1, 1);

    // 立即发送：主动回一条 open_ack，不必等上位机再发一次开箱请求
    auto* btnOpenOk = new QPushButton(QStringLiteral("回开柜成功"));
    connect(btnOpenOk, &QPushButton::clicked, this, &EdgeSimulator::onSendOpenSuccessClicked);
    btnOpenOk->setToolTip(QStringLiteral("立即发送 open_ack（result=1）：编号取最近一次 open_req"));
    auto* btnOpenFail = new QPushButton(QStringLiteral("回开柜失败"));
    connect(btnOpenFail, &QPushButton::clicked, this, &EdgeSimulator::onSendOpenFailureClicked);
    btnOpenFail->setToolTip(QStringLiteral("立即发送 open_ack（result=0）：原因取上方文案，编号取最近一次 open_req"));
    auto* sendRow = new QHBoxLayout;
    sendRow->addWidget(btnOpenOk);
    sendRow->addWidget(btnOpenFail);
    sendRow->addStretch(1);
    g->addLayout(sendRow, 3, 0, 1, 2);
    g->setColumnStretch(1, 1);   // 多余宽度给控件列，按钮列不被推走
    return box;
}

/**
 * @brief 构建"异常注入"页（协议3.6，一次性动作）
 * @return 组装完成的页面控件
 */
QWidget* EdgeSimulator::buildFaultTab()
{
    auto* w = new QWidget;
    auto* g = new QGridLayout(w);

    // ── 异常注入区（协议3.6，一次性动作）──
    auto* injTitle = new QLabel(QStringLiteral("异常注入（协议3.6 · 一次性动作）"));
    injTitle->setStyleSheet(QStringLiteral("font-weight:bold;color:#333;"));
    g->addWidget(injTitle, 0, 0, 1, 2);
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
    g->addWidget(btnBad, 1, 0, 1, 2);
    g->addWidget(btnUnknown, 2, 0, 1, 2);
    g->addWidget(btnOver, 3, 0, 1, 2);
    g->addWidget(btnBadId, 4, 0, 1, 2);
    g->addWidget(btnDisc, 5, 0, 1, 2);

    // 识别清单结构校验：下拉选错误类型后立即回一帧，不必等上位机重新建单
    auto* structTitle = new QLabel(QStringLiteral("识别清单结构校验（协议3.3）"));
    structTitle->setStyleSheet(QStringLiteral("font-weight:bold;color:#333;"));
    g->addWidget(structTitle, 6, 0, 1, 2);
    auto* structCombo = new QComboBox;
    addStructScenes(structCombo);
    g->addWidget(structCombo, 7, 0, 1, 2);
    auto* btnStruct = new QPushButton(QStringLiteral("立即注入一帧（替换本次识别回应）"));
    connect(btnStruct, &QPushButton::clicked, this, [this, structCombo] {
        if (!m_sock || m_sock->state() != QAbstractSocket::ConnectedState) {
            logEvent(QStringLiteral("注入失败：尚未连接上位机"));
            return;
        }
        if (m_lastRequest.isEmpty()) {
            logEvent(QStringLiteral("注入失败：还没有收到 recognize_req，请先在上位机发起一次识别核对"));
            return;
        }
        const int kind = structCombo->currentData().toInt();
        logEvent(QStringLiteral("立即注入：") + structCombo->currentText());
        sendFrame(structErrorAck(kind, m_lastRequest));
    });
    g->addWidget(btnStruct, 8, 0, 1, 2);
    auto* btnStructToEditor = new QPushButton(QStringLiteral("载入到编辑框（改完再发，不立即发）"));
    btnStructToEditor->setToolTip(QStringLiteral("把选中的结构错帧填到自定义帧编辑框，字段与格式都能改，改完点发送"));
    connect(btnStructToEditor, &QPushButton::clicked, this, [this, structCombo] {
        const int kind = structCombo->currentData().toInt();
        exportFrameToEditor(structErrorAck(kind, m_lastRequest));
    });
    g->addWidget(btnStructToEditor, 9, 0, 1, 2);
    auto* structHint = new QLabel(QStringLiteral(
        "③页选【结构错】场景，可在上位机发起核对时自动回一帧；本按钮用于请求已发出时立刻补一帧。\n"
        "配合③页【不回应】或较长延迟使用；请求已被回应配对后再注入会被上位机按重复编号丢弃。\n"
        "想自己改帧内容，点【载入到编辑框】改完再发；结构不合规时上位机应弹【核对失败】且不写库。"));
    structHint->setWordWrap(true);
    g->addWidget(structHint, 10, 0, 1, 2);
    g->setRowStretch(11, 1);
    return w;
}

/**
 * @brief 构建"自定义帧"子页：编辑框内容就是最终发出的帧，提供载入与排版工具
 * @return 组装完成的页面控件
 * @note  预制场景、清单表、结构错帧都能载入编辑框作为起点，改完点发送才真正发出
 */
QWidget* EdgeSimulator::buildCustomTab()
{
    auto* w = new QWidget;
    auto* g = new QGridLayout(w);

    m_custom = new QTextEdit;
    m_custom->setPlainText(QStringLiteral(
        "{\n"
        "    \"type\": \"recognize_ack\",\n"
        "    \"request_id\": \"{{request_id}}\",\n"
        "    \"items\": [\n"
        "        { \"position\": \"A-01-05\", \"tool_id\": \"C01-T0007\", \"confidence\": 0.9, \"alert_type\": \"none\" }\n"
        "    ]\n"
        "}"));
    m_custom->setToolTip(QStringLiteral("这里的内容就是最终发出的帧：字段、取值、排版都能改，改完点【发送】"));

    m_rawSend = new QCheckBox(QStringLiteral("原文发送（不校验JSON，可发坏帧/任意文本）"));
    m_rawSend->setToolTip(QStringLiteral("勾选后按文本原样发出（含换行拼包），用于坏帧与粘包组合测试"));

    m_loopMs = new QSpinBox;
    m_loopMs->setRange(100, 600000);
    m_loopMs->setValue(3000);
    m_loopMs->setSuffix(QStringLiteral(" ms"));
    m_btnLoop = new QPushButton(QStringLiteral("启动循环发送"));
    connect(m_btnLoop, &QPushButton::clicked, this, [this] { toggleLoopSend(m_loopTimer.isActive() == false); });

    auto* btn = new QPushButton(QStringLiteral("发送（编辑框所见即所发）"));
    connect(btn, &QPushButton::clicked, this, &EdgeSimulator::sendCustomFrame);

    auto* btnFromScene = new QPushButton(QStringLiteral("载入③页当前场景帧"));
    btnFromScene->setToolTip(QStringLiteral("把③页下拉选中的场景生成一帧填到这里，只当起点，改完再发"));
    connect(btnFromScene, &QPushButton::clicked, this, [this] {
        const int scene = m_recScene ? m_recScene->currentData().toInt()
                                     : static_cast<int>(SceneNormal);
        QJsonObject ack = sceneAckFrame(scene, m_lastRequest);
        if (scene == SceneCustomList) {
            QJsonObject editorFrame;
            if (m_frameText && parseFrameText(m_frameText->toPlainText(), editorFrame)) {
                ack = editorFrame;
            } else {
                logEvent(QStringLiteral("②页帧 JSON 解析失败，已改用空白帧模板；请先修好②页帧"));
            }
        }
        exportFrameToEditor(ack);
    });

    auto* btnBlank = new QPushButton(QStringLiteral("空白帧模板"));
    btnBlank->setToolTip(QStringLiteral("给一帧最小骨架，从零自己搭字段"));
    connect(btnBlank, &QPushButton::clicked, this, [this] {
        QJsonObject frame;
        frame[QStringLiteral("type")] = QStringLiteral("recognize_ack");
        frame[QStringLiteral("request_id")] = QStringLiteral("{{request_id}}");
        frame[QStringLiteral("items")] = QJsonArray();
        exportFrameToEditor(frame);
    });

    auto* btnPretty = new QPushButton(QStringLiteral("整理排版"));
    btnPretty->setToolTip(QStringLiteral("把编辑框里的单帧 JSON 重新缩进排版，方便逐字段改；多帧或坏帧保持原样"));
    connect(btnPretty, &QPushButton::clicked, this, [this] {
        const QJsonDocument doc = QJsonDocument::fromJson(m_custom->toPlainText().toUtf8());
        if (!doc.isObject()) {
            logEvent(QStringLiteral("排版跳过：当前内容不是单个 JSON 对象（多帧或坏帧保持原样）"));
            return;
        }
        m_custom->setPlainText(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)).trimmed());
        logEvent(QStringLiteral("已按缩进重新排版"));
    });

    g->addWidget(new QLabel(QStringLiteral("自定义内容（这里的文字就是最终发出的帧）")), 0, 0, 1, 3);
    g->addWidget(m_custom, 1, 0, 1, 3);
    g->addWidget(m_rawSend, 2, 0, 1, 3);
    g->addWidget(btn, 3, 0, 1, 3);
    auto* toolRow = new QHBoxLayout;
    toolRow->addWidget(btnFromScene);
    toolRow->addWidget(btnBlank);
    toolRow->addWidget(btnPretty);
    toolRow->addStretch(1);
    g->addLayout(toolRow, 4, 0, 1, 3);
    g->addWidget(new QLabel(QStringLiteral("循环发送间隔")), 5, 0);
    g->addWidget(m_loopMs, 5, 1);
    g->addWidget(m_btnLoop, 5, 2);
    g->addWidget(new QLabel(QStringLiteral(
        "怎么用：①帧里写 {{request_id}}，发送时自动换成本次上位机发来的编号，不用手抄；\n"
        "②缩进、换行、字段顺序都能改，整段 JSON 会当成一帧发出；\n"
        "③一行写多帧或勾选【原文发送】仍可测粘包与坏帧；④从②页清单表或③页场景载入后，改完再点发送。")),
        6, 0, 1, 3);
    return w;
}

// ─────────────────────── 自定义帧（协议3.3） ───────────────────────

/**
 * @brief 把识别回应场景切到“自定义清单”，并提示是否还有可用行
 * @return 无。切场景后，下一条识别请求即按清单文本回应
 */
void EdgeSimulator::onCustomListActivate()
{
    if (!m_recScene) {
        return;
    }
    const int index = m_recScene->findData(SceneCustomList);
    if (index >= 0) {
        m_recScene->setCurrentIndex(index);
    }
    // 序列回放开着时会盖过场景选择，点了按钮却仍按序列回会让人以为功能没生效，这里直接关掉
    if (m_seqEnabled && m_seqEnabled->isChecked()) {
        m_seqEnabled->setChecked(false);
        logEvent(QStringLiteral("序列回放已启用会盖过场景选择，已自动关闭序列（要做多轮回放时把“自定义清单”加进序列即可）"));
    }
    QJsonObject frame;
    const int itemCount = parseFrameText(m_frameText ? m_frameText->toPlainText() : QString(), frame)
                          ? frame.value(QStringLiteral("items")).toArray().size() : 0;
    logEvent(QStringLiteral("识别回应场景已切到“自定义清单”（帧 JSON 含 %1 条货位，下一条请求按它回）").arg(itemCount));
    updateSceneHint();
}

/**
 * @brief 把当前选中的内置场景清单填入编辑区（在样例基础上微调）
 * @return 无。场景本身就是自定义清单时保持现有编辑内容不动
 */
void EdgeSimulator::onCustomListFromScene()
{
    if (!m_recScene) {
        return;
    }
    const int scene = m_recScene->currentData().toInt();
    if (scene == SceneCustomList) {
        logEvent(QStringLiteral("当前场景就是自定义清单，无需取样"));
        return;
    }
    const QJsonObject ack = sceneAckFrame(scene, QJsonObject());
    loadFrameText(formatFrameText(ack.value(QStringLiteral("items")).toArray()));
    logEvent(QStringLiteral("已把场景「%1」的清单载入帧 JSON（改完点【启用自定义清单】生效）").arg(m_recScene->currentText()));
}

/**
 * @brief 把当前帧 JSON 写入 exe 同目录 custom_frame.json
 * @return 无。写失败在日志区提示原因（不影响其他功能）
 */
void EdgeSimulator::onCustomListSaveToFile()
{
    if (!m_frameText) {
        return;
    }
    const QString path = frameFilePath();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        logEvent(QStringLiteral("帧 JSON 写入失败（%1）：%2").arg(path).arg(file.errorString()));
        return;
    }
    file.write(m_frameText->toPlainText().toUtf8());
    file.close();
    logEvent(QStringLiteral("帧 JSON 已保存：%1").arg(path));
}

/**
 * @brief 从 exe 同目录 custom_frame.json 读回帧 JSON
 * @return 无。文件不存在或读失败在日志区提示，不改动编辑框现有内容
 */
void EdgeSimulator::onCustomListReload()
{
    if (!m_frameText) {
        return;
    }
    const QString path = frameFilePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        logEvent(QStringLiteral("读取失败（%1）：%2；请先点【保存帧 JSON】").arg(path).arg(file.errorString()));
        return;
    }
    QByteArray raw = file.readAll();
    file.close();
    if (raw.startsWith("\xEF\xBB\xBF")) {
        raw.remove(0, 3);
    }
    loadFrameText(QString::fromUtf8(raw));
    logEvent(QStringLiteral("已从 %1 载入帧 JSON").arg(path));
}

/**
 * @brief 刷新清单解析预览表与统计文字（清单文本改动时自动调用）
 * @return 无。预览里显示的就是即将发给上位机的字段
 */

/**
 * @brief 按清单编辑区文本构造 recognize_ack 的 items 数组
 * @return items 数组；清单为空时返回空数组（上位机按“未检测到该货位”处理）
 * @note  清单表只作生成起点；实际发出的帧以②页帧 JSON 编辑框为准
 */
QJsonArray EdgeSimulator::customItemsArray() const
{
    QJsonArray items;
    if (!m_orderTable) {
        return items;
    }
    const QList<CustomRow> rows = orderRowsFromTable();
    for (const CustomRow& row : rows) {
        if (!row.valid || !row.reply) {
            continue;
        }
        // 每项输出表13 四字段；tool_id 为空表示该货位无工具，空货位同样回报本项
        QJsonObject item;
        item["position"]    = row.position;
        item["tool_id"]     = row.toolCode;
        item["confidence"]  = row.confidence;
        item["alert_type"]  = row.alertType.isEmpty() ? QStringLiteral("none") : row.alertType;
        items.append(item);
    }
    return items;
}

/**
 * @brief 取帧 JSON 文件绝对路径（固定 exe 同目录，可直接用文本编辑器改）
 * @return 形如 <exe目录>/custom_frame.json 的路径
 */
QString EdgeSimulator::frameFilePath() const
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/custom_frame.json");
}

/**
 * @brief 判断当前识别回应场景是否为"自定义清单"
 * @return true=场景已切到自定义清单（且序列未启用），本页清单会被发出去
 */
bool EdgeSimulator::isCustomListActive() const
{
    if (!m_recScene) {
        return false;
    }
    if (m_seqEnabled && m_seqEnabled->isChecked() && m_seqList && m_seqList->count() > 0) {
        return false;   // 序列回放会盖过场景选择
    }
    return m_recScene->currentData().toInt() == SceneCustomList;
}

/**
 * @brief 刷新顶部状态条：帧 JSON 语法是否正确、当前生效场景是什么
 * @return 无。帧语法坏用红字提示，场景不是自定义清单时用橙色警示
 */
void EdgeSimulator::updateSceneHint()
{
    const QString frameText = m_frameText ? m_frameText->toPlainText() : QString();
    const bool hasItems = !frameText.trimmed().isEmpty();
    QJsonObject checkFrame;
    const bool frameOk = hasItems && parseFrameText(frameText, checkFrame);
    const QString sceneName = m_recScene ? m_recScene->currentText() : QStringLiteral("(未选场景)");
    const bool seqOn = m_seqEnabled && m_seqEnabled->isChecked() && m_seqList && m_seqList->count() > 0;

    QString text;
    QString color;
    if (hasItems && !frameOk) {
        text = QStringLiteral("帧 JSON 语法有误：本次不会发出回应，上位机会等超时。请检查括号、引号与逗号");
        color = QStringLiteral("#c62828");
    } else if (isCustomListActive()) {
        text = QStringLiteral("当前生效：②页帧 JSON（上位机收到的就是编辑框里这一帧）");
        color = QStringLiteral("#1a7f37");
    } else if (hasItems) {
        text = QStringLiteral("注意：回应场景当前是“%1”%2，②页帧 JSON【不会生效】！请到“识别回应”页把场景切成自定义清单")
                   .arg(sceneName, seqOn ? QStringLiteral("（且序列回放开着，会盖过场景选择）") : QString());
        color = QStringLiteral("#b26a00");
    } else {
        text = QStringLiteral("当前生效：%1（帧 JSON 为空，先在“单据模拟”页生成帧或选内置场景）").arg(sceneName);
        color = QStringLiteral("#666666");
    }
    if (m_sceneHint) {
        m_sceneHint->setText(text);
        m_sceneHint->setStyleSheet(QStringLiteral("color:%1;font-weight:bold;").arg(color));
    }
    if (m_globalHint) {
        const QString dbText = m_dbStat ? m_dbStat->text() : QStringLiteral("未连接数据库");
        m_globalHint->setText(text + QStringLiteral("　｜　数据库：") + dbText);
        m_globalHint->setStyleSheet(QStringLiteral("color:%1;font-weight:bold;").arg(color));
    }
}

/**
 * @brief 有清单但场景不是自定义清单时，自动切到自定义清单并关掉序列
 * @param why 触发原因（写进日志，说明是谁让清单开始生效的）
 * @return 无。清单为空或本来就是自定义清单时不做任何事
 */
void EdgeSimulator::autoActivateCustomList(const QString& why)
{
    const bool hasItems = (m_frameText && !m_frameText->toPlainText().trimmed().isEmpty());
    if (!hasItems || isCustomListActive()) {
        return;
    }
    onCustomListActivate();
    logEvent(QStringLiteral("%1，已自动切换到自定义清单").arg(why));
}

// ─────────────────────── 清单数据直读数据库 ───────────────────────

/**
 * @brief 按界面参数连接 MySQL（连接串与上位机 DatabaseManager 同口径：QODBC 优先）
 * @return true=连接成功；false=失败，失败原因已写到界面状态行与日志区
 * @note  只做查询；口令不出现在日志里（仅写 ini 供下次复用）
 */
bool EdgeSimulator::openToolDatabase()
{
    const QString host = m_dbHost ? m_dbHost->text() : QString();
    const int    port = m_dbPort ? m_dbPort->value() : 3306;
    const QString dbName = m_dbName ? m_dbName->text() : QString();
    const QString user = m_dbUser ? m_dbUser->text() : QString();
    const QString pass = m_dbPass ? m_dbPass->text() : QString();

    const QString connName = QStringLiteral("edge_sim_tools");
    if (QSqlDatabase::contains(connName)) {
        QSqlDatabase::removeDatabase(connName);
    }
    const QStringList drivers = {QStringLiteral("QODBC"), QStringLiteral("QMYSQL")};
    QString lastError;
    for (const QString& driver : drivers) {
        if (!QSqlDatabase::isDriverAvailable(driver)) {
            continue;
        }
        QSqlDatabase db = QSqlDatabase::addDatabase(driver, connName);
        if (driver == QODBC_DRIVER_NAME) {
            db.setDatabaseName(QStringLiteral("DRIVER={MySQL ODBC 8.0 Unicode Driver};"
                                            "SERVER=%1;PORT=%2;DATABASE=%3;UID=%4;PWD=%5;OPTION=3")
                                   .arg(host).arg(port).arg(dbName).arg(user).arg(pass));
        } else {
            db.setHostName(host);
            db.setPort(port);
            db.setDatabaseName(dbName);
            db.setUserName(user);
            db.setPassword(pass);
        }
        if (db.open()) {
            return true;
        }
        lastError = db.lastError().text();
        QSqlDatabase::removeDatabase(connName);
    }
    const QString tip = lastError.isEmpty()
        ? QStringLiteral("没有可用的数据库驱动（QODBC/QMYSQL），请先安装 MySQL ODBC 驱动")
        : lastError;
    if (m_dbStat) {
        m_dbStat->setText(QStringLiteral("连接失败：%1（不影响手改帧 JSON）").arg(tip));
    }
    logEvent(QStringLiteral("连接数据库失败（%1:%2/%3）：%4").arg(host).arg(port).arg(dbName).arg(tip));
    return false;
}

/**
 * @brief 从 machine_group 表读取机组列表，填入 m_groupCombo（只读查询）
 * @return 无。连不上库时只保留"全部机组"一项，不影响其它功能
 * @note  机组下拉用于约束识别清单的数据范围：选了具体机组后，读库生成的清单
 *        只含该机组的工具与货位，避免把别的机组的货位一起推给上位机造成误报
 */
void EdgeSimulator::loadMachineGroups()
{
    if (!m_groupCombo) {
        return;
    }
    m_groupCombo->clear();
    m_groupCombo->addItem(QStringLiteral("全部机组"), 0);
    if (!openToolDatabase()) {
        return;
    }
    QSqlDatabase db = QSqlDatabase::database(QStringLiteral("edge_sim_tools"));
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("SELECT group_id, group_name FROM machine_group ORDER BY group_id"))) {
        while (q.next()) {
            m_groupCombo->addItem(q.value(1).toString(), q.value(0).toInt());
        }
    }
    QSqlDatabase::removeDatabase(QStringLiteral("edge_sim_tools"));
    // 恢复上次选择的机组
    const int idx = m_groupCombo->findData(m_selectedGroupId);
    if (idx >= 0) {
        m_groupCombo->setCurrentIndex(idx);
    }
}

/**
 * @brief 确认与上位机同一个 MySQL 库能连通（帧 JSON 由②页编辑框维护，不再填选择框）
 * @return true=连通；false=连不上，原因写到状态行
 */
bool EdgeSimulator::loadToolsAndPositions()
{
    if (!openToolDatabase()) {
        if (m_dbStat) {
            m_dbStat->setText(QStringLiteral("数据库连接失败"));
        }
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(QStringLiteral("edge_sim_tools"));
    QSqlQuery probe(db);
    const bool ok = probe.exec(QStringLiteral("SELECT 1"));
    const QString err = ok ? QString() : probe.lastError().text();
    QSqlDatabase::removeDatabase(QStringLiteral("edge_sim_tools"));
    if (m_dbStat) {
        m_dbStat->setText(ok
            ? QStringLiteral("数据库已连接：%1").arg(m_dbName ? m_dbName->text() : QString())
            : QStringLiteral("数据库查询失败：%1").arg(err));
    }
    updateSceneHint();
    return ok;
}

/**
 * @brief 连接数据库并做连通性检查，成功后填机组下拉
 * @return 无。成功失败都写到状态行与日志区
 */
void EdgeSimulator::onLoadToolsFromDb()
{
    loadToolsAndPositions();
    // 连库成功后把机组列表填进下拉：选了具体机组后，
    // 后续读库生成的清单只含该机组工具与货位（避免跨机组误报）
    loadMachineGroups();
}


/**
 * @brief 按本机组全部已登记货位生成清单，并自动切到自定义清单场景
 * @return 无。覆盖当前清单（原清单可先用【保存帧 JSON】备份）；失败只在状态行与日志区提示
 * @note  生成的是本机组全部已登记货位的实况基线：在库货位带真实工具编号，
 *        其余货位编号为空但行不省略。测试人员再在其上改几行就能造出各类场景
 */
void EdgeSimulator::onGenerateStockList()
{
    if (!m_orderTable) {
        return;
    }
    QList<CustomRow> rows;
    if (!collectStockRows(rows) || rows.isEmpty()) {
        if (m_dbStat) {
            m_dbStat->setText(QStringLiteral("生成失败：库里没有已登记货位（不影响手改帧 JSON）"));
        }
        logEvent(QStringLiteral("按本机组全部已登记货位生成清单失败：库里没有已登记货位"));
        return;
    }
    fillOrderTable(rows);
    int emptySlots = 0;
    for (const CustomRow& row : rows) {
        if (row.toolCode.isEmpty()) {
            ++emptySlots;
        }
    }
    logEvent(QStringLiteral("已按本机组全部已登记货位生成清单：%1 条（其中空货位 %2 条，"
                            "工具编号为空但行不省略；可在此表上改几行造场景）")
                 .arg(rows.size()).arg(emptySlots));
    onCustomListActivate();
}

/**
 * @brief 把上方选择框的内容追加/替换成清单一行
 * @return 无。已有选中行时替换该行，否则追加到末尾
 */

/**
 * @brief 删除清单里当前选中的一行
 * @return 无。未选中行时不做任何事
 */

/**
 * @brief 选中清单某行时，把该行内容反选回上方选择框
 * @return 无。行内容不合法或行号越界时直接返回
 * @note  这样改单行只需在选择框里改完点【插入】，不用回到文本区手改
 */

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
    m_seqCursor = 0;   // 新连接从头回放序列，保证多轮流程可复现
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
        // 回应场景确定：启用序列且游标未越界时取序列下一项，否则回落单场景默认；
        // 延迟统一取数字框（0=立即）。连接前后改配置都即时生效，无状态残留。
        int scene = static_cast<int>(SceneNormal);
        QString sceneText;
        const bool useSeq = m_seqEnabled && m_seqEnabled->isChecked()
                            && m_seqList && m_seqList->count() > 0;
        if (useSeq && m_seqCursor < m_seqList->count()) {
            const int idx = m_seqCursor++;
            scene = m_seqList->item(idx)->data(Qt::UserRole).toInt();
            sceneText = QStringLiteral("序列第%1项：%2").arg(idx + 1).arg(m_seqList->item(idx)->text());
        } else {
            scene = m_recScene ? m_recScene->currentData().toInt() : static_cast<int>(SceneNormal);
            sceneText = m_recScene ? m_recScene->currentText() : QString();
        }
        const int delay = m_delayMs ? m_delayMs->value() : 0;
        if (scene == SceneSilent) {
            logEvent(QStringLiteral("收到recognize_req → ") + sceneText
                     + QStringLiteral("（配置为不回应，测上位机超时重发）"));
        } else {
            logEvent(QString(QStringLiteral("收到recognize_req → 按配置延迟%1ms回： %2")).arg(delay).arg(sceneText));
            // 兜底提醒：清单配了却没启用，是"配了不生效"最容易踩的坑，这里直接点破
            if (scene != SceneCustomList && m_frameText
                && !m_frameText->toPlainText().trimmed().isEmpty()) {
                logEvent(QStringLiteral("*** 提醒：②页已有帧 JSON，但当前生效场景是“%1”，本次回的是内置场景清单；"
                                        "要用帧 JSON 请到②页点【启用自定义清单】").arg(sceneText));
            }
            QTimer::singleShot(delay, this, [this, scene] { replyScene(scene); });
        }
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

/**
 * @brief 按当前生效场景回送一条 recognize 回应帧
 * @param scene 场景编号；自定义清单场景读②页帧 JSON 编辑框，其余用内置场景
 * @return 无
 */
void EdgeSimulator::replyScene(int scene)
{
    if (!m_sock || m_lastRequest.isEmpty()) {
        logEvent(QStringLiteral("没有待回应的recognize请求（先等上位机发一次请求）"));
        return;
    }
    if (scene == SceneCustomList) {
        // 自定义清单：②页帧 JSON 编辑框就是发出的内容，改结构直接改那里
        if (!m_frameText || !sendFrameText(m_frameText->toPlainText())) {
            return;
        }
        logEvent(QStringLiteral("按②页帧 JSON 回应（内容以编辑框为准）"));
        return;
    }
    sendFrame(sceneAckFrame(scene, m_lastRequest));
}

/**
 * @brief 向识别回应序列列表追加一个场景
 * @param scene 场景枚举值（与 Scene 枚举一致）
 * @return 无。同时在列表项用户数据里写入枚举值，供回放时取出
 */
void EdgeSimulator::addSeqScene(int scene)
{
    if (!m_seqList) {
        return;
    }
    auto* item = new QListWidgetItem(sceneNameById(scene));
    item->setData(Qt::UserRole, scene);
    m_seqList->addItem(item);
}

/**
 * @brief 取场景枚举对应的下拉显示名（持久化读回时重建序列列表用）
 * @param scene 场景枚举值
 * @return 下拉中该场景的文字；找不到时回退为 "场景N"
 */
QString EdgeSimulator::sceneNameById(int scene) const
{
    if (m_recScene) {
        for (int i = 0; i < m_recScene->count(); ++i) {
            if (m_recScene->itemData(i).toInt() == scene) {
                return m_recScene->itemText(i);
            }
        }
    }
    return QStringLiteral("场景%1").arg(scene);
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

/**
 * @brief 构造一条 open_ack 帧
 * @param requestId 与开箱请求配对的编号
 * @param success   true=柜体已真实打开，false=开柜失败
 * @param message   失败原因（success=true 时传空串）
 * @return 含 type/request_id/result/message 四字段的帧对象，result 为 1 或 0
 */
QJsonObject EdgeSimulator::buildOpenAckFrame(const QString& requestId, bool success,
                                             const QString& message)
{
    QJsonObject frame;
    frame["type"] = QStringLiteral("open_ack");
    frame["request_id"] = requestId;
    frame["result"] = success ? 1 : 0;
    frame["message"] = message;
    return frame;
}

/**
 * @brief 立即发送一条 open_ack（自动回应策略与两枚主动按钮共用入口）
 * @param success true=开柜成功，false=开柜失败
 * @param message 失败原因；成功时传空串
 * @return 无
 * @note  编号取最近一次收到的 open_req；从未收到过或连接未建立时只提示、不发帧
 */
void EdgeSimulator::sendOpenAckNow(bool success, const QString& message)
{
    if (!m_sock || m_sock->state() != QAbstractSocket::ConnectedState) {
        logEvent(QStringLiteral("发送open_ack失败：未连接"));
        return;
    }
    if (m_lastOpenRequest.isEmpty()) {
        logEvent(QStringLiteral("未收到过 open_req：本次不发送 open_ack（避免发出编号为空的帧）。"
                                "请先在上位机点一次开柜，或到④页自定义帧手填编号后发送"));
        return;
    }
    const QString id = m_lastOpenRequest["request_id"].toString();
    logEvent(QString(QStringLiteral("主动发送 open_ack（result=%1）：request_id=%2 取自最近一次 open_req；"
                                    "若该请求已被回应配对或已超时，上位机会按重复/未知编号丢弃，"
                                    "请重新发起开柜后再点按钮"))
                 .arg(success ? 1 : 0).arg(id));
    sendFrame(buildOpenAckFrame(id, success, message));
}

/**
 * @brief 点【回开柜成功】：立即回一条 result=1 的 open_ack
 * @return 无
 */
void EdgeSimulator::onSendOpenSuccessClicked()
{
    sendOpenAckNow(true, QString());
}

/**
 * @brief 点【回开柜失败】：立即回一条 result=0 的 open_ack，原因取界面文案
 * @return 无
 */
void EdgeSimulator::onSendOpenFailureClicked()
{
    const QString msg = m_openFailMsg ? m_openFailMsg->text() : QStringLiteral("开箱失败（模拟）");
    sendOpenAckNow(false, msg);
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
 * @brief 把识别清单组装成缩进排版的完整 recognize_ack 帧文本
 * @param items 协议3.3 的 items 数组
 * @return 带 {{request_id}} 占位符的帧文本，可直接编辑后发送
 */
QString EdgeSimulator::formatFrameText(const QJsonArray& items)
{
    QJsonObject frame;
    frame[QStringLiteral("type")] = QStringLiteral("recognize_ack");
    frame[QStringLiteral("request_id")] = QStringLiteral("{{request_id}}");
    frame[QStringLiteral("items")] = items;
    return QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Indented)).trimmed();
}

/**
 * @brief 解析帧文本，供发送前校验
 * @param text 编辑框里的帧文本
 * @param frame 解析成功时输出帧对象
 * @return true=文本是单个 JSON 对象；false=语法坏或不是对象
 */
bool EdgeSimulator::parseFrameText(const QString& text, QJsonObject& frame)
{
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (!doc.isObject()) {
        return false;
    }
    frame = doc.object();
    return true;
}

/**
 * @brief 把帧文本写入②页编辑框（程序写入，不标记为手工改动）
 * @param text 帧 JSON 文本
 * @return 无
 */
void EdgeSimulator::loadFrameText(const QString& text)
{
    if (!m_frameText) {
        return;
    }
    m_frameLoading = true;
    m_frameText->setPlainText(text);
    m_frameLoading = false;
    m_frameEdited = false;
}

/**
 * @brief 从②页清单表重新生成帧 JSON 并载入编辑框
 * @return 无
 * @note  手工改过帧之后，只有显式点【从表格重新生成帧】才会覆盖
 */
void EdgeSimulator::refreshFrameTextFromTable()
{
    loadFrameText(formatFrameText(customItemsArray()));
    autoActivateCustomList(QStringLiteral("帧 JSON 已按清单表重新生成"));
}

/**
 * @brief 替换占位符并发送一帧；自动回应与手动发送共用同一套规则
 * @param text 帧 JSON 文本
 * @return true=已发出一帧；false=文本不是合法 JSON 对象或连接未建立
 */
bool EdgeSimulator::sendFrameText(const QString& text)
{
    const QString resolved = resolveFramePlaceholders(text, m_lastRequest, m_lastOpenRequest);
    QJsonObject frame;
    if (!parseFrameText(resolved, frame)) {
        logEvent(QStringLiteral("帧 JSON 解析失败，本次未发送：请检查括号、引号与字段结构"));
        return false;
    }
    return sendFrame(frame);
}

/**
 * @brief 把帧文本里的占位符换成最近一次上位机请求编号
 * @param text 编辑框里的帧文本
 * @param lastRequest 最近收到的识别请求帧
 * @param lastOpenRequest 最近收到的开箱请求帧
 * @return 替换后的文本；没有对应请求时保留原占位符
 * @note  只处理文本，不回写编辑框：循环发送时每轮都能换成最新编号
 */
QString EdgeSimulator::resolveFramePlaceholders(const QString& text,
                                                const QJsonObject& lastRequest,
                                                const QJsonObject& lastOpenRequest)
{
    QString resolved = text;
    const QString reqId = lastRequest.value(QStringLiteral("request_id")).toString();
    const QString openId = lastOpenRequest.value(QStringLiteral("request_id")).toString();
    if (!reqId.isEmpty()) {
        resolved.replace(QStringLiteral("{{request_id}}"), reqId);
    }
    if (!openId.isEmpty()) {
        resolved.replace(QStringLiteral("{{open_request_id}}"), openId);
    }
    return resolved;
}

/**
 * @brief 把一帧载入自定义帧编辑框，编号写成占位符，切到可编辑页面
 * @param frame 待载入的协议帧
 * @return 无
 * @note  载入的是起点，不是成品：字段、取值、缩进格式都能在编辑框里随意改，
 *        点【发送】时才按编辑框里的实际内容发出
 */
void EdgeSimulator::exportFrameToEditor(const QJsonObject& frame)
{
    if (!m_custom) {
        return;
    }
    QJsonObject editable = frame;
    if (editable.contains(QStringLiteral("request_id"))) {
        editable[QStringLiteral("request_id")] = QStringLiteral("{{request_id}}");
    }
    const QString text = QString::fromUtf8(
        QJsonDocument(editable).toJson(QJsonDocument::Indented)).trimmed();
    m_custom->setPlainText(text);
    showTab(3);
    if (m_tabs) {
        m_tabs->setCurrentIndex(2);
    }
    logEvent(QStringLiteral("帧已载入编辑框：字段与格式可随意改，发送时自动回带本次 request_id"));
}

/**
 * @brief 发送编辑框里的内容：发送时才替换占位符，编辑框所见即所发
 * @return 无
 * @note  JSON 模式先把整段当一帧解析（支持缩进多行、自由排版）；整段不是单个
 *        JSON 对象时退回逐行发送，保留多帧粘包与逐行 JSON 的原有能力。
 *        原文模式始终逐行原样发出，坏帧与任意字节注入不受影响。
 */
void EdgeSimulator::sendCustomFrame()
{
    if (!m_sock || m_sock->state() != QAbstractSocket::ConnectedState) {
        logEvent(QStringLiteral("发送失败：连接未建立"));
        return;
    }
    const QString source = m_custom->toPlainText();
    if (source.trimmed().isEmpty()) {
        logEvent(QStringLiteral("自定义内容为空"));
        return;
    }
    const QString text = resolveFramePlaceholders(source, m_lastRequest, m_lastOpenRequest);
    const QString reqId = m_lastRequest.value(QStringLiteral("request_id")).toString();
    if (source.contains(QStringLiteral("{{request_id}}")) && !reqId.isEmpty()) {
        logEvent(QStringLiteral("已回带识别请求编号：") + reqId
            + QStringLiteral("（该请求若已被回应配对，上位机会按重复编号丢弃；可配合③页【不回应】或延迟回应使用）"));
    }
    if (text.contains(QStringLiteral("{{request_id}}"))
        || text.contains(QStringLiteral("{{open_request_id}}"))) {
        logEvent(QStringLiteral("提示：还没收到对应的上位机请求，占位符未替换，这帧会被上位机按编号不认识丢弃"));
    }

    int okCount = 0;
    if (!m_rawSend->isChecked()) {
        // 整段优先：缩进排版后的单帧 JSON 不会被按行拆坏
        QJsonObject whole;
        if (parseFrameText(text, whole)) {
            if (sendFrameText(text)) {
                logEvent(QStringLiteral("自定义发送完成：整段 1 帧"));
            }
            return;
        }
    }

    const QStringList lines = text.split(QStringLiteral("\n"), Qt::SkipEmptyParts);
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

/// 通过已建立的 TCP 连接发送一帧 JSON 协议帧，并在日志区记录收发内容
/// @param frame 待发送的协议帧（JSON 对象，含 type 与业务字段）
/// @return 发送成功返回 true；连接未建立或写入失败返回 false
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
    // 日志区控件在界面构建完成后才存在；构造期的提示只能写文件
    if (m_logView) {
        m_logView->append(QString(QStringLiteral("<span style='color:%1'>[%2] %3</span>")).arg(color, stamp, text));
    }
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

// ─────────────────────── 工作台框架 ───────────────────────

/**
 * @brief 填充左侧步骤导航
 * @return 无。导航项与右侧工作区页面一一对应
 */
void EdgeSimulator::buildStepNav()
{
    if (!m_stepNav) {
        return;
    }
    m_stepNav->clear();
    m_stepNav->addItem(QStringLiteral("① 连接与数据"));
    m_stepNav->addItem(QStringLiteral("② 单据模拟"));
    m_stepNav->addItem(QStringLiteral("③ 识别回应"));
    m_stepNav->addItem(QStringLiteral("④ 异常注入"));
    m_stepNav->setCurrentRow(0);
}

/**
 * @brief 构建页①：连接与数据
 * @return 组装完成的页面控件
 */
QWidget* EdgeSimulator::buildConnectDataPage()
{
    auto* page = new QWidget;
    auto* box = new QVBoxLayout(page);
    box->addWidget(buildDbPanel());
    auto* tip = new QLabel(QStringLiteral(
        "第①步：先在窗口顶部连上上位机，再读库。\n"
        "· 上位机IP/端口默认 127.0.0.1:9000，点【连接】即可；\n"
        "· 数据库是上位机同一个库 smart_cabinet，账号口令取自 simulator.ini（root/root），不必手输；\n"
        "· 读到的工具与货位会填进②页的本单候选列表，勾选后即可生成清单；\n"
        "· 不想连库也能用：在②页改帧 JSON。"));
    tip->setWordWrap(true);
    box->addWidget(tip);
    box->addStretch(1);
    return page;
}

/**
 * @brief 构建页③：识别回应（版本查询 3.5、开箱回应 3.7 + 识别场景 3.3）
 * @return 组装完成的页面控件
 * @note  版本与开箱两类"回应配置"放在顶部，识别场景配置放在下面：
 *        测试人员点一次【确认】就要用到这三样，放一起不用来回翻页
 */
QWidget* EdgeSimulator::buildRespondPage()
{
    auto* page = new QWidget;
    auto* box = new QVBoxLayout(page);
    box->addWidget(buildOpenPanel());
    box->addWidget(buildRecognizeTab(), 1);
    return page;
}

/**
 * @brief 构建页④：异常注入（周期帧与摄像头自检、帧级异常、自定义帧）
 * @return 组装完成的页面控件
 */
QWidget* EdgeSimulator::buildExceptionPage()
{
    auto* page = new QWidget;
    auto* box = new QVBoxLayout(page);
    auto* tabs = new QTabWidget;
    tabs->addTab(buildPeriodicTab(), QStringLiteral("周期帧与摄像头 3.4"));
    tabs->addTab(buildFaultTab(), QStringLiteral("异常注入 3.6"));
    tabs->addTab(buildCustomTab(), QStringLiteral("自定义帧"));
    m_tabs = tabs;
    box->addWidget(tabs);
    return page;
}

/**
 * @brief 左侧步骤导航切换
 * @param row 选中的步骤序号（从0开始）
 * @return 无。右侧工作区切到对应页面
 */
void EdgeSimulator::onStepClicked(int row)
{
    logEvent(QStringLiteral("步骤导航切换到第 %1 页").arg(row));
    if (m_stack && row >= 0 && row < m_stack->count()) {
        m_stack->setCurrentIndex(row);
    }
}

/**
 * @brief 一键标准联调：把常用配置一次摆好
 * @return 无
 * @note  做四件事：连上位机、读库、生成本机组全部已登记货位清单、识别回应切自定义清单并打开周期帧
 */
void EdgeSimulator::onOneClickStart()
{
    if (!m_sock || m_sock->state() != QAbstractSocket::ConnectedState) {
        connectToEdge();
    }
    loadOrderCandidates();
    QList<CustomRow> rows;
    if (collectStockRows(rows) && !rows.isEmpty()) {
        fillOrderTable(rows);
    }
    onCustomListActivate();
    if (m_periodicOn && !m_periodicOn->isChecked()) {
        m_periodicOn->setChecked(true);
    }
    logEvent(QStringLiteral("一键标准联调已就绪：上位机已连接、库已读取、清单已按本机组全部已登记货位生成（空货位编号为空但行不省略）、识别回应切到自定义清单、周期帧已开"));
}
