/**
 * @file ToolCheckinPage.h
 * @brief 工具出入库页面 - 三选项卡（出入库管理 + 入库记录 + 出库记录）
 * @author 袁燕
 *
 * 出入库以"单据"方式操作：
 *   1. 单据列表逐行添加指令，每行仅两项内容：工具名称 + 类型（入库/出库），
 *      位置由系统按对照关系自动带出（一个工具只在一个位置）
 *   2. 点击"确认出入库"先进入开箱环节（柜体打开动画 + 单据摘要）
 *   3. 用户按单据放入/取出工具后关闭柜门，系统逐项执行并检查：
 *      成功项写入记录（入库写 checkin 日志 / 出库写 checkout 日志），
 *      未完成项红字提示且不写记录，可重新执行
 * 入库成功写 sys_operation_log(operation_type='checkin')，出库成功写
 * sys_operation_log(operation_type='checkout')，两个记录 Tab 分别展示历史。
 */
#pragma once
#include <QWidget>
#include <QDialog>
#include <QComboBox>
#include <QPushButton>
class QTableWidget;  // 前置声明：仅指针使用，降低编译耦合
#include <QLabel>
#include <QTabWidget>
#include <QJsonObject>
#include <QJsonArray>

class PaginationBar;  // 通用分页栏组件

namespace Ui { class ToolCheckinPage; }  // Qt Designer 生成的静态布局

class ToolCheckinPage : public QWidget {
    Q_OBJECT
public:
    explicit ToolCheckinPage(QWidget* parent = nullptr);
    ~ToolCheckinPage();
    void refresh();
    void setUser(const QJsonObject& user) { m_user = user; }  // 设置当前登录用户

private slots:
    void onAddOrderRow();      // 添加一行入库指令
    void onAddOrderRowOut();   // 添加一行出库指令
    void onSubmit();           // 确认出入库：校验单据→开箱→关柜检查
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
    };

    void setupUI();
    void loadLocalMachineGroup();      // 读取本机组信息回填界面
    void loadCheckinRecords();         // 加载入库历史记录
    void loadCheckoutRecords();        // 加载出库历史记录

    // ── 出入库单据 ──
    void appendOrderRow(const QString& type);            // 向单据表格追加一行
    void reloadToolOptions(int row);                     // 按行类型重载该行工具下拉
    void updateRowPosition(int row);                     // 行内选工具后自动带出位置
    int  rowOfWidget(QWidget* w);                        // 查找单元格控件所在行号
    QList<OrderRow> collectOrderItems(QString& errorMsg); // 收集并校验单据，失败返回空列表
    void clearOrderTable();                              // 清空单据

    // ── 确认后的两段式流程 ──
    bool validateOrderItems(const QList<OrderRow>& items); // 执行前最终校验（位置实时核对）
    void showOrderExecuteDialog(const QList<OrderRow>& items); // 开箱环节：柜体打开+单据摘要
    void showCheckResultDialog(const QList<OrderRow>& items);  // 关柜检查：逐项执行并显示结果

    // Tab容器
    QTabWidget* m_tabWidget = nullptr;

    // 单据表格
    QTableWidget* m_orderTable = nullptr;

    // 机组信息
    QLabel* m_machineGroupLabel = nullptr;
    int m_localMachineGroupId = 0;

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
