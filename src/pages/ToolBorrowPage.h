/**
 * @file ToolBorrowPage.h
 * @brief 工具借用页面（1:1复刻BS端ToolBorrow.vue）
 * @author 袁燕
 * @修改说明 V1.00.9 2026-06-15 添加标签页导航
 *   - 标签页：借用任务/常用工具/借用记录
 *   - 任务类型多选checkbox
 *   - 添加预计归还时间选择器
 *   - 添加单位字段
 * [V6.6 2026-06-22] 搜索框集成SoftKeyboard + 表格内边距对齐Web
 * [V7.1 2026-06-24] 借用确认流程优化：确认对话框→工具核对→写入记录
 * [V7.4 2026-06-24] 推荐工具默认勾选 + 多工具借用 + 全部工具列表中标记推荐
 * [V8.3 2026-06-25] 添加分页功能（全部工具表+借用记录表）
 */
#pragma once
#include <QWidget>
#include <QDialog>
#include <QJsonObject>
#include <QJsonArray>
#include <QPushButton>
#include <QTableWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QList>
#include <QTabWidget>
#include <QDateTimeEdit>
#include <QCheckBox>
#include <QFrame>
#include <QVBoxLayout>
#include <QPair>
#include <QTimer>
#include <QSet>

class SoftKeyboard;  // [V6.6]

// [V7.4] 选中的工具信息结构
// 按工具种类选中，借用时自动分配位置
// 加回mappingId，展开清单时保存具体位置ID
// 根因：V2.12去掉mappingId后，executeBorrow重新查"第一个in_stock位置"，
// 可能与展开分配的位置不匹配→借用记录mappingId与实际借用的位置不一致
struct SelectedToolInfo {
    int toolId = 0;
    int mappingId = 0;  // [V2.12-fix3] 展开清单时保存的具体位置mappingId
    QString toolName;
    QString toolCode;     // [2026-06-27] 工具编号
    QString position;     // [2026-06-27] 存放位置（借用时自动分配具体位置）
    int stock = 0;        // 可用库存数（in_stock位置数）
    int quantity = 1;     // 用户选择的借用数量
};

class ToolBorrowPage : public QWidget {
    Q_OBJECT
public:
    explicit ToolBorrowPage(QWidget* parent = nullptr);
    ~ToolBorrowPage();
    void setUser(const QJsonObject& user);
    void refresh();

signals:
    // [2026-06-27] 点击归还按钮时请求跳转到工具归还页面，携带recordId用于自动选中
    void returnRequested(int recordId);

private slots:
    void onTaskTypeChanged();
    void onToolSelected(int row, int col);
    void onBorrowConfirm();
    void onReasonChanged(int index);
    void onSearchTool();
    // 任务类型下拉面板相关
    void onTaskTypeBtnClicked();
    void onFilterTaskTypes();
    void onConfirmTaskTypes();
    void onSearchFieldClicked();  // [V6.6] 工具搜索框软键盘
    // [2026-06-25] 分页
    void onToolPrevPage();     // 全部工具表上一页
    void onToolNextPage();     // 全部工具表下一页
    void onRecordPrevPage();   // 借用记录表上一页
    void onRecordNextPage();   // 借用记录表下一页

private:
    void setupUI();
    // 标签页创建
    QWidget* createBorrowTaskTab();
    QWidget* createBorrowRecordTab();
    // 数据加载
    void loadTaskTypes();
    void loadRecommendedTools();
    void loadAllTools();
    void loadBorrowRecords();
    // [V7.4] 刷新全部工具列表（推荐工具排前面+已添加标记）
    void refreshAllToolTable();
    // [V2.02 2026-06-28] 从缓存重建工具表格 — 勾选跳页专用，不查DB效率高
    void rebuildToolTableFromCache();

