/**
 * @file SidebarMenu.h
 * @brief 侧边栏导航菜单：按登录角色生成入口项，发出页面切换信号
 * @author 袁燕
 */
#pragma once
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
