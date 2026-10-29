/**
 * @file CabinetSessionDialog.h
 * @brief 普通用户智能柜会话对话框 - 借用/归还开柜会话（真实硬件闭环）
 * @author 袁燕
 * @说明 普通用户登录成功后进入本会话，不进管理主界面。
 *   开柜：经 EdgeService 下发开箱指令（在线/摄像头闸门），IO 板真实开柜；
 *   关柜：用户点"关闭智能柜"触发（TODO: 接入下位机 IO 板「已关柜」信号），
 *        随后发起全柜识别，与开柜基线（本机组在库货位）比对，得出借/还/错放清单；
 *   落库：确认关闭时调用 BorrowService/ReturnService 写库并记操作日志。
 *   复用：EdgeService（开箱/识别/在线/摄像头）、ToolService（全柜比对）、
 *        BorrowService/ReturnService（写库）、RecordDAO（操作日志）均为出入库侧已有能力。
 */
#ifndef CABINETSESSIONDIALOG_H
#define CABINETSESSIONDIALOG_H

#include <QDialog>
#include <QStackedWidget>
#include <QJsonObject>
#include <QList>

#include "services/ToolService.h"   // RecogSpot 类型（页面中立结构，不碰通信层）

class QLabel;
class QPushButton;
class QVBoxLayout;
class QScrollArea;
class TopBar;
class EdgeService;
class DrawerOpeningDialog;

/**
 * @brief 普通用户智能柜会话（全屏模态）
 *
 * 页面0：取放页（本机组在库工具参考面板 + 关闭智能柜按钮）
 * 页面1：关柜清单页（借用/归还/告警差异列表 + 重新开柜/确认关闭按钮）
 */
class CabinetSessionDialog : public QDialog {
    Q_OBJECT
public:
    explicit CabinetSessionDialog(const QJsonObject& user, QWidget* parent = nullptr);

    /// 以全屏方式启动会话并阻塞，会话结束后返回
    void startSession();

    /// 注入边缘机通信服务（复用 MainWindow 对出入库页的适配方式，不持有生命周期）
    void setEdgeService(EdgeService* svc);

signals:
    /// 会话内顶栏点击"注销"：先结束会话，主窗口收到后执行注销回登录页
    void logoutRequested();

public slots:
    // MainWindow 适配层转发的开箱/识别结果（与出入库页同款接线方式）
    void onOpenFinished(const QString& requestId, bool success, const QString& message);
    void onOpenFailed(const QString& requestId, const QString& reason);
    void onRecognitionResult(const QString& requestId, const QList<ToolService::RecogSpot>& spots);
    void onRecognitionFailed(const QString& requestId, const QString& reason);
    void onEdgeOnlineChanged(bool online);

private slots:
    // ── 开柜 ──
    void onOpenCabinetClicked();                       // 点"开柜"：在线/摄像头闸门 + requestOpen
    void onOpenStarted(const QString& requestId);
    // ── 关柜 + 识别 ──
    void onCloseCabinetClicked();                     // 点"关闭智能柜"（或下位机「已关柜」信号）：发起识别
    void onRecognitionStarted(const QString& requestId);
    // ── 关柜等待 ──
    void onCloseWaitTimeout();                        // 柜体打开后长时间未关柜：提示确认柜门已关（零写库）
    // ── 结果页 ──
    void onConfirmCloseClicked();                     // 确认关闭：借/还写库 + 操作日志
    void onReopenClicked();                           // 重新开柜整理（错放≤3轮）

private:
    void buildOpenPage();      // 构建开柜提示页
    void buildResultPage();    // 构建关柜清单页
    bool passEntryGate();      // 进入会话自动自检（边缘机在线+摄像头），不通过弹原因并返回 false
    void startCloseWaitTimer(); // 柜体打开后启动关柜等待计时（超时提示确认柜门已关）
    void showWait(const QString& windowTitle, const QString& title, const QString& desc); // 开柜中/核对中等待框
    void closeWait();          // 关闭并释放等待框
    void applyResult();        // 识别结果比对并填充清单页
    void fillResultUI();       // 把借/还/告警结果渲染到清单页

