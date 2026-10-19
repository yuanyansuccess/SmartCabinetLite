/**
 * @file UserManagementPageUserDialog.cpp
 * @brief 人员管理-用户编辑对话框（构建/新增/编辑/删除/提交）
 * @author 袁燕
 *
 * 本文件实现上述功能，成员函数声明见 UserManagementPage.h。
 */

#include "UserManagementPage.h"
#include "ui_UserManagementPage.h"
#include "components/PaginationBar.h"
#include "components/SoftKeyboard.h"
#include "components/NumKeypad.h"
#include "components/FaceCameraWidget.h"  // 人脸录入对话框摄像头组件
#include "components/DeepFaceExtractor.h"  // 人脸方位检测
#include "components/SingleSelectFilter.h"  // 通用单选筛选组件
#include "utils/StyleHelper.h"
#include "controller/UserController.h"
#include "db/UserDAO.h"        // 用户实体查询（读取操作）
#include "db/DepartmentDAO.h"  // 部门下拉数据源（部门ID + 名称）
#include "services/AuthService.h"
#include "services/SettingService.h"
#include "services/FaceRecognitionService.h"  // 人脸录入/清除
#include "components/BaseDialog.h"  // 统一圆角对话框基类
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialog>
#include "components/MessageDialog.h"
#include <QDebug>
#include <QFrame>
#include <QPushButton>
#include <QLabel>
#include <QTimer>  // 人脸录入成功延迟关闭
#include <functional>  // std::function
#include "common/Constants.h"
#include "common/UserInputValidator.h"  // 工号与密码校验规则及提示文案

/**
 * @brief 两处共用同一套表单，避免维护两份布局。 标题与主按钮文案由 onAddUser / onEditUser 在打开前按场景设置， 用户表单对话框（新增与编辑共用同一实例）
 */
