/**
 * @file AlertLogsPage.h
 * @brief 告警日志页面 — 告警列表、筛选、确认处理、统计卡片、分页、导出
 * @author 袁燕
 */
#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QJsonArray>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
class QTableWidget;  // 前置声明：仅指针使用，降低编译耦合
#include <QLabel>
#include <QFrame>

class SoftKeyboard;         // 前向声明
class MultiSelectFilter;  // 通用多选筛选组件
class SingleSelectFilter;  // 通用单选筛选组件
class PaginationBar;  // 通用分页栏组件

namespace Ui { class AlertLogsPage; }  // Qt Designer 生成的静态布局

class AlertLogsPage : public QWidget {
    Q_OBJECT
public:
    explicit AlertLogsPage(QWidget* parent = nullptr);
    ~AlertLogsPage();
    void refresh();
    // 设置当前登录用户（用于忽略按钮权限控制）
    void setUser(const QJsonObject& user) { m_user = user; applyAdminPermission(); }
    // 当前用户是否为管理员
    bool isAdmin() const { return m_user["role"].toString() == "admin"; }

private slots:
    void onSearch();
    void onReset();
    void onAcknowledge(int alertId);
    void onResolve(int alertId);
    void onExportLogs();
    void onIgnore(int alertId);
    void onDetail(int alertId);
    void showAlertDetail(const QJsonObject& detail);  // 告警详情弹窗
    void onSearchFieldClicked();  // 搜索框点击弹出软键盘
    void onPrevPage();   // 上一页
    void onNextPage();   // 下一页

private:
    void setupUI();
    void loadAlerts();
    void loadAlertTypes();  // 从数据库加载告警类型列表
    QFrame* createStatCard(const QString& label, const QString& value, const QString& color);
    void updateStatCards();  // 从数据库查询全局统计（不受分页影响）
    // 根据当前用户角色应用权限控制（解除告警按钮等）
    void applyAdminPermission();

    // 标题栏
    QLabel* m_alarmIndicator;
    QPushButton* m_exportBtn;

    // 统计卡片
    QFrame* m_totalCard;
    QFrame* m_critCard;
    QFrame* m_warnCard;
    QFrame* m_infoCard;
    QFrame* m_resolvedCard;

    // 筛选栏（匹配Web版AlertLogs.vue）类型筛选为多选弹出面板
    MultiSelectFilter* m_typeFilter;
    SingleSelectFilter* m_levelFilter;  // CheckBox样式单选组件
    QLineEdit* m_keywordEdit;  // 关键词搜索框（Web版有）
    QLineEdit* m_startDateEdit = nullptr;  // 开始日期
    QLineEdit* m_endDateEdit = nullptr;  // 结束日期
    QPushButton* m_searchBtn;
    QPushButton* m_resetBtn;

    // 表格
    QTableWidget* m_table;

    // 分页 
    PaginationBar* m_paginationBar = nullptr;  // 通用分页栏
    int m_currentPage = 1;
    int m_pageSize = 20;
    int m_totalRecords = 0;

    // 软键盘 
    SoftKeyboard* m_softKeyboard = nullptr;

    // 告警类型缓存（从数据库sys_alert_type加载）
    QMap<QString, QString> m_typeMap;       // typeCode → typeName 映射
    QMap<QString, QString> m_typeLevelMap;  // typeCode → alertLevel 映射

    // 当前登录用户信息（用于忽略按钮权限控制）
    QJsonObject m_user;

    Ui::AlertLogsPage* ui = nullptr;  // 静态布局（AlertLogsPage.ui）
};
