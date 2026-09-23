/**
 * @file LedgerStatsPage.h
 * @brief 台账统计页面 - 借还统计、工具使用频率、部门统计
 * @author 袁燕
 * @修改说明 V6.9 2026-06-24 统计周期筛选从QComboBox改为SingleSelectFilter
 * @修改说明 V7.2 2026-06-24 新增台账导出和USB导出功能
 */
#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QJsonArray>
#include <QLabel>
#include <QTableWidget>
#include <QPushButton>

class SingleSelectFilter;  // [V6.9]

class LedgerStatsPage : public QWidget {
    Q_OBJECT
public:
    explicit LedgerStatsPage(QWidget* parent = nullptr);
    void refresh();

private slots:
    void onFilterChanged();
    // [V7.2 2026-06-24] 导出功能
    void onExportLedger();    // 台账导出（生成CSV文件）
    void onExportToUSB();     // USB导出（复制到USB设备）

private:
    void setupUI();
    void loadStats();
    // [2026-06-24v2] 数字滚动动画（与DashboardPage统一风格）
    void setStatValue(QLabel* label, int targetValue);
    // [V7.2] 导出辅助方法
    QString generateCSV();                        // 生成台账CSV内容
    QString detectUSBDrive();                     // 检测USB设备挂载路径
    void showExportSuccess(const QString& path);  // 显示导出成功提示

    QLabel* m_totalBorrowLabel;
    QLabel* m_totalReturnLabel;
    QLabel* m_currentBorrowedLabel;
    QLabel* m_overdueLabel;

    SingleSelectFilter* m_periodFilter;  // [V6.9] CheckBox样式单选组件
    QTableWidget* m_categoryTable;
    QTableWidget* m_deptTable;

    QPushButton* m_exportLedgerBtn;  // [V7.2] 台账导出按钮
    QPushButton* m_exportUSBBtn;     // [V7.2] USB导出按钮
};
