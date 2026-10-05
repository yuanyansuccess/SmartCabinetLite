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


// ==============================================================
// 【① 界面构建与用户列表加载
//   工具栏、部门筛选、用户表格与分页
// ==============================================================
/**
 * @brief 构建页面界面：读取 .ui 静态布局并补充动态控件
 */
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
    m_deptPopup->setStyleSheet(StyleHelper::popupCheckList());
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

/**
 * @brief 刷新页面数据与统计显示
 */
void UserManagementPage::refresh() {
    loadDepartments();  // 每次刷新重新加载部门列表(对齐Web版onMounted)
    loadUsers();
}


// ==============================================================
// 【② 查询与筛选（搜索/重置/部门多选）
//   搜索条件与部门筛选交互
// ==============================================================
/**
 * @brief 处理搜索
 */
void UserManagementPage::onSearch() {
    m_currentPage = 1;  // 筛选查询必须重置到第1页
    loadUsers();
}

/**
 * @brief 处理重置
 */
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
    m_deptPopup->setStyleSheet(StyleHelper::popupCheckList());

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

/**
 * @brief 加载用户
 */
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


// ==============================================================
// 【③ 分页
//   上一页/下一页
// ==============================================================
/**
 * @brief 处理页面
 */
void UserManagementPage::onPrevPage() {
    if (m_currentPage > 1) {
        m_currentPage--;
        loadUsers();
    }
}

/**
 * @brief 处理页面
 */
void UserManagementPage::onNextPage() {
    int totalPages = (m_totalRecords + m_pageSize - 1) / m_pageSize;
    if (m_currentPage < totalPages) {
        m_currentPage++;
        loadUsers();
    }
}


// ==============================================================
// 【④ 用户增删改（新增/编辑/删除/提交）
//   表单校验、口令设置、重复工号校验
// 【⑤ 输入控件事件与部门筛选交互
//   软键盘弹起、清空/确认部门
// ==============================================================
/**
 * @brief 处理搜索输入框点击事件
 */
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

/**
         * 事件过滤器：拦截控件与窗口事件并转交专用处理
         * @param obj 事件来源控件
         * @param event 事件对象
         * @return true=事件已被处理
         */
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

// ==============================================================
// 【⑥ 启用/停用切换
//   状态互斥与二次确认
// ==============================================================
/**
 * @brief 处理状态
 */
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

// 人脸录入相关（FaceEnrollCtx 会话结构、5方位检测常量 POSTURE_*）已随人脸流程
// 迁至 UserManagementPageFaceEnroll.cpp（定义须与使用它的方法同文件）

// ==============================================================

// 角色按钮组样式更新 — 44px高统一弹窗内按钮风格
void UserManagementPage::updateRoleBtnStyles() {
    QPushButton* btns[2] = { m_dlgRoleBtn1, m_dlgRoleBtn2 };
    for (int i = 0; i < 2; ++i) {
        if (!btns[i]) continue;
        bool sel = (i == m_dlgRoleMode);
        btns[i]->setChecked(sel);
        if (sel) {
            btns[i]->setStyleSheet(StyleHelper::buttonGroupPrimary());
        } else {
            btns[i]->setStyleSheet(StyleHelper::buttonGroupDefault());
        }
    }
}
