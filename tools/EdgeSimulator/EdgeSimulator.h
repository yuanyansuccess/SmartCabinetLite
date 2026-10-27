/**
 * @file  EdgeSimulator.h
 * @brief 边缘计算机模拟器（带界面）——按上位机出入库流程分步的联调工具
 * @author 袁燕
 *
 * 界面结构（4 步工作台，新手按步骤走即可）：
 *   ① 连接与数据：连上位机 + 连库读工具货位（口令取自 simulator.ini，不必手输）
 *   ② 单据模拟：定义"这一单要核对什么"——清单表逐行标注本单归属与识别结果
 *   ③ 识别回应：收到识别请求时怎么答（场景/延迟/多轮序列/开箱策略）
 *   ④ 异常注入：链路与帧级异常（离线、超时、坏帧、摄像头闸门…）
 *   ⑤ 场景与日志：6 个典型场景一键套用 + 接口能力速查
 *
 * 覆盖范围（与《智能工具柜配置项通信协议》对应）：
 *   3.3 recognize_ack 全部场景回应（正常/有借有还/无变化/错放/未知物/
 *       置信度低/missing/自定义清单）
 *   3.3 自定义工具清单：按真实单据逐行填"回什么清单"，脱离内置样例
 *   3.3 异常回应（延迟/重复×2/粘包/不回应/乱序request_id）
 *   3.4 periodic 周期帧（相机5态自检/空自检/uptime）
 *   3.5 version_ack（版本号可配/自动回应）
 *   3.6 坏帧/未知type/超长帧/断开
 *   3.7 open_req 开箱回应（自动成功/固定失败/不回应）
 *   第5章 停发周期帧（验证离线判定与恢复补发）
 *   断线自动重连（模拟真实边缘机退避重连行为）
 */
#pragma once
#include <QWidget>
#include <QTcpSocket>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>

class QTextEdit;
class QPushButton;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QLineEdit;
class QLabel;
class QTabWidget;
class QStackedWidget;
class QFile;
class QListWidget;
class QTableWidget;

class EdgeSimulator : public QWidget {
    Q_OBJECT
public:
    explicit EdgeSimulator(QWidget* parent = nullptr);
    ~EdgeSimulator() override;

    /// 一键连接（供 --auto-connect 命令行与自动化测试调用）
    void connectToEdge();

    /**
     * @brief 指定要连接的上位机地址（命令行 --host/--port 用，优先于配置文件）
     * @param host 上位机 IP 或主机名
     * @param port 上位机监听端口
     * @return 无。调用后界面上的地址与端口同步更新
     * @note  命令行参数不生效会让跨电脑联调连错机器，这里显式覆盖配置值
     */
    void setPeerAddress(const QString& host, int port);

    /// 切换到指定步骤页（供 --tab 命令行与截图自动化调用）
    void showTab(int index);

