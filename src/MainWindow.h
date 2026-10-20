/**
 * @file MainWindow.h
 * @brief 主窗口 - TopBar(68px) + 左侧240px深蓝侧边栏 + QStackedWidget
 * @author 袁燕
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
class QDialog;
class LoginPage;
class MenuHomePage;
class UserManagementPage;
class ToolManagementPage;
class ToolBorrowPage;
class ToolReturnPage;
class ToolCheckinPage;
class LedgerStatsPage;
class AlertLogsPage;
class SystemSettingsPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();
    void showPage(const QString& name);
    void setCurrentUser(const QJsonObject& user);

signals:
    void userLoggedIn(QJsonObject user);
    void userLoggedOut();

private slots:
    void onLoginSuccess(const QJsonObject& user);
    void onLogout();
    void navigateToPage(const QString& name);
    /// 功能导航主页卡片点击分流：管理员页面名跳转，普通用户动作弹窗
    void onMenuCardClicked(const QString& key);
    void onAutoLockTimeout();  // 自动锁屏超时→退出登录回登录页
    void onBackupCheckTimeout();  // 定时检查是否到了备份时间
    void resetIdleTimer();  // 用户操作重置空闲计时器

private:
    void setupUI();
    void setupTopBar();
    QWidget* createSidebar();
    void updateSidebarActive(const QString& name);
    void updateSidebarVisibility();
    /// 进入功能导航主页：按当前登录角色注入卡片（管理员8项/普通用户3项）
    void enterMenuHomePage();
    /// 按当前登录角色刷新导航主页卡片与欢迎语
    void refreshMenuCards();
    /// 启动普通用户开柜借用/归还会话（会话结束回导航主页）
    void startCabinetSession();
    /// 弹出当前用户借用明细对话框（普通用户"查询"卡片）
    void showUserBorrowDetail();
    /// 弹出操作日志对话框（普通用户"操作日志"卡片）
    void showUserOperationLog();
    /// 全屏打开内置HTML操作手册（TopBar"帮助"按钮触发）
    void showHelpManual();
    /// 给全屏对话框挂管理员同款顶栏（复用TopBar组件，普通用户子界面头部统一）
    /// @param dlg 目标全屏对话框（须已有QVBoxLayout主布局，顶栏插入第0行）
    /// @param title 顶栏页面标题
    /// @return 创建的TopBar指针
    /// @note 信号语义与主窗口一致：主页=关闭对话框回功能主页；
    ///       注销=关闭对话框后注销回登录页；退出=结束整个程序；帮助=全屏打开操作手册
    TopBar* attachDialogTopBar(QDialog* dlg, const QString& title);

    // 布局组件
    TopBar* m_topBar;
    QStackedWidget* m_stack;
    QWidget* m_sidebar;
    QWidget* m_contentArea;  // sidebar + stack 的容器
    QWidget* m_userFlowCover = nullptr;  // 普通用户流程遮罩页（防弹窗间隙闪现登录页）

    QVBoxLayout* m_sidebarNav;
    QList<QPushButton*> m_navButtons;

    QJsonObject m_user;
    /// 管理用户本次是否选择"以普通用户身份登录"：true=按普通用户权限显示菜单与页面
    bool m_userModeAsNormalUser = false;

    // 10个页面（人脸录入功能集成在人员管理页）
    LoginPage* m_loginPage;
    MenuHomePage* m_menuHomePage = nullptr;  // 功能导航主页（登录后大按钮入口页）
    UserManagementPage* m_userMgmtPage;
    ToolManagementPage* m_toolMgmtPage;
    ToolBorrowPage* m_borrowPage;
    ToolReturnPage* m_returnPage;
    ToolCheckinPage* m_checkinPage;
    LedgerStatsPage* m_ledgerPage;
    AlertLogsPage* m_alertsPage;
    SystemSettingsPage* m_settingsPage;

    // 自动锁屏定时器（空闲超时自动退出登录）
    QTimer* m_idleTimer = nullptr;
    QDateTime m_lastActivity;        // 最近一次用户操作时间

    // 数据库备份检查定时器（每小时检查一次是否到了备份时间）
    QTimer* m_backupCheckTimer = nullptr;
    QDate m_lastBackupDate;          // 上次备份日期（避免同一天重复备份）

protected:
    // 事件过滤器：监听全局鼠标/键盘活动，重置空闲计时器
    bool eventFilter(QObject* watched, QEvent* event) override;
};
