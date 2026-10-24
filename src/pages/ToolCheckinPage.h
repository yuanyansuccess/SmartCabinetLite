/**
 * @file ToolCheckinPage.h
 * @brief 工具出入库页面 - 三选项卡（出入库管理 + 入库记录 + 出库记录）
 * @author 袁燕
 *
 * 出入库以"单据"方式操作：
 *   1. 单据列表逐行添加指令，每行仅两项内容：工具名称 + 类型（入库/出库），
 *      位置由系统按对照关系自动带出（一个工具只在一个位置）
 *   2. 点击"确认出入库"先进入开箱环节（柜体打开动画 + 单据摘要）
 *   3. 用户按单据放入/取出工具后关闭柜门，系统进行边缘机核对：
 *      比对全过则整单写入记录（入库写 checkin 日志 / 出库写 checkout 日志）；
 *      未全过可【重新开柜整理】重来（上限 SC::ORDER_MAX_RETRY_ROUNDS 轮）或
 *      【按当前实况写库】（通过项写入、未过项红字提示且不写记录）
 * 入库成功写 sys_operation_log(operation_type='checkin')，出库成功写
 * sys_operation_log(operation_type='checkout')，两个记录 Tab 分别展示历史。
 */
#pragma once
#include <QWidget>
#include <QDialog>
#include <QComboBox>
#include <QPushButton>
#include <QStringList>
class QTableWidget;  // 前置声明：仅指针使用，降低编译耦合
#include <QLabel>
#include <QTabWidget>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include "services/ToolService.h"  // 单据与实况比对（services 层，页面经中立结构调用）

class PaginationBar;  // 通用分页栏组件
class DrawerOpeningDialog;  // 开柜/取放工具统一弹窗（识别等待态复用）
class QTimer;  // 关柜等待超时计时

namespace Ui { class ToolCheckinPage; }  // Qt Designer 生成的静态布局

class ToolCheckinPage : public QWidget {
    Q_OBJECT
public:
    /// 单据轮次状态（内存态）：Running=本轮核对中；Passed=本轮全部通过；
    /// Partial=有未过项可继续整理；GivenUp=轮次用尽按当前实况写库
    enum class RoundState { Running, Passed, Partial, GivenUp };

    explicit ToolCheckinPage(QWidget* parent = nullptr);
    ~ToolCheckinPage();
    void refresh();
    void setUser(const QJsonObject& user) { m_user = user; }  // 设置当前登录用户

signals:
    /// 请求 MainWindow 向边缘机发起识别（页面不直接持有通信层，符合分层铁律）
    void recognitionRequested();
    /// 请求 MainWindow 向边缘机发起开箱（开锁）指令（页面不直接持有通信层）
    void openRequested();

public slots:
    /// MainWindow 回传识别请求结果：requestId 非空=已发出（进入等待态）；空串=未发出（离线/写入失败）
    void onRecognitionStarted(const QString& requestId);
    /// 识别成功：requestId 与当前在途请求配对，spots 为全柜实况清单（页面中立结构）
    void onRecognitionResult(const QString& requestId, const QList<ToolService::RecogSpot>& spots);
    /// 识别失败（超时/离线/补发放弃）：reason 为人读原因，直接提示用户
    void onRecognitionFailed(const QString& requestId, const QString& reason);
    /// 边缘机链路心跳状态变化（周期帧驱动）：更新链路标签，等待中掉线时即时更新
    void onEdgeOnlineChanged(bool online);
    /// 开箱指令回传：requestId 非空=已发出（进入开柜等待态）；空串=未发出（离线/写入失败）
    void onOpenStarted(const QString& requestId);
    /// 开箱成功（柜体已真实打开）：统一弹窗原地切换到"柜体已打开"状态（不再新弹窗）
    void onOpenFinished(const QString& requestId, bool success, const QString& message);
    /// 开箱失败（超时/离线/补发放弃）：提示并终止本次
    void onOpenFailed(const QString& requestId, const QString& reason);

private slots:
    void onAddOrderRow();      // 添加一行入库指令
    void onAddOrderRowOut();   // 添加一行出库指令
    void onBatchImportInRows();// 一键导入入库行：批量勾选待入库工具，一次生成多条入库指令
    void onSubmit();           // 确认出入库：校验单据→发开箱指令→关柜检查
    void cancelOpen();         // 开柜等待中用户取消（清状态，零写库）
    void onCloseWaitTimeout(); // 柜体打开后等待关柜超时：关闭弹窗并提示确认柜门已关（零写库）
    void onTabChanged(int index);   // Tab切换时加载数据
    void onRecordPrevPage();   // 入库记录上一页
    void onRecordNextPage();   // 入库记录下一页
    void onOutRecordPrevPage();  // 出库记录上一页
    void onOutRecordNextPage();  // 出库记录下一页

private:
    /// 单据一行指令执行所需的数据（自表格控件收集）
    struct OrderRow {
        QString type;        // "in"=入库 / "out"=出库
        int toolId = 0;      // 工具ID
        int mappingId = 0;   // 出库行的位置映射ID（入库行为0）
        QString toolCode;    // 工具编号
        QString toolName;    // 工具名称
        QString position;    // 位置展示串（A-01-02）
        QJsonObject posInfo; // 位置原始信息（cabinetId/layer/position等，入库执行用）
        bool borrowedOut = false; // true=该出库行是"已借出未归还"转出库（工具不在柜内，不需要开箱）
    };