    /**
     * @brief 取当前 8 路摄像头中状态异常的摘要（非 ok 且非"不报自检"）
     * @return 形如 "CAM3(offline) CAM5(dirty)"；全部正常或均为空时返回空串
     */
    QString cameraAbnormalText() const;

private slots:
    // 连接
    void onConnectToggle();      ///< 连接/断开上位机
    void onSocketConnected();    ///< 连接建立
    void onSocketDisconnected(); ///< 连接断开
    void onSocketError(QAbstractSocket::SocketError err); ///< 连接失败（端口未监听/被拒）：继续退避重连并提示
    void onReadyRead();          ///< 收到上位机帧
    // 周期帧（协议3.4）
    void sendPeriodicNow();      ///< 立即发一帧周期帧
    void togglePeriodic(bool on);///< 周期帧自动发送启停
    // 识别回应（协议3.3）
    void replyScene(int scene);  ///< 按场景回应最近recognize
    void executeSilenceDrop();   ///< 停发周期帧N秒（第5章离线场景）
    // 版本（协议3.5）
    void sendVersionAckNow();    ///< 立即回version_ack
    // 异常注入（协议3.6）
    void injectBadFrame();       ///< 注入非法JSON帧
    void injectUnknownType();    ///< 注入未知type帧
    void injectOversize();       ///< 注入超长帧（测断开保护）
    void sendBadIdAck();         ///< 回应乱序request_id（测3.6-②丢弃）
    // 自定义帧
    void sendCustomFrame();      ///< 发送自定义内容（按行发送，JSON或原文）
    void toggleLoopSend(bool on);///< 循环发送自定义内容（压力/持续异常测试）
    // 自定义工具清单（协议3.3：按真实单据逐行配置"回什么工具清单"）
    void onCustomListActivate();     ///< 把识别回应场景切到"自定义清单"
    void onCustomListFromScene();    ///< 把当前场景的内置清单填入清单表（在样例基础上微调）
    void onCustomListSaveToFile();   ///< 把当前清单写入 custom_items.txt（可用 Excel 备好后重载）
    void onCustomListReload();       ///< 从 exe 同目录 custom_items.txt 重载清单
    // 清单数据直读数据库（与上位机同一库同一批表，选出来的编号货位保证与单据一致）
    void onLoadToolsFromDb();        ///< 连接数据库并读取工具与货位，填入两个选择框
    void onGenerateStockList();      ///< 按库内在库货位一键生成全量清单（造"上一轮在库"基线，再在其上改）
    void onPositionPicked();         ///< 选了货位后自动带出该货位上的工具编号
    // 单据模拟（页②）
    void onOrderCellChanged(int row, int column); ///< 清单表某格被改：联动告警类型/级别并刷新统计
    void onOrderRowSelected();       ///< 清单表选中行变化：把该行内容回填到下方选择区
    void onGenerateOrderList();      ///< 按勾选的本单内容生成清单（本单-入库/出库/借出转出库 + 全柜背景行）
    void onSetAllNormal();           ///< 把所有非出库行改成"识别正常"
    void onKeepOnlyOrderRows();      ///< 只保留本单行（背景行不回应，模拟只扫本单区域）
    void onClearOrderList();         ///< 清空清单（造"识别清单为空"场景）
    void onDeleteOrderRow();         ///< 删除清单表选中行
    void onInsertOrderRow();         ///< 按下方选择区内容新增一行
    void onLoadOrderCandidates();    ///< 从库读本单候选（待入库/在库/借出中）
    // 场景与工作台（页⑤ 与 顶部）
    void onApplyPreset(int presetId);///< 套用典型场景预设
    void onOneClickStart();          ///< 一键标准联调：连上位机+读库+生成基线+开周期帧
    void onStepClicked(int row);     ///< 左侧步骤导航切换
    // 日志
    void onSaveLogClicked();     ///< 保存日志：导出日志区内容为文本文件

private:
    /// 清单一行（表格与文本共用同一份结构，解析与生成规则只写一处）
    struct CustomRow {
        int     lineNo    = 0;    ///< 行号（从1开始，便于回查）
        QString position;         ///< 货位（A-01-02）
        QString toolCode;         ///< 工具编号（空串=该货位没有工具/认不出）
        QString toolName;         ///< 工具名（只读展示，来自库）
        QString belong;           ///< 本单归属：in=本单入库 out=本单出库 borrow_out=本单借出转出库 other=非本单
        QString result;           ///< 识别结果：normal/misplaced/unknown/missing/low_conf/empty
        double  confidence = 0.95;///< 置信度 0~1
        QString alertType;        ///< 协议告警类型（由识别结果推导，可手改）
        int     alertLevel = 0;   ///< 协议告警级别 0~3
        bool    reply = true;     ///< 是否把这一行发给上位机
        QString note;             ///< 问题说明（空串=该行可用）
        bool    valid = true;     ///< true=该行会进入回应的 items
    };

