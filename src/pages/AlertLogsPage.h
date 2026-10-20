/**
 * @file AlertLogsPage.h
 * @brief 告警日志页面 — 系统告警与操作告警两个入口共用，按类别参数显示对应类型
 * @author 袁燕
 *
 * 页面只显示告警时间、告警类型、告警内容三列；
 * 显示范围由 setCategory 注入的类别决定（系统告警/操作告警）。
 */
#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QJsonArray>
#include <QLineEdit>
#include <QPushButton>
class QTableWidget;  // 前置声明：仅指针使用，降低编译耦合
#include <QLabel>
#include <QFrame>

class SoftKeyboard;         // 前向声明
class MultiSelectFilter;  // 通用多选筛选组件
class PaginationBar;  // 通用分页栏组件

namespace Ui { class AlertLogsPage; }  // Qt Designer 生成的静态布局

class AlertLogsPage : public QWidget {
    Q_OBJECT
public:
    /// 告警类别：同一页面被两个入口复用，类别决定表格显示的告警类型范围
    enum class Category { System, Operation };

    explicit AlertLogsPage(QWidget* parent = nullptr);
    ~AlertLogsPage();

    /// 注入当前页面显示的告警类别（进入页面前由 MainWindow 调用）
    /// @param category 类别：System=系统告警，Operation=操作告警
    void setCategory(Category category);

    void refresh();
    // 设置当前登录用户（用于忽略按钮权限控制）
    void setUser(const QJsonObject& user) { m_user = user; }

private slots:
    void onSearch();
    void onReset();
    void onSearchFieldClicked();  // 搜索框点击弹出软键盘
    void onPrevPage();   // 上一页
    void onNextPage();   // 下一页

private:
    void setupUI();
    void loadAlerts();
    void loadAlertTypes();  // 从数据库加载告警类型列表
    /// 按当前类别重建类型筛选选项（只保留本类别包含的类型，默认全选）
    void applyCategoryOptions();
    /// 当前类别包含的告警类型编码集合
    /// @return 类别对应的 type_code 列表（取自 SC::ALERT_*_TYPES 常量）
    QStringList categoryTypeCodes() const;
    QString buildDbTypeFilter() const;  // 构建类型筛选条件（类别约束+用户选择合并）

    // 筛选栏：类型筛选为多选弹出面板（选项随类别变化）
    MultiSelectFilter* m_typeFilter;
    QLineEdit* m_keywordEdit;  // 关键词搜索框
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

    // 当前显示的告警类别（默认系统告警）
    Category m_category = Category::System;
    // 类别切换后类型筛选选项待重建标记（refresh 时消费，避免重复重建丢用户选择）
    bool m_categoryOptionsDirty = true;

    // 当前登录用户信息（用于权限控制）
    QJsonObject m_user;

    Ui::AlertLogsPage* ui = nullptr;  // 静态布局（AlertLogsPage.ui）
};
