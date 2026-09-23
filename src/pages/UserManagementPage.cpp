/**
 * @file UserManagementPage.cpp
 * @brief 人员管理页面实现 - 1:1复刻BS端UserManagement.vue
 * @author 袁燕
 * @修改说明 V1.00.9 2026-06-15 完全重构以匹配BS端
 *   - 表格列：工号/姓名/部门/角色/联系电话/人脸录入/创建时间/状态/操作
 *   - 添加部门多选下拉
 *   - 添加分页组件
 *   - 触屏优化：按钮最小48px，字体16px+
 * @修改说明 V6.8 2026-06-22 深度对齐修复:
 *   - 添加loadDepartments()方法,每次刷新从DB动态加载部门
 *   - 操作按钮改为高对比度配色(蓝底白字/红底白字/绿底白字)
 *   - 列宽重新分配(操作列200px+stretch)
 *   - QComboBox下拉样式优化(hover+列表项)
 */
#include "UserManagementPage.h"
#include "components/SoftKeyboard.h"
#include "components/NumKeypad.h"
#include "components/FaceCameraWidget.h"  // [2026-06-23] 人脸录入对话框摄像头组件
#include "components/DeepFaceExtractor.h"   // [V2.17fix-0706] 人脸方位检测
#include "components/SingleSelectFilter.h"  // [V6.9] 通用单选筛选组件
#include "utils/StyleHelper.h"
#include "controller/UserController.h"
#include "controller/AuthController.h"
#include <QtNetwork>
// [V6.8.2] 重新引入db/UserDAO(仅用于getDistinctDepartments合并数据源) —— 作者：袁燕
// [V1.00.9.1 架构修复] 其他User数据访问仍通过UserController
#include "db/UserDAO.h"
#include "services/AuthService.h"
#include "services/SettingService.h"
#include "services/FaceRecognitionService.h"  // [2026-06-23] 人脸录入/清除
#include "pages/BatchImportDialog.h"  // [2026-06-26] 批量导入用户对话框
#include "components/BaseDialog.h"       // [2026-06-26] 统一圆角对话框基类
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
#include <QMouseEvent>               // [V6.5] eventFilter
#include <QSet>                      // [V6.8] loadDepartments选中状态保留
#include <QTimer>                    // [2026-06-23] 人脸录入成功延迟关闭
#include <QDateTime>                 // [V2.17fix-0706] 方位检测时间戳
#include <functional>                // [V2.17fix-0706] std::function

UserManagementPage::UserManagementPage(QWidget* parent) : QWidget(parent),
    m_userDialog(nullptr), m_editUserId(0), m_currentPage(1), m_pageSize(20), m_totalRecords(0) {
    setupUI();
    // [V6.7] 初始化安全软键盘 - confirmed信号仅关闭键盘(attach已处理输入)
    m_softKeyboard = new SoftKeyboard(this);
    connect(m_softKeyboard, &SoftKeyboard::confirmed, this, [this]() {
        // [2026-06-26v5] 数字输入已由NumKeypad独立负责，SoftKeyboard仅处理字母/搜索
        m_softKeyboard->hide();
    });
    connect(m_softKeyboard, &SoftKeyboard::cancelled, this, [this]() {
        m_softKeyboard->hide();
    });
}

UserManagementPage::~UserManagementPage() = default;