    // 界面构建
    QWidget* buildConnPanel();      ///< 连接区（上位机地址端口）
    QWidget* buildDbPanel();        ///< 数据库区（参数取自 ini，读库填选择框）
    QWidget* buildConnectDataPage();///< 页① 连接与数据
    QWidget* buildOrderPage();      ///< 页② 单据模拟（清单表 + 本单候选）
    QWidget* buildRecognizeTab();   ///< 页③ 识别回应（3.3）
    QWidget* buildOpenVersionPanel();///< 版本查询 3.5 与开箱回应 3.7（放在识别回应页顶部）
    QWidget* buildRespondPage();    ///< 页③ 识别回应（版本/开箱 + 识别场景/延迟/序列）
    QWidget* buildPeriodicTab();    ///< 页④ 周期帧（3.4）
    QWidget* buildFaultTab();       ///< 页④ 版本/异常（3.5-3.6）
    QWidget* buildCustomTab();      ///< 页④ 自定义帧（高级）
    QWidget* buildExceptionPage();  ///< 页④ 异常注入（周期帧+异常+自定义帧）
    QWidget* buildScenePage();      ///< 页⑤ 场景与日志
    void buildStepNav();            ///< 左侧步骤导航
    // 清单处理
    QList<CustomRow> parseCustomRows(const QString& text) const; ///< 解析清单文本（兼容旧5列格式）
    void fillOrderTable(const QList<CustomRow>& rows);              ///< 用解析结果填清单表
    QList<CustomRow> orderRowsFromTable() const;                    ///< 从清单表取当前各行
    QString serializeOrderTable() const;                             ///< 清单表序列化成文本（存 ini/文件用）
    bool collectStockRows(QList<CustomRow>& rows);                   ///< 取库内全柜在库货位作为清单背景行
    void refreshOrderStat();                                        ///< 刷新清单统计与"是否生效"提示
    void applyOrderText(const QString& text);                       ///< 用文本整体替换清单表
    bool loadOrderCandidates();                                      ///< 从库读本单候选（待入库/在库/借出中）
    void generateOrderList();                                        ///< 按勾选候选生成清单
    void setAllResultsNormal();                                      ///< 非出库行全部改成识别正常
    void keepOnlyOrderRows();                                        ///< 背景行取消回应（只回本单区域）
    // 协议与配置
    void processLine(const QByteArray& line);   ///< 分帧解析（收）
    bool sendFrame(const QJsonObject& frame);   ///< 发送一帧
    QJsonObject sceneAck(int scene) const;      ///< 构造场景回应帧
    QJsonArray customItemsArray() const;        ///< 按清单表构造 items 数组（所见即所发）
    QString customListFilePath() const;        ///< 清单文件路径（exe 同目录 custom_items.txt）
    bool openToolDatabase();                   ///< 按界面参数连上 MySQL（与上位机同一套 QODBC 连接串）
    bool loadToolsAndPositions();              ///< 读取工具与货位填入选择框（只读查询，不改任何数据）
    void loadMachineGroups();                   ///< 从 machine_group 表读取机组填进 m_groupCombo（只读查询）
    bool isCustomListActive() const;            ///< 当前识别回应场景是否为"自定义清单"
    void updateSceneHint();                     ///< 刷新顶部状态条：这份清单到底生效没有
    void autoActivateCustomList(const QString& why); ///< 有清单且场景不是自定义清单时自动切过去（避免"配了不生效"）
    void sendOpenAckNow(bool success, const QString& message);  ///< 立即回应最近open_req（开箱协议3.7）
    void saveConfig();           ///< 全部回应配置持久化到 exe 同目录 simulator.ini
    void loadConfig();           ///< 启动时恢复上次的回应配置（连接前配好即可长期复用）
    void addSeqScene(int scene); ///< 向识别回应序列列表追加一个场景（同时写入场景枚举值供回放）
    QString sceneNameById(int scene) const; ///< 取场景枚举对应的下拉显示名（持久化读回时重建列表用）
    void log(const QString& color, const QString& text);
    void logFrame(const QString& dir, const QJsonObject& f);
    void logEvent(const QString& text) { log("#666666", "[事件] " + text); }
    void refreshStat();

