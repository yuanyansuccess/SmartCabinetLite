/**
 * @file DashboardPage.h
 * @brief 系统概览仪表盘（1:1复刻BS端Dashboard.vue）
 * @author 袁燕
 * @修改说明 V1.00.9 2026-06-15 完全重构以匹配BS端
 *   - 统计卡片：工具总数/在库/已借出/异常告警
 *   - 双面板布局：最近操作记录 + 快捷操作/实时告警
 *   - 添加用户视图支持
 */
#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <QPainter>
#include <QPaintEvent>

class LoadingWidget;  // 前向声明，定义在DashboardPage之后

class DashboardPage : public QWidget {
    Q_OBJECT
public:
    explicit DashboardPage(QWidget* parent = nullptr);
    ~DashboardPage();
    void setUser(const QJsonObject& user);
    void refresh();
    // [V1.00.9 新增] 设置统计数值（带动画）
    void setStatValue(QLabel* label, int targetValue);

signals:
    void navigateRequested(const QString& name);

private slots:
    void onQuickLedger();

private:
    void setupUI();
    void setupBaseUI();  // [v4.6新增] 仅创建基础框架+加载控件，无角色判断
    // [编译兼容] 用户视图子面板创建
    QWidget* createWelcomeBanner();
    QWidget* createFunctionCards();
    QWidget* createUserRecentBorrowsPanel();
    QWidget* createUserReturnRemindersPanel();
    void updateUserStats(const QJsonObject& stats);
    // 统计卡片
    QWidget* createStatCard(const QString& title, const QString& icon, 
                           QLabel*& valueLabel, const QString& colorClass = "blue");
    void setupStatsCards(QHBoxLayout* row);
    // 面板创建
    QWidget* createRecentLogsPanel();
    QWidget* createQuickActionsPanel();
    QWidget* createAlertPanel();
    // 数据更新
    void updateStats(const QJsonObject& stats);
    void updateRecentLogs(const QJsonArray& logs);
    void updateAlerts(const QJsonArray& alerts);  // [2026-06-23] 接受真实告警数据

    QJsonObject m_user;
    // 统计卡片标签
    QLabel* m_toolCountLabel;   // 工具总数
    QLabel* m_inStockLabel;      // 在库工具
    QLabel* m_borrowedLabel;     // 已借出
    QLabel* m_checkedOutLabel;   // [V2.03] 已出库
    QLabel* m_alertsLabel;       // 异常告警
    // 日志表格
    QTableWidget* m_logTable;
    // 告警列表布局
    QVBoxLayout* m_alertList;
    // [2026-06-23] 告警面板头部控件引用（用于动态更新角标）
    QLabel* m_alertBadge;
    QWidget* m_alertPanelWidget;  // 告警项容器，用于清空重建

    // 用户视图相关
    QTableWidget* m_userRecentTable;   // 用户最近借用记录表格
    QTableWidget* m_userReturnTable;   // 用户归还提醒表格
    void updateUserRecentBorrows(const QJsonArray& records);  // [编译兼容]
    void updateUserReturnReminders(const QJsonArray& records); // [编译兼容]

    // 加载状态
    bool m_loading = false;
    LoadingWidget* m_loadingWidget = nullptr; // 加载动画控件
    QWidget* m_roleContent = nullptr;     // [v4.6新增] 角色视图容器，setUser时整体替换
    void setLoading(bool loading);
};

// [V1.00.9 新增] 加载状态控件
class LoadingWidget : public QWidget {
    Q_OBJECT
public:
    explicit LoadingWidget(QWidget* parent = nullptr);
    void startAnimation();
    void stopAnimation();
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    QTimer* m_timer = nullptr;
    int m_dotCount = 0;
};
