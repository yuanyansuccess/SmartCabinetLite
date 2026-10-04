/**
 * @file UserManagementPageUserDialog.cpp
 * @brief 人员管理-用户编辑对话框（构建/新增/编辑/删除/提交）
 * @author 袁燕
 *
 * 本文件实现上述功能，成员函数声明见 UserManagementPage.h。
 */

#include "UserManagementPage.h"
#include "ui_UserManagementPage.h"
#include <QTableWidget>  // 用户列表表格
#include "components/PaginationBar.h"
#include "components/SoftKeyboard.h"
#include "components/NumKeypad.h"
#include "components/FaceCameraWidget.h"  // 人脸录入对话框摄像头组件
#include "components/DeepFaceExtractor.h"  // 人脸方位检测
#include "components/SingleSelectFilter.h"  // 通用单选筛选组件
#include "utils/StyleHelper.h"
#include "controller/UserController.h"
#include "controller/AuthController.h"
#include <QtNetwork>
// 重新引入db/UserDAO(仅用于getDistinctDepartments合并数据源)
  // 其他User数据访问仍通过UserController
#include "db/UserDAO.h"
#include "services/AuthService.h"
#include "services/SettingService.h"
#include "services/FaceRecognitionService.h"  // 人脸录入/清除
#include "pages/BatchImportDialog.h"  // 批量导入用户对话框
#include "components/BaseDialog.h"  // 统一圆角对话框基类
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDialog>
#include <QFormLayout>
#include "components/MessageDialog.h"
#include <QDebug>
#include <QFrame>
#include <QPushButton>
#include <QLabel>
#include <QMouseEvent>  // eventFilter
#include <QSet>  // loadDepartments选中状态保留
#include <QTimer>  // 人脸录入成功延迟关闭
#include <QDateTime>  // 方位检测时间戳
#include <functional>  // std::function
#include "common/Constants.h"

// ==============================================================
  // 统一圆角风格 — BaseDialog基类
