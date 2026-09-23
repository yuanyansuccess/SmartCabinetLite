/**
 * @file ToolManagementPage.h
 * @brief 工具管理页面 - 参考工程机组管理风格重做
 * @author 袁燕
 * @修改说明 V7.0 2026-06-24 全面改造：
 *   1. 顶部统计卡片（全部工具/在库/已借用/维护中）
 *   2. 表格列：编号/名称/规格型号/机组/位置/状态/最近操作/操作
 *   3. 状态用彩色标签（在库绿/已借用橙/维护中灰）
 *   4. 操作按钮改为"详情"
 *   5. 数据使用数据库 machine_group + tool_info 关联查询
 * [V6.6 2026-06-22] 软键盘占位改为真实集成
 * [V6.9 2026-06-24] 清理僵尸m_statusFilter
 */
#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QJsonArray>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTableWidget>
#include <QLabel>
#include <QDialog>
#include <QFrame>
#include "model/ToolInfo.h"  // onDetailTool改为传ToolInfo引用，含位置信息

class SoftKeyboard;
class MultiSelectFilter;
class BaseDialog;

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
    void onDetailTool(const ToolInfo& toolInfo);  // 改为传ToolInfo，含位置信息用于过滤操作记录

private:
    void setupUI();
    void setupStatsCards();    // 统计卡片
    void loadTools();
    void loadCategories();
    void loadMachineGroups();  // 加载机组筛选选项
    void loadStats();          // 加载统计数据
    // 构建详情对话框的工具文档Tab（下载+在线浏览）
    QWidget* createDocumentTab(int toolId, const QString& docPath);
    // 详情页上传工具文档（选文件+校验+复制+更新DB+刷新）
    // 返回: true=上传成功, false=用户取消或上传失败
    bool onUploadDocument(int toolId);

    // 统计卡片 [V7.0]
    QFrame* m_statsCardAll;
    QFrame* m_statsCardInStock;
    QFrame* m_statsCardBorrowed;
    QFrame* m_statsCardCheckedOut;   // 已出库卡片
    QFrame* m_statsCardPending;     // 待入库卡片
    QFrame* m_statsCardMaintenance;
    QLabel* m_labelTotalTools;
    QLabel* m_labelInStock;
    QLabel* m_labelBorrowed;
    QLabel* m_labelCheckedOut;       // 已出库数量
    QLabel* m_labelPending;          // 待入库数量
    QLabel* m_labelMaintenance;

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
    QPushButton* m_prevBtn;
    QPushButton* m_nextBtn;
    QLabel* m_pageLabel;
    QLabel* m_totalLabel;
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

    // 软键盘 [V6.6]
    SoftKeyboard* m_softKeyboard = nullptr;
};
