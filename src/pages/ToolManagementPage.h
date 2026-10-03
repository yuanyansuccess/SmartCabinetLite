/**
 * @file ToolManagementPage.h
 * @brief 工具管理页面 - 参考工程机组管理风格重做
 * @author 袁燕
 *   1. 顶部统计卡片（全部工具/在库/已借用/维护中）
 *   2. 表格列：编号/名称/规格型号/机组/位置/状态/最近操作/操作
 *   3. 状态用彩色标签（在库绿/已借用橙/维护中灰）
 *   4. 操作按钮改为"详情"
 *   5. 数据使用数据库 machine_group + tool_info 关联查询
 * 软键盘占位改为真实集成
 * 清理僵尸m_statusFilter
 */
#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QJsonArray>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
class QTableWidget;  // 前置声明：仅指针使用，降低编译耦合
#include <QLabel>
#include <QDialog>
#include <QFrame>
#include "model/ToolInfo.h"  // ToolInfo含位置信息，用于操作记录按位置过滤

class SoftKeyboard;
class MultiSelectFilter;
class BaseDialog;
class QTabWidget;
class QVBoxLayout;
struct ToolDetailCtx;  // 工具详情对话框会话上下文（定义于ToolManagementPage.cpp）

class PaginationBar;  // 通用分页栏组件

namespace Ui { class ToolManagementPage; }  // Qt Designer 生成的静态布局

class ToolManagementPage : public QWidget {
    Q_OBJECT
public:
    explicit ToolManagementPage(QWidget* parent = nullptr);
    ~ToolManagementPage();
    void refresh();

private slots:
    void onSearch();
    void onReset();
    void onAddTool();
    void onEditTool(int toolId);
    void onDeleteTool(int toolId);
    void onSubmitTool();
    void onPrevPage();
    void onNextPage();
    void onSearchKeyboardClicked();
    void onDetailTool(const ToolInfo& toolInfo);  // 传ToolInfo，含位置信息用于过滤操作记录

private:
    void setupUI();
    void setupStatsCards();    // 统计卡片
    void loadTools();
    void loadCategories();
    void loadMachineGroups();  // 加载机组筛选选项
    void loadStats();          // 加载统计数据
    // 构建详情对话框的工具文档Tab（下载+在线浏览）
    QWidget* createDocumentTab(int toolId, const QString& docPath);
    /** 详情对话框构建（onDetailTool拆解，ctx聚合对话框控件与权威工具数据） */
    ToolInfo resolveToolInfo(const ToolInfo& toolInfo);
    void buildDetailHeader(QVBoxLayout* cl, const ToolInfo& t);
    QTabWidget* createDetailTabWidget();
    void buildToolDetailTab(ToolDetailCtx& ctx);
    void buildOperationRecordTab(ToolDetailCtx& ctx);
    QList<QJsonObject> collectOperationRecords(ToolDetailCtx& ctx);
    void fillRecordTable(QTableWidget* table, const QList<QJsonObject>& records);
    void finalizeDetailDialog(ToolDetailCtx& ctx);
    // 详情页上传工具文档（选文件+校验+复制+更新DB+刷新）
    // 返回: true=上传成功, false=用户取消或上传失败
    bool onUploadDocument(int toolId);

    // 统计卡片 
    QFrame* m_statsCardAll;
    QFrame* m_statsCardInStock;
    QFrame* m_statsCardBorrowed;
    QFrame* m_statsCardCheckedOut;   // 已出库卡片
    QFrame* m_statsCardPending;     // 待入库卡片
    QFrame* m_statsCardMaintenance;
    QLabel* m_labelTotalTools = nullptr;
    QLabel* m_labelInStock = nullptr;
    QLabel* m_labelBorrowed = nullptr;
    QLabel* m_labelCheckedOut = nullptr;       // 已出库数量
    QLabel* m_labelPending = nullptr;          // 待入库数量
    QLabel* m_labelMaintenance = nullptr;

    // 搜索筛选
    QLineEdit* m_searchEdit;
    MultiSelectFilter* m_categoryFilter;
    MultiSelectFilter* m_machineGroupFilter;  // 机组筛选
    QPushButton* m_searchBtn;
    QPushButton* m_resetBtn;
    QStringList m_allCategories;     // 全部类别列表（用于判断全选）
    QStringList m_allMachineGroups;  // 全部机组列表（用于判断全选）

    // 表格
    QTableWidget* m_table;

    // 分页
    PaginationBar* m_paginationBar = nullptr;  // 通用分页栏
    int m_currentPage = 1;
    int m_pageSize = 20;
    int m_totalRecords = 0;

    // 弹窗（统一圆角风格）
    BaseDialog* m_toolDialog = nullptr;
    QLineEdit* m_dlgName = nullptr;
    QLineEdit* m_dlgCode = nullptr;
    QComboBox* m_dlgCategory = nullptr;
    QLineEdit* m_dlgPosition = nullptr;
    QLineEdit* m_dlgUnit = nullptr;  // 已删除 m_dlgQuantity（数量恒为1）
    QLineEdit* m_dlgSpec = nullptr;
    QPushButton* m_dlgSaveBtn = nullptr;
    int m_editToolId = 0;

    // 软键盘 
    SoftKeyboard* m_softKeyboard = nullptr;

    Ui::ToolManagementPage* ui = nullptr;  // 静态布局（ToolManagementPage.ui）
};
