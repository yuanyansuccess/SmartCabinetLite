/**
 * @file UserManagementPage.h
 * @brief 人员管理页面（1:1复刻BS端UserManagement.vue）
 * @author 袁燕
 * @修改说明 V1.00.9 2026-06-15 完全重构以匹配BS端
 *   - 表格列：工号/姓名/部门/角色/联系电话/人脸录入/创建时间/状态/操作
 *   - 添加部门多选下拉
 *   - 添加分页组件
 *   - 触屏优化：按钮最小48px，字体16px+
 * @修改说明 V6.5 2026-06-22 布局对齐Web+部门弹窗修复+搜索增加软键盘
 * @修改说明 V6.7 2026-06-22 致命修复:
 *   - 软键盘confirmed野指针崩溃(m_dlgPassword未初始化+模式混用)
 *   - 部门多选改为IN查询(修复精确匹配失败)
 *   - 操作列加宽180px+按钮加背景色确保可见
 * @修改说明 V6.8 2026-06-22 深度对齐修复:
 *   - 部门列表改为DB动态加载(loadDepartments)替代仅setupUI一次
 *   - 操作按钮高对比度(蓝底白字/红底白字/绿底白字)列宽200px
 *   - 列宽重新分配对齐Web版自动布局
 *   - QComboBox下拉样式优化(箭头+列表项)
 * @修改说明 V6.8.1 2026-06-22 紧急修复:
 *   - 修复loadDepartments崩溃:废弃widget预删方案,改delete+recreate弹窗
 *   - 修复操作按钮文字截断:padding16→12px,字体14→15px,minH36→40px
 * @修改说明 V6.9 2026-06-24 单选组件统一:
 *   - m_roleFilter/m_statusFilter 从QComboBox改为SingleSelectFilter(CheckBox样式单选)
 */
#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QJsonArray>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTableWidget>
#include <QLabel>
#include <QDialog>
#include <QFrame>
#include <QCheckBox>
#include <QPair>

class SoftKeyboard;
class NumKeypad;
class SingleSelectFilter;  // [V6.9] 通用单选筛选组件
class BaseDialog;          // [2026-06-26] 统一圆角对话框基类

class UserManagementPage : public QWidget {
    Q_OBJECT
public:
    explicit UserManagementPage(QWidget* parent = nullptr);
    ~UserManagementPage();
    void refresh();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;  // [V6.5] 搜索框点击事件

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
    /** [2026-06-15 新增] 密码框点击→弹出软键盘 */
    void onDlgPasswordClicked();
    /** [V6.5 新增] 搜索框点击→弹出用户名软键盘(对齐登录页) */
    void onSearchFieldClicked();
    // [V7.0] 更新角色按钮组样式
    void updateRoleBtnStyles();
    // [V7.0.1] 初始化用户对话框（仅创建控件，不弹窗），供onAddUser/onEditUser复用
    void createUserDialog();
    /** 部门筛选相关 */
    void onDeptFilterClicked();
    void onClearDepts();
    void onConfirmDepts();
    /** 人脸录入相关 */
    void onFaceEnroll(int userId);
    void onFaceEnrolledClick(int userId, const QString& realName, const QString& workNo);
    void onStartFaceCapture();
    void onCancelFaceCapture();

private:
    void setupUI();
    void loadUsers();
    /** [V6.8] 从DB动态加载部门列表，失败降级硬编码(对齐Web版loadDepartments策略) */
    void loadDepartments();

    // 搜索筛选
    QLineEdit* m_searchEdit;
    QPushButton* m_searchKeyboardBtn = nullptr;  // [V6.5] 搜索框⌨按钮
    QPushButton* m_deptFilterBtn;  // 部门多选按钮
    // [V6.8] 动态部门列表(从DB加载,对齐Web版departmentOptions)
    QStringList m_departmentOptions;
    QDialog* m_deptPopup = nullptr;  // [V6.5] QFrame→QDialog修复checkbox选不中问题
    QList<QPair<QString, QCheckBox*>> m_deptCheckBoxes;  // 部门复选框列表
    SingleSelectFilter* m_roleFilter;   // [V6.9] 改为CheckBox样式单选组件
    SingleSelectFilter* m_statusFilter; // [V6.9] 改为CheckBox样式单选组件
    QPushButton* m_searchBtn;
    QPushButton* m_resetBtn;
    QPushButton* m_addBtn;

    // 表格
    QTableWidget* m_table;

    // 分页
    QPushButton* m_prevBtn;
    QPushButton* m_nextBtn;
    QLabel* m_pageLabel;
    QLabel* m_totalLabel;
    int m_currentPage = 1;
    int m_pageSize = 20;
    int m_totalRecords = 0;

    // 弹窗 [2026-06-26] 改为BaseDialog统一圆角风格
    BaseDialog* m_userDialog = nullptr;
    QLineEdit* m_dlgUsername = nullptr;
    QLineEdit* m_dlgRealName = nullptr;
    QLineEdit* m_dlgPassword = nullptr;  // [V6.7] 初始化为nullptr防野指针崩溃
    QFrame* m_dlgPasswordFrame = nullptr;   // [新增] 密码输入框容器(含⌨按钮)
    QLineEdit* m_dlgWorkNo = nullptr;
    // [V7.0] 角色改为按钮组（2选1，风格统一）
    QPushButton* m_dlgRoleBtn1 = nullptr;  // 普通用户
    QPushButton* m_dlgRoleBtn2 = nullptr;  // 管理员
    int m_dlgRoleMode = 0;  // 0=普通用户, 1=管理员
    QComboBox* m_dlgDept = nullptr;  // 部门保留下拉框（动态数据+可编辑）
    QLineEdit* m_dlgPhone = nullptr;
    QPushButton* m_dlgSaveBtn = nullptr;
    int m_editUserId = 0;

    // [2026-06-15 新增] 字母软键盘(搜索/用户名输入)
    SoftKeyboard* m_softKeyboard = nullptr;
    
    // [2026-06-26] 独立数字键盘 - 嵌入弹窗中，零穿透
    NumKeypad* m_numKeypad = nullptr;
    
    // 人脸录入对话框相关
    QDialog* m_faceDialog = nullptr;
    int m_faceEnrollUserId = 0;
    QLabel* m_faceStatusLabel = nullptr;
    QPushButton* m_faceStartBtn = nullptr;
    QPushButton* m_faceCancelBtn = nullptr;
    QPushButton* m_faceCloseBtn = nullptr;
};