// 供 onAddUser / onEditUser 复用，避免 onEditUser 错误调用 onAddUser 导致弹出"新增用户"标题
void UserManagementPage::createUserDialog() {
    if (m_userDialog) return;  // 已初始化，跳过

    m_userDialog = new BaseDialog(this, 520);
    m_userDialog->setDialogTitle(QStringLiteral("用户"));  // 占位标题，由调用方覆盖
    m_userDialog->setMinimumHeight(460);

    auto* cl = m_userDialog->contentLayout();
    cl->setSpacing(12);

    QString inputStyle =
        "QLineEdit{padding:10px 14px;border:2px solid #e0e0e0;border-radius:10px;"
        "font-size:15px;background:#fff;color:#333;min-height:44px;}"
        "QLineEdit:focus{border-color:#4da3ff;}";
    QString comboStyle =
        "QComboBox{padding:10px 14px;border:2px solid #e0e0e0;border-radius:10px;"
        "font-size:15px;background:#fff;color:#333;min-height:44px;min-width:200px;}"
        "QComboBox:focus{border-color:#4da3ff;}QComboBox::drop-down{border:none;width:30px;}";

    m_dlgRealName = new QLineEdit();
    m_dlgRealName->setPlaceholderText(QStringLiteral("请输入真实姓名"));
    m_dlgRealName->setStyleSheet(inputStyle);

    m_dlgWorkNo = new QLineEdit();
    // 工号改纯数字，点击弹出数字键盘输入
    m_dlgWorkNo->setPlaceholderText(QStringLiteral("纯数字，如007"));
    m_dlgWorkNo->setReadOnly(true);
    m_dlgWorkNo->setCursor(Qt::PointingHandCursor);
    m_dlgWorkNo->setStyleSheet(inputStyle);
    m_dlgWorkNo->installEventFilter(this);

    m_dlgDept = new QComboBox();
    m_dlgDept->setEditable(true);
    m_dlgDept->setStyleSheet(comboStyle);

    // 角色按钮组（2选1）- 固定宽度不Expanding撑满整行
    {
        auto makeRoleBtn = [&](const QString& text, int mode) -> QPushButton* {
            auto* btn = new QPushButton(text);
            btn->setCheckable(true);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setFixedHeight(StyleHelper::Token::ControlHeight);
            btn->setMinimumWidth(120);
            btn->setMaximumWidth(180);
            btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
            if (mode == 0) btn->setChecked(true);
            connect(btn, &QPushButton::clicked, this, [this, mode]() {
                m_dlgRoleMode = mode;
                updateRoleBtnStyles();
            });
            return btn;
        };
        m_dlgRoleBtn1 = makeRoleBtn(QStringLiteral("普通用户"), 0);
        m_dlgRoleBtn2 = makeRoleBtn(QStringLiteral("管理员"), 1);
    }
    m_dlgPhone = new QLineEdit();
    m_dlgPhone->setPlaceholderText(QStringLiteral("请输入手机号"));
    m_dlgPhone->setStyleSheet(inputStyle);

    // 初始密码区域
    m_dlgPasswordFrame = new QFrame();
    m_dlgPasswordFrame->setAttribute(Qt::WA_StyledBackground, true);
    m_dlgPasswordFrame->setStyleSheet(
        "QFrame{border:2px solid #e0e0e0;border-radius:10px;background:#fff;min-height:44px;}"
    );
    auto* pwdFL = new QHBoxLayout(m_dlgPasswordFrame);
    pwdFL->setContentsMargins(14, 0, 4, 0);
    pwdFL->setSpacing(0);

    m_dlgPassword = new QLineEdit();
    m_dlgPassword->setPlaceholderText(QStringLiteral("点击⌨使用安全键盘"));
    m_dlgPassword->setEchoMode(QLineEdit::Password);
    m_dlgPassword->setReadOnly(true);
    m_dlgPassword->setStyleSheet(
        "QLineEdit{border:none;padding:0;font-size:15px;color:#1a1a2e;background:transparent;}");
    pwdFL->addWidget(m_dlgPassword, 1);

    auto* skbPwdBtn = new QPushButton(QStringLiteral("⌨"));
    skbPwdBtn->setFixedSize(40, 40);
    skbPwdBtn->setCursor(Qt::PointingHandCursor);
    skbPwdBtn->setStyleSheet(
        "QPushButton{border:none;border-radius:10px;font-size:20px;background:transparent;color:#666;}"
        "QPushButton:hover{background:#e6f0ff;color:#4da3ff;}"
    );
    connect(skbPwdBtn, &QPushButton::clicked, this, [this]() {
        // 先隐藏字母键盘防止穿透残留
        if (m_softKeyboard) m_softKeyboard->hide();
        if (m_numKeypad && m_dlgPassword) {
            m_numKeypad->setShuffle(true);
            m_numKeypad->setShowPassword(false);
            m_numKeypad->attach(m_dlgPassword);
            m_numKeypad->show();
        }
    });
    pwdFL->addWidget(skbPwdBtn);

    // 独立数字键盘 - 顶层Popup弹窗，不嵌入布局
    m_numKeypad = new NumKeypad(this);
    m_numKeypad->setShuffle(true);
    connect(m_numKeypad, &NumKeypad::confirmed, this, [this]() {
        if (m_numKeypad) m_numKeypad->hide();
    });
    connect(m_numKeypad, &NumKeypad::cancelled, this, [this]() {
        if (m_numKeypad) m_numKeypad->hide();
    });

    // 加载部门
    loadDepartments();
    m_dlgDept->clear();
    for (const QString& d : m_departmentOptions) m_dlgDept->addItem(d);

    // 表单标签样式
    auto makeLabel = [](const QString& text) -> QLabel* {
        auto* lbl = new QLabel(text);
        lbl->setStyleSheet(StyleHelper::labelText());
        lbl->setMinimumHeight(StyleHelper::Token::ControlHeight);
        lbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        return lbl;
    };

    // 表单行：标签 + 控件
    auto addFormRow = [&](QLabel* label, QWidget* widget) {
        auto* row = new QHBoxLayout();
        row->setSpacing(12);
        label->setFixedWidth(80);
        row->addWidget(label);
        row->addWidget(widget, 1);
        cl->addLayout(row);
    };

    addFormRow(makeLabel(QStringLiteral("姓名")), m_dlgRealName);
    addFormRow(makeLabel(QStringLiteral("工号")), m_dlgWorkNo);
    addFormRow(makeLabel(QStringLiteral("部门")), m_dlgDept);

    // 角色按钮组
    {
        auto* roleBtnGroup = new QWidget();
        roleBtnGroup->setStyleSheet("background:transparent;");
        auto* roleBtnLayout = new QHBoxLayout(roleBtnGroup);
        roleBtnLayout->setContentsMargins(0, 0, 0, 0);
        roleBtnLayout->setSpacing(8);
        roleBtnLayout->addWidget(m_dlgRoleBtn1);
        roleBtnLayout->addWidget(m_dlgRoleBtn2);
        updateRoleBtnStyles();
        addFormRow(makeLabel(QStringLiteral("角色")), roleBtnGroup);
    }
    addFormRow(makeLabel(QStringLiteral("电话")), m_dlgPhone);
    addFormRow(makeLabel(QStringLiteral("密码")), m_dlgPasswordFrame);

    // 底部按钮（通过 BaseDialog::buttonLayout() 添加）
    auto* bl = m_userDialog->buttonLayout();
    // 清空 stretch
    while (bl->count() > 0) {
        QLayoutItem* item = bl->takeAt(0);
        delete item;
    }
    bl->addStretch();

    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    cancelBtn->setMinimumHeight(StyleHelper::Token::ControlHeightTouch);
    cancelBtn->setMinimumWidth(100);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setStyleSheet(
        "QPushButton{background:#fff;color:#666;border:2px solid #ddd;border-radius:12px;"
        "font-size:15px;font-weight:600;}"
        "QPushButton:hover{background:#f5f5f5;}"
    );
    connect(cancelBtn, &QPushButton::clicked, this, [this]() { m_userDialog->reject(); });

    m_dlgSaveBtn = new QPushButton(QStringLiteral("确认"));
    m_dlgSaveBtn->setMinimumHeight(StyleHelper::Token::ControlHeightTouch);
    m_dlgSaveBtn->setMinimumWidth(100);
    m_dlgSaveBtn->setCursor(Qt::PointingHandCursor);
    m_dlgSaveBtn->setStyleSheet(
        "QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:12px;"
        "font-size:16px;font-weight:700;}"
        "QPushButton:hover{background:#3d8ae0;}"
    );
    connect(m_dlgSaveBtn, &QPushButton::clicked, this, &UserManagementPage::onSubmitUser);

    bl->addWidget(cancelBtn);
    bl->addWidget(m_dlgSaveBtn);
}

