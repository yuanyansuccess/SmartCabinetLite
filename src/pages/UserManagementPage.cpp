/**
 * @file UserManagementPage.cpp
 * @brief 人员管理页面实现 - 1:1复刻BS端UserManagement.vue
 * @author 袁燕
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

UserManagementPage::UserManagementPage(QWidget* parent) : QWidget(parent), ui(new Ui::UserManagementPage),
    m_userDialog(nullptr), m_editUserId(0), m_currentPage(1), m_pageSize(20), m_totalRecords(0) {
    // 静态布局来自UserManagementPage.ui（Qt Designer可视化维护）
    ui->setupUi(this);
    setupUI();
    // 初始化安全软键盘 - confirmed信号仅关闭键盘(attach已处理输入)
    m_softKeyboard = new SoftKeyboard(this);
    connect(m_softKeyboard, &SoftKeyboard::confirmed, this, [this]() {
        // 数字输入已由NumKeypad独立负责，SoftKeyboard仅处理字母/搜索
        m_softKeyboard->hide();
    });
    connect(m_softKeyboard, &SoftKeyboard::cancelled, this, [this]() {
        m_softKeyboard->hide();
    });
}

UserManagementPage::~UserManagementPage() {
    delete ui;
}

void UserManagementPage::setupUI() {
    // 桥接.ui控件（业务逻辑沿用m_成员，零改动）
    m_addBtn = ui->addBtn;
    m_searchEdit = ui->searchBar->lineEdit();
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索姓名 / 工号..."));
    m_searchEdit->setReadOnly(true);
    m_searchEdit->setCursor(Qt::PointingHandCursor);
    m_searchKeyboardBtn = ui->searchBar->keyboardButton();
    m_deptFilterBtn = ui->deptFilterBtn;
    m_searchBtn = ui->searchBtn;
    m_resetBtn = ui->resetBtn;
    m_table = ui->table;
    m_paginationBar = ui->paginationBar;

    // 搜索框行为（点击弹软键盘）
    m_searchEdit->setCursor(Qt::PointingHandCursor);
    m_searchEdit->installEventFilter(this);
    connect(m_searchKeyboardBtn, &QPushButton::clicked, this, &UserManagementPage::onSearchFieldClicked);

    // 新增/批量导入
    connect(m_addBtn, &QPushButton::clicked, this, &UserManagementPage::onAddUser);
    connect(ui->importBtn, &QPushButton::clicked, this, [this]() {
        // 批量导入对话框：模板下载 → Excel上传 → 校验 → 批量入库 → 刷新列表
        BatchImportDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            loadUsers();  // 导入成功后自动刷新列表
        }
    });

    // 部门筛选按钮
    connect(m_deptFilterBtn, &QPushButton::clicked, this, &UserManagementPage::onDeptFilterClicked);

    // 部门多选下拉面板 QFrame→QDialog修复checkbox选不中致命Bug（弹窗动态构建）
    m_deptPopup = new QDialog(this);
    m_deptPopup->setWindowFlags(Qt::FramelessWindowHint | Qt::Popup);
    m_deptPopup->setModal(false);
    m_deptPopup->setFixedWidth(240);  // Vue .multi-select width:240px
    m_deptPopup->setStyleSheet(
        "QDialog{background:white;border:2px solid #e0e0e0;border-radius:10px;}"
        "QCheckBox{font-size:15px;padding:10px 20px;spacing:10px;}"
        "QCheckBox::indicator{width:20px;height:20px;}"
        "QPushButton{min-height:40px;font-size:15px;border-radius:8px;}"
    );
    m_deptPopup->hide();
    auto* deptPopupLayout = new QVBoxLayout(m_deptPopup);
    deptPopupLayout->setContentsMargins(12, 12, 12, 12);
    deptPopupLayout->setSpacing(4);
    m_deptPopup->setLayout(deptPopupLayout);  // 确保布局已关联,loadDepartments可重建

    // 部门列表由loadDepartments()动态加载(对齐Web版异步加载策略)
    loadDepartments();

    // 角色/状态筛选 - CheckBox样式单选组件（自定义组件装入.ui槽位）
    m_roleFilter = new SingleSelectFilter(QStringLiteral("全部角色"), this);
    m_roleFilter->setOptions({QStringLiteral("全部角色"), QStringLiteral("管理员"), QStringLiteral("普通用户")});
    connect(m_roleFilter, &SingleSelectFilter::selectionChanged, this, [this](const QString&) { m_currentPage = 1; loadUsers(); });
    ui->roleFilterSlotLayout->addWidget(m_roleFilter);

    m_statusFilter = new SingleSelectFilter(QStringLiteral("全部状态"), this);
    m_statusFilter->setOptions({QStringLiteral("全部状态"), QStringLiteral("启用"), QStringLiteral("待激活"),
                                QStringLiteral("禁用"), QStringLiteral("已锁定")});
    connect(m_statusFilter, &SingleSelectFilter::selectionChanged, this, [this](const QString&) { m_currentPage = 1; loadUsers(); });
    ui->statusFilterSlotLayout->addWidget(m_statusFilter);

    // 查询/重置/分页
    connect(m_searchBtn, &QPushButton::clicked, this, &UserManagementPage::onSearch);
    connect(m_resetBtn, &QPushButton::clicked, this, &UserManagementPage::onReset);
    connect(m_paginationBar, &PaginationBar::prevClicked, this, &UserManagementPage::onPrevPage);
    connect(m_paginationBar, &PaginationBar::nextClicked, this, &UserManagementPage::onNextPage);

    // 表格列宽策略：数据列Stretch均分，操作列Fixed 200px（触屏按钮）
    m_table->horizontalHeader()->setStretchLastSection(false);
    for (int i = 0; i < 8; i++) {
        m_table->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
    }
    m_table->horizontalHeader()->setSectionResizeMode(8, QHeaderView::Fixed);
    m_table->setColumnWidth(8, 200);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setMinimumSectionSize(60);
}

void UserManagementPage::refresh() {
    loadDepartments();  // 每次刷新重新加载部门列表(对齐Web版onMounted)
    loadUsers();
}

void UserManagementPage::onSearch() {
    m_currentPage = 1;  // 筛选查询必须重置到第1页
    loadUsers();
}

void UserManagementPage::onReset() {
    // 重置所有筛选条件
    m_searchEdit->clear();
    m_roleFilter->reset();  // SingleSelectFilter::reset()
    m_statusFilter->reset();  // SingleSelectFilter::reset()
    for (auto& pair : m_deptCheckBoxes) {
        pair.second->setChecked(false);
    }
    m_deptFilterBtn->setText(QStringLiteral("部门筛选"));
    m_currentPage = 1;  // 重置筛选必须重置到第1页
    loadUsers();
}

/** 从DB动态加载部门列表(合并sys_department + sys_user DISTINCT)
 *  确保筛选下拉框与列表显示的部门数据完全一致
 *  delete+recreate弹窗方案(安全)
 */