    // 主框架控件
    QListWidget*    m_stepNav = nullptr;   ///< 左侧步骤导航（4 步工作台）
    QStackedWidget* m_stack = nullptr;     ///< 右侧工作区（每个步骤一页）
    QLabel*         m_globalHint = nullptr;///< 顶部全局状态条（生效场景 + 数据库状态）
    // 连接区控件
    QLineEdit*   m_addr = nullptr;
    QSpinBox*    m_port = nullptr;
    QPushButton* m_btnConn = nullptr;
    QLabel*      m_state = nullptr;
    QCheckBox*   m_autoReconn = nullptr;  ///< 断开后自动重连开关
    // 周期帧区
    QList<QComboBox*> m_camList;         ///< 8 路摄像头自检状态下拉框（CAM1..CAM8）
    QSpinBox*    m_uptime = nullptr;     ///< uptime_sec
    QCheckBox*   m_periodicOn = nullptr; ///< 周期帧自动发送开关
    QSpinBox*    m_periodicMs = nullptr; ///< 周期帧间隔
    QPushButton* m_btnPeriodic = nullptr;///< 立即发一帧
    // 识别回应区（自动回应配置：连接前配好、连接后即改即生效）
    QComboBox*   m_recScene = nullptr;   ///< 识别回应场景单选（含"不回应"选项，测识别超时）
    QSpinBox*    m_delayMs = nullptr;    ///< 识别回应延迟毫秒（0=立即；>3000 可测上位机超时重发）
    // 识别回应序列（多轮流程测试：按序回放，播完回落单场景默认）
    QCheckBox*   m_seqEnabled = nullptr; ///< 序列回应开关
    QListWidget* m_seqList = nullptr;    ///< 序列场景有序列表（每行存场景枚举值）
    int          m_seqCursor = 0;       ///< 序列当前游标（每次连接重置为0）
    QSpinBox*    m_silenceAt = nullptr;  ///< 几秒后停发周期帧
    QSpinBox*    m_silenceFor = nullptr; ///< 停发持续秒数
    // 版本/开箱/异常区
    QComboBox*   m_openPolicy = nullptr; ///< 开箱回应策略单选（自动成功/固定失败/不回应，3.7）
    QLineEdit*   m_openFailMsg = nullptr;///< 开箱"固定失败"时 open_ack 附带的原因文案
    QLineEdit*   m_version = nullptr;    ///< version_ack 版本号
    // 自定义帧区
    QTextEdit*   m_custom = nullptr;     ///< 自定义内容输入（每行一帧）
    QCheckBox*   m_rawSend = nullptr;    ///< 原文发送开关（不校验JSON，可发坏帧/任意文本）
    QSpinBox*    m_loopMs = nullptr;     ///< 循环发送间隔
    QPushButton* m_btnLoop = nullptr;    ///< 循环发送启停
    // 清单与本单配置区
    QTableWidget* m_orderTable = nullptr;///< 清单表（主编辑界面：一行=一个货位的识别结果）
    QLabel*      m_orderStat = nullptr;  ///< 清单统计文字（总行数/本单行/可发送条数）
    QTextEdit*   m_orderText = nullptr;  ///< 清单文本镜像（只读，可复制到 Excel 备用）
    QListWidget* m_inList = nullptr;     ///< 本单入库候选（待入库工具 + 其可入库位置）
    QListWidget* m_outList = nullptr;    ///< 本单出库候选（在库工具 + 其当前货位）
    QListWidget* m_borrowOutList = nullptr; ///< 本单借出转出库候选（借出中工具 + 其原位置）
    QCheckBox*   m_bgBaseline = nullptr; ///< 生成清单时是否附带全柜在库背景行
    // 清单选数据用的下拉与旧的选择区控件
    QComboBox*   m_pickPos = nullptr;       ///< 货位选择框（显示 A-01-01 工具名[在库]）
    QComboBox*   m_pickTool = nullptr;      ///< 工具选择框（显示 编号 工具名[在库]）
    QSpinBox*    m_pickConf = nullptr;      ///< 置信度（0~100，按百分比填更直观）
    QComboBox*   m_pickAlert = nullptr;     ///< 告警类型选择框（默认正常项）
    QSpinBox*    m_pickLevel = nullptr;     ///< 告警级别 0~3
    QLabel*      m_sceneHint = nullptr;     ///< 清单页顶部状态条：当前识别回应场景，这份清单生效没有
    // 数据库区
    QLineEdit*   m_dbHost = nullptr;        ///< 数据库地址
    QSpinBox*    m_dbPort = nullptr;        ///< 数据库端口
    QLineEdit*   m_dbName = nullptr;        ///< 库名（默认 smart_cabinet）
    QLineEdit*   m_dbUser = nullptr;        ///< 账号
    QLineEdit*   m_dbPass = nullptr;        ///< 口令（默认取 ini 里的本机联调库口令）
    QCheckBox*   m_chkLoadOnStart = nullptr; ///< 启动即读取工具与货位开关（默认开）
    bool         m_dbLoadOnStart = true;    ///< 启动即读库（实际生效值存 ini 的 db/loadOnStart）
    bool         m_tableUpdating = false;   ///< 重建清单表时为 true（屏蔽单元格联动，避免自触发）
    QLabel*      m_dbStat = nullptr;        ///< 连接与读取结果文字
    QComboBox*   m_groupCombo = nullptr;    ///< 机组下拉（识别清单按所选机组过滤，0=全部机组）
    int          m_selectedGroupId = 0;     ///< 当前所选机组；0 表示不过滤（全部机组）
    // 场景预设区
    QComboBox*   m_presetCombo = nullptr;   ///< 典型场景预设下拉
    QPushButton* m_btnOneClick = nullptr;   ///< 一键标准联调按钮
    QTabWidget*  m_tabs = nullptr;       ///< 异常注入页内部的分组页签（周期帧/版本异常/自定义帧）
    QTimer       m_loopTimer;            ///< 循环发送定时器
    // 日志
    QTextEdit*   m_logView = nullptr;
    QPushButton* m_btnSaveLog = nullptr;  ///< 保存日志按钮（导出日志区内容）
    QLabel*      m_stat = nullptr;

    QTcpSocket*  m_sock = nullptr;
    QTimer       m_periodicTimer;
    QJsonObject  m_lastRequest;      ///< 最近收到的recognize（回应对象）
    QJsonObject  m_lastOpenRequest;   ///< 最近收到的open_req（回应对象）
    bool         m_userDisconnect = false;  ///< 用户主动断开（不触发自动重连）
    int          m_reconnTry = 0;    ///< 自动重连退避计数（1/2/4/8/16/30秒封顶）
    int          m_rx = 0, m_tx = 0, m_bad = 0;
    QFile*       m_logFile = nullptr;
};