void UserManagementPage::onAddUser() {
    m_editUserId = 0;
    createUserDialog();  // 独立初始化，不弹窗
    m_userDialog->setDialogTitle(QStringLiteral("新增用户"));
    m_dlgSaveBtn->setText(QStringLiteral("确认新增"));
    m_dlgRealName->clear(); m_dlgWorkNo->clear(); m_dlgPassword->clear();
    m_dlgRoleMode = 0; updateRoleBtnStyles();
    m_dlgDept->setCurrentIndex(0); m_dlgPhone->clear();
    m_userDialog->exec();
}

void UserManagementPage::onEditUser(int userId) {
    m_editUserId = userId;
  // 通过UserController替代直接调用db/UserDAO
    UserController ctrl;
    User u = ctrl.getUserById(userId);
    if (u.userId == 0) {
        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("用户不存在"));
        return;
    }
  // 使用独立初始化方法，不错误调用onAddUser（避免弹出"新增用户"标题）
    createUserDialog();
    m_userDialog->setDialogTitle(QStringLiteral("编辑用户"));
    m_dlgSaveBtn->setText(QStringLiteral("保存修改"));
    // 刷新部门列表(防止DB新增部门后弹窗列表过期)
    loadDepartments();
    m_dlgDept->clear();
    for (const QString& d : m_departmentOptions) m_dlgDept->addItem(d);
    m_dlgRealName->setText(u.realName);
    m_dlgWorkNo->setText(u.workNo);
    m_dlgWorkNo->setEnabled(false);  // 工号编辑时禁用
    m_dlgDept->setCurrentText(u.department);
    m_dlgRoleMode = (u.role == SC::ROLE_ADMIN) ? 1 : 0; updateRoleBtnStyles();
    m_dlgPhone->setText(u.phone);
    m_dlgPassword->setText(QString());
    m_dlgPassword->setPlaceholderText(QStringLiteral("留空则不修改"));
    m_userDialog->exec();
}

void UserManagementPage::onDeleteUser(int userId) {
    if (!MessageDialog::showQuestion(this, QStringLiteral("确认删除"),
        QStringLiteral("确定要删除该用户吗？"))) return;
  // 通过UserController替代直接调用db/UserDAO
    UserController ctrl;
    if (ctrl.deleteUser(userId)) {
        loadUsers();
    } else {
        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("删除失败"));
    }
}

void UserManagementPage::onSubmitUser() {
    QString realName = m_dlgRealName->text().trimmed();
    QString workNo = m_dlgWorkNo->text().trimmed();
    QString password = m_dlgPassword->text();
    QString dept = m_dlgDept->currentText();
    // 角色从按钮组获取
    QString role = (m_dlgRoleMode == 1) ? SC::ROLE_ADMIN : SC::ROLE_USER;
    QString phone = m_dlgPhone->text().trimmed();

    if (realName.isEmpty() || workNo.isEmpty()) {
        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("请填写姓名和工号"));
        return;
    }
    if (m_editUserId == 0 && dept.isEmpty()) {
        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("请选择所属部门"));
        return;
    }
    if (m_editUserId == 0 && password.isEmpty()) {
        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("请输入初始密码"));
        return;
    }

  // 通过UserController替代直接调用db/UserDAO
    UserController ctrl;
    bool ok = false;
    if (m_editUserId == 0) {
        // 新建用户
        User newUser;
        newUser.username = workNo;
        newUser.realName = realName;
        newUser.workNo = workNo;
        newUser.department = dept;
        newUser.role = role;
        newUser.phone = phone;
        newUser.status = SC::USER_ACTIVE;
        int newId = ctrl.createUser(newUser, password);
        ok = (newId > 0);
    } else {
        // 更新用户
        User updateUser;
        updateUser.userId = m_editUserId;
        updateUser.realName = realName;
        updateUser.workNo = workNo;
        updateUser.department = dept;
        updateUser.role = role;
        updateUser.phone = phone;
        ok = ctrl.updateUser(updateUser);
        if (ok && !password.isEmpty()) {
            ok = ctrl.changePassword(m_editUserId, "", password);
        }
    }
    if (ok) {
        m_userDialog->accept();
        loadUsers();
    } else {
        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("保存失败"));
    }
}

/** 搜索框点击→弹出用户名软键盘(对齐登录页ModeEn模式) */

// ==============================================================
