#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0  顶栏组件
// 日期：2026-06-21  功能：品牌logo+标题+实时时钟+用户信息+退出
// [2026-06-23] 新增exitSystemClicked信号，退出按钮改为退出整个系统（非注销）
// [2026-06-27] 新增软件版本标签显示，版本号从AppConfig读取（非硬编码）
// [V2.03 2026-06-29] 新增电池电量+网络连接状态指示器，小米极简美学设计
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

class TopBar : public QWidget {
    Q_OBJECT
public:
    explicit TopBar(QWidget* parent = nullptr);

    void setUserName(const QString& name);
    void setDepartment(const QString& dept);
    void setPageTitle(const QString& title);
    /// [2026-06-21] 控制右侧用户信息区显隐（登录页应隐藏）
    void setUserAreaVisible(bool visible);
    /// [2026-06-27] 更新版本号显示（从AppConfig读取）
    void refreshVersionLabel();

signals:
    void logoutClicked();        // 侧边栏注销用（保留兼容）
    void exitSystemClicked();    // [2026-06-23] 退出整个系统

private:
    void setupUI();
    void updateClock();
    /// [V2.03] 更新电池电量显示（Windows API GetSystemPowerStatus）
    void updateBatteryStatus();
    /// [V2.03] 更新网络连接状态显示
    void updateNetworkStatus();

    QLabel* m_clockLabel;
    QLabel* m_userNameLabel;
    QLabel* m_userDeptLabel;
    QLabel* m_userAvatar;
    QLabel* m_pageTitle;
    QLabel* m_versionLabel = nullptr;  // [2026-06-27] 软件版本标签
    QTimer* m_clockTimer;
    QWidget* m_rightArea = nullptr;  // [2026-06-21] 右侧用户信息容器

    // [V2.03] 电池+网络状态指示器
    QLabel* m_batteryLabel = nullptr;   // 电池电量显示
    QLabel* m_networkLabel = nullptr;   // 网络连接状态
    QTimer* m_statusTimer = nullptr;    // 状态刷新定时器(电池+网络)
};