void UserManagementPage::loadDepartments() {
    m_departmentOptions.clear();
    QSet<QString> seen;

    // 1) sys_department表(预定义部门列表)
    SettingService svc;
    QJsonArray depts = svc.getDepartments();
    for (const auto& d : depts) {
        QString name = d.toString().trimmed();
        if (!name.isEmpty() && !seen.contains(name)) {
            seen.insert(name);
            m_departmentOptions.append(name);
        }
    }

    // 2) sys_user表中用户实际使用的部门(DISTINCT) —— 确保100%覆盖所有用户
    {
        db::UserDAO userDao;
        QStringList userDepts = userDao.getDistinctDepartments();
        for (const QString& name : userDepts) {
            QString trimmed = name.trimmed();
            if (!trimmed.isEmpty() && !seen.contains(trimmed)) {
                seen.insert(trimmed);
                m_departmentOptions.append(trimmed);
            }
        }
    }

    if (m_departmentOptions.isEmpty()) {
        qDebug() << "[UserManagementPage] 部门列表为空, 降级使用默认值";
        m_departmentOptions = {"技术部", "维修一组", "维修二组", "维修三组", "质检部"};
    } else {
        qDebug() << "[UserManagementPage] 从DB加载了" << m_departmentOptions.size() << "个部门(合并sys_department+sys_user)";
    }

    // 3) 同步重建筛选弹窗(保留旧选中状态)
    if (m_deptPopup) {
        QSet<QString> selected;
        for (auto& pair : m_deptCheckBoxes) {
            if (pair.second && pair.second->isChecked())
                selected.insert(pair.first);
        }
        m_deptCheckBoxes.clear();
        delete m_deptPopup;
        m_deptPopup = nullptr;
    }

    m_deptPopup = new QDialog(this);
    m_deptPopup->setWindowFlags(Qt::FramelessWindowHint | Qt::Popup);
    m_deptPopup->setModal(false);
    m_deptPopup->setFixedWidth(240);
    m_deptPopup->setStyleSheet(
        "QDialog{background:white;border:2px solid #e0e0e0;border-radius:10px;}"
        "QCheckBox{font-size:15px;padding:10px 20px;spacing:10px;}"
        "QCheckBox::indicator{width:20px;height:20px;}"
        "QPushButton{min-height:40px;font-size:15px;border-radius:8px;}"
    );

    auto* deptLayout = new QVBoxLayout(m_deptPopup);
    deptLayout->setContentsMargins(12, 12, 12, 12);
    deptLayout->setSpacing(4);

    // 默认全选
    for (const QString& deptName : m_departmentOptions) {
        auto* cb = new QCheckBox(deptName);
        cb->setChecked(true);  // 默认全选，让用户自行取消筛选
        m_deptCheckBoxes.append(qMakePair(deptName, cb));
        deptLayout->addWidget(cb);
    }

    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(8);
    auto* clearBtn = new QPushButton(QStringLiteral("清空"));
    clearBtn->setStyleSheet(
        "QPushButton{background:#fff;color:#666;border:none;border-radius:8px;padding:8px 16px;font-size:13px;font-weight:600;}"
        "QPushButton:hover{background:#f0f2f5;}"
    );
    connect(clearBtn, &QPushButton::clicked, this, &UserManagementPage::onClearDepts);
    auto* confirmBtn = new QPushButton(QStringLiteral("确定"));
    confirmBtn->setStyleSheet(
        "QPushButton{background:transparent;color:#4da3ff;border:none;border-radius:8px;padding:8px 16px;font-size:13px;font-weight:600;}"
        "QPushButton:hover{background:#f0f2f5;}"
    );
    connect(confirmBtn, &QPushButton::clicked, this, &UserManagementPage::onConfirmDepts);
    btnLayout->addWidget(clearBtn);
    btnLayout->addWidget(confirmBtn);
    deptLayout->addLayout(btnLayout);

    m_deptPopup->hide();
}

