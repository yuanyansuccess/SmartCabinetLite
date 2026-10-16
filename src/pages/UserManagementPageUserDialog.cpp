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
#include "common/UserInputValidator.h"  // 工号与密码校验规则及提示文案

// ==============================================================
// 用户表单对话框（新增与编辑共用同一实例）
// 标题与主按钮文案由 onAddUser / onEditUser 在打开前按场景设置，
// 两处共用同一套表单，避免维护两份布局。
void UserManagementPage::createUserDialog() {
    if (m_userDialog) return;  // 已初始化，跳过

    m_userDialog = new BaseDialog(this, 520);
    m_userDialog->setDialogTitle(QStringLiteral("用户信息"));
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
    // 新建账户工号为6-12位纯数字，点击弹出数字键盘输入
    m_dlgWorkNo->setPlaceholderText(QStringLiteral("6-12位数字"));
    m_dlgWorkNo->setReadOnly(true);
    m_dlgWorkNo->setCursor(Qt::PointingHandCursor);
    m_dlgWorkNo->setStyleSheet(inputStyle);
    m_dlgWorkNo->installEventFilter(this);

    m_dlgDept = new QComboBox();
    m_dlgDept->setEditable(true);
    m_dlgDept->setStyleSheet(comboStyle);

    // 角色按钮组（3选1）- 固定宽度不Expanding撑满整行
    {
        auto makeRoleBtn = [&](const QString& text, int mode) -> QPushButton* {
            auto* btn = new QPushButton(text);
            btn->setCheckable(true);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setFixedHeight(StyleHelper::Token::ControlHeight);
            btn->setMinimumWidth(110);
            btn->setMaximumWidth(160);
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
        m_dlgRoleBtn3 = makeRoleBtn(QStringLiteral("管理用户"), 2);
    }
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
        roleBtnLayout->addWidget(m_dlgRoleBtn3);
        updateRoleBtnStyles();
        addFormRow(makeLabel(QStringLiteral("角色")), roleBtnGroup);
    }
    // 密码整行：新增与编辑均不显示——口令不经过管理员代设明文，
    // 需要重置时用下方"重置密码"按钮写回初始密码并要求用户下次登录自改
    m_dlgPasswordRow = new QWidget();
    m_dlgPasswordRow->setStyleSheet("background:transparent;");
    auto* pr = new QHBoxLayout(m_dlgPasswordRow);
    pr->setContentsMargins(0, 0, 0, 0);
    pr->setSpacing(12);
    auto* pwdLabel = makeLabel(QStringLiteral("密码"));
    pwdLabel->setFixedWidth(80);
    pr->addWidget(pwdLabel);
    pr->addWidget(m_dlgPasswordFrame, 1);
    cl->addWidget(m_dlgPasswordRow);

    // 一键重置口令：编辑态显示的唯一口令操作（不涉及明文输入），
    // 写回初始密码并要求该用户下次登录自行修改
    m_dlgResetPwdBtn = new QPushButton(QStringLiteral("重置密码"));
    m_dlgResetPwdBtn->setCursor(Qt::PointingHandCursor);
    m_dlgResetPwdBtn->setFixedHeight(StyleHelper::Token::ControlHeight);
    m_dlgResetPwdBtn->setStyleSheet(
        "QPushButton{background:#fff7e6;color:#fa8c16;border:1px solid #ffd591;"
        "border-radius:10px;font-size:14px;font-weight:600;}"
        "QPushButton:hover{background:#ffe7ba;}");
    // 重置密码同样含确认弹窗与查库写库，不在点击事件派发中同步执行
    connect(m_dlgResetPwdBtn, &QPushButton::clicked, this, [this]() {
        QTimer::singleShot(0, this, &UserManagementPage::onResetPwdInDialog);
    });
    m_dlgResetPwdBtn->setVisible(false);
    cl->addWidget(m_dlgResetPwdBtn);

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
    // 确认按钮不在点击事件派发中直接执行提交：提交内含对话框 accept、查库与
    // 模态提示弹窗，压在按钮事件派发栈内会让弹窗尺寸协商异常、界面假死，
    // 先让派发结束、在干净的事件循环轮次里执行（登录页软键盘确认同款处理）
    connect(m_dlgSaveBtn, &QPushButton::clicked, this, [this]() {
        QTimer::singleShot(0, this, &UserManagementPage::onSubmitUser);
    });

    bl->addWidget(cancelBtn);
    bl->addWidget(m_dlgSaveBtn);
}

/**
 * @brief 处理用户
 */
void UserManagementPage::onAddUser() {
    m_editUserId = 0;
    createUserDialog();  // 独立初始化，不弹窗
    m_userDialog->setDialogTitle(QStringLiteral("新增用户"));
    m_dlgSaveBtn->setText(QStringLiteral("确认新增"));
    m_dlgRealName->clear(); m_dlgWorkNo->clear(); m_dlgPassword->clear();
    m_dlgRoleMode = 0; updateRoleBtnStyles();
    m_dlgDept->setCurrentIndex(0);
    // 新增用户不输入口令：系统统一使用初始密码，口令相关行均隐藏
    if (m_dlgPasswordRow) m_dlgPasswordRow->setVisible(false);
    if (m_dlgResetPwdBtn) m_dlgResetPwdBtn->setVisible(false);
    m_userDialog->exec();
}

