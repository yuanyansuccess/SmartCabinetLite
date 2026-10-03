/**
 * @file UserManagementPage.h
 * @brief 人员管理页面（1:1复刻BS端UserManagement.vue）
 * @author 袁燕
 */
#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QJsonArray>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
class QTableWidget;  // 前置声明：仅指针使用，降低编译耦合
#include <QLabel>
#include <QDialog>
#include <QFrame>
#include <QCheckBox>
#include <QPair>

class SoftKeyboard;
class NumKeypad;
class SingleSelectFilter;  // 通用单选筛选组件
class BaseDialog;  // 统一圆角对话框基类
class QVBoxLayout;
class QNetworkReply;
struct User;  // 用户数据模型
struct FaceEnrollCtx;  // 人脸录入会话上下文（定义于UserManagementPage.cpp）

class PaginationBar;  // 通用分页栏组件

namespace Ui { class UserManagementPage; }  // Qt Designer 生成的静态布局

class UserManagementPage : public QWidget {
    Q_OBJECT
public:
    explicit UserManagementPage(QWidget* parent = nullptr);
    ~UserManagementPage();
    void refresh();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;  // 搜索框点击事件

private slots:
    void onSearch();
    void onReset();
    void onAddUser();
    void onEditUser(int userId);
    void onDeleteUser(int userId);
    void onSubmitUser();
    void onPrevPage();
    void onNextPage();
    void onToggleUserStatus(int userId);  // 禁用/启用用户
    /** 密码框点击→弹出软键盘 */
    void onDlgPasswordClicked();
    /** 搜索框点击→弹出用户名软键盘(对齐登录页) */
    void onSearchFieldClicked();
    // 更新角色按钮组样式
    void updateRoleBtnStyles();
  // 初始化用户对话框（仅创建控件，不弹窗），供onAddUser/onEditUser复用
    void createUserDialog();
    /** 部门筛选相关 */
    void onDeptFilterClicked();
    void onClearDepts();
    void onConfirmDepts();
    /** 人脸录入相关 */
    void onFaceEnroll(int userId);
    void onFaceEnrolledClick(int userId, const QString& realName, const QString& workNo);

private:
    void setupUI();
    void loadUsers();
    /** 从DB动态加载部门列表，失败降级硬编码(对齐Web版loadDepartments策略) */
    void loadDepartments();

    /** 人脸录入对话框构建与状态机（ctx聚合本次会话全部控件与采集状态） */
    void buildFaceEnrollHeader(const User& user, QVBoxLayout* cl);
    void buildFaceEnrollBody(FaceEnrollCtx& ctx, QVBoxLayout* cl);
    void buildFaceEnrollButtons(FaceEnrollCtx& ctx);
    void connectFaceEnrollFlow(FaceEnrollCtx& ctx);
    void onFacePostureTick(FaceEnrollCtx& ctx);
    void onFacePostureReply(FaceEnrollCtx& ctx, QNetworkReply* reply);
    void onFaceCaptureReady(FaceEnrollCtx& ctx, double confidence);
    void onFaceConfirmSave(FaceEnrollCtx& ctx);

    // 搜索筛选
    QLineEdit* m_searchEdit;
    QPushButton* m_searchKeyboardBtn = nullptr;  // 搜索框⌨按钮
    QPushButton* m_deptFilterBtn;  // 部门多选按钮
    // 动态部门列表(从DB加载,对齐Web版departmentOptions)
    QStringList m_departmentOptions;
    QDialog* m_deptPopup = nullptr;  // QFrame→QDialog修复checkbox选不中问题
    QList<QPair<QString, QCheckBox*>> m_deptCheckBoxes;  // 部门复选框列表
    SingleSelectFilter* m_roleFilter;  // CheckBox样式单选组件
    SingleSelectFilter* m_statusFilter;  // CheckBox样式单选组件
    QPushButton* m_searchBtn;
    QPushButton* m_resetBtn;
    QPushButton* m_addBtn;

    // 表格
    QTableWidget* m_table;

    // 分页
    PaginationBar* m_paginationBar = nullptr;  // 通用分页栏
    int m_currentPage = 1;
    int m_pageSize = 20;
    int m_totalRecords = 0;

    // 用户弹窗：BaseDialog统一圆角风格
    BaseDialog* m_userDialog = nullptr;
    QLineEdit* m_dlgUsername = nullptr;
    QLineEdit* m_dlgRealName = nullptr;
    QLineEdit* m_dlgPassword = nullptr;  // 初始化为nullptr防野指针崩溃
    QFrame* m_dlgPasswordFrame = nullptr;  // 密码输入框容器(含⌨按钮)
    QLineEdit* m_dlgWorkNo = nullptr;
    // 角色为按钮组（2选1，风格统一）
    QPushButton* m_dlgRoleBtn1 = nullptr;  // 普通用户
    QPushButton* m_dlgRoleBtn2 = nullptr;  // 管理员
    int m_dlgRoleMode = 0;  // 0=普通用户, 1=管理员
    QComboBox* m_dlgDept = nullptr;  // 部门保留下拉框（动态数据+可编辑）
    QLineEdit* m_dlgPhone = nullptr;
    QPushButton* m_dlgSaveBtn = nullptr;
    int m_editUserId = 0;

    // 字母软键盘(搜索/用户名输入)
    SoftKeyboard* m_softKeyboard = nullptr;

    // 独立数字键盘 - 嵌入弹窗中，零穿透
    NumKeypad* m_numKeypad = nullptr;

    Ui::UserManagementPage* ui = nullptr;  // 静态布局（UserManagementPage.ui）
};