void UserManagementPage::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    // [2026-06-23] 工具栏仿照设计图：标题+操作按钮右对齐
    auto* toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(0, 0, 0, 4);
    toolbar->setSpacing(0);
    
    auto* title = new QLabel(QStringLiteral("人员信息管理"));
    title->setStyleSheet("font-size:20px;font-weight:700;color:#1a1a2e;");
    toolbar->addWidget(title);
    toolbar->addStretch();
    
    auto* actionsLayout = new QHBoxLayout();
    actionsLayout->setSpacing(12);
    
    m_addBtn = new QPushButton(QStringLiteral("＋ 新增人员"));
    m_addBtn->setStyleSheet(
        "QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:10px;"
        "padding:10px 22px;font-size:14px;font-weight:700;}"
        "QPushButton:hover{background:#3d8ae0;}"
        "QPushButton:pressed{transform:scale(0.96);}"
    );
    m_addBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addBtn, &QPushButton::clicked, this, &UserManagementPage::onAddUser);
    
    auto* importBtn = new QPushButton(QStringLiteral("📥 批量导入"));
    importBtn->setStyleSheet(
        "QPushButton{background:#fff;color:#4da3ff;border:2px solid #4da3ff;border-radius:10px;"
        "padding:10px 22px;font-size:14px;font-weight:700;}"
        "QPushButton:hover{background:#f0f7ff;}"
        "QPushButton:pressed{transform:scale(0.96);}"
    );
    importBtn->setCursor(Qt::PointingHandCursor);
    connect(importBtn, &QPushButton::clicked, this, [this]() {
        // [2026-06-26] 批量导入对话框：模板下载 → Excel上传 → 校验 → 批量入库 → 刷新列表
        BatchImportDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            loadUsers();  // 导入成功后自动刷新列表
        }
    });
    
    actionsLayout->addWidget(m_addBtn);
    actionsLayout->addWidget(importBtn);
    toolbar->addLayout(actionsLayout);
    mainLayout->addLayout(toolbar);

    // ==================== 搜索筛选行 (1:1复刻Vue .search-row) ====================
    auto* searchRow = new QHBoxLayout();
    searchRow->setSpacing(12);

    // [2026-06-23] 搜索框仿照设计图：统一外层Frame包裹输入框+⌨按钮，杜绝边框不全
    // [2026-06-23v3] 添加WA_StyledBackground确保border-radius正确渲染
    auto* searchInputWrap = new QFrame();
    searchInputWrap->setAttribute(Qt::WA_StyledBackground, true);
    searchInputWrap->setFixedWidth(320);
    searchInputWrap->setFixedHeight(48);  // [2026-06-25] 与筛选按钮高度统一48px
    searchInputWrap->setStyleSheet(
        "QFrame{border:2px solid #e0e0e0;border-radius:12px;background:#fff;}"
    );
    auto* searchInputLayout = new QHBoxLayout(searchInputWrap);
    // [2026-06-25] 右内边距1px防止按钮覆盖QFrame右下角边框
    searchInputLayout->setContentsMargins(0, 0, 1, 0);
    searchInputLayout->setSpacing(0);

    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索姓名 / 工号..."));
    m_searchEdit->setReadOnly(true);
    m_searchEdit->setCursor(Qt::PointingHandCursor);
    m_searchEdit->installEventFilter(this);
    // [2026-06-23修复] placeholder文字垂直居中：padding上下对称，去掉setMinimumHeight让QLineEdit自适应父容器
    m_searchEdit->setStyleSheet(
        "QLineEdit{border:none;padding:0 16px;font-size:16px;background:transparent;color:#333;min-height:42px;}"
    );
    searchInputLayout->addWidget(m_searchEdit, 1);

    m_searchKeyboardBtn = new QPushButton(QStringLiteral("⌨"));
    m_searchKeyboardBtn->setFixedSize(46, 44);  // [2026-06-25] 适配48px搜索框(内部44px=48-2-2边框)
    m_searchKeyboardBtn->setCursor(Qt::PointingHandCursor);
    // [2026-06-25] 按钮圆角10px对齐QFrame内边距(12px外框-2px边框=10px内径)
    m_searchKeyboardBtn->setStyleSheet(
        "QPushButton{border:none;border-radius:0 10px 10px 0;"
        "background:#f0f2f5;font-size:22px;color:#888;}"
        "QPushButton:hover{background:#e6f0ff;color:#4da3ff;}"
    );
    connect(m_searchKeyboardBtn, &QPushButton::clicked, this, &UserManagementPage::onSearchFieldClicked);

    // [2026-06-25] 部门多选下拉按钮 - 统一QFrame外壳(与搜索框边框模式一致)
    auto* deptFilterWrap = new QFrame();
    deptFilterWrap->setAttribute(Qt::WA_StyledBackground, true);
    deptFilterWrap->setFixedHeight(48);
    deptFilterWrap->setStyleSheet(
        "QFrame{border:2px solid #e0e0e0;border-radius:12px;background:#fff;}"
    );
    auto* deptFilterInnerLayout = new QHBoxLayout(deptFilterWrap);
    deptFilterInnerLayout->setContentsMargins(0, 0, 0, 0);
    deptFilterInnerLayout->setSpacing(0);

    m_deptFilterBtn = new QPushButton(QStringLiteral("部门筛选"));
    m_deptFilterBtn->setStyleSheet(
        "QPushButton{padding:0 16px;border:none;border-radius:10px;"
        "font-size:16px;background:transparent;color:#333;text-align:left;min-width:120px;}"
        "QPushButton:hover{background:#f5f7fa;}"
    );
    m_deptFilterBtn->setCursor(Qt::PointingHandCursor);
    connect(m_deptFilterBtn, &QPushButton::clicked, this, &UserManagementPage::onDeptFilterClicked);
    deptFilterInnerLayout->addWidget(m_deptFilterBtn);
    
    // 部门多选下拉面板 [V6.5] QFrame→QDialog修复checkbox选不中致命Bug
    m_deptPopup = new QDialog(this);
    m_deptPopup->setWindowFlags(Qt::FramelessWindowHint | Qt::Popup);
    m_deptPopup->setModal(false);
    m_deptPopup->setFixedWidth(240);  // [1:1复刻] Vue .multi-select width:240px
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
    m_deptPopup->setLayout(deptPopupLayout);  // [V6.8] 确保布局已关联,loadDepartments可重建
    
    // [V6.8] 部门列表由loadDepartments()动态加载(对齐Web版异步加载策略)
    loadDepartments();

    // [V6.9 2026-06-24] 角色筛选 - 改为CheckBox样式单选组件(与MultiSelectFilter外观统一)
    m_roleFilter = new SingleSelectFilter(QStringLiteral("全部角色"), this);
    m_roleFilter->setOptions({QStringLiteral("全部角色"), QStringLiteral("管理员"), QStringLiteral("普通用户")});
    connect(m_roleFilter, &SingleSelectFilter::selectionChanged, this, [this](const QString&) { m_currentPage = 1; loadUsers(); });

    // [V6.9 2026-06-24] 状态筛选 - 改为CheckBox样式单选组件
    m_statusFilter = new SingleSelectFilter(QStringLiteral("全部状态"), this);
    m_statusFilter->setOptions({QStringLiteral("全部状态"), QStringLiteral("启用"), QStringLiteral("待激活"),
                                QStringLiteral("禁用"), QStringLiteral("已锁定")});
    connect(m_statusFilter, &SingleSelectFilter::selectionChanged, this, [this](const QString&) { m_currentPage = 1; loadUsers(); });

    m_searchBtn = new QPushButton(QStringLiteral("查询"));
    m_searchBtn->setFixedHeight(48);
    m_searchBtn->setStyleSheet(
        "QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:12px;"
        "padding:0 24px;font-size:16px;font-weight:700;}"
        "QPushButton:hover{background:#3d8ae0;}"
        "QPushButton:pressed{transform:scale(0.96);}"
    );
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    connect(m_searchBtn, &QPushButton::clicked, this, &UserManagementPage::onSearch);

    m_resetBtn = new QPushButton(QStringLiteral("重置"));
    m_resetBtn->setFixedHeight(48);
    m_resetBtn->setStyleSheet(
        "QPushButton{background:#fff;color:#4da3ff;border:2px solid #4da3ff;border-radius:12px;"
        "padding:0 24px;font-size:16px;font-weight:700;}"
        "QPushButton:hover{background:#f0f7ff;}"
        "QPushButton:pressed{transform:scale(0.96);}"
    );
    m_resetBtn->setCursor(Qt::PointingHandCursor);
    connect(m_resetBtn, &QPushButton::clicked, this, &UserManagementPage::onReset);

    searchRow->addWidget(searchInputWrap);
    searchRow->addWidget(deptFilterWrap);  // [2026-06-25] 改用QFrame外壳统一边框高度
    searchRow->addWidget(m_roleFilter);
    searchRow->addWidget(m_statusFilter);
    searchRow->addWidget(m_searchBtn);
    searchRow->addWidget(m_resetBtn);
    searchRow->addStretch();
    mainLayout->addLayout(searchRow);

    // ==================== 用户数据表格面板 [V8.2 2026-06-25] 去除外阴影，保持简洁 ====================
    auto* panel = new QFrame();
    panel->setStyleSheet(
        "QFrame#userTablePanel{ background:white; border-radius:12px; }"
    );
    panel->setObjectName("userTablePanel");
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(0);

    m_table = new QTableWidget();
    m_table->setColumnCount(9);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("工号"), QStringLiteral("姓名"), QStringLiteral("部门"),
        QStringLiteral("角色"), QStringLiteral("联系电话"), QStringLiteral("人脸录入"),
        QStringLiteral("创建时间"), QStringLiteral("状态"), QStringLiteral("操作")
    });
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->setAlternatingRowColors(false);
    // [V2.02 2026-06-28] 移除内联表格QSS，使用全局QSS统一表格样式（小米设计语言）
    // [V8.2 2026-06-25] 数据列Stretch均分，操作列Fixed紧凑（触屏按钮~130px）
    //   列：工号  姓名  部门  角色  联系电话  人脸录入  创建时间  状态  操作(idx8)
    for (int i = 0; i < 8; i++) {
        m_table->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
    }
    m_table->horizontalHeader()->setSectionResizeMode(8, QHeaderView::Fixed);
    m_table->setColumnWidth(8, 200);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setMinimumSectionSize(60);




    panelLayout->addWidget(m_table, 1);

    // [2026-06-23] 分页栏仿照设计图：共N条在左，页码按钮在右
    auto* pageRow = new QHBoxLayout();
    pageRow->setContentsMargins(18, 12, 18, 12);
    pageRow->setSpacing(6);

    m_totalLabel = new QLabel(QStringLiteral("共 0 条"));
    m_totalLabel->setStyleSheet("font-size:13px;color:#999;");

    m_prevBtn = new QPushButton(QStringLiteral("上一页"));
    m_prevBtn->setStyleSheet(
        "QPushButton{border:1px solid #ddd;border-radius:6px;padding:5px 12px;"
        "font-size:13px;font-weight:600;color:#555;background:#fff;min-height:30px;}"
        "QPushButton:hover{border-color:#4da3ff;color:#4da3ff;}"
        "QPushButton:disabled{opacity:0.35;}"
    );
    m_prevBtn->setCursor(Qt::PointingHandCursor);
    connect(m_prevBtn, &QPushButton::clicked, this, &UserManagementPage::onPrevPage);

    m_nextBtn = new QPushButton(QStringLiteral("下一页"));
    m_nextBtn->setStyleSheet(
        "QPushButton{border:1px solid #ddd;border-radius:6px;padding:5px 12px;"
        "font-size:13px;font-weight:600;color:#555;background:#fff;min-height:30px;}"
        "QPushButton:hover{border-color:#4da3ff;color:#4da3ff;}"
        "QPushButton:disabled{opacity:0.35;}"
    );
    m_nextBtn->setCursor(Qt::PointingHandCursor);
    connect(m_nextBtn, &QPushButton::clicked, this, &UserManagementPage::onNextPage);

    m_pageLabel = new QLabel(QStringLiteral("第 1 页"));
    m_pageLabel->setStyleSheet("font-size:13px;color:#999;padding:0 4px;");

    // [V7.5 2026-06-26] 布局顺序：stretch | 上一页 | 第X页 | 下一页 | 共N条
    pageRow->addStretch();
    pageRow->addWidget(m_prevBtn);
    pageRow->addWidget(m_pageLabel);
    pageRow->addWidget(m_nextBtn);
    pageRow->addWidget(m_totalLabel);

    // [V6.5] 分页区域放入panel，加border-top分隔线 (Web: .pagination border-top:1px solid #f0f0f0)
    auto* pageWidget = new QWidget();
    pageWidget->setStyleSheet("border-top:1px solid #f0f0f0; background:transparent;");
    pageWidget->setLayout(pageRow);
    panelLayout->addWidget(pageWidget);
    mainLayout->addWidget(panel, 1);

    // [编译兼容] 部门列表已通过上方popup面板加载，无需下拉框
}

