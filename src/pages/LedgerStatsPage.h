/**
 * @file LedgerStatsPage.h
 * @brief 台账统计页面 - 借还统计、工具使用频率、部门统计
 * @author 袁燕
 */
#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QJsonArray>
#include <QLabel>
class QTableWidget;  // 前置声明：仅指针使用，降低编译耦合
#include <QPushButton>

class SingleSelectFilter;

namespace Ui { class LedgerStatsPage; }  // Qt Designer 生成的静态布局

class LedgerStatsPage : public QWidget {
    Q_OBJECT
public:
    explicit LedgerStatsPage(QWidget* parent = nullptr);
    ~LedgerStatsPage();
    void refresh();

private slots:
    void onFilterChanged();
    // 导出功能
    void onExportLedger();    // 台账导出（生成CSV文件）

private:
    void setupUI();
    void loadStats();
    // 数字滚动动画（与DashboardPage统一风格）
    void setStatValue(QLabel* label, int targetValue);
    // 导出辅助方法
    QString generateCSV();                        // 生成台账CSV内容
    void showExportSuccess(const QString& path);  // 显示导出成功提示

    Ui::LedgerStatsPage* ui = nullptr;  // 静态布局（LedgerStatsPage.ui）
    QLabel* m_totalBorrowLabel = nullptr;
    QLabel* m_totalReturnLabel = nullptr;
    QLabel* m_currentBorrowedLabel = nullptr;
    QLabel* m_overdueLabel = nullptr;

    SingleSelectFilter* m_periodFilter;  // CheckBox样式单选组件
    QTableWidget* m_categoryTable;
    QTableWidget* m_deptTable;

    QPushButton* m_exportLedgerBtn;  // 台账导出按钮
};
