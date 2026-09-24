#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0  侧边栏导航组件
// 日期：2026-06-21 功能：垂直菜单栏(250px深色#1a1a2e)，1:1复刻Web
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QList>

class SidebarMenu : public QWidget {
    Q_OBJECT
public:
    explicit SidebarMenu(QWidget* parent = nullptr);

    QList<QPushButton*> navButtons() const { return m_navBtns; }

    void setUserInfo(const QString& name, const QString& role);
    void setActivePage(int pageIdx);
    void filterByRole(const QString& role);  // admin/user

signals:
    void pageSelected(int pageIndex);

private:
    void setupUI();

    QVBoxLayout* m_layout = nullptr;
    QList<QPushButton*> m_navBtns;
    QWidget* m_userInfoWidget = nullptr;
    QLabel* m_userNameLabel = nullptr;
    QLabel* m_userRoleLabel = nullptr;
};