void UserManagementPage::refresh() {
    loadDepartments();  // [V6.8] 每次刷新重新加载部门列表(对齐Web版onMounted)
    loadUsers();
}

void UserManagementPage::onSearch() {
    m_currentPage = 1;  // [2026-06-26v8] 筛选查询必须重置到第1页
    loadUsers();
}

void UserManagementPage::onReset() {
    // [2026-06-23] 重置所有筛选条件
    m_searchEdit->clear();
    m_roleFilter->reset();     // [V6.9] SingleSelectFilter::reset()
    m_statusFilter->reset();   // [V6.9] SingleSelectFilter::reset()
    for (auto& pair : m_deptCheckBoxes) {
        pair.second->setChecked(false);
    }
    m_deptFilterBtn->setText(QStringLiteral("部门筛选"));
    m_currentPage = 1;  // [2026-06-26v8] 重置筛选必须重置到第1页
    loadUsers();
}

/** [V6.8.2] 从DB动态加载部门列表(合并sys_department + sys_user DISTINCT)
 *  确保筛选下拉框与列表显示的部门数据完全一致
 *  [V6.8.1修复] delete+recreate弹窗方案(安全)
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
        cb->setChecked(true);  // [V6.8.2] 默认全选，让用户自行取消筛选
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
    // [V6.9] SingleSelectFilter::selectedText() + selectedIndex() 替换 QComboBox::currentText() + currentIndex()
    QString roleText = m_roleFilter->selectedText();
    int roleIdx = m_roleFilter->selectedIndex();
    QString role = (roleIdx > 0) ? (roleText == QStringLiteral("管理员") ? "admin" : "user") : "";

    QString statusText = m_statusFilter->selectedText();
    int statusIdx = m_statusFilter->selectedIndex();
    QString status = (statusIdx > 0) ?
        (statusText == QStringLiteral("启用") ? "active" :
         statusText == QStringLiteral("待激活") ? "pending" :
         statusText == QStringLiteral("禁用") ? "disabled" : "locked") : "";
    // [v5.2修复] 从部门多选checkbox中收集选中部门，拼成逗号分隔字符串传给API
    QStringList selectedDepts;
    for (auto& pair : m_deptCheckBoxes) {
        if (pair.second->isChecked()) selectedDepts.append(pair.first);
    }
    QString dept = selectedDepts.join(",");

    // [V1.00.9.1 架构修复] 通过UserController替代直接调用db/UserDAO —— 作者：袁燕
    UserController ctrl;
    auto pageResult = ctrl.getUserList(m_currentPage, m_pageSize, kw, dept, status, role);
    m_totalRecords = pageResult.total;

    // 更新分页信息
    int totalPages = (m_totalRecords + m_pageSize - 1) / m_pageSize;
    m_pageLabel->setText(QStringLiteral("第 %1/%2 页").arg(m_currentPage).arg(qMax(1, totalPages)));
    m_totalLabel->setText(QStringLiteral("共 %1 条").arg(m_totalRecords));
    m_prevBtn->setEnabled(m_currentPage > 1);
    m_nextBtn->setEnabled(m_currentPage < totalPages);

    // 填充表格（BS端列：工号/姓名/部门/角色/联系电话/人脸录入/创建时间/状态/操作）
    m_table->setRowCount(pageResult.list.size());
    for (int i = 0; i < pageResult.list.size(); ++i) {
        const User& u = pageResult.list[i];
        int userId = u.userId;
        QString roleText = (u.role == "admin") ? QStringLiteral("管理员") : QStringLiteral("普通用户");
        QString statusText = (u.status == "active") ? QStringLiteral("启用") :
                          (u.status == "pending") ? QStringLiteral("待激活") :
                          (u.status == "disabled") ? QStringLiteral("禁用") : QStringLiteral("已锁定");

        m_table->setItem(i, 0, new QTableWidgetItem(u.workNo));  // 工号
        m_table->setItem(i, 1, new QTableWidgetItem(u.realName));  // 姓名
        m_table->setItem(i, 2, new QTableWidgetItem(u.department));  // 部门

        // [2026-06-23] 角色标签仿照设计图：小圆角标签样式
        auto* roleItem = new QTableWidgetItem(roleText);
        roleItem->setTextAlignment(Qt::AlignCenter);
        if (u.role == "admin") {
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

        // [2026-06-23] 人脸录入列：改为可点击按钮（已录入→查看/清除，未录入→录入）
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

        // [2026-06-23] 状态标签仿照设计图：启用(绿)/待激活(灰)/禁用(灰)/已锁定(红)
        auto* statusItem = new QTableWidgetItem(statusText);
        statusItem->setTextAlignment(Qt::AlignCenter);
        if (u.status == "active") {
            statusItem->setForeground(QColor("#389e0d"));
            statusItem->setBackground(QColor("#f6ffed"));
        } else if (u.status == "pending") {
            statusItem->setForeground(QColor("#999999"));
            statusItem->setBackground(QColor("#f5f5f5"));
        } else if (u.status == "disabled") {
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

        // [v6.9 操作按钮优化] 按钮加大+间距增大，确保触屏可点、文字完整显示
        auto* opWidget = new QWidget();
        opWidget->setStyleSheet("background:transparent;");
        auto* opLayout = new QHBoxLayout(opWidget);
        opLayout->setContentsMargins(4, 4, 4, 4);
        opLayout->setSpacing(8);  // 6→8 按钮间距加大

        // "编辑"按钮 (58x34→64x36, 触屏优化)
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

        // "禁用/启用"按钮 (58x34→64x36)
        bool isActive = (u.status == "active");
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
        m_table->setRowHeight(i, 64);  // [v6.9] 58→64 行高加大，确保36px按钮+8px边距完整显示不裁剪
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

// [V7.0.1][2026-06-26v3] 统一圆角风格 — 改用BaseDialog基类
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
    m_dlgWorkNo->setPlaceholderText(QStringLiteral("如CF007"));
    m_dlgWorkNo->setStyleSheet(inputStyle);

    m_dlgDept = new QComboBox();
    m_dlgDept->setEditable(true);
    m_dlgDept->setStyleSheet(comboStyle);

    // [V7.0][2026-06-26] 角色按钮组（2选1）- 固定宽度不再Expanding撑满整行
    {
        auto makeRoleBtn = [&](const QString& text, int mode) -> QPushButton* {
            auto* btn = new QPushButton(text);
            btn->setCheckable(true);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setFixedHeight(44);
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
        // [2026-06-26v4] 先隐藏字母键盘防止穿透残留
        if (m_softKeyboard) m_softKeyboard->hide();
        if (m_numKeypad && m_dlgPassword) {
            m_numKeypad->setShuffle(true);
            m_numKeypad->setShowPassword(false);
            m_numKeypad->attach(m_dlgPassword);
            m_numKeypad->show();
        }
    });
    pwdFL->addWidget(skbPwdBtn);

    // [2026-06-26v2] 独立数字键盘 - 顶层Popup弹窗，不嵌入布局
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
        lbl->setMinimumHeight(44);
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
    cancelBtn->setMinimumHeight(48);
    cancelBtn->setMinimumWidth(100);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setStyleSheet(
        "QPushButton{background:#fff;color:#666;border:2px solid #ddd;border-radius:12px;"
        "font-size:15px;font-weight:600;}"
        "QPushButton:hover{background:#f5f5f5;}"
    );
    connect(cancelBtn, &QPushButton::clicked, this, [this]() { m_userDialog->reject(); });

    m_dlgSaveBtn = new QPushButton(QStringLiteral("确认"));
    m_dlgSaveBtn->setMinimumHeight(48);
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
    createUserDialog();  // [V7.0.1] 独立初始化，不弹窗
    m_userDialog->setDialogTitle(QStringLiteral("新增用户"));
    m_dlgSaveBtn->setText(QStringLiteral("确认新增"));
    m_dlgRealName->clear(); m_dlgWorkNo->clear(); m_dlgPassword->clear();
    m_dlgRoleMode = 0; updateRoleBtnStyles();
    m_dlgDept->setCurrentIndex(0); m_dlgPhone->clear();
    m_userDialog->exec();
}

void UserManagementPage::onEditUser(int userId) {
    m_editUserId = userId;
    // [V1.00.9.1 架构修复] 通过UserController替代直接调用db/UserDAO —— 作者：袁燕
    UserController ctrl;
    User u = ctrl.getUserById(userId);
    if (u.userId == 0) {
        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("用户不存在"));
        return;
    }
    // [V7.0.1] 使用独立初始化方法，不再错误调用onAddUser（避免弹出"新增用户"标题）
    createUserDialog();
    m_userDialog->setDialogTitle(QStringLiteral("编辑用户"));
    m_dlgSaveBtn->setText(QStringLiteral("保存修改"));
    // [V6.8] 刷新部门列表(防止DB新增部门后弹窗列表过期)
    loadDepartments();
    m_dlgDept->clear();
    for (const QString& d : m_departmentOptions) m_dlgDept->addItem(d);
    m_dlgRealName->setText(u.realName);
    m_dlgWorkNo->setText(u.workNo);
    m_dlgWorkNo->setEnabled(false);  // 工号编辑时禁用
    m_dlgDept->setCurrentText(u.department);
    m_dlgRoleMode = (u.role == "admin") ? 1 : 0; updateRoleBtnStyles();
    m_dlgPhone->setText(u.phone);
    m_dlgPassword->setText(QString());
    m_dlgPassword->setPlaceholderText(QStringLiteral("留空则不修改"));
    m_userDialog->exec();
}

void UserManagementPage::onDeleteUser(int userId) {
    if (!MessageDialog::showQuestion(this, QStringLiteral("确认删除"),
        QStringLiteral("确定要删除该用户吗？"))) return;
    // [V1.00.9.1 架构修复] 通过UserController替代直接调用db/UserDAO —— 作者：袁燕
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
    // [V7.0] 角色从按钮组获取
    QString role = (m_dlgRoleMode == 1) ? "admin" : "user";
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

    // [V1.00.9.1 架构修复] 通过UserController替代直接调用db/UserDAO —— 作者：袁燕
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
        newUser.status = "active";
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

/** [V6.5][2026-06-26v4] 搜索框点击→弹出用户名软键盘(对齐登录页ModeEn模式) */
void UserManagementPage::onSearchFieldClicked() {
    // [2026-06-26v4] 先隐藏数字键盘防止穿透残留
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
    }
    return QWidget::eventFilter(obj, event);
}

