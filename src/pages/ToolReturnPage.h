/**
 * @file ToolReturnPage.h
 * @brief 工具归还页面（1:1复刻BS端ToolReturn.vue）
 * @author 袁燕
 * @修改说明 V1.00.9 2026-06-15 完全重构以匹配BS端
 *   - 添加用户信息条（姓名/工号/日期）
 *   - 添加提示栏
 *   - 表格列：借用时间/工具名称/位置/借用数量/借用原因/流水号/状态
 *   - 触屏优化：按钮最小48px，字体16px+
 * @修改说明 V1.00.10 2026-06-24 归还流程模拟改造 + 页面美化对齐ToolBorrowPage
 *   - 添加模拟异常提示（工具未放好），用户调整后再次点击归还成功
 *   - 美观的自定义异常/成功对话框，与ToolBorrowPage风格统一
 *   - 用户信息条从竖向布局改为横向布局（归还人/工号/所属机组/日期）
 *   - 提示栏从橙色警告改为蓝色提示风格
 *   - 底部操作栏添加白色背景+顶部边框+返回按钮
 *   - 确认归还按钮改用紫色渐变主按钮风格
 * @修改说明 V8.3 2026-06-25 添加分页功能
 */
#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QJsonArray>
#include <QPushButton>
#include <QTableWidget>
#include <QLabel>
#include <QSet>
#include "services/ReturnService.h"  // [V8.4] showReturnSuccessDialog需要ReturnService::Result类型

class ToolReturnPage : public QWidget {
    Q_OBJECT
public:
    explicit ToolReturnPage(QWidget* parent = nullptr);
    ~ToolReturnPage();
    void setUser(const QJsonObject& user);
    void refresh();
    // [2026-06-27] 从借用记录页面跳转时设置待归还记录ID，自动勾选
    void setPendingReturnRecordId(int recordId);

signals:
    // [V1.00.10] 返回按钮信号，由父级路由处理
    void backClicked();

private slots:
    void onSelectAll(bool checked);
    void onReturnSelected();
    void onReturnAll();
    void onPrevPage();  // [2026-06-25] 上一页
    void onNextPage();  // [2026-06-25] 下一页

private:
    void setupUI();
    void loadRecords();
    void updateReturnBtn();
    // [2026-06-27] 归还确认四步流程（与借用页风格对齐）
    void showReturnConfirmDialog();                                    // 步骤1：归还清单确认
    void showReturnDrawerOpeningDialog();                              // 步骤2：抽屉打开中（动画）
    void showReturnErrorDialog();                                      // 步骤3：工具未放好异常（假异常）
    void showReturnSuccessDialog(const ReturnService::Result& result); // 步骤4：归还成功对话框
    void executeReturn();              // [V1.00.10] 执行实际归还操作

    QJsonObject m_user;

    // [V1.00.10] 用户信息条（横向布局对齐ToolBorrowPage）
    QLabel* m_userNameLabel;           // 归还人
    QLabel* m_workNoLabel;             // 工号
    QLabel* m_deptLabel = nullptr;     // 所属机组（V1.00.10新增）
    QLabel* m_dateLabel;               // 日期

    // 提示栏
    QLabel* m_warningLabel;

    // 操作按钮
    QPushButton* m_selectAllBtn;       // [编译兼容] 恢复声明

    // 表格
    QTableWidget* m_table;
    QSet<int> m_checkedRecordIds;
    QJsonArray m_records;

    // [V1.00.10] 归还流程状态标记
    bool m_returnAttempted = false;    // 是否已经模拟过异常，第二次点击执行实际归还

    // [2026-06-27] 从借用记录跳转时，待自动选中的归还记录ID
    int m_pendingReturnRecordId = 0;

    // [2026-06-25] 分页
    QPushButton* m_prevBtn = nullptr;
    QPushButton* m_nextBtn = nullptr;
    QLabel* m_pageLabel = nullptr;
    QLabel* m_totalLabel = nullptr;
    int m_currentPage = 1;
    int m_pageSize = 20;
    int m_totalRecords = 0;
};
