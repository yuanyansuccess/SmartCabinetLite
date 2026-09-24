/**
 * @file MainWindow.h
 * @brief 主窗口 - TopBar(68px) + 左侧240px深蓝侧边栏 + QStackedWidget
 * @author 袁燕
 * @修改说明 2026-06-21 集成TopBar组件，融合版本B页面+版本A组件
 */
#pragma once
#include <QMainWindow>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QJsonObject>
#include <QList>
#include <QTimer>
#include <QDateTime>
                           
class TopBar;
class LoginPage;
class DashboardPage;
class UserManagementPage;
class ToolManagementPage;
class ToolBorrowPage;
class ToolReturnPage;
class ToolCheckinPage;
class ToolCheckoutPage;
class LedgerStatsPage;
class AlertLogsPage;
class SystemSettingsPage;
class SystemMaintenancePage;  // [V2.03g] 系统维护页面

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();
    void showPage(const QString& name);
    QJsonObject currentUser() const { return m_user; }
    void setCurrentUser(const QJsonObject& user);

signals:
    void userLoggedIn(QJsonObject user);
    void userLoggedOut();

private slots:
    void onLoginSuccess(const QJsonObject& user);
    void onLogout();
    void navigateToPage(const QString& name);
    void openBorrowReturnSession();  // [2026-09-23] 管理员借用/归还模拟会话入口
    void onAutoLockTimeout();      // [2026-06-27] 自动锁屏超时→退出登录回登录页
    void onBackupCheckTimeout();   // [2026-06-27] 定时检查是否到了备份时间
    void resetIdleTimer();         // [2026-06-27] 用户操作重置空闲计时器

private:
    void setupUI();
    void setupTopBar();
    QWidget* createSidebar();
    void updateSidebarActive(const QString& name);
    void updateSidebarVisibility();

    // 布局组件
    TopBar* m_topBar;
    QStackedWidget* m_stack;
    QWidget* m_sidebar;
    QWidget* m_contentArea;  // sidebar + stack 的容器
    QWidget* m_userFlowCover = nullptr;  // [2026-09-24] 普通用户流程遮罩页（防弹窗间隙闪现登录页）

    QVBoxLayout* m_sidebarNav;
    QList<QPushButton*> m_navButtons;

    QJsonObject m_user;

    // 11个页面（已移除人脸录入页，人脸录入已集成到人员管理页）
    LoginPage* m_loginPage;
    DashboardPage* m_dashboardPage;
    UserManagementPage* m_userMgmtPage;
    ToolManagementPage* m_toolMgmtPage;
    ToolBorrowPage* m_borrowPage;
    ToolReturnPage* m_returnPage;
    ToolCheckinPage* m_checkinPage;
    ToolCheckoutPage* m_checkoutPage;
    LedgerStatsPage* m_ledgerPage;
    AlertLogsPage* m_alertsPage;
    SystemSettingsPage* m_settingsPage;
    SystemMaintenancePage* m_maintenancePage = nullptr;  // [V2.03g] 系统维护页面

    // [2026-06-27] 自动锁屏定时器（空闲超时自动退出登录）
    QTimer* m_idleTimer = nullptr;
    QDateTime m_lastActivity;        // 最近一次用户操作时间

    // [2026-06-27] 数据库备份检查定时器（每小时检查一次是否到了备份时间）
    QTimer* m_backupCheckTimer = nullptr;
    QDate m_lastBackupDate;          // 上次备份日期（避免同一天重复备份）

protected:
    // [2026-06-27] 事件过滤器：监听全局鼠标/键盘活动，重置空闲计时器
    bool eventFilter(QObject* watched, QEvent* event) override;
};