void UserManagementPage::loadUsers() {
    QString kw = m_searchEdit->text().trimmed();
    // SingleSelectFilter::selectedText() + selectedIndex() 替换 QComboBox::currentText() + currentIndex()
    QString roleText = m_roleFilter->selectedText();
    int roleIdx = m_roleFilter->selectedIndex();
    QString role = (roleIdx > 0) ? (roleText == QStringLiteral("管理员") ? SC::ROLE_ADMIN : SC::ROLE_USER) : "";

    QString statusText = m_statusFilter->selectedText();
    int statusIdx = m_statusFilter->selectedIndex();
    QString status = (statusIdx > 0) ?
        (statusText == QStringLiteral("启用") ? SC::USER_ACTIVE :
         statusText == QStringLiteral("待激活") ? SC::USER_PENDING :
         statusText == QStringLiteral("禁用") ? SC::USER_DISABLED : SC::USER_LOCKED) : "";
    // 从部门多选checkbox中收集选中部门，拼成逗号分隔字符串传给API
    QStringList selectedDepts;
    for (auto& pair : m_deptCheckBoxes) {
        if (pair.second->isChecked()) selectedDepts.append(pair.first);
    }
    QString dept = selectedDepts.join(",");

  // 通过UserController替代直接调用db/UserDAO
    UserController ctrl;
    auto pageResult = ctrl.getUserList(m_currentPage, m_pageSize, kw, dept, status, role);
    m_totalRecords = pageResult.total;

    // 更新分页信息
    int totalPages = (m_totalRecords + m_pageSize - 1) / m_pageSize;
    m_paginationBar->setPageInfo(m_currentPage, totalPages);
    m_paginationBar->setTotalRecords(m_totalRecords);

    // 填充表格（BS端列：工号/姓名/部门/角色/联系电话/人脸录入/创建时间/状态/操作）
    m_table->setRowCount(pageResult.list.size());
    for (int i = 0; i < pageResult.list.size(); ++i) {
        const User& u = pageResult.list[i];
        int userId = u.userId;
        QString roleText = (u.role == SC::ROLE_ADMIN) ? QStringLiteral("管理员") : QStringLiteral("普通用户");
        QString statusText = (u.status == SC::USER_ACTIVE) ? QStringLiteral("启用") :
                          (u.status == SC::USER_PENDING) ? QStringLiteral("待激活") :
                          (u.status == SC::USER_DISABLED) ? QStringLiteral("禁用") : QStringLiteral("已锁定");

        m_table->setItem(i, 0, new QTableWidgetItem(u.workNo));  // 工号
        m_table->setItem(i, 1, new QTableWidgetItem(u.realName));  // 姓名
        m_table->setItem(i, 2, new QTableWidgetItem(u.department));  // 部门

        // 角色标签仿照设计图：小圆角标签样式
        auto* roleItem = new QTableWidgetItem(roleText);
        roleItem->setTextAlignment(Qt::AlignCenter);
        if (u.role == SC::ROLE_ADMIN) {
            roleItem->setForeground(QColor("#2b6cdf"));
            roleItem->setBackground(QColor("#e6f0ff"));
        } else {
            roleItem->setForeground(QColor("#389e0d"));
            roleItem->setBackground(QColor("#f0f9e8"));
        }
        QFont roleFont = roleItem->font();
        roleFont.setBold(true);
        roleItem->setFont(roleFont);
        m_table->setItem(i, 3, roleItem);

        m_table->setItem(i, 4, new QTableWidgetItem(u.phone));  // 联系电话

        // 人脸录入列：可点击按钮（已录入→查看/清除，未录入→录入）
        bool faceEnrolled = !u.faceFeature.isEmpty();
        auto* faceWidget = new QWidget();
        faceWidget->setStyleSheet("background:transparent;");
        auto* faceLayout = new QHBoxLayout(faceWidget);
        faceLayout->setContentsMargins(4, 4, 4, 4);
        faceLayout->setSpacing(0);
        faceLayout->setAlignment(Qt::AlignCenter);

        auto* faceBtn = new QPushButton(faceEnrolled ? QStringLiteral("已录入") : QStringLiteral("未录入"));
        faceBtn->setFixedSize(72, 32);
        faceBtn->setCursor(Qt::PointingHandCursor);
        if (faceEnrolled) {
            faceBtn->setStyleSheet(
                "QPushButton{background:#f6ffed;color:#389e0d;border:1px solid #b7eb8f;"
                "border-radius:12px;font-size:13px;font-weight:600;}"
                "QPushButton:hover{background:#d9f7be;border-color:#52c41a;}"
                "QPushButton:pressed{background:#b7eb8f;}");
            connect(faceBtn, &QPushButton::clicked, this, [this, userId, u, i]() {
                m_table->selectRow(i);
                onFaceEnrolledClick(userId, u.realName, u.workNo);
            });
        } else {
            faceBtn->setStyleSheet(
                "QPushButton{background:#fff7e6;color:#fa8c16;border:1px solid #ffd591;"
                "border-radius:12px;font-size:13px;font-weight:600;}"
                "QPushButton:hover{background:#ffe7ba;border-color:#fa8c16;}"
                "QPushButton:pressed{background:#ffd591;}");
            connect(faceBtn, &QPushButton::clicked, this, [this, userId, i]() {
                m_table->selectRow(i);
                onFaceEnroll(userId);
            });
        }
        faceLayout->addWidget(faceBtn);
        m_table->setCellWidget(i, 5, faceWidget);

        m_table->setItem(i, 6, new QTableWidgetItem(u.createdAt.toString("yyyy-MM-dd HH:mm")));  // 创建时间

        // 状态标签仿照设计图：启用(绿)/待激活(灰)/禁用(灰)/已锁定(红)
        auto* statusItem = new QTableWidgetItem(statusText);
        statusItem->setTextAlignment(Qt::AlignCenter);
        if (u.status == SC::USER_ACTIVE) {
            statusItem->setForeground(QColor("#389e0d"));
            statusItem->setBackground(QColor("#f6ffed"));
        } else if (u.status == SC::USER_PENDING) {
            statusItem->setForeground(QColor("#999999"));
            statusItem->setBackground(QColor("#f5f5f5"));
        } else if (u.status == SC::USER_DISABLED) {
            statusItem->setForeground(QColor("#999999"));
            statusItem->setBackground(QColor("#f5f5f5"));
        } else {
            statusItem->setForeground(QColor(StyleHelper::dangerColor()));
            statusItem->setBackground(QColor("#fef2f2"));
        }
        QFont statusFont = statusItem->font();
        statusFont.setBold(true);
        statusItem->setFont(statusFont);
        m_table->setItem(i, 7, statusItem);

        // 按钮加大+间距增大，确保触屏可点、文字完整显示
        auto* opWidget = new QWidget();
        opWidget->setStyleSheet("background:transparent;");
        auto* opLayout = new QHBoxLayout(opWidget);
        opLayout->setContentsMargins(4, 4, 4, 4);
        opLayout->setSpacing(8);  // 8 按钮间距加大

        // "编辑"按钮 (64x36, 触屏优化)
        auto* editLink = new QPushButton(QStringLiteral("编辑"));
        editLink->setFixedSize(64, 36);
        editLink->setStyleSheet(
            "QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:8px;"
            "font-size:14px;font-weight:700;}"
            "QPushButton:hover{background:#3d8ae0;}"
            "QPushButton:pressed{background:#2b6cdf;}"
        );
        editLink->setCursor(Qt::PointingHandCursor);
        connect(editLink, &QPushButton::clicked, this, [this, userId, i] { m_table->selectRow(i); onEditUser(userId); });

        // "禁用/启用"按钮 (64x36)
        bool isActive = (u.status == SC::USER_ACTIVE);
        auto* toggleLink = new QPushButton(isActive ? QStringLiteral("禁用") : QStringLiteral("启用"));
        toggleLink->setFixedSize(64, 36);
        QString toggleBg = isActive ? "#e74c3c" : "#27ae60";
        QString toggleHoverBg = isActive ? "#c0392b" : "#219a52";
        toggleLink->setStyleSheet(QString(
            "QPushButton{background:%1;color:#fff;border:none;border-radius:8px;"
            "font-size:14px;font-weight:700;}"
            "QPushButton:hover{background:%2;}"
            "QPushButton:pressed{background:%3;}"
        ).arg(toggleBg, toggleHoverBg, isActive ? "#a93226" : "#1e8449"));
        toggleLink->setCursor(Qt::PointingHandCursor);
        connect(toggleLink, &QPushButton::clicked, this, [this, userId, i] { m_table->selectRow(i); onToggleUserStatus(userId); });

        opLayout->addWidget(editLink);
        opLayout->addWidget(toggleLink);
        opLayout->addStretch();
        m_table->setCellWidget(i, 8, opWidget);
        m_table->setRowHeight(i, 64);  // 64 行高加大，确保36px按钮+8px边距完整显示不裁剪
    }
}

void UserManagementPage::onPrevPage() {
    if (m_currentPage > 1) {
        m_currentPage--;
        loadUsers();
    }
}

void UserManagementPage::onNextPage() {
    int totalPages = (m_totalRecords + m_pageSize - 1) / m_pageSize;
    if (m_currentPage < totalPages) {
        m_currentPage++;
        loadUsers();
    }
}

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
        lbl->setStyleSheet("font-size:15px;font-weight:600;color:#333;background:transparent;");
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
void UserManagementPage::onSearchFieldClicked() {
    // 先隐藏数字键盘防止穿透残留
    if (m_numKeypad) m_numKeypad->hide();
    if (m_softKeyboard && m_searchEdit) {
        m_softKeyboard->setMode(SoftKeyboard::ModeEn);
        m_softKeyboard->setConfirmText(QStringLiteral("搜索"));
        m_softKeyboard->attach(m_searchEdit);
        m_softKeyboard->show();
    }
}

