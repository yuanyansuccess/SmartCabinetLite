/**
 * @file TopBar.h
 * @brief 顶部栏：品牌标识、系统标题、时钟、用户信息、退出按钮与电量状态
 * @author 袁燕
 */
#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

// 退出按钮为退出整个程序，与登录页的"注销"语义不同。
class TopBar : public QWidget {
    Q_OBJECT
public:
    explicit TopBar(QWidget* parent = nullptr);

    void setUserName(const QString& name);
    void setDepartment(const QString& dept);
    void setPageTitle(const QString& title);
    /// 控制右侧用户信息区显隐（登录页应隐藏）
    void setUserAreaVisible(bool visible);
    /// 控制左侧"主页"按钮显隐（登录页与功能导航主页隐藏，子页面显示）
    void setHomeButtonVisible(bool visible);
    /// 外部设置版本号显示（页面层从配置读取后传入，组件层不依赖配置层）
    void setVersionText(const QString& version);

signals:
    void homeClicked();          // 点击"主页"按钮，请求返回功能导航主页
    void logoutClicked();        // 注销回登录页
    void exitSystemClicked();  // 退出整个系统
    void helpClicked();          // 点击"帮助"按钮，请求全屏打开操作手册

private:
    void setupUI();
    void updateClock();
    /// 更新电池电量显示（Windows API GetSystemPowerStatus）
    void updateBatteryStatus();

    QLabel* m_clockLabel;
    QLabel* m_userNameLabel;
    QLabel* m_userDeptLabel;
    QLabel* m_userAvatar;
    QLabel* m_pageTitle;
    QLabel* m_versionLabel = nullptr;  // 软件版本标签
    QTimer* m_clockTimer;
    QWidget* m_rightArea = nullptr;  // 右侧用户信息容器
    QPushButton* m_homeBtn = nullptr;  // 左侧"主页"按钮（子页面返回功能导航主页）

    // 电池状态指示器
    QLabel* m_batteryLabel = nullptr;   // 电池电量显示
    QTimer* m_statusTimer = nullptr;    // 电池状态刷新定时器
};