    void setupUI();
    void loadLocalMachineGroup();      // 读取本机组信息回填界面
    void loadCheckinRecords();         // 加载入库历史记录
    void loadCheckoutRecords();        // 加载出库历史记录

    // ── 出入库单据 ──
    void appendOrderRow(const QString& type);            // 向单据表格追加一行
    void appendInRowForTool(int toolId);                 // 追加一行入库指令并预选指定工具（已有该工具行则跳过）
    void reloadToolOptions(int row);                     // 按行类型重载该行工具下拉
    void updateRowPosition(int row);                     // 行内选工具后自动带出位置
    int  rowOfWidget(QWidget* w);                        // 查找单元格控件所在行号
    QList<OrderRow> collectOrderItems(QString& errorMsg); // 收集并校验单据，失败返回空列表
    void clearOrderTable();                              // 清空单据

    // ── 确认后的两段式流程 ──
    bool validateOrderItems(QList<OrderRow>& items); // 执行前最终校验（位置实时核对并回填）
    bool orderNeedsOpening(const QList<OrderRow>& items) const; // 单据是否需要开箱
    void submitBorrowedOutWithoutOpening(const QList<OrderRow>& items); // 整单借出转出库：不开箱直接写库并出结果页

    /**
     * @brief 构建"开柜+取放工具"统一弹窗（开柜中与柜体已打开共用同一个页面）
     * @param items 本次要展示的出入库单据（首次=全量单据；重新开柜整理=未通过项）
     * @return 新建弹窗指针（显示与关闭由调用方负责）
     * @note 弹窗初始为"正在打开柜体"状态，开柜成功后由 applyOpenOpenedState 原地切换文案与按钮，
     *       全程只有一个页面，避免两个弹窗先后出现造成画面跳闪
     */
    DrawerOpeningDialog* buildOpenExecuteDialog(const QList<OrderRow>& items);

    /**
     * @brief 统一弹窗切换到"柜体已打开"状态：更新标题、提示文案与按钮，并启动关柜等待计时
     * @param dlg 统一弹窗指针（空指针直接返回，不做处理）
     */
    void applyOpenOpenedState(DrawerOpeningDialog* dlg);

    /**
     * @brief 关闭统一弹窗（程序内部关闭：开柜失败/链路中断等场景，不弹"用户取消"提示）
     */
    void closeOpenExecuteDialog();
    /// 关柜检查：按边缘机比对结论逐项执行并显示结果（含三轮重开柜流程）
    /// @param items         单据指令列表（与 compareItems 同序，为全量单据 m_orderItems）
    /// @param compareItems  比对结论：Matched 项视为通过，其余项红字展示不写库
    /// @param misplaced     错放明细（结果页提示行，不写库）
    /// @param stockOutcome  全柜核对结论（库内全量货位 vs 实况清单；diffs 为空=全柜一致）
    /// @param round         当前轮次（从 1 开始，用于判定是否仍可重新开柜整理）
    /// @param maxRounds     重试上限（SC::ORDER_MAX_RETRY_ROUNDS），到达后只给按实况写库
    void showCheckResultDialog(const QList<OrderRow>& items,
                               const QList<ToolService::RecogCompareItem>& compareItems,
                               const QList<ToolService::RecogMisplaced>& misplaced,
                               const ToolService::RecogStockOutcome& stockOutcome,
                               int round, int maxRounds);