    // 开柜页"我的任务工具"只读面板（本机组任务类型+在库工具，位置维度）
    struct ToolRow { QString name; QString code; QString pos; };
    struct MyTypeTools { int typeId; QString typeName; QList<ToolRow> tools; };
    void loadMyTaskTools();                  // 读库：本机组全部任务类型及各自在库工具
    void selectTypeTab(int idx);             // 切换任务类型页签
    void rebuildToolCards();                 // 按当前页签重建工具卡片网格（三列）
    QWidget* makeToolCard(const ToolRow& row); // 构建单个只读工具卡片
    QList<MyTypeTools> m_myTypes;            // 任务类型及在库工具数据
    int m_curTypeIdx = 0;                    // 当前选中页签索引
    QList<QPushButton*> m_typeTabBtns;       // 任务类型页签按钮
    QScrollArea* m_toolsScroll = nullptr;    // 工具卡片滚动区
    QLabel* m_stockCountLabel = nullptr;     // "在库 N 件"统计标签

    // 开柜基线：开柜瞬间本机组全部在库货位（与关柜识别结果比对）
    struct StockItem {
        int mappingId = 0;
        int toolId = 0;
        QString toolCode;
        QString toolName;
        QString position;        // 格式化后货位（A-01-02），与识别 spot.position 对齐
    };
    QList<StockItem> m_baselineStock;        // 开柜基线（在库货位快照）

    // 本次识别结论（供确认关闭写库）
    struct BorrowItem { int toolId = 0; int mappingId = 0; QString toolCode; QString toolName; QString position; };
    struct ReturnItem { int recordId = 0; QString toolCode; QString toolName; QString position; };
    QList<BorrowItem> m_borrowItems;         // 借出（在库→缺失）
    QList<ReturnItem> m_returnItems;         // 归还（用户已借出→柜内复现）
    QStringList m_alertItems;                // 告警（错放/未登记工具）

    QJsonObject m_user;              // 当前登录用户
    int m_userId = 0;                // 用户ID（写库用）
    int m_machineGroupId = 0;        // 本机组ID（按机组隔离）
    EdgeService* m_edge = nullptr;   // 边缘机通信服务（MainWindow 注入）
    QStackedWidget* m_stack = nullptr;

    // 页面0：取放页
    QWidget* m_openReady = nullptr;  // 开柜成功后展示：欢迎语 + 任务工具面板 + 关闭智能柜
    QPushButton* m_closeBtn = nullptr;
    QLabel* m_iconLabel = nullptr;   // 页面标题图标
    QLabel* m_titleLabel = nullptr;  // 页面标题

    // 页面1：关柜清单
    QVBoxLayout* m_borrowListLayout = nullptr;
    QVBoxLayout* m_returnListLayout = nullptr;
    QVBoxLayout* m_alertListLayout = nullptr;
    QLabel* m_noChangeLabel = nullptr;
    QWidget* m_borrowBlock = nullptr;
    QWidget* m_returnBlock = nullptr;
    QWidget* m_alertBlock = nullptr;
    QPushButton* m_reopenBtn = nullptr;   // 重新开柜整理（错放≤3轮）

    // 流程状态
    QString m_openRequestId;          // 在途开箱请求编号
    QString m_recognizeRequestId;     // 在途识别请求编号
    QList<ToolService::RecogSpot> m_spots; // 最近一次识别实况
    DrawerOpeningDialog* m_waitDlg = nullptr; // "开柜中/核对中"等待框
    int m_round = 1;                  // 当前轮次（错放重开柜，上限见 kMaxRounds）
    bool m_opened = false;            // 是否已真实开柜
    QTimer* m_closeWaitTimer = nullptr; // 关柜等待计时（柜体打开起算，超时提示确认柜门已关）

    static const int kMaxRounds = 3;  // 错放重开柜次数上限（含首次共 3 轮）
};

#endif // CABINETSESSIONDIALOG_H