/**
 * @brief 处理编辑框用户
 */
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
    // 三种角色：普通用户(0)/管理员(1)/管理用户(2)
    m_dlgRoleMode = (u.role == SC::ROLE_ADMIN) ? 1 : (u.role == SC::ROLE_MANAGER ? 2 : 0);
    updateRoleBtnStyles();
    m_dlgPassword->setText(QString());
    // 编辑态不显示口令输入：柜机屏幕处于公共区域，敏感信息不上屏；
    // 口令操作只保留"重置密码"一键写回初始密码
    if (m_dlgPasswordRow) m_dlgPasswordRow->setVisible(false);
    if (m_dlgResetPwdBtn) m_dlgResetPwdBtn->setVisible(true);
    m_userDialog->exec();
}

/**
 * @brief 处理用户
 */
void UserManagementPage::onSubmitUser() {
    QString realName = m_dlgRealName->text().trimmed();
    QString workNo = m_dlgWorkNo->text().trimmed();
    QString password = m_dlgPassword->text();
    QString dept = m_dlgDept->currentText();
    // 角色从按钮组获取：0=普通用户 1=管理员 2=管理用户
    QString role = SC::ROLE_USER;
    if (m_dlgRoleMode == 1)      role = SC::ROLE_ADMIN;
    else if (m_dlgRoleMode == 2) role = SC::ROLE_MANAGER;

    if (realName.isEmpty() || workNo.isEmpty()) {
        MessageDialog::showError(this, QStringLiteral("提示"), QStringLiteral("请填写姓名和工号"));
        return;
    }
    // 工号格式校验（仅新增时）：新建账户为6-12位纯数字，与本控制器 createUser 共用同一规则
    if (m_editUserId == 0 && !common::isValidNewWorkNo(workNo)) {
        MessageDialog::showError(this, QStringLiteral("提示"), common::newWorkNoInvalidText());
        return;
    }
    // 部门为必填，新增与编辑都校验，避免编辑时被清空或改成不存在的部门
    if (dept.isEmpty()) {
        MessageDialog::showError(this, QStringLiteral("提示"), QStringLiteral("请选择部门"));
        return;
    }
    if (m_editUserId != 0 && !password.isEmpty() && password.length() < SC::PASSWORD_MIN_LENGTH) {
        MessageDialog::showError(this, QStringLiteral("提示"), common::passwordTooShortText());
        return;
    }
    if (m_editUserId != 0 && !password.isEmpty() && password.length() > SC::PASSWORD_MAX_LENGTH) {
        MessageDialog::showError(this, QStringLiteral("提示"), common::passwordTooLongText());
        return;
    }

  // 通过UserController替代直接调用db/UserDAO
    UserController ctrl;
    bool ok = false;
    if (m_editUserId == 0) {
        // 新建用户：口令统一使用系统初始密码，用户首次登录须自行修改
        User newUser;
        newUser.username = workNo;
        newUser.realName = realName;
        newUser.workNo = workNo;
        newUser.department = dept;
        newUser.role = role;
        newUser.phone = QString();  // 联系方式不在新增表单采集，落空值
        newUser.status = SC::USER_ACTIVE;
        int newId = ctrl.createUser(newUser, SC::DEFAULT_INIT_PASSWORD);
        ok = (newId > 0);
        if (ok) {
            m_userDialog->accept();
            loadUsers();
            MessageDialog::showSuccess(this, QStringLiteral("新增成功"),
                                       QStringLiteral("初始密码 %1").arg(SC::DEFAULT_INIT_PASSWORD));
        } else {
            MessageDialog::showError(this, QStringLiteral("提示"),
                                     QStringLiteral("新增失败，工号可能已存在"));
        }
        return;
    }
    // 更新用户：整体更新写库前从库里取一次原记录，
// 把不在编辑界面展示的字段（电话/邮箱/状态）原样带回，避免被空值覆盖——
// 状态尤其重要：禁用中的用户编辑保存后不能被意外激活
    const User original = ctrl.getUserById(m_editUserId);
    User updateUser;
    updateUser.userId = m_editUserId;
    updateUser.realName = realName;
    updateUser.workNo = workNo;
    updateUser.department = dept;
    updateUser.role = role;
    updateUser.phone = original.phone;   // 电话不在编辑界面展示，保留原值
    updateUser.email = original.email;
    updateUser.status = original.status;
    ok = ctrl.updateUser(updateUser);
    if (ok) {
        m_userDialog->accept();
        loadUsers();
    } else {
        MessageDialog::showError(this, QStringLiteral("提示"), QStringLiteral("保存失败，请重试"));
    }
}

/**
 * @brief 编辑对话框内一键重置口令：写回系统初始密码，并标记该用户下次登录必须修改
 * @note 仅编辑态可用（新增时用户尚未创建）；重置后清空口令输入框，
 *       管理员若同时在框内输入了新口令，以重置结果为准
 */
void UserManagementPage::onResetPwdInDialog() {
    if (m_editUserId <= 0) return;
    if (!MessageDialog::showQuestion(this, QStringLiteral("确认重置"),
        QStringLiteral("将该用户口令重置为初始密码？"))) return;

    UserController ctrl;
    if (ctrl.resetPassword(m_editUserId, SC::DEFAULT_INIT_PASSWORD, true)) {
        m_dlgPassword->clear();
        MessageDialog::showSuccess(this, QStringLiteral("重置成功"),
                                   QStringLiteral("下次登录须修改密码"));
    } else {
        MessageDialog::showError(this, QStringLiteral("提示"), QStringLiteral("重置失败，请重试"));
    }
}

/** 搜索框点击→弹出用户名软键盘(对齐登录页ModeEn模式) */

// ==============================================================
