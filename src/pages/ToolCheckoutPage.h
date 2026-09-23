/**
 * @file ToolCheckoutPage.h
 * @brief 工具出库页面 - 双选项卡（待出库 + 出库记录）
 * @author 袁燕
 *
 * [2026-06-15 重构] 完全重写以1:1匹配BS端
 * [V6.6 2026-06-22] 修复：1)空指针崩溃(搜索框未创建) 2)集成SoftKeyboard 3)表格内边距对齐Web
 * [V8.3 2026-06-25] 修复分页：按钮改为成员变量，添加onPrevPage/onNextPage方法
 * [2026-06-27] 双Tab结构：待出库操作 + 出库历史记录；出库成功写DB(扣库存+记日志)
 */
#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QMouseEvent>
#include <QTabWidget>

class SoftKeyboard;  // 前置声明
class MultiSelectFilter;  // [2026-06-24v8]

class ToolCheckoutPage : public QWidget {
    Q_OBJECT
public:
    explicit ToolCheckoutPage(QWidget* parent = nullptr);
    void refresh();
    void setUser(const QJsonObject& user) { m_user = user; }  // [2026-06-27] 设置当前登录用户

private slots:
    void onCheck(int row, bool checked);
    void onToggleAll(bool checked);
    void onBatchCheckout();
    void onReset();
    void onSearchFieldClicked();  // [V6.6] 搜索框点击弹出软键盘
    void onPrevPage();  // [2026-06-25] 待出库列表上一页
    void onNextPage();  // [2026-06-25] 待出库列表下一页
    void onRecordPrevPage();  // [2026-06-27] 出库记录上一页
    void onRecordNextPage();  // [2026-06-27] 出库记录下一页
    void onTabChanged(int index);  // [2026-06-27] Tab切换时加载数据

private:
    void setupUI();
    QWidget* createCheckoutTab();     // [2026-06-27] Tab1: 待出库
    QWidget* createRecordTab();       // [2026-06-27] Tab2: 出库记录
    void loadTools();
    void loadCheckoutRecords();       // [2026-06-27] 加载出库历史记录
    int selectedCount() const;
    bool eventFilter(QObject* obj, QEvent* event) override;

    // [2026-06-27] 批量出库四步流程
    struct CheckoutItem {
        int mappingId;  // [V2.09] 映射表ID（位置唯一标识）
        int toolId;
        QString toolCode;
        QString toolName;
        QString position;
        int quantity;
        QString reason;
    };
    QList<CheckoutItem> collectSelectedItems() const;
    void showCheckoutListDialog();       // 步骤1: 待出库清单
    void showCheckoutDrawerOpeningDialog(); // 步骤2: 抽屉打开中（动画）
    void showCheckoutWarningDialog();    // 步骤3: 抽屉异常警告（假异常）
    void showCheckoutSuccessDialog();    // 步骤4: 出库成功（写DB+扣库存+记日志）

    // [2026-06-27] Tab容器
    QTabWidget* m_tabWidget = nullptr;

    // 表格
    QTableWidget* m_table = nullptr;       // 待出库表格
    QTableWidget* m_recordTable = nullptr; // [2026-06-27] 出库记录表格

    // 筛选 [2026-06-24v8] 分类筛选改为多选弹出面板
    QLineEdit* m_searchEdit = nullptr;
    MultiSelectFilter* m_categoryFilter = nullptr;

    // 搜索按钮 [V2.04 2026-06-30 袁燕] 参考人员管理页风格
    QPushButton* m_searchBtn = nullptr;
    QPushButton* m_searchResetBtn = nullptr;

    // 状态
    QLabel* m_selectedHint = nullptr;
    QPushButton* m_batchBtn = nullptr;
    QPushButton* m_resetBtn = nullptr;

    // 数据
    QJsonArray m_tools;
    QSet<int> m_selectedSet; // 选中的工具ID

    // [2026-06-27] 当前登录用户信息（用于出库记录操作人）
    QJsonObject m_user;

    // 待出库列表分页
    QPushButton* m_prevBtn = nullptr;
    QPushButton* m_nextBtn = nullptr;
    QLabel* m_pageLabel = nullptr;
    QLabel* m_totalLabel = nullptr;
    int m_currentPage = 1;
    int m_pageSize = 20;
    int m_totalRecords = 0;

    // [2026-06-27] 出库记录分页
    QPushButton* m_recordPrevBtn = nullptr;
    QPushButton* m_recordNextBtn = nullptr;
    QLabel* m_recordPageLabel = nullptr;
    QLabel* m_recordTotalLabel = nullptr;
    int m_recordCurrentPage = 1;
    int m_recordPageSize = 20;
    int m_recordTotalRecords = 0;

    // 软键盘 [V6.6]
    SoftKeyboard* m_softKeyboard = nullptr;
};