void UserManagementPage::createUserDialog() {
    if (m_userDialog) return;  // 已初始化，跳过

    m_userDialog = new BaseDialog(this, 520);
    m_userDialog->setDialogTitle(QStringLiteral("用户信息"));
    // 高度随内容自适应，不硬编码最小高度，避免字段多时被压紧
    // m_userDialog->setMinimumHeight(460);

    auto* cl = m_userDialog->contentLayout();
    // BaseDialog 内容区默认 spacing 为 16，此处不再压成 12，让表单更舒展
    // cl->setSpacing(12);

    QString inputStyle =
        "QLineEdit{padding:10px 14px;border:2px solid #e0e0e0;border-radius:10px;"
        "" + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + ";background:#fff;color:#333;min-height:44px;}"
        "QLineEdit:focus{border-color:#4da3ff;}"
        // 编辑态姓名/工号只读：灰底明示不可改，避免误以为输入框失效
        "QLineEdit:disabled{border-color:#e0e0e0;background:#f5f5f5;color:#666;}";
    QString comboStyle =
        "QComboBox{padding:10px 14px;border:2px solid #e0e0e0;border-radius:10px;"
        "" + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + ";background:#fff;color:#333;min-height:44px;min-width:200px;}"
        "QComboBox:focus{border-color:#4da3ff;}QComboBox::drop-down{border:none;width:30px;}"
        "QComboBox:disabled{border-color:#e0e0e0;background:#f5f5f5;color:#666;}";

    m_dlgRealName = new QLineEdit();
    m_dlgRealName->setPlaceholderText(QStringLiteral("点击输入中文姓名"));
    m_dlgRealName->setStyleSheet(inputStyle);
    // 姓名走内置中文软键盘（拼音词库）输入：只读避免系统键盘弹出，
    // 点击由 UserManagementPage::eventFilter 弹出 SoftKeyboard 中文模式
    m_dlgRealName->setReadOnly(true);
    m_dlgRealName->setCursor(Qt::PointingHandCursor);
    m_dlgRealName->installEventFilter(this);

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
        m_dlgRoleBtn3 = makeRoleBtn(QStringLiteral("管理+用户"), 2);
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
        "QLineEdit{border:none;padding:0;" + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + ";color:#1a1a2e;background:transparent;}");
    pwdFL->addWidget(m_dlgPassword, 1);

    auto* skbPwdBtn = new QPushButton(QStringLiteral("⌨"));
    skbPwdBtn->setFixedSize(40, 40);
    skbPwdBtn->setCursor(Qt::PointingHandCursor);
    skbPwdBtn->setStyleSheet(
        "QPushButton{border:none;border-radius:10px;" + StyleHelper::fontSize(StyleHelper::Token::FontHeading) + ";background:transparent;color:#666;}"
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
    // 下拉项同时携带部门ID，提交时按ID落库，避免只存名称导致dept_id为空
    db::DepartmentDAO deptDao;
    for (const auto& dept : deptDao.allDepartments())
        m_dlgDept->addItem(dept.deptName, dept.deptId);

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
        row->setSpacing(16);
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
        roleBtnLayout->setSpacing(12);
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
    pr->setSpacing(16);
    auto* pwdLabel = makeLabel(QStringLiteral("密码"));
    pwdLabel->setFixedWidth(80);
    pr->addWidget(pwdLabel);
    pr->addWidget(m_dlgPasswordFrame, 1);
    cl->addWidget(m_dlgPasswordRow);

    // 一键重置口令：编辑态显示的唯一口令操作（不涉及明文输入），
    // 写回初始密码并要求该用户下次登录自行修改
    m_dlgResetPwdBtn = new QPushButton(QStringLiteral("重置密码"));
    m_dlgResetPwdBtn->setCursor(Qt::PointingHandCursor);
    m_dlgResetPwdBtn->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
    m_dlgResetPwdBtn->setStyleSheet(
        "QPushButton{background:#fff7e6;color:#fa8c16;border:1px solid #ffd591;"
        "border-radius:10px;" + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + ";font-weight:600;}"
        "QPushButton:hover{background:#ffe7ba;}");
    // 重置密码同样含确认弹窗与查库写库，不在点击事件派发中同步执行
    connect(m_dlgResetPwdBtn, &QPushButton::clicked, this, [this]() {
        QTimer::singleShot(0, this, &UserManagementPage::onResetPwdInDialog);
    });
    m_dlgResetPwdBtn->setVisible(false);
    cl->addWidget(m_dlgResetPwdBtn);

    // 人脸录入（仅编辑态可用）：未录入→录入，已录入→查看/清除
    m_dlgFaceBtn = new QPushButton(QStringLiteral("人脸录入"));
    m_dlgFaceBtn->setCursor(Qt::PointingHandCursor);
    m_dlgFaceBtn->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
    m_dlgFaceBtn->setStyleSheet(
        "QPushButton{background:#fff7e6;color:#fa8c16;border:1px solid #ffd591;"
        "border-radius:10px;" + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + ";font-weight:600;}"
        "QPushButton:hover{background:#ffe7ba;}");
    m_dlgFaceBtn->setVisible(false);  // 默认隐藏，编辑态由 onEditUser 显示
    // 人脸录入会打开带摄像头的模态窗口，同样不在按钮点击事件派发中同步执行
    connect(m_dlgFaceBtn, &QPushButton::clicked, this, [this]() {
        QTimer::singleShot(0, this, &UserManagementPage::onFaceEnrollInDialog);
    });
    cl->addWidget(m_dlgFaceBtn);

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
        "" + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + ";font-weight:600;}"
        "QPushButton:hover{background:#f5f5f5;}"
    );
    connect(cancelBtn, &QPushButton::clicked, this, [this]() { m_userDialog->reject(); });

    m_dlgSaveBtn = new QPushButton(QStringLiteral("确认"));
    m_dlgSaveBtn->setMinimumHeight(StyleHelper::Token::ControlHeightTouch);
    m_dlgSaveBtn->setMinimumWidth(100);
    m_dlgSaveBtn->setCursor(Qt::PointingHandCursor);
    m_dlgSaveBtn->setStyleSheet(
        "QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:12px;"
        "" + StyleHelper::fontSize(StyleHelper::Token::FontInput) + ";font-weight:700;}"
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
    // 新增时姓名/工号/部门均可录入（编辑态被置灰，此处必须恢复）
    m_dlgRealName->setEnabled(true);
    m_dlgWorkNo->setEnabled(true);
    m_dlgWorkNo->setCursor(Qt::PointingHandCursor);  // 新增态工号可录入，手型明示可点
    m_dlgDept->setEnabled(true);
    // 新增用户不输入口令：系统统一使用初始密码，口令相关行均隐藏
    if (m_dlgPasswordRow) m_dlgPasswordRow->setVisible(false);
    if (m_dlgResetPwdBtn) m_dlgResetPwdBtn->setVisible(false);
    if (m_dlgFaceBtn) m_dlgFaceBtn->setVisible(false);
    // 编辑态曾把对话框最小高度顶大；隐藏行后必须先释放该最小高度约束，
    // 否则 adjustSize() 会把结果 expandedTo(minimumSize()) 而被卡住缩不回去
    // （表现为行之间出现一大段留白）。只重置高度，宽度由 setFixedWidth 保持不变。
    m_userDialog->setMinimumHeight(0);
    m_userDialog->adjustSize();
    m_userDialog->exec();
}