bool UserManagementPage::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        if (obj == m_searchEdit) {
            onSearchFieldClicked();
            return true;
        }
        // 工号改纯数字：点击弹出数字键盘（不随机打乱、明文显示）
        if (obj == m_dlgWorkNo) {
            if (m_softKeyboard) m_softKeyboard->hide();
            if (m_numKeypad) {
                m_numKeypad->setShuffle(false);
                m_numKeypad->setShowPassword(true);
                m_numKeypad->attach(m_dlgWorkNo);
                m_numKeypad->show();
            }
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

/** 密码框点击 → 弹出安全软键盘(密码随机模式) */
/// NumKeypad嵌入式控件，此函数已废弃但保留接口兼容
void UserManagementPage::onDlgPasswordClicked() {
    // 已NumKeypad嵌入式控件，逻辑移至⌨按钮的lambda中
}

/** 部门筛选按钮点击 */
void UserManagementPage::onDeptFilterClicked() {
    if (m_deptPopup->isVisible()) {
        m_deptPopup->hide();
    } else {
        // 定位到按钮下方
        QPoint pos = m_deptFilterBtn->mapToGlobal(QPoint(0, m_deptFilterBtn->height() + 4));
        m_deptPopup->move(pos);
        m_deptPopup->show();
    }
}

/** 清空部门选择 */
void UserManagementPage::onClearDepts() {
    for (auto& pair : m_deptCheckBoxes) {
        pair.second->setChecked(false);
    }
    m_deptFilterBtn->setText(QStringLiteral("部门筛选"));
}

/** 确认部门选择 */
void UserManagementPage::onConfirmDepts() {
    QStringList selected;
    for (auto& pair : m_deptCheckBoxes) {
        if (pair.second->isChecked()) {
            selected.append(pair.first);
        }
    }

    if (selected.isEmpty()) {
        m_deptFilterBtn->setText(QStringLiteral("部门筛选"));
    } else if (selected.size() <= 2) {
        m_deptFilterBtn->setText(selected.join(", "));
    } else {
        m_deptFilterBtn->setText(QStringLiteral("%1个部门").arg(selected.size()));
    }

    m_deptPopup->hide();
    loadUsers();  // 重新加载数据
}

/** 禁用/启用用户 */
void UserManagementPage::onToggleUserStatus(int userId) {
  // 通过UserController替代直接调用db/UserDAO
    UserController ctrl;
    User user = ctrl.getUserById(userId);
    if (user.userId == 0) return;

    QString currentStatus = user.status;
    QString newStatus = (currentStatus == SC::USER_ACTIVE) ? SC::USER_DISABLED : SC::USER_ACTIVE;
    QString actionText = (newStatus == SC::USER_DISABLED) ? "禁用" : "启用";

    if (!MessageDialog::showQuestion(this, QStringLiteral("确认操作"),
        QStringLiteral("确定要%1用户「%2」吗？").arg(actionText).arg(user.realName))) return;

    // 调用UserController的setUserStatus
    bool ok = ctrl.setUserStatus(userId, newStatus);

    if (ok) {
        MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("操作成功"));
        loadUsers();
    } else {
        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("操作失败"));
    }
}

namespace {

// 每次录入需要采集的帧数（对应5个方位）
const int MAX_CAPTURES = 5;

// 方位检测服务预检参数：face-server首次加载模型约8秒，预检窗口取20秒
const int POSTURE_PRECHECK_MAX_ATTEMPTS = 40;
const int POSTURE_PRECHECK_INTERVAL_MS = 500;

// 5个方位的目标参数
struct PostureTarget {
    QString name, instruction;
    double yawMin, yawMax, pitchMin, pitchMax;
};
// 方位目标参数（yaw/pitch方向与face-server.js /posture接口一致）
// yaw: 正值=脸偏右(用户左转露右脸)，负值=脸偏左(用户右转露左脸)
// pitch: 正值=低头，负值=抬头
// 归一化基准为人脸框宽高，阈值经实测校准
const PostureTarget POSTURE_TARGETS[5] = {
    { QStringLiteral("居中"), QStringLiteral("请面向摄像头，保持正脸"),          -0.15, 0.15, -0.15, 0.15 },
    { QStringLiteral("左侧"), QStringLiteral("请将头部向右转，露出左侧面部"),   -0.50,-0.15, -0.30, 0.30 },
    { QStringLiteral("右侧"), QStringLiteral("请将头部向左转，露出右侧面部"),    0.15, 0.50, -0.30, 0.30 },
    { QStringLiteral("上偏"), QStringLiteral("请略微抬头，露出面部上方"),       -0.30, 0.30, -0.50,-0.15 },
    { QStringLiteral("下偏"), QStringLiteral("请略微低头，露出面部下方"),       -0.30, 0.30,  0.15, 0.50 },
};

}  // namespace

/** 人脸录入会话上下文：聚合对话框控件与5方位采集状态机全部状态 */
struct FaceEnrollCtx {
    // 对话框与控件
    BaseDialog* dlg = nullptr;
    FaceCameraWidget* camera = nullptr;
    QLabel* directionLabel = nullptr;
    QLabel* instructionLabel = nullptr;
    QLabel* statusLabel = nullptr;
    QPushButton* startBtn = nullptr;
    QPushButton* confirmBtn = nullptr;
    QPushButton* cancelBtn = nullptr;
    QTimer* postureTimer = nullptr;   // 方位检测定时器
    QTimer* simpleTimer = nullptr;    // 简易模式定时器
    QNetworkAccessManager* postureNam = nullptr;  // 方位检测HTTP客户端
    // 会话参数
    int userId = 0;
    // 采集状态机状态
    int captureCount = 0;
    QString bestDescriptor;
    double bestConfidence = 0.0;
    bool isCapturing = false;
    bool simpleMode = false;
    int targetIdx = 0;
    int postureMatchCount = 0;
    qint64 postureFirstMatchTime = 0;
    qint64 postureStartTime = 0;
    int postureFailCount = 0;
    bool postureRequestPending = false;  // 是否有HTTP请求在飞行中
    int postureHttpFailCount = 0;        // HTTP连续失败计数
    // 工具函数（connectFaceEnrollFlow中初始化，供状态机方法调用）
    std::function<void()> stopTimers;
    std::function<void()> doCapture;
};

/** 人脸录入 — 统一BaseDialog圆角风格 */
void UserManagementPage::onFaceEnroll(int userId) {
    UserController ctrl;
    User user = ctrl.getUserById(userId);
    if (user.userId == 0) return;

    auto* dlg = new BaseDialog(this, 520);
    dlg->setDialogTitle(QStringLiteral("人脸信息录入"));
    dlg->setMinimumHeight(580);

    FaceEnrollCtx ctx;
    ctx.dlg = dlg;
    ctx.userId = userId;
    // 方位检测定时器（本地估算0延迟，100ms高频检测）
    ctx.postureTimer = new QTimer(dlg);
    ctx.postureTimer->setInterval(100);
    // 简单模式定时器
    ctx.simpleTimer = new QTimer(dlg);
    ctx.simpleTimer->setSingleShot(true);
    ctx.postureNam = new QNetworkAccessManager(dlg);

    auto* cl = dlg->contentLayout();
    cl->setSpacing(12);
    buildFaceEnrollHeader(user, cl);
    buildFaceEnrollBody(ctx, cl);
    buildFaceEnrollButtons(ctx);

    connectFaceEnrollFlow(ctx);

    dlg->exec();
    dlg->deleteLater();
}

/** 构建录入对话框头部：说明文字与用户信息行 */
void UserManagementPage::buildFaceEnrollHeader(const User& user, QVBoxLayout* cl) {
    // 说明文字
    auto* descLabel = new QLabel(QStringLiteral("请面向摄像头，保持正脸清晰可见"));
    descLabel->setStyleSheet("font-size:14px;color:#999;background:transparent;");
    cl->addWidget(descLabel);

    // 用户信息行
    auto* infoRow = new QHBoxLayout();
    infoRow->setSpacing(12);
    auto* avatar = new QLabel(user.realName.isEmpty() ? "?" : user.realName.left(1));
    avatar->setFixedSize(44, 44);
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setStyleSheet(
        "background:#4da3ff;color:white;border-radius:22px;font-size:20px;font-weight:700;");
    auto* nameCol = new QVBoxLayout();
    nameCol->setSpacing(2);
    auto* nameLbl = new QLabel(user.realName);
    nameLbl->setStyleSheet("font-size:16px;font-weight:700;color:#333;background:transparent;");
    auto* metaLbl = new QLabel(QString("%1  %2").arg(user.workNo, user.department));
    metaLbl->setStyleSheet("font-size:13px;color:#999;background:transparent;");
    nameCol->addWidget(nameLbl);
    nameCol->addWidget(metaLbl);
    infoRow->addWidget(avatar);
    infoRow->addLayout(nameCol);
    infoRow->addStretch();
    cl->addLayout(infoRow);
}

