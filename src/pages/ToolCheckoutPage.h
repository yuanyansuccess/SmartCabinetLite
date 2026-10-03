/**
 * @file ToolCheckoutPage.h
 * @brief 工具出库页面 - 双选项卡（待出库 + 出库记录）
 * @author 袁燕
 *
 * 完全重写以1:1匹配BS端
 * 修复：1)空指针崩溃(搜索框未创建) 2)集成SoftKeyboard 3)表格内边距对齐Web
 * 修复分页：按钮改为成员变量，添加onPrevPage/onNextPage方法
 * 双Tab结构：待出库操作 + 出库历史记录；出库成功写DB(扣库存+记日志)
 */
#pragma once
#include <QWidget>
class QTableWidget;  // 前置声明：仅指针使用，降低编译耦合
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QMouseEvent>
#include <QTabWidget>
#include "services/CheckoutService.h"  // CheckoutItem（定义随业务下沉至服务层）

class SoftKeyboard;  // 前置声明
class MultiSelectFilter;

class PaginationBar;  // 通用分页栏组件

namespace Ui { class ToolCheckoutPage; }  // Qt Designer 生成的静态布局

class ToolCheckoutPage : public QWidget {
    Q_OBJECT
public:
    explicit ToolCheckoutPage(QWidget* parent = nullptr);
    ~ToolCheckoutPage();
    void refresh();
    void setUser(const QJsonObject& user) { m_user = user; }  // 设置当前登录用户

private slots:
    void onCheck(int row, bool checked);
    void onToggleAll(bool checked);
    void onBatchCheckout();
    void onReset();
    void onSearchFieldClicked();  // 搜索框点击弹出软键盘
    void onPrevPage();  // 待出库列表上一页
    void onNextPage();  // 待出库列表下一页
    void onRecordPrevPage();  // 出库记录上一页
    void onRecordNextPage();  // 出库记录下一页
    void onTabChanged(int index);  // Tab切换时加载数据

private:
    void setupUI();
    void loadTools();
    void loadCheckoutRecords();  // 加载出库历史记录
    int selectedCount() const;
    bool eventFilter(QObject* obj, QEvent* event) override;

    // 批量出库四步流程（CheckoutItem定义已随业务下沉至services/CheckoutService.h）
    QList<CheckoutItem> collectSelectedItems() const;
    void showCheckoutListDialog();       // 步骤1: 待出库清单
    void showCheckoutDrawerOpeningDialog(); // 步骤2: 抽屉打开中（动画）
    void showCheckoutWarningDialog();    // 步骤3: 抽屉异常警告（假异常）
    void showCheckoutSuccessDialog();    // 步骤4: 出库成功（写DB+扣库存+记日志）

    // Tab容器
    QTabWidget* m_tabWidget = nullptr;

    // 表格
    QTableWidget* m_table = nullptr;       // 待出库表格
    QTableWidget* m_recordTable = nullptr;  // 出库记录表格

    // 筛选 分类筛选为多选弹出面板
    QLineEdit* m_searchEdit = nullptr;
    MultiSelectFilter* m_categoryFilter = nullptr;

    // 搜索按钮 参考人员管理页风格
    QPushButton* m_searchBtn = nullptr;
    QPushButton* m_searchResetBtn = nullptr;

    // 状态
    QLabel* m_selectedHint = nullptr;
    QPushButton* m_batchBtn = nullptr;
    QPushButton* m_resetBtn = nullptr;

    // 数据
    QJsonArray m_tools;
    QSet<int> m_selectedSet; // 选中的工具ID

    // 当前登录用户信息（用于出库记录操作人）
    QJsonObject m_user;

    // 待出库列表分页
    PaginationBar* m_paginationBar = nullptr;  // 通用分页栏
    int m_currentPage = 1;
    int m_pageSize = 20;
    int m_totalRecords = 0;

    // 出库记录分页
    PaginationBar* m_recordPaginationBar = nullptr;  // 通用分页栏
    int m_recordCurrentPage = 1;
    int m_recordPageSize = 20;
    int m_recordTotalRecords = 0;

    // 软键盘 
    SoftKeyboard* m_softKeyboard = nullptr;

    Ui::ToolCheckoutPage* ui = nullptr;  // 静态布局（ToolCheckoutPage.ui）
};