/** [2026-06-15 新增] 密码框点击 → 弹出安全软键盘(密码随机模式) */
/// [2026-06-26] 改用NumKeypad嵌入式控件，此函数已废弃但保留接口兼容
void UserManagementPage::onDlgPasswordClicked() {
    // 已改用NumKeypad嵌入式控件，逻辑移至⌨按钮的lambda中
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
    // [V1.00.9.1 架构修复] 通过UserController替代直接调用db/UserDAO —— 作者：袁燕
    UserController ctrl;
    User user = ctrl.getUserById(userId);
    if (user.userId == 0) return;
    
    QString currentStatus = user.status;
    QString newStatus = (currentStatus == "active") ? "disabled" : "active";
    QString actionText = (newStatus == "disabled") ? "禁用" : "启用";
    
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

/** [2026-06-26v3] 人脸录入 — 统一BaseDialog圆角风格 */
void UserManagementPage::onFaceEnroll(int userId) {
    UserController ctrl;
    User user = ctrl.getUserById(userId);
    if (user.userId == 0) return;

    auto* dlg = new BaseDialog(this, 520);
    dlg->setDialogTitle(QStringLiteral("人脸信息录入"));
    dlg->setMinimumHeight(580);

    auto* cl = dlg->contentLayout();
    cl->setSpacing(12);

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

    // 摄像头组件 [V2.03l 2026-06-30] 参数优化：降低阈值+减少稳定帧数，支持偏侧脸自动采集
    FaceCameraWidget* camera = new FaceCameraWidget();
    camera->setMinimumSize(280, 260);
    camera->setMaximumSize(360, 300);
    camera->setAutoCapture(false);
    camera->setMinConfidence(0.60);   // [V2.03l] 0.65→0.60 偏侧脸也能检测
    camera->setStableFrames(6);       // [V2.03l] 20→6 减少稳定帧数，自动采集更快
    camera->setDetectInterval(80);    // [V2.03l] 120→80 加快检测频率
    cl->addWidget(camera, 0, Qt::AlignCenter);

    // [V2.17fix-0706 袁燕] 方位大字提示标签：36px醒目显示，实时方位反馈
    auto* directionLabel = new QLabel(QStringLiteral("选择开始录入"));
    directionLabel->setAlignment(Qt::AlignCenter);
    directionLabel->setMinimumHeight(56);
    directionLabel->setStyleSheet(
        "font-size:32px; font-weight:900; color:#ffffff; "
        "background:#52c41a; border-radius:14px; padding:10px 20px;");
    cl->addWidget(directionLabel);

    // 指令提示
    auto* instructionLabel = new QLabel(QStringLiteral("点击「开始录入」启动摄像头"));
    instructionLabel->setAlignment(Qt::AlignCenter);
    instructionLabel->setWordWrap(true);
    instructionLabel->setStyleSheet("font-size:15px;color:#666;padding:4px 0;background:transparent;");
    cl->addWidget(instructionLabel);

    // 状态提示
    auto* statusLabel = new QLabel("");
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setStyleSheet("font-size:14px;color:#4da3ff;background:transparent;");
    cl->addWidget(statusLabel);

    // 按钮区（通过 BaseDialog::buttonLayout()）
    auto* bl = dlg->buttonLayout();
    while (bl->count() > 0) {
        QLayoutItem* item = bl->takeAt(0);
        delete item;
    }

    auto* startBtn = new QPushButton(QStringLiteral("开始人脸录入"));
    startBtn->setFixedHeight(48);
    startBtn->setCursor(Qt::PointingHandCursor);
    startBtn->setStyleSheet(
        "QPushButton{background:#4da3ff;color:white;border:none;border-radius:12px;"
        "font-size:15px;font-weight:700;padding:0 28px;}"
        "QPushButton:hover{background:#3d8ae0;}");

    auto* confirmBtn = new QPushButton(QStringLiteral("确认保存"));
    confirmBtn->setFixedHeight(48);
    confirmBtn->setVisible(false);
    confirmBtn->setCursor(Qt::PointingHandCursor);
    confirmBtn->setStyleSheet(
        "QPushButton{background:#27ae60;color:white;border:none;border-radius:12px;"
        "font-size:15px;font-weight:700;padding:0 28px;}"
        "QPushButton:hover{background:#219a52;}");

    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    cancelBtn->setFixedHeight(48);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setStyleSheet(
        "QPushButton{background:#f5f5f5;color:#666;border:1px solid #ddd;border-radius:12px;"
        "font-size:15px;font-weight:600;padding:0 24px;}"
        "QPushButton:hover{background:#e8e8e8;}");
    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);

    bl->addWidget(startBtn);
    bl->addWidget(confirmBtn);
    bl->addWidget(cancelBtn);
    bl->addStretch();

    // ---- [V2.17fix-0706 袁燕] 5方位检测引导采集逻辑 ----
    //   使用face-server.js /posture接口实时检测人脸yaw/pitch
    //   只有用户真正转到目标方位才采集，不是简单的定时采集
    int captureCount = 0;
    QString bestDescriptor;
    double bestConfidence = 0.0;
    const int MAX_CAPTURES = 5;
    bool isCapturing = false;

    // 5个方位的目标参数
    struct PostureTarget {
        QString name, instruction;
        double yawMin, yawMax, pitchMin, pitchMax;
    };
    // 方位目标参数（yaw/pitch方向与face-server.js /posture接口一致）
    //   yaw:  正值=脸偏右(用户左转露右脸)，负值=脸偏左(用户右转露左脸)
    //   pitch: 正值=低头，负值=抬头
    //   归一化基准为人脸框宽高，阈值经实测校准
    static const PostureTarget POSTURE_TARGETS[5] = {
        { QStringLiteral("居中"), QStringLiteral("请面向摄像头，保持正脸"),          -0.15, 0.15, -0.15, 0.15 },
        { QStringLiteral("左侧"), QStringLiteral("请将头部向右转，露出左侧面部"),   -0.50,-0.15, -0.30, 0.30 },
        { QStringLiteral("右侧"), QStringLiteral("请将头部向左转，露出右侧面部"),    0.15, 0.50, -0.30, 0.30 },
        { QStringLiteral("上偏"), QStringLiteral("请略微抬头，露出面部上方"),       -0.30, 0.30, -0.50,-0.15 },
        { QStringLiteral("下偏"), QStringLiteral("请略微低头，露出面部下方"),       -0.30, 0.30,  0.15, 0.50 },
    };

    // 方位检测状态（dialog是模态exec，局部变量生命周期覆盖整个采集过程）
    bool simpleMode = false;
    int targetIdx = 0;
    int postureMatchCount = 0;
    qint64 postureFirstMatchTime = 0;
    qint64 postureStartTime = 0;
    int postureFailCount = 0;

    // [V2.18] 异步HTTP方位检测状态
    QNetworkAccessManager* postureNam = new QNetworkAccessManager(dlg);
    bool postureRequestPending = false;  // 是否有HTTP请求在飞行中
    int postureHttpFailCount = 0;        // HTTP连续失败计数

    // 方位检测定时器（本地估算0延迟，100ms高频检测）
    QTimer* postureTimer = new QTimer(dlg);
    postureTimer->setInterval(100);

    // 简单模式定时器
    QTimer* simpleTimer = new QTimer(dlg);
    simpleTimer->setSingleShot(true);

    // 关闭所有定时器
    auto stopTimers = [postureTimer, simpleTimer]() {
        postureTimer->stop();
        simpleTimer->stop();
    };

    // 执行采集
    auto doCapture = [camera, instructionLabel]() {
        instructionLabel->setText(QStringLiteral("正在采集..."));
        instructionLabel->setStyleSheet(
            "font-size:15px;font-weight:bold;color:#4da3ff;padding:4px 0;background:transparent;");
        camera->captureNow();
    };

    // [V2.18 2026-07-06 袁燕] 方位检测回调 — 异步HTTP模式
    //   定时器触发异步POST /posture，HTTP响应回调中做方位匹配，不阻塞UI
    connect(postureTimer, &QTimer::timeout, dlg, [=, &captureCount, &isCapturing, &simpleMode, &targetIdx,
        &postureMatchCount, &postureFirstMatchTime, &postureStartTime, &postureFailCount,
        &postureNam, &postureRequestPending, &postureHttpFailCount]() {
        if (!isCapturing) { postureTimer->stop(); return; }
        if (simpleMode) { postureTimer->stop(); return; }
        // 上一个请求还在飞行中，跳过本次（避免请求堆积）
        if (postureRequestPending) return;

        qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - postureStartTime;
        if (elapsed > 30000) {
            instructionLabel->setText(QStringLiteral("⚠ 方位超时，将采集当前帧作为兜底"));
            instructionLabel->setStyleSheet(
                "font-size:15px;font-weight:bold;color:#ff4d4f;padding:4px 0;background:transparent;");
            postureTimer->stop();
            QTimer::singleShot(100, dlg, [doCapture]() { doCapture(); });
            return;
        }

        QImage frame = camera->currentFrame();
        QRect faceRect = camera->faceRect();
        if (frame.isNull() || faceRect.isNull()) {
            postureMatchCount = 0;
            postureFirstMatchTime = 0;
            instructionLabel->setText(QStringLiteral("🔍 未检测到人脸，请对准摄像头..."));
            instructionLabel->setStyleSheet(
                "font-size:15px;color:#4da3ff;padding:4px 0;background:transparent;");
            return;
        }

        // 发起异步HTTP请求到 face-server.js /posture
        postureRequestPending = true;
        QByteArray base64Data = DeepFaceExtractor::imageToBase64Jpeg(frame);
        QJsonObject bodyObj;
        bodyObj["image"] = QString::fromUtf8(base64Data);
        QByteArray body = QJsonDocument(bodyObj).toJson(QJsonDocument::Compact);

        QNetworkRequest req;
        req.setUrl(QUrl("http://127.0.0.1:8089/posture"));
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setTransferTimeout(1500);
        QNetworkReply* reply = postureNam->post(req, body);

        QObject::connect(reply, &QNetworkReply::finished, dlg, [=, &captureCount, &isCapturing, &simpleMode,
            &targetIdx, &postureMatchCount, &postureFirstMatchTime, &postureFailCount,
            &postureRequestPending, &postureHttpFailCount]() {
            postureRequestPending = false;
            reply->deleteLater();

            if (!isCapturing || simpleMode) return;

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
                postureHttpFailCount++;
                // 不再因HTTP失败切简易模式，改用本地估算兜底继续工作
                // 只有连续15次失败才切简易模式（服务真的挂了）
                if (postureHttpFailCount >= 15) {
                    instructionLabel->setText(QStringLiteral("⚠ 方位识别服务异常，切换简易模式..."));
                    instructionLabel->setStyleSheet(
                        "font-size:15px;color:#fa8c16;padding:4px 0;background:transparent;");
                    simpleMode = true;
                    postureTimer->stop();
                    simpleTimer->start(1000);
                    return;
                }
                // HTTP失败，用本地估算兜底
                QRect fr = camera->faceRect();
                camera->estimatePosture(fr, yaw, pitch);
            } else {
                postureHttpFailCount = 0;
                postureFailCount = 0;
            }

            // 方位匹配判断
            const PostureTarget& target = POSTURE_TARGETS[targetIdx];
            bool matched = (yaw >= target.yawMin && yaw <= target.yawMax &&
                            pitch >= target.pitchMin && pitch <= target.pitchMax);

            if (matched) {
                postureMatchCount++;
                if (postureFirstMatchTime == 0)
                    postureFirstMatchTime = QDateTime::currentMSecsSinceEpoch();
                qint64 stayMs = QDateTime::currentMSecsSinceEpoch() - postureFirstMatchTime;
                double remainSec = qMax(0.0, (500.0 - stayMs) / 1000.0);

                directionLabel->setText(QStringLiteral("【 %1 】 %2s").arg(target.name).arg(remainSec, 0, 'f', 1));
                directionLabel->setStyleSheet(
                    "font-size:32px; font-weight:900; color:#ffffff; "
                    "background:#52c41a; border-radius:14px; padding:10px 20px;");
                instructionLabel->setText(QStringLiteral("✅ 方位匹配 %1/2帧  (yaw=%2 pitch=%3)\n还需保持%4秒...")
                    .arg(postureMatchCount)
                    .arg(yaw, 0, 'f', 2).arg(pitch, 0, 'f', 2).arg(remainSec, 0, 'f', 1));
                instructionLabel->setStyleSheet(
                    "font-size:15px;font-weight:bold;color:#389e0d;padding:4px 0;background:transparent;");

                if (postureMatchCount >= 2 && stayMs >= 500) {
                    postureTimer->stop();
                    directionLabel->setText(QStringLiteral("【 %1 】✓").arg(target.name));
                    doCapture();
                }
            } else {
                postureMatchCount = 0;
                postureFirstMatchTime = 0;
                QString hint;
                if (yaw > 0.15) hint = QStringLiteral("当前：右转(露右脸)");
                else if (yaw < -0.15) hint = QStringLiteral("当前：左转(露左脸)");
                else if (pitch > 0.15) hint = QStringLiteral("当前：低头");
                else if (pitch < -0.15) hint = QStringLiteral("当前：抬头");
                else hint = QStringLiteral("当前：居中");
                directionLabel->setText(QStringLiteral("【 %1 】").arg(target.name));
                directionLabel->setStyleSheet(
                    "font-size:32px; font-weight:900; color:#ffffff; "
                    "background:#4da3ff; border-radius:14px; padding:10px 20px;");
                instructionLabel->setText(QStringLiteral("%1\n请调整到【%2】方位\n%3")
                    .arg(hint).arg(target.name).arg(target.instruction));
                instructionLabel->setStyleSheet(
                    "font-size:15px;font-weight:bold;color:#fa8c16;padding:4px 0;background:transparent;");
            }
        });
    });

    // 简易模式定时器触发采集
    connect(simpleTimer, &QTimer::timeout, dlg, [=, &captureCount, &isCapturing, &simpleMode]() {
        if (!isCapturing) return;
        if (!camera->faceRect().isNull()) {
            doCapture();
        } else {
            instructionLabel->setText(QStringLiteral("🔍 未检测到人脸，请对准摄像头..."));
            instructionLabel->setStyleSheet(
                "font-size:15px;color:#4da3ff;padding:4px 0;background:transparent;");
            simpleTimer->start(1000);
        }
    });

    // [V2.17fix-0706] 开始录入按钮 — 启动方位检测流程
    connect(startBtn, &QPushButton::clicked, camera, [=, &captureCount, &bestConfidence, &bestDescriptor,
        &isCapturing, &simpleMode, &targetIdx, &postureMatchCount, &postureFirstMatchTime,
        &postureStartTime, &postureFailCount, &postureRequestPending, &postureHttpFailCount]() {
        startBtn->setVisible(false);
        cancelBtn->setVisible(true);
        captureCount = 0;
        bestConfidence = 0.0;
        bestDescriptor.clear();
        isCapturing = true;
        simpleMode = false;
        targetIdx = 0;
        postureMatchCount = 0;
        postureFirstMatchTime = 0;
        postureFailCount = 0;
        postureRequestPending = false;
        postureHttpFailCount = 0;

        instructionLabel->setText(QStringLiteral("正在检测方位识别服务..."));
        instructionLabel->setStyleSheet(
            "font-size:15px;font-weight:bold;color:#4da3ff;padding:4px 0;background:transparent;");
        directionLabel->setText(QStringLiteral("初始化"));
        directionLabel->setStyleSheet(
            "font-size:32px; font-weight:900; color:#ffffff; "
            "background:#52c41a; border-radius:14px; padding:10px 20px;");

        camera->startCamera();

        // 800ms后预检/posture（等摄像头和face-server就绪）
        // [V2.17fix-0706 袁燕] 改用QTimer+property状态存储，避免MSVC std::function递归崩溃
        QTimer::singleShot(800, dlg, [=, &isCapturing, &simpleMode, &postureStartTime]() {
            if (!isCapturing) return;

            QTimer* retryTimer = new QTimer(dlg);
            retryTimer->setSingleShot(true);
            retryTimer->setInterval(500);
            retryTimer->setProperty("retryLeft", 10);

            connect(retryTimer, &QTimer::timeout, dlg, [=, &isCapturing, &simpleMode, &postureStartTime]() {
                if (!isCapturing) { retryTimer->deleteLater(); return; }

                int retryLeft = retryTimer->property("retryLeft").toInt();
                QImage frame = camera->currentFrame();
                DeepFaceExtractor ext;
                double y, p; QString e;
                if (!frame.isNull() && ext.detectPosture(frame, y, p, e)) {
                    simpleMode = false;
                    const auto& t = POSTURE_TARGETS[0];
                    directionLabel->setText(QStringLiteral("【 %1 】").arg(t.name));
                    directionLabel->setStyleSheet(
                        "font-size:32px; font-weight:900; color:#ffffff; "
                        "background:#52c41a; border-radius:14px; padding:10px 20px;");
                    instructionLabel->setText(QStringLiteral("📸 第 1/5 帧 — 方位检测已就绪\n%1").arg(t.instruction));
                    instructionLabel->setStyleSheet(
                        "font-size:15px;font-weight:bold;color:#fa8c16;padding:4px 0;background:transparent;");
                    postureStartTime = QDateTime::currentMSecsSinceEpoch();
                    postureTimer->start();
                    retryTimer->deleteLater();
                } else if (retryLeft > 0) {
                    retryTimer->setProperty("retryLeft", retryLeft - 1);
                    instructionLabel->setText(QStringLiteral("正在连接人脸识别服务... (%1)").arg(retryLeft - 1));
                    retryTimer->start(500);
                } else {
                    simpleMode = true;
                    const auto& t = POSTURE_TARGETS[0];
                    directionLabel->setText(QStringLiteral("【 %1 】(简易)").arg(t.name));
                    directionLabel->setStyleSheet(
                        "font-size:32px; font-weight:900; color:#ffffff; "
                        "background:#fa8c16; border-radius:14px; padding:10px 20px;");
                    instructionLabel->setText(QStringLiteral("⚠ 方位检测服务未连接\n简易模式：3秒后自动采集\n%1").arg(t.instruction));
                    instructionLabel->setStyleSheet(
                        "font-size:15px;font-weight:bold;color:#fa8c16;padding:4px 0;background:transparent;");
                    simpleTimer->start(3000);
                    retryTimer->deleteLater();
                }
            });

            retryTimer->start(0);
        });
    });

    // [V2.17fix-0706] 采集结果回调 — 进入下一个方位或完成
    connect(camera, &FaceCameraWidget::captureReady, camera, [=, &captureCount, &bestConfidence,
        &bestDescriptor, &isCapturing, &simpleMode, &targetIdx, &postureMatchCount, &postureFirstMatchTime,
        &postureStartTime, &postureFailCount, &postureRequestPending, &postureHttpFailCount](const QImage&, double confidence) {
        if (!isCapturing) return;
        QString descriptor = camera->getLastDescriptor();
        if (descriptor.isEmpty()) {
            instructionLabel->setText(QStringLiteral("⚠ 特征提取失败，稍后自动重试..."));
            instructionLabel->setStyleSheet(
                "font-size:15px;color:#fa8c16;padding:4px 0;background:transparent;");
            camera->reset();
            if (simpleMode) {
                simpleTimer->start(1500);
            } else {
                postureStartTime = QDateTime::currentMSecsSinceEpoch();
                postureMatchCount = 0;
                postureFirstMatchTime = 0;
                postureFailCount = 0;
                postureRequestPending = false;
                postureHttpFailCount = 0;
                postureTimer->start();
            }
            return;
        }
        captureCount++;
        int dimCount = descriptor.split(",").size();
        double score = confidence * 0.7 + (dimCount >= 128 ? 0.3 : 0.1);
        if (score > bestConfidence) { bestConfidence = score; bestDescriptor = descriptor; }

        if (captureCount < MAX_CAPTURES) {
            // 进入下一个方位
            targetIdx = captureCount;
            const auto& next = POSTURE_TARGETS[targetIdx];
            camera->reset();
            if (simpleMode) {
                directionLabel->setText(QStringLiteral("【 %1 】(简易)").arg(next.name));
                directionLabel->setStyleSheet(
                    "font-size:32px; font-weight:900; color:#ffffff; "
                    "background:#fa8c16; border-radius:14px; padding:10px 20px;");
                instructionLabel->setText(QStringLiteral("✅ 第 %1/5 帧采集成功\n下一个【%2】— 3秒后自动采集\n%3")
                    .arg(captureCount).arg(next.name).arg(next.instruction));
                instructionLabel->setStyleSheet(
                    "font-size:15px;color:#389e0d;padding:4px 0;background:transparent;");
                simpleTimer->start(3000);
            } else {
                directionLabel->setText(QStringLiteral("【 %1 】").arg(next.name));
                directionLabel->setStyleSheet(
                    "font-size:32px; font-weight:900; color:#ffffff; "
                    "background:#52c41a; border-radius:14px; padding:10px 20px;");
                instructionLabel->setText(QStringLiteral("✅ 第 %1/5 帧采集成功\n请准备【%2】\n%3")
                    .arg(captureCount).arg(next.name).arg(next.instruction));
                instructionLabel->setStyleSheet(
                    "font-size:15px;color:#389e0d;padding:4px 0;background:transparent;");
                postureStartTime = QDateTime::currentMSecsSinceEpoch();
                postureMatchCount = 0;
                postureFirstMatchTime = 0;
                postureFailCount = 0;
                postureRequestPending = false;
                postureHttpFailCount = 0;
                postureTimer->start();
            }
        } else {
            // 采集完毕
            isCapturing = false;
            stopTimers();
            directionLabel->setText(QStringLiteral("5方位采集完成"));
            directionLabel->setStyleSheet(
                "font-size:32px; font-weight:900; color:#ffffff; "
                "background:#52c41a; border-radius:14px; padding:10px 20px;");
            confirmBtn->setVisible(true);
            instructionLabel->setText(QStringLiteral("🎉 采集完成！最佳置信度: %1%\n请点击「确认保存」")
                .arg(QString::number(bestConfidence * 100, 'f', 1)));
            instructionLabel->setStyleSheet(
                "font-size:15px;font-weight:600;color:#389e0d;padding:4px 0;background:transparent;");
        }
    });

    connect(confirmBtn, &QPushButton::clicked, camera, [=, &bestDescriptor, &isCapturing]() {
        if (bestDescriptor.isEmpty() || userId <= 0) return;
        confirmBtn->setEnabled(false);
        isCapturing = false;
        instructionLabel->setText(QStringLiteral("正在保存人脸特征..."));
        instructionLabel->setStyleSheet("font-size:15px;color:#4da3ff;padding:4px 0;background:transparent;");
        FaceRecognitionService svc;
        if (svc.enrollFace(userId, bestDescriptor)) {
            statusLabel->setText(QStringLiteral("人脸特征采集成功！"));
            statusLabel->setStyleSheet("font-size:15px;font-weight:700;color:#389e0d;background:transparent;");
            camera->stopCamera();
            refresh();
            QTimer::singleShot(800, dlg, &QDialog::accept);
        } else {
            statusLabel->setText(QStringLiteral("人脸录入失败，请重试"));
            statusLabel->setStyleSheet("font-size:15px;font-weight:700;color:#ff4d4f;background:transparent;");
            confirmBtn->setEnabled(true);
        }
    });

    connect(dlg, &QDialog::finished, camera, [camera]() { camera->stopCamera(); });

    dlg->exec();
    dlg->deleteLater();
}

/** [2026-06-26v3] 已录入用户点击 → 弹出查看/清除人脸对话框 — 统一BaseDialog圆角风格 */
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
    clearBtn->setFixedHeight(48);
    clearBtn->setMinimumWidth(140);
    clearBtn->setCursor(Qt::PointingHandCursor);
    clearBtn->setStyleSheet(
        "QPushButton{background:#ff4d4f;color:white;border:none;border-radius:12px;"
        "font-size:15px;font-weight:600;}"
        "QPushButton:hover{background:#e04343;}");

    auto* closeBtn = new QPushButton(QStringLiteral("关闭"));
    closeBtn->setFixedHeight(48);
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

/** 开始人脸采集 */
void UserManagementPage::onStartFaceCapture() {
    // 已集成到 onFaceEnroll 中，此方法保留兼容性
}

/** 取消人脸采集 */
void UserManagementPage::onCancelFaceCapture() {
    if (m_faceDialog) {
        m_faceDialog->close();
    }
}

// [V7.0][2026-06-26] 角色按钮组样式更新 — 44px高统一弹窗内按钮风格
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
