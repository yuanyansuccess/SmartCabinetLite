/**
 * @file LedgerStatsPage.h
 * @brief 导入导出页面 - 人员批量导入、台账导出、系统/操作日志导出、借还统计
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
    void onExportLedger();          // 台账导出（生成CSV文件）
    void onExportOperationLog();    // 操作日志导出（操作告警CSV）
    void onExportSystemLog();       // 系统日志导出（系统告警CSV）

private:
    void setupUI();
    void loadStats();
    // 数字滚动动画
    void setStatValue(QLabel* label, int targetValue);
    // 导出辅助方法
    QString generateCSV();                        // 生成台账CSV内容
    void showExportSuccess(const QString& path);  // 显示导出成功提示
    /// @brief 按告警类别导出日志CSV到桌面log文件夹
    /// @param dbType 逗号分隔的告警类型编码串（限定导出范围）
    /// @param filePrefix 导出文件名前缀，用于区分操作/系统两类文件
    void exportAlertsToCsv(const QString& dbType, const QString& filePrefix);

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