    // [2026-06-27] 借用确认流程四步走（原三步基础上增加"抽屉打开"步骤）
    void showBorrowConfirmDialog();   // 第一步：确认借用信息（清单+借用人+归还时间）
    void showBorrowDrawerOpeningDialog();  // 第二步：抽屉打开中（动态旋转框）
    void showToolVerifyDialog();      // 第三步：工具核对（假异常提示）
    void executeBorrow();             // 第四步：执行借用（DAO写入记录）

    QJsonObject m_user;
    QString m_flowNo;

    // 标签页
    QTabWidget* m_tabWidget;

    // 借用任务Tab
    QLabel* m_userInfoLabel = nullptr;  // 借用人
    QLabel* m_workNoLabel = nullptr;    // 工号
    QLabel* m_deptLabel = nullptr;      // 所属机组
    QLabel* m_dateLabel = nullptr;      // 日期
    QLabel* m_flowNoDisplay;  // 任务单号显示
    
    // 任务类型多选下拉
    QPushButton* m_taskTypeBtn;  // 任务类型下拉按钮
    QDialog* m_taskTypePopup;  // 任务类型下拉面板（QDialog+Popup避免checkbox事件被拦截）
    QLineEdit* m_taskTypeSearch;  // 任务类型搜索框
    QVBoxLayout* m_taskTypeListLayout;  // 任务类型列表布局
    QList<QPair<QString, QCheckBox*>> m_taskTypeCheckBoxes;  // 任务类型复选框列表
    QList<int> m_selectedTypeIds;
    
    QLabel* m_recommendHint;  // 推荐提示
    QTableWidget* m_recommendTable;
    QTableWidget* m_allToolTable;
    QLineEdit* m_toolSearchEdit;
    QPushButton* m_toolSearchBtn;
    QLineEdit* m_quantityEdit = nullptr;
    QLabel* m_returnTimeLabel = nullptr;   // [2026-06-27] 改为只读Label自动显示系统参数计算的归还时间
    QPushButton* m_borrowBtn;

    // 借用记录Tab
    QTableWidget* m_recordTable;

    // [V7.4] 选中的工具列表（支持多选）
    QList<SelectedToolInfo> m_selectedTools;
    QSet<int> m_selectedToolIds;  // [V2.12] O(1)快速查找，用toolId（按工具种类选中）
    QSet<int> m_recommendedToolIds;  // [V2.12] 推荐工具ID集合（用toolId）
    // [2026-06-27] 选中的任务类型名称列表，用于借用时写入borrow_reason保持归还页任务类型一致
    QStringList m_selectedTypeNames;
    QJsonArray m_allToolsCache;  // [V8.0] 缓存全量工具数据，避免onBorrowConfirm重复查库

    // [V2.03d 2026-06-29] 展开后的借用清单（一个位置=一个工具，quantity>1时按位置展开）
    //   作者：袁燕 — 符合"机组-柜-层-位号唯一确定一个工具"设计理念
    QList<SelectedToolInfo> m_expandedBorrowList;

    // [V7.1] 借用临时数据
    int m_pendingQuantity = 0;
    QString m_pendingReturnTime;

    // 软键盘 [V6.6]
    SoftKeyboard* m_softKeyboard = nullptr;

    // [2026-06-25] 分页 - 全部工具表
    QPushButton* m_toolPrevBtn = nullptr;
    QPushButton* m_toolNextBtn = nullptr;
    QLabel* m_toolPageLabel = nullptr;
    QLabel* m_toolTotalLabel = nullptr;
    int m_toolCurrentPage = 1;
    int m_toolPageSize = 20;
    int m_toolTotalRecords = 0;

    // [2026-06-25] 分页 - 借用记录表
    QPushButton* m_recordPrevBtn = nullptr;
    QPushButton* m_recordNextBtn = nullptr;
    QLabel* m_recordPageLabel = nullptr;
    QLabel* m_recordTotalLabel = nullptr;
    int m_recordCurrentPage = 1;
    int m_recordPageSize = 20;
    int m_recordTotalRecords = 0;
};