/**
 * @brief
 * @param userId 用户唯一标识 user_id
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
    // 下拉项同时携带部门ID，提交时按ID落库，避免只存名称导致dept_id为空
    db::DepartmentDAO deptDao;
    for (const auto& dept : deptDao.allDepartments())
        m_dlgDept->addItem(dept.deptName, dept.deptId);
    m_dlgRealName->setText(u.realName);
    m_dlgWorkNo->setText(u.workNo);
    // 编辑态姓名/工号/部门不允许改动：这三项是人员档案的固定标识，
    // 管理员只能调整角色与口令，避免误改后与借用记录、人脸档案对不上
    m_dlgRealName->setEnabled(false);
    m_dlgWorkNo->setEnabled(false);
    m_dlgWorkNo->setCursor(Qt::ArrowCursor);  // 编辑态工号禁止编辑，箭头光标明示不可点
    m_dlgDept->setEnabled(false);
    // 优先按部门ID定位，历史数据找不到时退回按名称匹配
    const int deptIdx = m_dlgDept->findData(u.deptId);
    if (deptIdx >= 0) m_dlgDept->setCurrentIndex(deptIdx);
    else              m_dlgDept->setCurrentText(u.department);
    // 三种角色：普通用户(0)/管理员(1)/管理用户(2)
    m_dlgRoleMode = (u.role == SC::ROLE_ADMIN) ? 1 : (u.role == SC::ROLE_MANAGER ? 2 : 0);
    updateRoleBtnStyles();
    m_dlgPassword->setText(QString());
    // 编辑态不显示口令输入：柜机屏幕处于公共区域，敏感信息不上屏；
    // 口令操作只保留"重置密码"一键写回初始密码
    if (m_dlgPasswordRow) m_dlgPasswordRow->setVisible(false);
    if (m_dlgResetPwdBtn) m_dlgResetPwdBtn->setVisible(true);
    if (m_dlgFaceBtn) {
        m_dlgFaceEnrolled = !u.faceFeature.isEmpty();
        m_dlgFaceBtn->setText(m_dlgFaceEnrolled ? QStringLiteral("人脸信息") : QStringLiteral("人脸录入"));
        m_dlgFaceBtn->setVisible(true);
    }
    // 先释放上一场景遗留的最小高度约束，再按编辑态内容重算高度，各自贴合内容
    m_userDialog->setMinimumHeight(0);
    m_userDialog->adjustSize();
    m_userDialog->exec();
}

/**
 * @brief 处理用户
 */
void UserManagementPage::onSubmitUser() {
    QString realName = m_dlgRealName->text().trimmed();
    QString workNo = m_dlgWorkNo->text().trimmed();
    QString password = m_dlgPassword->text();
    QString dept = m_dlgDept->currentText().trimmed();
    // 部门必须来自部门表：按名称精确定位下拉项并取出部门ID
    const int deptIdx = m_dlgDept->findText(dept, Qt::MatchExactly);
    const int deptId = (deptIdx >= 0) ? m_dlgDept->itemData(deptIdx).toInt() : 0;
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
    // 部门为必填且必须是部门表中存在的部门，保证 dept_id 能正确关联
    if (dept.isEmpty()) {
        MessageDialog::showError(this, QStringLiteral("提示"), QStringLiteral("请选择部门"));
        return;
    }
    if (deptId <= 0) {
        MessageDialog::showError(this, QStringLiteral("提示"),
                                 QStringLiteral("部门「%1」不存在，请从下拉列表中选择").arg(dept));
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
        newUser.deptId = deptId;
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
    updateUser.deptId = deptId;
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

/**
 * @brief 编辑对话框内人脸录入与查看：未录入打开采集流程，已录入打开查看与清除
 * @note 采集与查看都是带摄像头的模态窗口，放在按钮点击事件派发结束后执行，
 *       避免嵌套模态压在事件派发栈里导致窗口尺寸协商异常；
 *       流程结束后回读该用户的人脸特征，同步按钮文案为「人脸信息」或「人脸录入」
 */
void UserManagementPage::onFaceEnrollInDialog() {
    if (m_editUserId <= 0) return;
    if (m_dlgFaceEnrolled) {
        onFaceEnrolledClick(m_editUserId, m_dlgRealName->text(), m_dlgWorkNo->text());
    } else {
        onFaceEnroll(m_editUserId);
    }
    UserController ctrl;
    User u = ctrl.getUserById(m_editUserId);
    if (u.userId != 0) {
        m_dlgFaceEnrolled = !u.faceFeature.isEmpty();
        m_dlgFaceBtn->setText(m_dlgFaceEnrolled ? QStringLiteral("人脸信息") : QStringLiteral("人脸录入"));
    }
}

/** 搜索框点击→弹出用户名软键盘(对齐登录页ModeEn模式) */

// ==============================================================