    /**
     * @brief 写本轮识别的操作日志（每轮一条，用于追溯"第几轮、哪些项没过"）
     * @param round        当前轮次（从 1 开始）
     * @param items        本轮核对的全量单据
     * @param compareItems 本轮逐项比对结论
     * @param misplaced    本轮错放明细
     * @param allPassed    本轮是否全部通过
     * @return 无。写库失败只记录日志，不影响界面
     * @note  日志内容按约定写 JSON：{round, order[], misplaced[], unknown[], result}
     */
    void writeRoundLog(int round, const QList<OrderRow>& items,
                       const QList<ToolService::RecogCompareItem>& compareItems,
                       const QList<ToolService::RecogMisplaced>& misplaced,
                       bool allPassed);

    // ── 边缘机识别两段式流程 ──
    void startEdgeCheck();  // 发起识别并进入等待态（用全量单据 m_orderItems，链路离线则拒绝本次）
    void applyRecognitionOutcome();                     // 识别回来：比对→写库→结果页
    void exitWaitState();                               // 退出等待态（失败/取消时恢复界面可用）
    void resetOrderSession();                            // 复位本次单据会话（全量单据/轮次/状态/在途请求）

    // Tab容器
    QTabWidget* m_tabWidget = nullptr;

    // 单据表格
    QTableWidget* m_orderTable = nullptr;

    // 机组信息
    QLabel* m_machineGroupLabel = nullptr;
    int m_localMachineGroupId = 0;

    bool m_edgeOnline = false;             // 边缘机链路在线缓存（周期帧驱动，开箱前离线闸门校验用）

    // 在途识别状态：等待边缘机识别结果期间暂存
    QString m_currentRequestId;          ///< 当前在途识别请求编号（空=无在途）
    QList<OrderRow> m_pendingItems;      ///< 等待识别结果的单据（识别期间暂存）
    QList<ToolService::RecogSpot> m_lastSpots;  ///< 最近一次识别返回的全柜实况清单
    ToolService::RecogStockOutcome m_lastStockOutcome;  ///< 最近一次全柜核对结论（结果页展示用）
    QList<OrderRow> m_orderItems;        ///< 本轮出入库全量单据（首轮确认后固定，重开柜不重置）
    int m_currentRound = 1;              ///< 当前重试轮次（从 1 开始，到达上限后只给按实况写库）
    RoundState m_roundState = RoundState::Running;  ///< 本单轮次状态（内存态，控制结果页出口与文案）
    QDialog* m_waitDialog = nullptr;     ///< 识别等待框（模态，结果回来/失败时关闭）
    QString m_openRequestId;             ///< 当前在途开箱请求编号（空=无在途）
    QList<OrderRow> m_pendingOpenItems;  ///< 开箱成功后待进入开箱环节的单据（开箱期间暂存）
    bool m_reopenPending = false;        ///< 下一次开箱属"重新开柜整理"（保留轮次与全量单据，不重置）
    DrawerOpeningDialog* m_openExecuteDialog = nullptr; ///< 开柜+取放工具统一弹窗（开柜中→柜体已打开，同一页面原地切换）
    bool m_openDialogOpened = false;      ///< 统一弹窗是否已切换到"柜体已打开"状态（区分开柜阶段与取放阶段）
    bool m_openDialogInternalClose = false; ///< 统一弹窗是否为程序内部关闭（区分用户取消与程序关闭）
    QTimer* m_closeWaitTimer = nullptr;   ///< 关柜等待超时计时（柜体打开后起算，超时提示确认柜门已关）

    // 状态
    QLabel* m_errorLabel = nullptr;
    QPushButton* m_submitBtn = nullptr;
    QPushButton* m_resetBtn = nullptr;

    // 入库记录表格 + 分页
    QTableWidget* m_recordTable = nullptr;
    PaginationBar* m_recordPaginationBar = nullptr;  // 通用分页栏
    int m_recordCurrentPage = 1;
    int m_recordPageSize = 20;
    int m_recordTotalRecords = 0;

    // 出库记录表格 + 分页
    QTableWidget* m_outRecordTable = nullptr;
    PaginationBar* m_outRecordPaginationBar = nullptr;  // 通用分页栏
    int m_outRecordCurrentPage = 1;
    int m_outRecordPageSize = 20;
    int m_outRecordTotalRecords = 0;

    // 当前登录用户信息（用于出入库记录操作人）
    QJsonObject m_user;

    Ui::ToolCheckinPage* ui = nullptr;  // 静态布局（ToolCheckinPage.ui）
};
