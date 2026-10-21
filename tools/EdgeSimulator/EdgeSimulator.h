/**
 * @file  EdgeSimulator.h
 * @brief 边缘计算机模拟器（带界面）——按协议逐接口覆盖的测试工具
 * @author 袁燕
 *
 * 覆盖范围（与《智能工具柜配置项通信协议》对应）：
 *   3.3 recognize_ack 全部场景回应（正常/有借有还/无变化/错放/未知物/
 *       置信度低/missing/自定义）——alert_type 全部枚举值覆盖
 *   3.3 异常回应（延迟/重复×2/粘包/不回应/乱序request_id）
 *   3.4 periodic 周期帧（相机5态自检/空自检/uptime）
 *   3.5 version_ack（版本号可配/自动回应）
 *   3.6 坏帧/未知type/超长帧/断开
 *   第5章 停发周期帧（验证离线判定与恢复补发）
 *   断线自动重连（模拟真实边缘机退避重连行为）
 * 每个按钮对应一个协议测试场景，协议依据与期望结果见《接口测试方案.txt》。
 * 完整注释见 EdgeSimulator.cpp 各函数实现处。
 */
#pragma once
#include <QWidget>
#include <QTcpSocket>
#include <QTimer>
#include <QJsonObject>

class QTextEdit;
class QPushButton;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QLineEdit;
class QLabel;
class QTabWidget;
class QFile;

class EdgeSimulator : public QWidget {
    Q_OBJECT
public:
    explicit EdgeSimulator(QWidget* parent = nullptr);
    ~EdgeSimulator() override;

    /// 一键连接（供 --auto-connect 命令行与自动化测试调用）
    void connectToEdge();

    /// 切换到指定页签（供 --tab 命令行与截图自动化调用）
    void showTab(int index);

private slots:
    // 连接
    void onConnectToggle();      ///< 连接/断开上位机
    void onSocketConnected();    ///< 连接建立
    void onSocketDisconnected(); ///< 连接断开
    void onReadyRead();          ///< 收到上位机帧
    // 周期帧（协议3.4）
    void sendPeriodicNow();      ///< 立即发一帧周期帧
    void togglePeriodic(bool on);///< 周期帧自动发送启停
    // 识别回应（协议3.3）
    void replyScene(int scene);  ///< 按场景回应最近recognize
    void sendDelayedReply();     ///< 延迟回应（毫秒可设）
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

private:
    QWidget* buildConnPanel();   ///< 连接区
    QWidget* buildPeriodicTab(); ///< 周期帧Tab（3.4）
    QWidget* buildRecognizeTab();///< 识别回应Tab（3.3）
    QWidget* buildFaultTab();    ///< 异常注入Tab（3.6/第5章）
    QWidget* buildCustomTab();   ///< 自定义帧Tab
    void processLine(const QByteArray& line);   ///< 分帧解析（收）
    /**
     * @brief 取当前 8 路摄像头中状态异常的摘要（非 ok 且非"不报自检"）
     * @return 形如 "CAM3(offline) CAM5(dirty)"；全部正常或均为空时返回空串
     */
    QString cameraAbnormalText() const;
    bool sendFrame(const QJsonObject& frame);   ///< 发送一帧
    QJsonObject sceneAck(int scene) const;      ///< 构造场景回应帧
    void sendOpenAckNow(bool success, const QString& message);  ///< 立即回应最近open_req（开箱协议3.7；success=false 即 result=0 失败）
    void log(const QString& color, const QString& text);
    void logFrame(const QString& dir, const QJsonObject& f);
    void logEvent(const QString& text) { log("#666666", "[事件] " + text); }
    void refreshStat();

    // 连接区控件
    QLineEdit*   m_addr = nullptr;
    QSpinBox*    m_port = nullptr;
    QPushButton* m_btnConn = nullptr;
    QLabel*      m_state = nullptr;
    QCheckBox*   m_autoReconn = nullptr;  ///< 断开后自动重连开关
    // 周期帧Tab
    QList<QComboBox*> m_camList;         ///< 8 路摄像头自检状态下拉框（CAM1..CAM8）
    QSpinBox*    m_uptime = nullptr;     ///< uptime_sec
    QCheckBox*   m_periodicOn = nullptr; ///< 周期帧自动发送开关
    QSpinBox*    m_periodicMs = nullptr; ///< 周期帧间隔
    QPushButton* m_btnPeriodic = nullptr;///< 立即发一帧
    // 识别回应Tab
    QSpinBox*    m_delayMs = nullptr;    ///< 延迟回应毫秒
    QSpinBox*    m_silenceAt = nullptr;  ///< 几秒后停发周期帧
    QSpinBox*    m_silenceFor = nullptr; ///< 停发持续秒数
    // 版本Tab并入异常Tab
    QLineEdit*   m_version = nullptr;    ///< version_ack 版本号
    // 自定义帧Tab
    QTextEdit*   m_custom = nullptr;     ///< 自定义内容输入（每行一帧）
    QCheckBox*   m_rawSend = nullptr;    ///< 原文发送开关（不校验JSON，可发坏帧/任意文本）
    QSpinBox*    m_loopMs = nullptr;     ///< 循环发送间隔
    QPushButton* m_btnLoop = nullptr;    ///< 循环发送启停
    QTabWidget*  m_tabs = nullptr;       ///< 页签控件（showTab 用）
    QTimer       m_loopTimer;            ///< 循环发送定时器
    // 日志
    QTextEdit*   m_logView = nullptr;
    QLabel*      m_stat = nullptr;

    QTcpSocket*  m_sock = nullptr;
    QTimer       m_periodicTimer;
    QJsonObject  m_lastRequest;      ///< 最近收到的recognize（回应对象）
    QJsonObject  m_lastOpenRequest;   ///< 最近收到的open_req（回应对象）
    bool         m_autoReply = true; ///< 收到recognize自动回应开关（识别协议3.3）
    bool         m_autoOpenReply = true; ///< 收到open_req自动回应开关（开箱协议3.7）；关闭后可手动发失败/不回应以测异常分支
    bool         m_userDisconnect = false;  ///< 用户主动断开（不触发自动重连）
    int          m_reconnTry = 0;    ///< 自动重连退避计数（1/2/4/8/16/30秒封顶）
    int          m_rx = 0, m_tx = 0, m_bad = 0;
    QFile*       m_logFile = nullptr;
};