/** 构建录入对话框主体：摄像头、方位提示、指令提示与状态提示 */
void UserManagementPage::buildFaceEnrollBody(FaceEnrollCtx& ctx, QVBoxLayout* cl) {
    // 摄像头组件：降低阈值+减少稳定帧数，支持偏侧脸自动采集
    auto* camera = new FaceCameraWidget();
    camera->setMinimumSize(280, 260);
    camera->setMaximumSize(360, 300);
    camera->setAutoCapture(false);
    camera->setMinConfidence(0.60);  // 降低阈值，偏侧脸也能检测
    camera->setStableFrames(6);  // 减少稳定帧数，自动采集更快
    camera->setDetectInterval(80);  // 加快检测频率
    cl->addWidget(camera, 0, Qt::AlignCenter);
    ctx.camera = camera;

    // 方位大字提示标签：36px醒目显示，实时方位反馈
    auto* directionLabel = new QLabel(QStringLiteral("选择开始录入"));
    directionLabel->setAlignment(Qt::AlignCenter);
    directionLabel->setMinimumHeight(StyleHelper::Token::ControlHeightLarge);
    directionLabel->setStyleSheet(
        "font-size:32px; font-weight:900; color:#ffffff; "
        "background:#52c41a; border-radius:14px; padding:10px 20px;");
    cl->addWidget(directionLabel);
    ctx.directionLabel = directionLabel;

    // 指令提示
    auto* instructionLabel = new QLabel(QStringLiteral("点击「开始录入」启动摄像头"));
    instructionLabel->setAlignment(Qt::AlignCenter);
    instructionLabel->setWordWrap(true);
    instructionLabel->setStyleSheet("font-size:15px;color:#666;padding:4px 0;background:transparent;");
    cl->addWidget(instructionLabel);
    ctx.instructionLabel = instructionLabel;

    // 状态提示
    auto* statusLabel = new QLabel("");
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setStyleSheet("font-size:14px;color:#4da3ff;background:transparent;");
    cl->addWidget(statusLabel);
    ctx.statusLabel = statusLabel;
}

/** 构建录入对话框按钮区：开始录入/确认保存/取消 */
void UserManagementPage::buildFaceEnrollButtons(FaceEnrollCtx& ctx) {
    // 按钮区（通过 BaseDialog::buttonLayout()）
    auto* bl = ctx.dlg->buttonLayout();
    while (bl->count() > 0) {
        QLayoutItem* item = bl->takeAt(0);
        delete item;
    }

    auto* startBtn = new QPushButton(QStringLiteral("开始人脸录入"));
    startBtn->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
    startBtn->setCursor(Qt::PointingHandCursor);
    startBtn->setStyleSheet(
        "QPushButton{background:#4da3ff;color:white;border:none;border-radius:12px;"
        "font-size:15px;font-weight:700;padding:0 28px;}"
        "QPushButton:hover{background:#3d8ae0;}");

    auto* confirmBtn = new QPushButton(QStringLiteral("确认保存"));
    confirmBtn->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
    confirmBtn->setVisible(false);
    confirmBtn->setCursor(Qt::PointingHandCursor);
    confirmBtn->setStyleSheet(
        "QPushButton{background:#27ae60;color:white;border:none;border-radius:12px;"
        "font-size:15px;font-weight:700;padding:0 28px;}"
        "QPushButton:hover{background:#219a52;}");

    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    cancelBtn->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setStyleSheet(
        "QPushButton{background:#f5f5f5;color:#666;border:1px solid #ddd;border-radius:12px;"
        "font-size:15px;font-weight:600;padding:0 24px;}"
        "QPushButton:hover{background:#e8e8e8;}");
    connect(cancelBtn, &QPushButton::clicked, ctx.dlg, &QDialog::reject);

    bl->addWidget(startBtn);
    bl->addWidget(confirmBtn);
    bl->addWidget(cancelBtn);
    bl->addStretch();

    ctx.startBtn = startBtn;
    ctx.confirmBtn = confirmBtn;
    ctx.cancelBtn = cancelBtn;
}

