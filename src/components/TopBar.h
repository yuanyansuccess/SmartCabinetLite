/**
 * @file TopBar.h
 * @brief 顶部栏：品牌标识、系统标题、时钟、用户信息、退出按钮与网络/电量状态
 * @author 袁燕
 */
#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

class QTcpSocket;

// 网络状态以数据库连接是否可用为准（网卡处于 Up 状态不代表外网可达）。
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
    /// 外部设置版本号显示（页面层从配置读取后传入，组件层不依赖配置层）
    void setVersionText(const QString& version);

signals:
    void logoutClicked();        // 侧边栏注销用（保留兼容）
    void exitSystemClicked();  // 退出整个系统

private:
    void setupUI();
    void updateClock();
    /// 更新电池电量显示（Windows API GetSystemPowerStatus）
    void updateBatteryStatus();
    /// 发起非阻塞网络探测（结果经 showNetworkState 异步回调，不冻结UI）
    void updateNetworkStatus();
    /// 按探测结果更新网络状态图标（在线绿/离线红）
    void showNetworkState(bool online);

    QLabel* m_clockLabel;
    QLabel* m_userNameLabel;
    QLabel* m_userDeptLabel;
    QLabel* m_userAvatar;
    QLabel* m_pageTitle;
    QLabel* m_versionLabel = nullptr;  // 软件版本标签
    QTimer* m_clockTimer;
    QWidget* m_rightArea = nullptr;  // 右侧用户信息容器

    // 电池+网络状态指示器
    QLabel* m_batteryLabel = nullptr;   // 电池电量显示
    QLabel* m_networkLabel = nullptr;   // 网络连接状态
    QTimer* m_statusTimer = nullptr;    // 状态刷新定时器(电池+网络)

    // 异步网络探测（替代原QEventLoop同步探测——断网时原实现冻结UI 300ms）
    QTcpSocket* m_probeSocket = nullptr;
    QTimer* m_probeTimeout = nullptr;   // 探测超时（300ms，与原实现一致）
};
