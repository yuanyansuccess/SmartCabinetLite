/**
 * @file ToolReturnPage.h
 * @brief 工具归还页面（1:1复刻BS端ToolReturn.vue）
 * @author 袁燕
 */
#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QJsonArray>
#include <QPushButton>
class QTableWidget;  // 前置声明：仅指针使用，降低编译耦合
#include <QLabel>
#include <QSet>
#include "services/ReturnService.h"  // showReturnSuccessDialog需要ReturnService::Result类型

class PaginationBar;  // 通用分页栏组件

namespace Ui { class ToolReturnPage; }  // Qt Designer 生成的静态布局

class ToolReturnPage : public QWidget {
    Q_OBJECT
public:
    explicit ToolReturnPage(QWidget* parent = nullptr);
    ~ToolReturnPage();
    void setUser(const QJsonObject& user);
    void refresh();
    // 从借用记录页面跳转时设置待归还记录ID，自动勾选
    void setPendingReturnRecordId(int recordId);

signals:
  // 返回按钮信号，由父级路由处理
    void backClicked();

private slots:
    void onSelectAll(bool checked);
    void onReturnSelected();
    void onReturnAll();
    void onPrevPage();  // 上一页
    void onNextPage();  // 下一页

private:
    void setupUI();
    void loadRecords();
    void updateReturnBtn();
    // 归还确认四步流程（与借用页风格对齐）
    void showReturnConfirmDialog();                                    // 步骤1：归还清单确认
    void showReturnDrawerOpeningDialog();                              // 步骤2：抽屉打开中（动画）
    void showReturnErrorDialog();                                      // 步骤3：工具未放好异常（假异常）
    void showReturnSuccessDialog(const ReturnService::Result& result); // 步骤4：归还成功对话框
    void executeReturn();  // 执行实际归还操作

    QJsonObject m_user;

  // 用户信息条（横向布局对齐ToolBorrowPage）
    QLabel* m_userNameLabel;           // 归还人
    QLabel* m_workNoLabel;             // 工号
    QLabel* m_deptLabel = nullptr;     // 所属机组（V1.00.10新增）
    QLabel* m_dateLabel;               // 日期

    // 提示栏
    QLabel* m_warningLabel;

    // 操作按钮
    QPushButton* m_selectAllBtn;  // 恢复声明

    // 表格
    QTableWidget* m_table;
    QSet<int> m_checkedRecordIds;
    QJsonArray m_records;

  // 归还流程状态标记
    bool m_returnAttempted = false;    // 是否已经模拟过异常，第二次点击执行实际归还

    // 从借用记录跳转时，待自动选中的归还记录ID
    int m_pendingReturnRecordId = 0;

    // 分页
    PaginationBar* m_paginationBar = nullptr;  // 通用分页栏
    int m_currentPage = 1;
    int m_pageSize = 20;
    int m_totalRecords = 0;

    Ui::ToolReturnPage* ui = nullptr;  // 静态布局（ToolReturnPage.ui）
};