/** 连接人脸录入状态机全部信号（5方位检测引导采集流程） */
void UserManagementPage::connectFaceEnrollFlow(FaceEnrollCtx& ctx) {
    auto* dlg = ctx.dlg;
    auto* camera = ctx.camera;

    // 关闭所有定时器
    ctx.stopTimers = [&ctx]() {
        ctx.postureTimer->stop();
        ctx.simpleTimer->stop();
    };

    // 执行采集
    ctx.doCapture = [&ctx]() {
        ctx.instructionLabel->setText(QStringLiteral("正在采集..."));
        ctx.instructionLabel->setStyleSheet(
            "font-size:15px;font-weight:bold;color:#4da3ff;padding:4px 0;background:transparent;");
        ctx.camera->captureNow();
    };

    // 距离过远提示：采集前引导靠近，避免因特征像素不足导致采集质量差
    connect(ctx.camera, &FaceCameraWidget::faceTooFarChanged, dlg, [this, &ctx](bool tooFar) {
        if (!tooFar || !ctx.isCapturing) return;
        ctx.instructionLabel->setText(QStringLiteral("请靠近"));
        ctx.instructionLabel->setStyleSheet(
            "font-size:15px;font-weight:bold;color:#fa8c16;padding:4px 0;background:transparent;");
    });

    // 方位检测回调 — 异步HTTP模式
    // 定时器触发异步POST /posture，HTTP响应回调中做方位匹配，不阻塞UI
    connect(ctx.postureTimer, &QTimer::timeout, dlg, [this, &ctx]() {
        onFacePostureTick(ctx);
    });

    // 简易模式定时器触发采集
    connect(ctx.simpleTimer, &QTimer::timeout, dlg, [&ctx]() {
        if (!ctx.isCapturing) return;
        if (!ctx.camera->faceRect().isNull()) {
            ctx.doCapture();
        } else {
            ctx.instructionLabel->setText(QStringLiteral("🔍 未检测到人脸，请对准摄像头..."));
            ctx.instructionLabel->setStyleSheet(
                "font-size:15px;color:#4da3ff;padding:4px 0;background:transparent;");
            ctx.simpleTimer->start(1000);
        }
    });

  // 开始录入按钮 — 启动方位检测流程
    connect(ctx.startBtn, &QPushButton::clicked, camera, [this, &ctx]() {
        ctx.startBtn->setVisible(false);
        ctx.cancelBtn->setVisible(true);
        ctx.captureCount = 0;
        ctx.bestConfidence = 0.0;
        ctx.bestDescriptor.clear();
        ctx.isCapturing = true;
        ctx.simpleMode = false;
        ctx.targetIdx = 0;
        ctx.postureMatchCount = 0;
        ctx.postureFirstMatchTime = 0;
        ctx.postureFailCount = 0;
        ctx.postureRequestPending = false;
        ctx.postureHttpFailCount = 0;

        ctx.instructionLabel->setText(QStringLiteral("正在检测方位识别服务..."));
        ctx.instructionLabel->setStyleSheet(
            "font-size:15px;font-weight:bold;color:#4da3ff;padding:4px 0;background:transparent;");
        ctx.directionLabel->setText(QStringLiteral("初始化"));
        ctx.directionLabel->setStyleSheet(
            "font-size:32px; font-weight:900; color:#ffffff; "
            "background:#52c41a; border-radius:14px; padding:10px 20px;");

        ctx.camera->startCamera();

        // 800ms后预检/posture（等摄像头和face-server就绪）
        // ⚠ 不可动：预检用 QTimer+property 存储状态，改成 std::function 递归会触发 MSVC 栈崩溃
        // ⚠ 不可动：预检窗口=40×500ms=20s，服务冷启动实测5s，调小会误判"服务未连接"退简易模式
        QTimer::singleShot(800, ctx.dlg, [&ctx]() {
            if (!ctx.isCapturing) return;

            QTimer* retryTimer = new QTimer(ctx.dlg);
            retryTimer->setSingleShot(true);
            retryTimer->setInterval(POSTURE_PRECHECK_INTERVAL_MS);
            retryTimer->setProperty("retryLeft", POSTURE_PRECHECK_MAX_ATTEMPTS);

            connect(retryTimer, &QTimer::timeout, ctx.dlg, [&ctx, retryTimer]() {
                if (!ctx.isCapturing) { retryTimer->deleteLater(); return; }

                int retryLeft = retryTimer->property("retryLeft").toInt();
                QImage frame = ctx.camera->currentFrame();
                DeepFaceExtractor ext;
                double y, p; QString e;
                if (!frame.isNull() && ext.detectPosture(frame, y, p, e)) {
                    ctx.simpleMode = false;
                    const auto& t = POSTURE_TARGETS[0];
                    ctx.directionLabel->setText(QStringLiteral("【 %1 】").arg(t.name));
                    ctx.directionLabel->setStyleSheet(
                        "font-size:32px; font-weight:900; color:#ffffff; "
                        "background:#52c41a; border-radius:14px; padding:10px 20px;");
                    ctx.instructionLabel->setText(QStringLiteral("📸 第 1/5 帧 — 方位检测已就绪\n%1").arg(t.instruction));
                    ctx.instructionLabel->setStyleSheet(
                        "font-size:15px;font-weight:bold;color:#fa8c16;padding:4px 0;background:transparent;");
                    ctx.postureStartTime = QDateTime::currentMSecsSinceEpoch();
                    ctx.postureTimer->start();
                    retryTimer->deleteLater();
                } else if (retryLeft > 0) {
                    retryTimer->setProperty("retryLeft", retryLeft - 1);
                    ctx.instructionLabel->setText(QStringLiteral("正在等待人脸识别服务启动... (%1)").arg(retryLeft - 1));
                    retryTimer->start(POSTURE_PRECHECK_INTERVAL_MS);
                } else {
                    ctx.simpleMode = true;
                    const auto& t = POSTURE_TARGETS[0];
                    ctx.directionLabel->setText(QStringLiteral("【 %1 】(简易)").arg(t.name));
                    ctx.directionLabel->setStyleSheet(
                        "font-size:32px; font-weight:900; color:#ffffff; "
                        "background:#fa8c16; border-radius:14px; padding:10px 20px;");
                    ctx.instructionLabel->setText(QStringLiteral("⚠ 方位检测服务未连接\n简易模式：3秒后自动采集\n%1").arg(t.instruction));
                    ctx.instructionLabel->setStyleSheet(
                        "font-size:15px;font-weight:bold;color:#fa8c16;padding:4px 0;background:transparent;");
                    ctx.simpleTimer->start(3000);
                    retryTimer->deleteLater();
                }
            });

            retryTimer->start(0);
        });
    });

  // 采集结果回调 — 进入下一个方位或完成
    connect(camera, &FaceCameraWidget::captureReady, camera,
            [this, &ctx](const QImage&, double confidence) {
        onFaceCaptureReady(ctx, confidence);
    });

    connect(ctx.confirmBtn, &QPushButton::clicked, camera, [this, &ctx]() {
        onFaceConfirmSave(ctx);
    });

    connect(dlg, &QDialog::finished, camera, [&ctx]() { ctx.camera->stopCamera(); });
}

