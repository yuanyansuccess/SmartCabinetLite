/**
 * @file ToolListDialog.h
 * @brief 工具清单对话框 - 按状态（在库/已借出）查询工具列表
 * @author 袁燕
 *   顶部返回按钮（回到工具管理页）；
 *   搜索行：搜索栏（软键盘）+ 查询/重置；
 *   筛选行：类别/机组/状态三项等宽平分；
 *   中部工具表格（编号/名称/规格型号/机组/位置/状态/借用人/操作）；
 *   底部分页栏。点击"详情"通过 detailRequested 信号交由工具管理页打开详情。
 *   卡片宽度按屏幕与父窗口自适应，避免小屏被裁切。
 */
#pragma once
#include "components/BaseDialog.h"
#include "model/ToolInfo.h"

class QPushButton;
class QTableWidget;
class SearchBar;
class MultiSelectFilter;
class SingleSelectFilter;
class PaginationBar;
class SoftKeyboard;

class ToolListDialog : public BaseDialog {
    Q_OBJECT
public:
    /**
     * @brief 构造函数
     * @param status 过滤的工具状态（SC::TOOL_*，空串不过滤）
     * @param title 对话框标题
     * @param parent 父窗口
     */
    explicit ToolListDialog(const QString& status, const QString& title, QWidget* parent = nullptr);

signals:
    /**
     * @brief 点击行内"详情"按钮时发出
     * @param toolInfo 行对应工具（含位置信息，用于按位置过滤操作记录）
     */
    void detailRequested(const ToolInfo& toolInfo);

private slots:
    void onSearch();               ///< 应用筛选并回到第1页
    void onReset();                ///< 清空筛选条件并重新加载
    void onPrevPage();             ///< 上一页
    void onNextPage();             ///< 下一页
    void onSearchKeyboardClicked(); ///< 弹出搜索软键盘

private:
    void buildContent();      ///< 构建搜索行/表格/分页栏
    void loadCategories();    ///< 加载类别筛选选项
    void loadMachineGroups(); ///< 加载机组筛选选项
    void loadTools();         ///< 按当前条件加载工具列表

    QString m_status;                    ///< 打开对话框时的工具状态（决定状态筛选的初始选中项）
    SearchBar* m_searchBar = nullptr;    ///< 搜索栏（含软键盘按钮）
    SoftKeyboard* m_softKeyboard = nullptr; ///< 搜索软键盘
    MultiSelectFilter* m_categoryFilter = nullptr;   ///< 类别多选筛选
    MultiSelectFilter* m_machineGroupFilter = nullptr; ///< 机组多选筛选
    SingleSelectFilter* m_statusFilter = nullptr;    ///< 状态单选筛选
    QStringList m_statusOptions;         ///< 状态筛选项显示文案（下标与m_statusValues一一对应）
    QStringList m_statusValues;          ///< 状态筛选项对应的数据库状态值（首项为空串=不筛选）
    int m_initialStatusIndex = 0;        ///< 打开对话框时的状态项下标，重置时回到该项
    QPushButton* m_searchBtn = nullptr;  ///< 查询按钮
    QPushButton* m_resetBtn = nullptr;   ///< 重置按钮
    QTableWidget* m_table = nullptr;     ///< 工具表格
    PaginationBar* m_paginationBar = nullptr; ///< 分页栏
    QStringList m_allCategories;         ///< 全部类别（判断全选用）
    QStringList m_allMachineGroups;      ///< 全部机组（判断全选用）
    int m_page = 1;                      ///< 当前页码（从1开始）
    int m_pageSize = 20;                 ///< 每页条数
    int m_total = 0;                     ///< 总条数
};