/** 方位检测定时器回调 — 异步POST /posture，HTTP响应交给onFacePostureReply处理 */
void UserManagementPage::onFacePostureTick(FaceEnrollCtx& ctx) {
    if (!ctx.isCapturing) { ctx.postureTimer->stop(); return; }
    if (ctx.simpleMode) { ctx.postureTimer->stop(); return; }
    // 上一个请求还在飞行中，跳过本次（避免请求堆积）
    if (ctx.postureRequestPending) return;

    qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - ctx.postureStartTime;
    if (elapsed > 30000) {
        ctx.instructionLabel->setText(QStringLiteral("⚠ 方位超时，将采集当前帧作为兜底"));
        ctx.instructionLabel->setStyleSheet(
            "font-size:15px;font-weight:bold;color:#ff4d4f;padding:4px 0;background:transparent;");
        ctx.postureTimer->stop();
        QTimer::singleShot(100, ctx.dlg, [&ctx]() { ctx.doCapture(); });
        return;
    }

    QImage frame = ctx.camera->currentFrame();
    QRect faceRect = ctx.camera->faceRect();
    if (frame.isNull() || faceRect.isNull()) {
        ctx.postureMatchCount = 0;
        ctx.postureFirstMatchTime = 0;
        ctx.instructionLabel->setText(QStringLiteral("🔍 未检测到人脸，请对准摄像头..."));
        ctx.instructionLabel->setStyleSheet(
            "font-size:15px;color:#4da3ff;padding:4px 0;background:transparent;");
        return;
    }

    // 发起异步HTTP请求到 face-server.js /posture
    ctx.postureRequestPending = true;
    QByteArray base64Data = DeepFaceExtractor::imageToBase64Jpeg(frame);
    QJsonObject bodyObj;
    bodyObj["image"] = QString::fromUtf8(base64Data);
    QByteArray body = QJsonDocument(bodyObj).toJson(QJsonDocument::Compact);

    QNetworkRequest req;
    req.setUrl(QUrl("http://127.0.0.1:8089/posture"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(1500);
    QNetworkReply* reply = ctx.postureNam->post(req, body);

    QObject::connect(reply, &QNetworkReply::finished, ctx.dlg,
        [this, &ctx, reply]() { onFacePostureReply(ctx, reply); });
}

/** 方位检测HTTP响应处理 — 方位匹配判断与界面引导 */
void UserManagementPage::onFacePostureReply(FaceEnrollCtx& ctx, QNetworkReply* reply) {
    ctx.postureRequestPending = false;
    reply->deleteLater();

    if (!ctx.isCapturing || ctx.simpleMode) return;

    // 解析HTTP响应
    double yaw = 0, pitch = 0;
    bool postureOk = false;
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray respData = reply->readAll();
        QJsonParseError parseErr;
        QJsonDocument doc = QJsonDocument::fromJson(respData, &parseErr);
        if (parseErr.error == QJsonParseError::NoError) {
            QJsonObject obj = doc.object();
            if (obj["success"].toBool(false)) {
                yaw = obj["yaw"].toDouble(0);
                pitch = obj["pitch"].toDouble(0);
                postureOk = true;
            }
        }
    }

    if (!postureOk) {
        ctx.postureHttpFailCount++;
        // 不因HTTP失败切简易模式，本地估算兜底继续工作
        // 只有连续15次失败才切简易模式（服务真的挂了）
        if (ctx.postureHttpFailCount >= 15) {
            ctx.instructionLabel->setText(QStringLiteral("⚠ 方位识别服务异常，切换简易模式..."));
            ctx.instructionLabel->setStyleSheet(
                "font-size:15px;color:#fa8c16;padding:4px 0;background:transparent;");
            ctx.simpleMode = true;
            ctx.postureTimer->stop();
            ctx.simpleTimer->start(1000);
            return;
        }
        // HTTP失败，用本地估算兜底
        QRect fr = ctx.camera->faceRect();
        ctx.camera->estimatePosture(fr, yaw, pitch);
    } else {
        ctx.postureHttpFailCount = 0;
        ctx.postureFailCount = 0;
    }

    // 方位匹配判断
    const PostureTarget& target = POSTURE_TARGETS[ctx.targetIdx];
    bool matched = (yaw >= target.yawMin && yaw <= target.yawMax &&
                    pitch >= target.pitchMin && pitch <= target.pitchMax);

    if (matched) {
        ctx.postureMatchCount++;
        if (ctx.postureFirstMatchTime == 0)
            ctx.postureFirstMatchTime = QDateTime::currentMSecsSinceEpoch();
        qint64 stayMs = QDateTime::currentMSecsSinceEpoch() - ctx.postureFirstMatchTime;
        double remainSec = qMax(0.0, (500.0 - stayMs) / 1000.0);

        ctx.directionLabel->setText(QStringLiteral("【 %1 】 %2s").arg(target.name).arg(remainSec, 0, 'f', 1));
        ctx.directionLabel->setStyleSheet(
            "font-size:32px; font-weight:900; color:#ffffff; "
            "background:#52c41a; border-radius:14px; padding:10px 20px;");
        ctx.instructionLabel->setText(QStringLiteral("✅ 方位匹配 %1/2帧  (yaw=%2 pitch=%3)\n还需保持%4秒...")
            .arg(ctx.postureMatchCount)
            .arg(yaw, 0, 'f', 2).arg(pitch, 0, 'f', 2).arg(remainSec, 0, 'f', 1));
        ctx.instructionLabel->setStyleSheet(
            "font-size:15px;font-weight:bold;color:#389e0d;padding:4px 0;background:transparent;");

        if (ctx.postureMatchCount >= 2 && stayMs >= 500) {
            ctx.postureTimer->stop();
            ctx.directionLabel->setText(QStringLiteral("【 %1 】✓").arg(target.name));
            ctx.doCapture();
        }
    } else {
        ctx.postureMatchCount = 0;
        ctx.postureFirstMatchTime = 0;
        QString hint;
        if (yaw > 0.15) hint = QStringLiteral("当前：右转(露右脸)");
        else if (yaw < -0.15) hint = QStringLiteral("当前：左转(露左脸)");
        else if (pitch > 0.15) hint = QStringLiteral("当前：低头");
        else if (pitch < -0.15) hint = QStringLiteral("当前：抬头");
        else hint = QStringLiteral("当前：居中");
        ctx.directionLabel->setText(QStringLiteral("【 %1 】").arg(target.name));
        ctx.directionLabel->setStyleSheet(
            "font-size:32px; font-weight:900; color:#ffffff; "
            "background:#4da3ff; border-radius:14px; padding:10px 20px;");
        ctx.instructionLabel->setText(QStringLiteral("%1\n请调整到【%2】方位\n%3")
            .arg(hint).arg(target.name).arg(target.instruction));
        ctx.instructionLabel->setStyleSheet(
            "font-size:15px;font-weight:bold;color:#fa8c16;padding:4px 0;background:transparent;");
    }
}

/** 采集结果回调 — 记录最佳特征并进入下一个方位或完成 */
void UserManagementPage::onFaceCaptureReady(FaceEnrollCtx& ctx, double confidence) {
    if (!ctx.isCapturing) return;
    QString descriptor = ctx.camera->getLastDescriptor();
    if (descriptor.isEmpty()) {
        ctx.instructionLabel->setText(QStringLiteral("⚠ 特征提取失败，稍后自动重试..."));
        ctx.instructionLabel->setStyleSheet(
            "font-size:15px;color:#fa8c16;padding:4px 0;background:transparent;");
        ctx.camera->reset();
        if (ctx.simpleMode) {
            ctx.simpleTimer->start(1500);
        } else {
            ctx.postureStartTime = QDateTime::currentMSecsSinceEpoch();
            ctx.postureMatchCount = 0;
            ctx.postureFirstMatchTime = 0;
            ctx.postureFailCount = 0;
            ctx.postureRequestPending = false;
            ctx.postureHttpFailCount = 0;
            ctx.postureTimer->start();
        }
        return;
    }
    ctx.captureCount++;
    int dimCount = descriptor.split(",").size();
    double score = confidence * 0.7 + (dimCount >= 128 ? 0.3 : 0.1);
    if (score > ctx.bestConfidence) { ctx.bestConfidence = score; ctx.bestDescriptor = descriptor; }

    if (ctx.captureCount < MAX_CAPTURES) {
        // 进入下一个方位
        ctx.targetIdx = ctx.captureCount;
        const auto& next = POSTURE_TARGETS[ctx.targetIdx];
        ctx.camera->reset();
        if (ctx.simpleMode) {
            ctx.directionLabel->setText(QStringLiteral("【 %1 】(简易)").arg(next.name));
            ctx.directionLabel->setStyleSheet(
                "font-size:32px; font-weight:900; color:#ffffff; "
                "background:#fa8c16; border-radius:14px; padding:10px 20px;");
            ctx.instructionLabel->setText(QStringLiteral("✅ 第 %1/5 帧采集成功\n下一个【%2】— 3秒后自动采集\n%3")
                .arg(ctx.captureCount).arg(next.name).arg(next.instruction));
            ctx.instructionLabel->setStyleSheet(
                "font-size:15px;color:#389e0d;padding:4px 0;background:transparent;");
            ctx.simpleTimer->start(3000);
        } else {
            ctx.directionLabel->setText(QStringLiteral("【 %1 】").arg(next.name));
            ctx.directionLabel->setStyleSheet(
                "font-size:32px; font-weight:900; color:#ffffff; "
                "background:#52c41a; border-radius:14px; padding:10px 20px;");
            ctx.instructionLabel->setText(QStringLiteral("✅ 第 %1/5 帧采集成功\n请准备【%2】\n%3")
                .arg(ctx.captureCount).arg(next.name).arg(next.instruction));
            ctx.instructionLabel->setStyleSheet(
                "font-size:15px;color:#389e0d;padding:4px 0;background:transparent;");
            ctx.postureStartTime = QDateTime::currentMSecsSinceEpoch();
            ctx.postureMatchCount = 0;
            ctx.postureFirstMatchTime = 0;
            ctx.postureFailCount = 0;
            ctx.postureRequestPending = false;
            ctx.postureHttpFailCount = 0;
            ctx.postureTimer->start();
        }
    } else {
        // 采集完毕
        ctx.isCapturing = false;
        ctx.stopTimers();
        ctx.directionLabel->setText(QStringLiteral("5方位采集完成"));
        ctx.directionLabel->setStyleSheet(
            "font-size:32px; font-weight:900; color:#ffffff; "
            "background:#52c41a; border-radius:14px; padding:10px 20px;");
        ctx.confirmBtn->setVisible(true);
        ctx.instructionLabel->setText(QStringLiteral("🎉 采集完成！最佳置信度: %1%\n请点击「确认保存」")
            .arg(QString::number(ctx.bestConfidence * 100, 'f', 1)));
        ctx.instructionLabel->setStyleSheet(
            "font-size:15px;font-weight:600;color:#389e0d;padding:4px 0;background:transparent;");
    }
}

/** 确认保存 — 将最佳人脸特征写入数据库 */
void UserManagementPage::onFaceConfirmSave(FaceEnrollCtx& ctx) {
    if (ctx.bestDescriptor.isEmpty() || ctx.userId <= 0) return;
    ctx.confirmBtn->setEnabled(false);
    ctx.isCapturing = false;
    ctx.instructionLabel->setText(QStringLiteral("正在保存人脸特征..."));
    ctx.instructionLabel->setStyleSheet("font-size:15px;color:#4da3ff;padding:4px 0;background:transparent;");
    FaceRecognitionService svc;
    if (svc.enrollFace(ctx.userId, ctx.bestDescriptor)) {
        ctx.statusLabel->setText(QStringLiteral("人脸特征采集成功！"));
        ctx.statusLabel->setStyleSheet("font-size:15px;font-weight:700;color:#389e0d;background:transparent;");
        ctx.camera->stopCamera();
        refresh();
        QTimer::singleShot(800, ctx.dlg, &QDialog::accept);
    } else {
        ctx.statusLabel->setText(QStringLiteral("人脸录入失败，请重试"));
        ctx.statusLabel->setStyleSheet("font-size:15px;font-weight:700;color:#ff4d4f;background:transparent;");
        ctx.confirmBtn->setEnabled(true);
    }
}

/** 已录入用户点击 → 弹出查看/清除人脸对话框 — 统一BaseDialog圆角风格 */
void UserManagementPage::onFaceEnrolledClick(int userId, const QString& realName, const QString& workNo) {
    UserController ctrl;
    User user = ctrl.getUserById(userId);
    if (user.userId == 0) return;

    auto* dlg = new BaseDialog(this, 440);
    dlg->setDialogTitle(QStringLiteral("人脸信息"));
    dlg->setMinimumHeight(360);

    auto* cl = dlg->contentLayout();
    cl->setSpacing(16);

    // 用户信息行（头像 + 姓名/工号）
    auto* infoRow = new QHBoxLayout();
    infoRow->setSpacing(14);

    auto* avatar = new QLabel(realName.isEmpty() ? "?" : realName.left(1));
    avatar->setFixedSize(56, 56);
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setStyleSheet(
        "background:#4da3ff;color:white;border-radius:28px;font-size:24px;font-weight:700;");

    auto* nameCol = new QVBoxLayout();
    nameCol->setSpacing(4);
    auto* nameLbl = new QLabel(realName);
    nameLbl->setStyleSheet("font-size:18px;font-weight:700;color:#333;background:transparent;");
    auto* metaLbl = new QLabel(QString("%1  %2").arg(workNo, user.department));
    metaLbl->setStyleSheet("font-size:14px;color:#999;background:transparent;");
    nameCol->addWidget(nameLbl);
    nameCol->addWidget(metaLbl);

    infoRow->addWidget(avatar);
    infoRow->addLayout(nameCol);
    infoRow->addStretch();
    cl->addLayout(infoRow);

    // 人脸状态区
    auto* statusBox = new QFrame();
    statusBox->setStyleSheet(
        "QFrame{background:#f6ffed;border:none;border-radius:12px;}");
    auto* statusLayout = new QVBoxLayout(statusBox);
    statusLayout->setContentsMargins(24, 20, 24, 20);
    statusLayout->setSpacing(8);
    statusLayout->setAlignment(Qt::AlignCenter);

    auto* statusIcon = new QLabel(QStringLiteral("✓"));
    statusIcon->setAlignment(Qt::AlignCenter);
    statusIcon->setStyleSheet("font-size:36px;color:#52c41a;font-weight:bold;background:transparent;");
    auto* statusText = new QLabel(QStringLiteral("人脸特征已录入"));
    statusText->setAlignment(Qt::AlignCenter);
    statusText->setStyleSheet("font-size:16px;font-weight:600;color:#389e0d;background:transparent;");
    auto* statusHint = new QLabel(QStringLiteral("该用户可使用人脸识别登录智能柜系统"));
    statusHint->setAlignment(Qt::AlignCenter);
    statusHint->setStyleSheet("font-size:13px;color:#999;background:transparent;");

    statusLayout->addWidget(statusIcon);
    statusLayout->addWidget(statusText);
    statusLayout->addWidget(statusHint);
    cl->addWidget(statusBox);

    // 按钮区（通过 BaseDialog::buttonLayout()）
    auto* bl = dlg->buttonLayout();
    while (bl->count() > 0) {
        QLayoutItem* item = bl->takeAt(0);
        delete item;
    }
    bl->addStretch();

    auto* clearBtn = new QPushButton(QStringLiteral("清除人脸信息"));
    clearBtn->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
    clearBtn->setMinimumWidth(140);
    clearBtn->setCursor(Qt::PointingHandCursor);
    clearBtn->setStyleSheet(
        "QPushButton{background:#ff4d4f;color:white;border:none;border-radius:12px;"
        "font-size:15px;font-weight:600;}"
        "QPushButton:hover{background:#e04343;}");

    auto* closeBtn = new QPushButton(QStringLiteral("关闭"));
    closeBtn->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
    closeBtn->setMinimumWidth(100);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QPushButton{background:#f5f5f5;color:#666;border:1px solid #ddd;border-radius:12px;"
        "font-size:15px;font-weight:600;}"
        "QPushButton:hover{background:#e8e8e8;}");
    connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::reject);

    bl->addWidget(clearBtn);
    bl->addWidget(closeBtn);

    // 清除逻辑
    connect(clearBtn, &QPushButton::clicked, dlg, [dlg, userId, this]() {
        if (MessageDialog::showQuestion(dlg, QStringLiteral("确认清除"),
            QStringLiteral("确定要清除该用户的人脸信息吗？\n清除后用户将无法使用人脸识别登录。"))) {
            FaceRecognitionService svc;
            if (svc.deleteFace(userId)) {
                MessageDialog::showSuccess(dlg, QStringLiteral("成功"), QStringLiteral("人脸信息已清除"));
                dlg->accept();
                refresh();
            } else {
                MessageDialog::showError(dlg, QStringLiteral("失败"), QStringLiteral("清除失败，请重试"));
            }
        }
    });

    dlg->exec();
    dlg->deleteLater();
}

// 角色按钮组样式更新 — 44px高统一弹窗内按钮风格
void UserManagementPage::updateRoleBtnStyles() {
    QPushButton* btns[2] = { m_dlgRoleBtn1, m_dlgRoleBtn2 };
    for (int i = 0; i < 2; ++i) {
        if (!btns[i]) continue;
        bool sel = (i == m_dlgRoleMode);
        btns[i]->setChecked(sel);
        if (sel) {
            btns[i]->setStyleSheet(
                "QPushButton { background: #4da3ff;"
                "  color: white; border: none; border-radius: 10px;"
                "  padding: 8px 16px; font-size: 14px; font-weight: 600; min-height: 40px; }"
                "QPushButton:hover { background: #3d8ae0; }"
                "QPushButton:pressed { background: #2e7bd6; }"
            );
        } else {
            btns[i]->setStyleSheet(
                "QPushButton { background: white; color: #666666; border: 1px solid #e0e0e0;"
                "  border-radius: 10px; padding: 8px 16px; font-size: 14px; font-weight: 600; min-height: 40px; }"
                "QPushButton:hover { border-color: #4da3ff; color: #4da3ff; background: #f0f7ff; }"
                "QPushButton:pressed { background: #e6f0ff; }"
            );
        }
    }
}
