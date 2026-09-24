/**
 * @file ToolBorrowPage.cpp
 * @brief 工具借用页面 — 任务类型多选、工具推荐、借用确认、位置分配
 * @author 袁燕
 */
#include "ToolBorrowPage.h"
#include "utils/StyleHelper.h"
#include "services/BorrowService.h"
#include "common/AppConfig.h"       // 获取本机机组ID
#include "common/Constants.h"       // 分页常量
#include "db/AlertDAO.h"            // 写入告警
#include "model/AlertLog.h"         // AlertLog实体
#include "db/ToolDAO.h"            // 查询机组名称和位置映射
#include "components/SoftKeyboard.h"
// [V1.00.9.1 架构修复] 移除db/ToolDAO直接引用，改为通过BorrowService访问数据 —— 作者：袁燕
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include "components/MessageDialog.h"
#include <QDateTime>
#include <QScrollArea>
#include <QFrame>
#include <QDebug>
#include <QTabWidget>
#include <QCheckBox>
#include <QDateTimeEdit>
#include <QSet>            // [2026-06-27] 任务类型默认勾选
#include <QSpinBox>         // [2026-06-27] 数量选择
#include <QStyle>           // [2026-06-27] style()->unpolish/polish 刷新QSS属性
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QTimer>          // [V2.02] 勾选跳页延迟重建，避免信号回调中销毁widget
#include <QApplication>    // [V2.03k] 退出系统
#include <QCoreApplication> // [V2.03k] QCoreApplication::quit

ToolBorrowPage::ToolBorrowPage(QWidget* parent) : QWidget(parent) {
    setupUI();
}

ToolBorrowPage::~ToolBorrowPage() = default;

void ToolBorrowPage::setUser(const QJsonObject& user) {
    m_user = user;
    refresh();
}

void ToolBorrowPage::setupUI() {
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    // ==================== 标签页导航 ====================
    m_tabWidget = new QTabWidget();
    m_tabWidget->setStyleSheet(QString(
        "QTabWidget::pane { border: none; background: transparent; }"
        "QTabBar::tab { background: #f0f2f5; color: #666; padding: 14px 28px; "
        "  font-size: 17px; font-weight: 600; border-radius: 12px 12px 0 0; "
        "  min-height: 48px; margin-right: 4px; }"
        "QTabBar::tab:selected { background: white; color: %1; border-bottom: 3px solid %1; }"
        "QTabBar::tab:hover { background: #e8f0fe; }"
    ).arg(StyleHelper::primaryColor()));

    // Tab1: 借用任务
    auto* borrowWidget = createBorrowTaskTab();
    m_tabWidget->addTab(borrowWidget, QStringLiteral("📤 借用任务"));

    // Tab2: 借用记录 [V7.6 2026-06-24] 删除"常用工具"选项卡，简化界面
    auto* recordWidget = createBorrowRecordTab();
    m_tabWidget->addTab(recordWidget, QStringLiteral("📋 借用记录"));

    outerLayout->addWidget(m_tabWidget);
}

QWidget* ToolBorrowPage::createBorrowTaskTab() {
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("border:none;");
    auto* mainWidget = new QWidget();
    scroll->setWidget(mainWidget);

    auto* mainLayout = new QVBoxLayout(mainWidget);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    // ────── 标题 ──────
    auto* title = new QLabel(QStringLiteral("工具借用"));
    title->setStyleSheet("font-size:20px;font-weight:700;color:#1a1a2e;");
    mainLayout->addWidget(title);

    // ────── 顶部信息栏 ──────
    auto* infoBar = new QFrame();
    infoBar->setStyleSheet(QString(
        "QFrame{ background:white; border-radius:12px; border:1px solid #e8ecf0; padding:12px 18px; }"
    ));
    auto* infoBarLayout = new QHBoxLayout(infoBar);
    infoBarLayout->setSpacing(20);
    infoBarLayout->setContentsMargins(0, 0, 0, 0);

    auto addInfoItem = [](QHBoxLayout* parent, const QString& label, QLabel*& valueLabel) {
        auto* w = new QWidget();
        w->setStyleSheet("background:transparent;");
        auto* l = new QHBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(6);
        auto* lb = new QLabel(label);
        lb->setStyleSheet("font-size:14px;color:#999;background:transparent;");
        valueLabel = new QLabel("--");
        valueLabel->setWordWrap(true);
        valueLabel->setStyleSheet("font-size:15px;color:#1a1a2e;font-weight:600;background:transparent;");
        l->addWidget(lb);
        l->addWidget(valueLabel);
        parent->addWidget(w);
    };

    addInfoItem(infoBarLayout, QStringLiteral("借用人"), m_userInfoLabel);
    addInfoItem(infoBarLayout, QStringLiteral("工号"), m_workNoLabel);
    addInfoItem(infoBarLayout, QStringLiteral("机组"), m_deptLabel);
    addInfoItem(infoBarLayout, QStringLiteral("日期"), m_dateLabel);
    m_dateLabel->setText(QDate::currentDate().toString("yyyy-MM-dd"));

    infoBarLayout->addStretch();
    mainLayout->addWidget(infoBar);

    // ────── 任务类型 + 任务单号 + 归还时间 卡片（合并一行）──────
    // [2026-06-27] 原两行布局在低分辨率下列表被挤压，合并为一行提升空间利用率
    auto* taskCard = new QFrame();
    taskCard->setStyleSheet(QString(
        "QFrame#taskCard{ background:white; border-radius:12px; border:1px solid #e8ecf0; padding:14px 18px; }"
    ));
    taskCard->setObjectName("taskCard");
    auto* taskCardLayout = new QHBoxLayout(taskCard);
    taskCardLayout->setSpacing(14);
    taskCardLayout->setContentsMargins(0, 0, 0, 0);

    // [2026-06-27] 任务类型：标签+按钮，按钮设最大宽度避免占满整行
    auto* typeTitle = new QLabel(QStringLiteral("任务类型"));
    typeTitle->setStyleSheet("font-size:15px;font-weight:600;color:#333;background:transparent;");
    taskCardLayout->addWidget(typeTitle);

    m_taskTypeBtn = new QPushButton(QStringLiteral("▼ 请选择任务类型"));
    m_taskTypeBtn->setStyleSheet(
        "QPushButton{ background:#f5f7fa; border:2px solid #d9d9d9; border-radius:10px; "
        "font-size:14px; padding:10px 14px; color:#666; text-align:left; min-height:44px; font-weight:500; }"
        "QPushButton:hover{ border-color:#1677ff; background:#e6f0ff; color:#1677ff; }"
        "QPushButton[selected=\"true\"]{ background:#e6f0ff; border-color:#1677ff; color:#1677ff; font-weight:600; }"
    );
    m_taskTypeBtn->setCursor(Qt::PointingHandCursor);
    m_taskTypeBtn->setMaximumWidth(220);  // [2026-06-27] 限制最大宽度，避免占满整行
    connect(m_taskTypeBtn, &QPushButton::clicked, this, &ToolBorrowPage::onTaskTypeBtnClicked);
    taskCardLayout->addWidget(m_taskTypeBtn);

    // [2026-06-27] 任务单号：紧凑显示在同一行
    auto* flowLabel = new QLabel(QStringLiteral("借用单号"));
    flowLabel->setStyleSheet("font-size:15px;font-weight:600;color:#333;background:transparent;");
    taskCardLayout->addWidget(flowLabel);
    m_flowNoDisplay = new QLabel(QStringLiteral("--"));
    m_flowNoDisplay->setStyleSheet(
        "font-size:15px;font-weight:bold;color:#1677ff;background:transparent;"
    );
    m_flowNoDisplay->setMinimumWidth(80);
    taskCardLayout->addWidget(m_flowNoDisplay);

    // 弹性间隔，把归还时间推到右侧
    taskCardLayout->addStretch();

    // [2026-06-27] 预计归还时间：紧凑显示在同一行右侧
    // 从AppConfig读取默认借用期限(小时)，自动计算归还时间
    auto* returnLabel = new QLabel(QStringLiteral("预计归还"));
    returnLabel->setStyleSheet("font-size:15px;font-weight:600;color:#333;background:transparent;");
    int defaultPeriodHours = AppConfig::instance().borrowDefaultPeriod();
    if (defaultPeriodHours <= 0) defaultPeriodHours = 168; // 默认7天=168小时
    QDateTime returnTime = QDateTime::currentDateTime().addSecs(defaultPeriodHours * 3600);
    m_returnTimeLabel = new QLabel(returnTime.toString("yyyy-MM-dd HH:mm"));
    m_returnTimeLabel->setStyleSheet(
        "font-size:15px;font-weight:600;color:#1677ff;background:#f6f9ff;"
        "border:1px solid #d6e4ff;border-radius:10px;padding:10px 16px;"
    );
    m_returnTimeLabel->setMinimumHeight(44);
    taskCardLayout->addWidget(returnLabel);
    taskCardLayout->addWidget(m_returnTimeLabel);

    mainLayout->addWidget(taskCard);

    // ────── 推荐提示条 ──────
    m_recommendHint = new QLabel(QStringLiteral("💡 请先选择任务类型以查看推荐工具"));
    m_recommendHint->setWordWrap(true);
    m_recommendHint->setMinimumHeight(40);
    m_recommendHint->setStyleSheet(QString(
        "QLabel{ background:#e6f7ff; border:1px solid #91caff; border-radius:10px; "
        "font-size:15px; color:#1677ff; padding:10px 16px; font-weight:500; }"
    ));
    mainLayout->addWidget(m_recommendHint);

    // [2026-06-27] 去掉工具搜索行（标签+搜索框+按钮），让界面更简洁
    // 保留 m_toolSearchEdit / m_toolSearchBtn 成员变量为 nullptr 避免引用错误
    m_toolSearchEdit = nullptr;
    m_toolSearchBtn = nullptr;

    // ────── 统一工具表格（推荐工具+全部工具合并为一张表）──────
    m_allToolTable = new QTableWidget();
    m_allToolTable->setColumnCount(8);
    m_allToolTable->setHorizontalHeaderLabels({
        QStringLiteral("选择"), QStringLiteral("工具编号"), QStringLiteral("工具名称"),
        QStringLiteral("规格"), QStringLiteral("数量"), QStringLiteral("单位"),
        QStringLiteral("状态"), QStringLiteral("操作")
    });
    m_allToolTable->horizontalHeader()->setStretchLastSection(false);
    m_allToolTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_allToolTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_allToolTable->verticalHeader()->setVisible(false);
    m_allToolTable->setAlternatingRowColors(false);
    // [V2.02 2026-06-28] 关闭AutoScroll，修复鼠标滑动列表时页面自动滚动的问题
    //   作者：袁燕 — Qt默认鼠标移到边缘自动滚动，触屏设备体验差
    m_allToolTable->setAutoScroll(false);
    // [V2.02 2026-06-28] 移除内联表格QSS，使用全局QSS统一表格样式（小米设计语言）
    // [2026-06-27] 选择列Fixed窄宽(60px)，其他数据列Stretch均分
    m_allToolTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_allToolTable->setColumnWidth(0, 60);
    for (int c = 1; c < 7; c++) {
        m_allToolTable->horizontalHeader()->setSectionResizeMode(c, QHeaderView::Stretch);
    }
    m_allToolTable->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Fixed);
    m_allToolTable->setColumnWidth(7, 130);
    m_allToolTable->horizontalHeader()->setMinimumSectionSize(50);
    connect(m_allToolTable, &QTableWidget::cellClicked, this, &ToolBorrowPage::onToolSelected);
    mainLayout->addWidget(m_allToolTable, 1);

    // ────── 分页栏 ──────
    {
        auto* toolPageRow = new QHBoxLayout();
        toolPageRow->setContentsMargins(0, 8, 0, 0);
        toolPageRow->setSpacing(6);

        m_toolTotalLabel = new QLabel(QStringLiteral("共 0 条"));
        m_toolTotalLabel->setStyleSheet("font-size:13px;color:#999;");

        m_toolPrevBtn = new QPushButton(QStringLiteral("上一页"));
        m_toolPrevBtn->setStyleSheet(
            "QPushButton{border:1px solid #ddd;border-radius:6px;padding:5px 12px;"
            "font-size:13px;font-weight:600;color:#555;background:#fff;min-height:30px;}"
            "QPushButton:hover{border-color:#4da3ff;color:#4da3ff;}"
            "QPushButton:disabled{opacity:0.35;}"
        );
        m_toolPrevBtn->setCursor(Qt::PointingHandCursor);
        connect(m_toolPrevBtn, &QPushButton::clicked, this, &ToolBorrowPage::onToolPrevPage);

        m_toolNextBtn = new QPushButton(QStringLiteral("下一页"));
        m_toolNextBtn->setStyleSheet(
            "QPushButton{border:1px solid #ddd;border-radius:6px;padding:5px 12px;"
            "font-size:13px;font-weight:600;color:#555;background:#fff;min-height:30px;}"
            "QPushButton:hover{border-color:#4da3ff;color:#4da3ff;}"
            "QPushButton:disabled{opacity:0.35;}"
        );
        m_toolNextBtn->setCursor(Qt::PointingHandCursor);
        connect(m_toolNextBtn, &QPushButton::clicked, this, &ToolBorrowPage::onToolNextPage);

        m_toolPageLabel = new QLabel(QStringLiteral("第 1 页"));
        m_toolPageLabel->setStyleSheet("font-size:13px;color:#999;padding:0 4px;");

        toolPageRow->addStretch();
        toolPageRow->addWidget(m_toolPrevBtn);
        toolPageRow->addWidget(m_toolPageLabel);
        toolPageRow->addWidget(m_toolNextBtn);
        toolPageRow->addWidget(m_toolTotalLabel);
        mainLayout->addLayout(toolPageRow);
    }

    // ────── 底部操作栏 ──────
    auto* bottomBarFrame = new QFrame();
    bottomBarFrame->setStyleSheet(QString(
        "QFrame{ background:white; border-top:2px solid #e8ecf0; padding:12px 20px; }"
    ));
    auto* bottomLayout = new QHBoxLayout(bottomBarFrame);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->addStretch();

    m_borrowBtn = new QPushButton(QStringLiteral("确认借用(共0件)"));
    m_borrowBtn->setStyleSheet(QString(
        "QPushButton{ background:#4da3ff;color:white;border:none;border-radius:12px;"
        "padding:12px 36px;font-size:17px;font-weight:600;min-height:50px;}"
        "QPushButton:hover{background:#3d8ae0;}"
        "QPushButton:pressed{background:#2e7ad6;}"
    ));
    m_borrowBtn->setCursor(Qt::PointingHandCursor);
    connect(m_borrowBtn, &QPushButton::clicked, this, &ToolBorrowPage::onBorrowConfirm);
    bottomLayout->addWidget(m_borrowBtn);

    mainLayout->addWidget(bottomBarFrame);
    mainLayout->addStretch();

    // ────── 任务类型下拉面板（popup）──────
    // [2026-06-27 修复] QFrame→QDialog+Popup，解决checkbox选不中致命Bug
    // 参考 UserManagementPage 部门多选弹出面板的成功实现
    m_taskTypePopup = new QDialog(this);
    m_taskTypePopup->setWindowFlags(Qt::FramelessWindowHint | Qt::Popup);
    m_taskTypePopup->setModal(false);
    m_taskTypePopup->setFixedWidth(360);
    m_taskTypePopup->setStyleSheet(
        "QDialog{ background:white; border:2px solid #e0e0e0; border-radius:12px; }"
        "QLineEdit{ height:48px; padding:0 16px; border:none; border-bottom:1px solid #f0f0f0;"
        "  font-size:16px; color:#333; background:transparent; }"
        "QCheckBox{ font-size:16px; padding:14px 18px; color:#333; font-weight:500; spacing:12px; "
        "  border-bottom:1px solid #f7f7f7; }"
        "QCheckBox:hover{ background:#f8fbff; }"
        "QCheckBox::indicator{ width:22px; height:22px; border-radius:4px; "
        "  border:2px solid #d0d0d0; background:white; }"
        "QCheckBox::indicator:hover{ border-color:#4da3ff; }"
        "QCheckBox::indicator:checked{ background:#4da3ff; border-color:#4da3ff; }"
        "QPushButton{ min-height:48px; font-size:16px; border-radius:10px; }"
    );
    m_taskTypePopup->hide();

    auto* popupLayout = new QVBoxLayout(m_taskTypePopup);
    popupLayout->setContentsMargins(0, 0, 0, 0);
    popupLayout->setSpacing(0);

    m_taskTypeSearch = new QLineEdit();
    m_taskTypeSearch->setPlaceholderText(QStringLiteral("搜索任务类型..."));
    m_taskTypeSearch->setStyleSheet("QLineEdit{ height:48px; padding:0 12px; border:none; border-bottom:1px solid #eee; font-size:16px; }");
    connect(m_taskTypeSearch, &QLineEdit::textChanged, this, &ToolBorrowPage::onFilterTaskTypes);
    popupLayout->addWidget(m_taskTypeSearch);

    auto* taskScrollArea = new QScrollArea();
    taskScrollArea->setWidgetResizable(true);
    taskScrollArea->setStyleSheet("border:none; background:white;");
    taskScrollArea->setMaximumHeight(320);
    auto* taskListWidget = new QWidget();
    taskListWidget->setStyleSheet("background:white;");
    m_taskTypeListLayout = new QVBoxLayout(taskListWidget);
    m_taskTypeListLayout->setContentsMargins(0, 6, 0, 6);
    m_taskTypeListLayout->setSpacing(0);
    taskScrollArea->setWidget(taskListWidget);
    popupLayout->addWidget(taskScrollArea, 1);

    // [2026-06-27] 底部按钮：取消（清空全部勾选）+ 确定
    auto* btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(12, 8, 12, 12);
    btnRow->setSpacing(10);

    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    cancelBtn->setStyleSheet(
        "QPushButton{ background:#fff; color:#666; border:1px solid #d9d9d9; min-height:44px; "
        "font-size:16px; font-weight:600; border-radius:10px; }"
        "QPushButton:hover{ background:#f5f5f5; border-color:#aaa; }"
    );
    cancelBtn->setCursor(Qt::PointingHandCursor);
    // [2026-06-27] 取消=清空全部勾选+关闭面板
    connect(cancelBtn, &QPushButton::clicked, this, [this]() {
        for (auto& pair : m_taskTypeCheckBoxes) {
            pair.second->setChecked(false);
        }
        m_taskTypePopup->hide();
    });
    btnRow->addWidget(cancelBtn);

    // [2026-06-27] 确定按钮颜色统一为蓝色实底#4da3ff，与底部"确认借用"按钮一致
    auto* confirmBtn = new QPushButton(QStringLiteral("确定"));
    confirmBtn->setStyleSheet(
        "QPushButton{ background:#4da3ff;color:white;border:none;min-height:44px; "
        "font-size:16px;font-weight:600;border-radius:10px; }"
        "QPushButton:hover{ background:#3d8ae0; }"
        "QPushButton:pressed{ background:#2e7ad6; }"
    );
    confirmBtn->setCursor(Qt::PointingHandCursor);
    connect(confirmBtn, &QPushButton::clicked, this, &ToolBorrowPage::onConfirmTaskTypes);
    btnRow->addWidget(confirmBtn);

    auto* btnContainer = new QWidget();
    btnContainer->setStyleSheet("background:white;");
    btnContainer->setLayout(btnRow);
    popupLayout->addWidget(btnContainer);

    // [2026-06-27] 不再创建独立的推荐表格，全部工具统一在 m_allToolTable 中显示
    // m_recommendTable 保留但不再用于显示（保持头文件兼容性）
    m_recommendTable = nullptr;

    return scroll;
}

QWidget* ToolBorrowPage::createBorrowRecordTab() {
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(24, 24, 24, 24);

    auto* title = new QLabel(QStringLiteral("借用记录"));  // [V2.04] 显示全部借用记录
    title->setStyleSheet(QString("font-size:20px;font-weight:bold;color:%1;").arg(StyleHelper::textColor()));
    layout->addWidget(title);

    m_recordTable = new QTableWidget();
    m_recordTable->setColumnCount(5);
    m_recordTable->setHorizontalHeaderLabels({
        QStringLiteral("借用时间"), QStringLiteral("工具名称"), QStringLiteral("位置"),
        QStringLiteral("状态"), QStringLiteral("操作")
    });
    // [2026-06-27] 禁用表格编辑，所有列只读
    m_recordTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // [V2.02 2026-06-28] 移除内联表格QSS，使用全局QSS统一表格样式
    // [V8.2 2026-06-25] 数据列Stretch均分，操作列Fixed紧凑（触屏按钮~130px）
    // 列：借用时间 工具名称 位置 状态 操作(idx4)
    // 借用数量改为位置（以借用位置维度显示，同一工具可多位置借用）
    for (int i = 0; i < 4; i++) {
        m_recordTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
    }
    m_recordTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_recordTable->setColumnWidth(4, 130);
    m_recordTable->horizontalHeader()->setStretchLastSection(false);
    m_recordTable->horizontalHeader()->setMinimumSectionSize(60);
    layout->addWidget(m_recordTable, 1);

    // [2026-06-25] 借用记录表分页栏
    {
        auto* recordPageRow = new QHBoxLayout();
        recordPageRow->setContentsMargins(0, 8, 0, 0);
        recordPageRow->setSpacing(6);

        m_recordTotalLabel = new QLabel(QStringLiteral("共 0 条"));
        m_recordTotalLabel->setStyleSheet("font-size:13px;color:#999;");

        m_recordPrevBtn = new QPushButton(QStringLiteral("上一页"));
        m_recordPrevBtn->setStyleSheet(
            "QPushButton{border:1px solid #ddd;border-radius:6px;padding:5px 12px;"
            "font-size:13px;font-weight:600;color:#555;background:#fff;min-height:30px;}"
            "QPushButton:hover{border-color:#4da3ff;color:#4da3ff;}"
            "QPushButton:disabled{opacity:0.35;}"
        );
        m_recordPrevBtn->setCursor(Qt::PointingHandCursor);
        connect(m_recordPrevBtn, &QPushButton::clicked, this, &ToolBorrowPage::onRecordPrevPage);

        m_recordNextBtn = new QPushButton(QStringLiteral("下一页"));
        m_recordNextBtn->setStyleSheet(
            "QPushButton{border:1px solid #ddd;border-radius:6px;padding:5px 12px;"
            "font-size:13px;font-weight:600;color:#555;background:#fff;min-height:30px;}"
            "QPushButton:hover{border-color:#4da3ff;color:#4da3ff;}"
            "QPushButton:disabled{opacity:0.35;}"
        );
        m_recordNextBtn->setCursor(Qt::PointingHandCursor);
        connect(m_recordNextBtn, &QPushButton::clicked, this, &ToolBorrowPage::onRecordNextPage);

        m_recordPageLabel = new QLabel(QStringLiteral("第 1 页"));
        m_recordPageLabel->setStyleSheet("font-size:13px;color:#999;padding:0 4px;");

        recordPageRow->addStretch();
        recordPageRow->addWidget(m_recordPrevBtn);
        recordPageRow->addWidget(m_recordPageLabel);
        recordPageRow->addWidget(m_recordNextBtn);
        recordPageRow->addWidget(m_recordTotalLabel);
        layout->addLayout(recordPageRow);
    }

    return widget;
}

void ToolBorrowPage::refresh() {
    // [2026-06-27] 切换到本页面时默认显示"借用任务"选项卡
    if (m_tabWidget) m_tabWidget->setCurrentIndex(0);
    // [2026-06-27] 切换账号时重置所有借用状态，避免残留上个账号的数据
    m_selectedTools.clear();
    m_selectedToolIds.clear();  // [V2.12] QSet同步清空
    m_recommendedToolIds.clear();
    m_selectedTypeIds.clear();
    m_flowNo.clear();
    m_toolCurrentPage = 1;
    // 更新顶部信息栏
    m_userInfoLabel->setText(m_user["realName"].toString());
    m_workNoLabel->setText(m_user["workNo"].toString());
    // [2026-06-26] 修复：所属机组应显示本机机组名，而非用户部门
    int groupId = AppConfig::instance().localMachineGroupId();
    if (groupId > 0) {
        db::ToolDAO dao;
        QJsonObject mg = dao.getMachineGroupById(groupId);
        m_deptLabel->setText(mg["groupName"].toString("--"));
    } else {
        m_deptLabel->setText(QStringLiteral("未配置"));
    }
    m_dateLabel->setText(QDate::currentDate().toString("yyyy-MM-dd"));

    // [2026-06-27] 流水号在用户选择任务类型后才生成，默认显示--
    m_flowNoDisplay->setText("--");

    // [2026-06-27] 性能优化：任务类型列表仅首次加载或列表为空时重建（避免每次切换页面重复DB查询）
    if (m_taskTypeCheckBoxes.isEmpty()) {
        loadTaskTypes();
    } else {
        // 已加载过任务类型，仅重置选中状态
        m_selectedTypeIds.clear();
        m_selectedTypeNames.clear();
        for (auto& pair : m_taskTypeCheckBoxes) {
            pair.second->setChecked(false);
        }
        m_taskTypeBtn->setText(QStringLiteral("▼ 请选择任务类型"));
        m_taskTypeBtn->setProperty("selected", false);
        m_taskTypeBtn->style()->unpolish(m_taskTypeBtn);
        m_taskTypeBtn->style()->polish(m_taskTypeBtn);
        m_recommendHint->setText(QStringLiteral("💡 请先选择任务类型以查看推荐工具"));
        m_borrowBtn->setText(QStringLiteral("确认借用(共0件)"));
        m_selectedTools.clear();
        m_selectedToolIds.clear();  // [V2.12] QSet同步
        m_recommendedToolIds.clear();
        m_toolCurrentPage = 1;
        refreshAllToolTable();
    }
    loadAllTools();
    loadBorrowRecords();
}

void ToolBorrowPage::loadTaskTypes() {
    BorrowService svc;
    QJsonArray types = svc.getTaskTypes();
    
    // 清除旧复选框
    QLayoutItem* item;
    while ((item = m_taskTypeListLayout->takeAt(0)) != nullptr) {
        if (item->widget()) { delete item->widget(); }
        delete item;
    }
    m_taskTypeCheckBoxes.clear();
    m_selectedTypeIds.clear();

    // [2026-06-27] 默认不勾选任何任务类型，由用户主动选择
    QSet<int> defaultCheckedIds;

    for (int i = 0; i < types.size(); ++i) {
        QJsonObject t = types[i].toObject();
        QString typeName = t["typeName"].toString();
        int typeId = t["typeId"].toInt();
        QString typeCode = t["typeCode"].toString();
        
        auto* checkBox = new QCheckBox(typeName);
        checkBox->setProperty("typeId", typeId);
        checkBox->setProperty("typeCode", typeCode);  // [2026-06-27] 存储typeCode用于流水号
        checkBox->setStyleSheet("font-size:16px; padding:8px;");
        // [2026-06-27] 默认不勾选
        if (defaultCheckedIds.contains(typeId)) {
            checkBox->setChecked(true);
            m_selectedTypeIds.append(typeId);
        }
        connect(checkBox, &QCheckBox::toggled, this, [this, typeId](bool checked) {
            if (checked) {
                if (!m_selectedTypeIds.contains(typeId))
                    m_selectedTypeIds.append(typeId);
            } else {
                m_selectedTypeIds.removeAll(typeId);
            }
        });
        
        m_taskTypeCheckBoxes.append(qMakePair(typeName, checkBox));
        m_taskTypeListLayout->addWidget(checkBox);
    }
    
    m_taskTypeListLayout->addStretch();

    // [2026-06-27] 默认不勾选，按钮显示提示文本，重置所有状态
    m_taskTypeBtn->setText(QStringLiteral("▼ 请选择任务类型"));
    m_taskTypeBtn->setProperty("selected", false);
    m_taskTypeBtn->style()->unpolish(m_taskTypeBtn);
    m_taskTypeBtn->style()->polish(m_taskTypeBtn);
    m_flowNoDisplay->setText("--");
    m_recommendHint->setText(QStringLiteral("💡 请先选择任务类型以查看推荐工具"));
    m_borrowBtn->setText(QStringLiteral("确认借用(共0件)"));
    // 重置选中工具和推荐列表
    m_selectedTools.clear();
    m_selectedToolIds.clear();  // [V2.12] QSet同步
    m_recommendedToolIds.clear();
    m_toolCurrentPage = 1;
    refreshAllToolTable();
}

void ToolBorrowPage::loadRecommendedTools() {
    m_recommendedToolIds.clear();

    if (m_selectedTypeIds.isEmpty()) {
        m_recommendHint->setText(QStringLiteral("💡 请先选择任务类型以查看推荐工具"));
        refreshAllToolTable();
        return;
    }

    BorrowService svc;
    int groupId = AppConfig::instance().localMachineGroupId();
    QJsonArray tools = svc.getRecommendedTools(m_selectedTypeIds, groupId);

    // [2026-06-27] 合并推荐工具到统一表格：推荐工具默认勾选
    // 按工具种类选中，用toolId，stock=availableQty
    m_selectedTools.clear();
    m_selectedToolIds.clear();
    for (int i = 0; i < tools.size(); ++i) {
        QJsonObject t = tools[i].toObject();
        int toolId = t["toolId"].toInt();
        int availableQty = t["availableQty"].toInt();
        m_recommendedToolIds.insert(toolId);
        m_selectedToolIds.insert(toolId);

        SelectedToolInfo info;
        info.toolId = toolId;
        info.toolName = t["toolName"].toString();
        info.toolCode = t["toolCode"].toString();
        info.position = t["position"].toString();
        info.stock = availableQty;  // [V2.12] 可用位置数
        // 推荐借用数量不超过可用库存
        info.quantity = t.contains("recommendedQty") ? t["recommendedQty"].toInt() : 1;
        if (info.quantity < 1) info.quantity = 1;
        if (info.quantity > availableQty) info.quantity = availableQty;
        m_selectedTools.append(info);
    }

    // 构建推荐提示文本
    QStringList typeNames;
    for (const auto& pair : m_taskTypeCheckBoxes) {
        if (pair.second->isChecked()) typeNames.append(pair.first);
    }
    QString typeNameStr = typeNames.isEmpty() ? QStringLiteral("任务") : typeNames.join(QStringLiteral("、"));
    m_recommendHint->setText(QStringLiteral("💡 已为【%1】智能推荐 %2 件工具").arg(typeNameStr).arg(tools.size()));

    // [2026-06-27] 总件数 = quantity之和
    int totalQty = 0;
    for (const auto& s : m_selectedTools) totalQty += s.quantity;
    m_borrowBtn->setText(QStringLiteral("确认借用(共%1件)").arg(totalQty));
    m_toolCurrentPage = 1;
    refreshAllToolTable();
}

void ToolBorrowPage::loadAllTools() {
    refreshAllToolTable();
}

// [V7.4 2026-06-24] 刷新全部工具表格：推荐工具排最前面 + "已添加"标记
// [2026-06-25] 添加服务端分页
// [2026-06-27] 重构：8列布局 + 库存totalQty/currentQty + 状态列 + 操作列
// [2026-06-27] 改为客户端全量加载+排序+分页，支持已选工具自动跳首页
// [V2.02 2026-06-28] 拆分：查DB+缓存 → rebuildToolTableFromCache（排序+分页+填充）
//   作者：袁燕 — 勾选跳页时用rebuildToolTableFromCache不查DB，效率高
void ToolBorrowPage::refreshAllToolTable() {
    // 一次性加载本机组全部在库工具，缓存供后续跳页使用
    BorrowService svc;
    int groupId = AppConfig::instance().localMachineGroupId();
    QJsonObject result = svc.getAllInStockTools(1, SC::PAGE_SIZE_UNLIMITED, groupId);
    m_allToolsCache = result["list"].toArray();
    // 从缓存重建表格（排序+分页+填充行）
    rebuildToolTableFromCache();
}

// [V2.02 2026-06-28] 从缓存重建工具表格 — 不查DB，仅排序+分页+填充行
//   作者：袁燕 — 供勾选/添加按钮跳页使用，O(n)排序效率
void ToolBorrowPage::rebuildToolTableFromCache() {
    // [V8.0] 性能优化：禁用重绘+信号阻塞，避免重建过程中频繁刷新
    m_allToolTable->setUpdatesEnabled(false);
    m_allToolTable->blockSignals(true);
    QJsonArray allList = m_allToolsCache;
    m_toolTotalRecords = allList.size();

    int totalPages = (m_toolTotalRecords + m_toolPageSize - 1) / m_toolPageSize;
    if (totalPages == 0) totalPages = 1;
    if (m_toolCurrentPage > totalPages) m_toolCurrentPage = totalPages;
    if (m_toolCurrentPage < 1) m_toolCurrentPage = 1;
    m_toolPageLabel->setText(QStringLiteral("第 %1/%2 页").arg(m_toolCurrentPage).arg(totalPages));
    m_toolTotalLabel->setText(QStringLiteral("共 %1 条").arg(m_toolTotalRecords));
    m_toolPrevBtn->setEnabled(m_toolCurrentPage > 1);
    m_toolNextBtn->setEnabled(m_toolCurrentPage < totalPages);

    // [2026-06-27] 第一步：对全量数据做三类排序（先排序，再分页）
    // 排序顺序：1.推荐工具(自动勾选) → 2.用户手动勾选的工具 → 3.普通未勾选工具
    QJsonArray recommendedList;
    QJsonArray selectedList;     // 用户手动勾选的非推荐工具
    QJsonArray normalList;
    for (int i = 0; i < allList.size(); ++i) {
        QJsonObject t = allList[i].toObject();
        // 选中/推荐标识用toolId（按工具种类）
        int toolId = t["toolId"].toInt();
        bool isRecommended = m_recommendedToolIds.contains(toolId);
        bool isSelected = m_selectedToolIds.contains(toolId);
        if (isRecommended) {
            recommendedList.append(t);
        } else if (isSelected) {
            selectedList.append(t);
        } else {
            normalList.append(t);
        }
    }

    // 合并为排序后的完整列表
    QJsonArray sortedList;
    for (const auto& t : recommendedList) sortedList.append(t);
    for (const auto& t : selectedList) sortedList.append(t);
    for (const auto& t : normalList) sortedList.append(t);

    // [2026-06-27] 第二步：对排序后的数据做分页切片
    int startIdx = (m_toolCurrentPage - 1) * m_toolPageSize;
    int endIdx = qMin(startIdx + m_toolPageSize, sortedList.size());
    QJsonArray list;
    for (int i = startIdx; i < endIdx; ++i) {
        list.append(sortedList[i]);
    }

    int totalRows = list.size();
    m_allToolTable->setRowCount(totalRows);

    auto populateRow = [this](int row, const QJsonObject& t, bool isRecommended) {
        int toolId = t["toolId"].toInt();
        QString toolName = t["toolName"].toString();
        int currentQty = t["currentQty"].toInt();
        int totalQty = t["totalQty"].toInt();
        // 按工具种类选中，用toolId，availableQty=可用库存数
        int availableQty = t["availableQty"].toInt();
        QString unit = t["unit"].toString().isEmpty() ? QStringLiteral("件") : t["unit"].toString();
        // [2026-06-27] 推荐行浅蓝底，对齐 #4da3ff 蓝色主调
        QString rowBg = isRecommended ? QStringLiteral("#e6f4ff") : QStringLiteral("white");

        // 判断是否已选中及数量
        bool isSelected = m_selectedToolIds.contains(toolId);
        int savedQty = 1;
        if (isSelected) {
            for (const auto& sel : m_selectedTools) {
                if (sel.toolId == toolId) { savedQty = sel.quantity; break; }
            }
        }

        // Col 0: 选择复选框
        // [2026-06-27] checkbox选中整体填充蓝色（非边框），圆角4px对齐StyleHelper::tableCheckBox
        auto* checkBox = new QCheckBox();
        checkBox->setStyleSheet(
            "QCheckBox { background: transparent; }"
            "QCheckBox::indicator { width: 22px; height: 22px; border-radius: 4px; "
            "  border: 2px solid #d0d0d0; background: white; }"
            "QCheckBox::indicator:hover { border-color: #4da3ff; }"
            "QCheckBox::indicator:checked { background: #4da3ff; border-color: #4da3ff; }"
        );
        checkBox->setChecked(isSelected);

        // [2026-06-27] 辅助函数：计算当前已选总件数
        auto calcTotalSelected = [this]() {
            int total = 0;
            for (const auto& s : m_selectedTools) total += s.quantity;
            return total;
        };

        // [2026-06-27] 辅助函数：刷新底部按钮"共X件"
        auto refreshBorrowBtn = [this, calcTotalSelected]() {
            m_borrowBtn->setText(QStringLiteral("确认借用(共%1件)").arg(calcTotalSelected()));
        };

        // Col 4: 数量下拉框（1/availableQty, 2/availableQty...格式）
        // 数量上限改为availableQty（可用位置数）
        auto* qtyCombo = new QComboBox();
        int maxQty = qMax(1, availableQty);
        for (int n = 1; n <= maxQty; ++n) {
            qtyCombo->addItem(QStringLiteral("%1/%2").arg(n).arg(maxQty), n);
        }
        qtyCombo->setCurrentIndex(isSelected ? (savedQty - 1) : 0);
        qtyCombo->setStyleSheet(QString(
            "QComboBox{border:1px solid #d9d9d9;border-radius:8px;padding:6px 12px;"
            "font-size:14px;min-height:36px;min-width:90px;background:white;color:#333;font-weight:500;}"
            "QComboBox:hover{border-color:#4da3ff;}"
            "QComboBox::drop-down{width:24px;border:none;}"
            "QComboBox QAbstractItemView{background:#fff;border:1px solid #d9d9d9;"
            "selection-background-color:#4da3ff;selection-color:#fff;font-size:14px;padding:4px;}"
        ));
        // 数量变化时更新选中列表
        connect(qtyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, toolId, qtyCombo, refreshBorrowBtn](int idx) {
                int qty = qtyCombo->itemData(idx).toInt();
                for (auto& sel : m_selectedTools) {
                    if (sel.toolId == toolId) { sel.quantity = qty; break; }
                }
                refreshBorrowBtn();
            });

        // checkbox toggle 回调
        // 按工具种类选中，用toolId
        QString toolCode = t["toolCode"].toString();
        QString position = t["position"].toString();
        connect(checkBox, &QCheckBox::toggled, this, [this, toolId, toolName, toolCode, position, availableQty, qtyCombo, refreshBorrowBtn](bool checked) {
            int qty = qtyCombo->currentData().toInt();
            if (checked) {
                m_selectedToolIds.insert(toolId);
                bool found = false;
                for (auto& sel : m_selectedTools) {
                    if (sel.toolId == toolId) { sel.quantity = qty; found = true; break; }
                }
                if (!found) {
                    SelectedToolInfo info;
                    info.toolId = toolId;
                    info.toolName = toolName;
                    info.toolCode = toolCode;
                    info.position = position;
                    info.stock = availableQty;
                    info.quantity = qty;
                    m_selectedTools.append(info);
                }
            } else {
                m_selectedToolIds.remove(toolId);
                m_selectedTools.erase(
                    std::remove_if(m_selectedTools.begin(), m_selectedTools.end(),
                        [toolId](const SelectedToolInfo& s) { return s.toolId == toolId; }),
                    m_selectedTools.end());
            }
            refreshBorrowBtn();
            // [V2.02 2026-06-28] 恢复跳首页：已选工具排序靠前，跳到第一页可见
            // 用QTimer延迟执行，避免在toggled信号回调中同步销毁发出信号的checkbox
            // 用rebuildToolTableFromCache从缓存重建，不查DB效率高
            m_toolCurrentPage = 1;
            QTimer::singleShot(0, this, [this]() { rebuildToolTableFromCache(); });
        });
        m_allToolTable->setCellWidget(row, 0, checkBox);

        // Col 1: 工具编号
        m_allToolTable->setItem(row, 1, new QTableWidgetItem(t["toolCode"].toString()));

        // Col 2: 工具名称 + 推荐标识
        {
            auto* nameWidget = new QWidget();
            nameWidget->setStyleSheet(QString("background:%1;").arg(rowBg));
            auto* nameLayout = new QHBoxLayout(nameWidget);
            nameLayout->setContentsMargins(4, 0, 4, 0);
            nameLayout->setSpacing(6);
            auto* nameLabel = new QLabel(toolName);
            nameLabel->setStyleSheet("font-size:15px;color:#1a1a2e;font-weight:600;background:transparent;");
            nameLayout->addWidget(nameLabel);
            if (isRecommended) {
                auto* badge = new QLabel(QStringLiteral("推荐"));
                badge->setStyleSheet(
                    "background:#4da3ff;color:#fff;font-size:11px;"
                    "padding:3px 10px;border-radius:6px;font-weight:600;"
                );
                nameLayout->addWidget(badge);
            }
            nameLayout->addStretch();
            m_allToolTable->setCellWidget(row, 2, nameWidget);
        }

        // Col 3: 规格
        m_allToolTable->setItem(row, 3, new QTableWidgetItem(t["spec"].toString()));

        // Col 4: 数量（QComboBox 已创建）
        m_allToolTable->setCellWidget(row, 4, qtyCombo);

        // Col 5: 单位
        m_allToolTable->setItem(row, 5, new QTableWidgetItem(unit));

        // Col 6: 状态
        {
            auto* statusItem = new QTableWidgetItem(QStringLiteral("在库"));
            statusItem->setForeground(QColor("#43a047"));
            QFont f = statusItem->font();
            f.setBold(true);
            statusItem->setFont(f);
            m_allToolTable->setItem(row, 6, statusItem);
        }

        // 推荐工具行高亮
        if (isRecommended) {
            for (int col = 0; col < 8; col++) {
                auto* item = m_allToolTable->item(row, col);
                if (item) item->setBackground(QColor(rowBg));
            }
        }

        // Col 7: 操作按钮 — [2026-06-27] 配色对齐人员管理蓝色 #4da3ff，缩小尺寸
        {
            QPushButton* opBtn;
            if (isSelected) {
                // 已添加：浅蓝底+蓝字
                opBtn = new QPushButton(QStringLiteral("✓ 已添加"));
                opBtn->setEnabled(false);
                opBtn->setStyleSheet(
                    "QPushButton{background:#e6f4ff;color:#4da3ff;border:1px solid #91caff;"
                    "border-radius:6px;font-size:12px;font-weight:600;padding:5px 12px;}"
                    "QPushButton:disabled{color:#91caff;}"
                );
            } else {
                // 添加：纯蓝实底
                opBtn = new QPushButton(QStringLiteral("＋ 添加"));
                opBtn->setCursor(Qt::PointingHandCursor);
                opBtn->setStyleSheet(
                    "QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:6px;"
                    "font-size:12px;font-weight:600;padding:5px 12px;}"
                    "QPushButton:hover{background:#3d8ae0;}"
                    "QPushButton:pressed{background:#2e7ad6;}"
                );
                connect(opBtn, &QPushButton::clicked, this, [this, toolId, toolName, toolCode, position, availableQty, qtyCombo, refreshBorrowBtn](bool) {
                    int qty = qtyCombo->currentData().toInt();
                    m_selectedToolIds.insert(toolId);  // [V2.12] QSet同步
                    bool found = false;
                    for (auto& sel : m_selectedTools) {
                        if (sel.toolId == toolId) { sel.quantity = qty; found = true; break; }
                    }
                    if (!found) {
                        SelectedToolInfo info;
                        info.toolId = toolId;
                        info.toolName = toolName;
                        info.toolCode = toolCode;
                        info.position = position;
                        info.stock = availableQty;
                        info.quantity = qty;
                        m_selectedTools.append(info);
                    }
                    refreshBorrowBtn();
                    // [V2.02 2026-06-28] 恢复跳首页：已选工具排序靠前，跳到第一页可见
                    // 从缓存重建不查DB，QTimer延迟避免信号回调中销毁widget
                    m_toolCurrentPage = 1;
                    QTimer::singleShot(0, this, [this]() { rebuildToolTableFromCache(); });
                });
            }
            opBtn->setMinimumHeight(40);
            m_allToolTable->setCellWidget(row, 7, opBtn);
        }

        // [2026-06-27] 行高加大到64px，确保操作按钮完整显示
        m_allToolTable->setRowHeight(row, 64);
    };

    // [2026-06-27] 填充当前页数据（已排序+分页后的 list）
    for (int i = 0; i < list.size(); ++i) {
        QJsonObject t = list[i].toObject();
        int toolId = t["toolId"].toInt();  // [V2.12] 用toolId判断推荐
        bool isRecommended = m_recommendedToolIds.contains(toolId);
        populateRow(i, t, isRecommended);
    }

    // 更新底部按钮 — [2026-06-27] 总件数 = 所有已选工具 quantity 之和
    int totalQty = 0;
    for (const auto& s : m_selectedTools) totalQty += s.quantity;
    m_borrowBtn->setText(QStringLiteral("确认借用(共%1件)").arg(totalQty));

    // [V8.0 2026-06-28] 恢复表格重绘和信号
    m_allToolTable->blockSignals(false);
    m_allToolTable->setUpdatesEnabled(true);
}

void ToolBorrowPage::loadBorrowRecords() {
    // 显示所有位置的借用记录（不限当前用户）
    BorrowService svc;
    QJsonObject result = svc.getAllRecords(m_recordCurrentPage, m_recordPageSize);
    QJsonArray records = result["list"].toArray();
    m_recordTotalRecords = result["total"].toInt();

    // 更新分页信息
    int totalPages = (m_recordTotalRecords + m_recordPageSize - 1) / m_recordPageSize;
    m_recordPageLabel->setText(QStringLiteral("第 %1/%2 页").arg(m_recordCurrentPage).arg(qMax(1, totalPages)));
    m_recordTotalLabel->setText(QStringLiteral("共 %1 条").arg(m_recordTotalRecords));
    m_recordPrevBtn->setEnabled(m_recordCurrentPage > 1);
    m_recordNextBtn->setEnabled(m_recordCurrentPage < totalPages);

    m_recordTable->setRowCount(records.size());
    for (int i = 0; i < records.size(); ++i) {
        QJsonObject r = records[i].toObject();
        m_recordTable->setItem(i, 0, new QTableWidgetItem(r["borrowTime"].toString()));
        m_recordTable->setItem(i, 1, new QTableWidgetItem(r["toolName"].toString()));
        // 借用数量改为位置（以借用位置维度显示）
        // 位置格式A-01-01，空值显示--，同一工具可多位置借用（每条记录对应一个位置）
        QString posDisplay = r["position"].toString();
        auto* posItem = new QTableWidgetItem(posDisplay.isEmpty() ? QStringLiteral("--") : posDisplay);
        posItem->setTextAlignment(Qt::AlignCenter);
        m_recordTable->setItem(i, 2, posItem);
        QString status = r["status"].toString();
        // [2026-06-27] 状态英文转中文显示
        QString statusText;
        QColor statusColor;
        if (status == "borrowing" || status == QStringLiteral("借用中")) {
            statusText = QStringLiteral("借用中");
            statusColor = QColor("#fa8c16");  // 橙色
        } else if (status == "returned" || status == QStringLiteral("已归还")) {
            statusText = QStringLiteral("已归还");
            statusColor = QColor("#52c41a");  // 绿色
        } else if (status == "overdue" || status == QStringLiteral("已逾期")) {
            statusText = QStringLiteral("已逾期");
            statusColor = QColor("#f5222d");  // 红色
        } else {
            statusText = status.isEmpty() ? QStringLiteral("--") : status;
            statusColor = QColor("#8c8c8c");  // 灰色
        }
        auto* statusItem = new QTableWidgetItem(statusText);
        statusItem->setForeground(statusColor);
        QFont statusFont = statusItem->font();
        statusFont.setBold(true);
        statusItem->setFont(statusFont);
        m_recordTable->setItem(i, 3, statusItem);
        // 操作按钮（[v6.10] 对齐UserManagementPage实色块风格 64x36）
        // [2026-06-27] 已归还状态按钮灰色禁用，其他状态点击跳转归还页面
        bool isReturned = (status == "returned" || status == QStringLiteral("已归还"));
        auto* returnBtn = new QPushButton(QStringLiteral("归还"));
        returnBtn->setFixedSize(64, 36);
        if (isReturned) {
            // 已归还：灰色禁用
            returnBtn->setEnabled(false);
            returnBtn->setStyleSheet(
                "QPushButton{background:#d9d9d9;color:#bfbfbf;border:none;border-radius:8px;"
                "font-size:14px;font-weight:700;}"
                "QPushButton:disabled{background:#d9d9d9;color:#bfbfbf;}"
            );
        } else {
            // 借用中/已逾期：蓝色(#4da3ff)，点击跳转归还页面并自动选中对应工具
            // [2026-06-27] 颜色对齐人员管理编辑按钮#4da3ff
            returnBtn->setStyleSheet(
                "QPushButton{background:#4da3ff;color:white;border:none;border-radius:8px;"
                "font-size:14px;font-weight:700;}"
                "QPushButton:hover{background:#3d8ae0;}"
                "QPushButton:pressed{background:#2e7ad6;}"
            );
            returnBtn->setCursor(Qt::PointingHandCursor);
            int recordId = r["recordId"].toInt();
            connect(returnBtn, &QPushButton::clicked, this, [this, recordId] {
                emit returnRequested(recordId);  // 跳转工具归还页面并传recordId
            });
        }
        m_recordTable->setCellWidget(i, 4, returnBtn);
        m_recordTable->setRowHeight(i, 64);  // [v6.10] 行高64px，36px按钮+8px边距完整显示
    }
}

// [V8.0 2026-06-28] 恢复表格重绘和信号
void ToolBorrowPage::onTaskTypeChanged() {
    loadRecommendedTools();
}

void ToolBorrowPage::onToolSelected(int row, int col) {
    // 由checkbox处理选择
}

void ToolBorrowPage::onSearchTool() {
    // [2026-06-27] 搜索行已移除，此方法保留为空避免头文件引用错误
    refreshAllToolTable();
}

void ToolBorrowPage::onReasonChanged(int index) {
    // 暂未使用
}

void ToolBorrowPage::onBorrowConfirm() {
    if (m_user.isEmpty()) {
        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("请先登录"));
        return;
    }
    // [2026-06-27] 必须先选择任务类型才能借用（引导用户）
    if (m_selectedTypeIds.isEmpty()) {
        MessageDialog::showWarning(this, QStringLiteral("请选择任务类型"),
            QStringLiteral("请先点击「任务类型」下拉框选择对应的任务类型，\n系统将根据任务类型智能推荐工具。"));
        return;
    }
    if (m_selectedTools.isEmpty()) {
        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("请先选择要借用的工具"));
        return;
    }

    // [2026-06-27] 从本机组在库工具列表补全选中工具的 toolCode 和 position
    // 集中在此处补全，避免改动3处分散的lambda捕获列表
    // [V8.0 2026-06-28] #17修复：使用refreshAllToolTable缓存的数据，不再重复查库
    //   作者：袁燕
    {
        QJsonArray allList = m_allToolsCache.isEmpty() ? [&]() {
            BorrowService svc;
            int groupId = AppConfig::instance().localMachineGroupId();
            return svc.getAllInStockTools(1, SC::PAGE_SIZE_UNLIMITED, groupId)["list"].toArray();
        }() : m_allToolsCache;
        QHash<int, QJsonObject> toolMap;
        for (const auto& t : allList) {
            QJsonObject obj = t.toObject();
            toolMap.insert(obj["toolId"].toInt(), obj);
        }
        for (auto& sel : m_selectedTools) {
            auto it = toolMap.find(sel.toolId);
            if (it != toolMap.end()) {
                sel.toolCode = it.value()["toolCode"].toString();
                sel.position = it.value()["position"].toString();
            }
        }
    }

    // [2026-06-27] 每件工具默认借1件，m_pendingQuantity用于兼容旧逻辑
    m_pendingQuantity = 1;
    // [2026-06-27] 从系统参数自动计算归还时间（不再从用户编辑的DateTimeEdit读取）
    int defaultPeriodHours = AppConfig::instance().borrowDefaultPeriod();
    if (defaultPeriodHours <= 0) defaultPeriodHours = 168;
    m_pendingReturnTime = QDateTime::currentDateTime().addSecs(defaultPeriodHours * 3600)
                              .toString("yyyy-MM-dd HH:mm:ss");

    // [2026-06-27] 校验借用总数量不超过系统设置的单次最大借出数量
    int maxBorrow = AppConfig::instance().borrowMaxCount();
    if (maxBorrow <= 0) maxBorrow = 5;  // 兜底默认值
    int totalQty = 0;
    for (const auto& tool : m_selectedTools) totalQty += tool.quantity;
    if (totalQty > maxBorrow) {
        MessageDialog::showWarning(this, QStringLiteral("超出借用限制"),
            QStringLiteral("系统设置单次最大借出数量为 %1 件，\n"
                           "当前已选 %2 件，超出限制 %3 件。\n\n"
                           "请减少借用数量后再试。").arg(maxBorrow).arg(totalQty).arg(totalQty - maxBorrow));
        return;
    }

    // [V2.03d 2026-06-29] 构建按位置展开的借用清单
    // 设计理念：一个位置(机组-柜-层-位号)=一个工具，quantity>1时需找到同名同规格的多个位置
    //   展开后每条记录对应一个具体位置，quantity恒为1。作者：袁燕
    // 改为从映射表自动分配in_stock位置（不再从缓存匹配）
    // 每个选中工具按quantity从映射表找对应数量的in_stock位置
    m_expandedBorrowList.clear();
    {
        db::ToolDAO toolDao;
        for (const auto& sel : m_selectedTools) {
            // 从映射表查该工具的in_stock位置，取前quantity个
            QJsonArray positions = toolDao.findInStockPositions(sel.toolId, sel.quantity);
            int assigned = 0;
            for (const auto& posVal : positions) {
                QJsonObject pos = posVal.toObject();
                if (assigned >= sel.quantity) break;
                SelectedToolInfo expanded = sel;
                expanded.quantity = 1;  // 每条1件1位置
                expanded.mappingId = pos["mappingId"].toInt();
                // 记录分配的具体位置（显示给用户引导取用）
                QString posDisplay = StyleHelper::formatPosition(
                    pos["cabinetName"].toString(), pos["layer"].toString(), pos["position"].toString());
                expanded.position = posDisplay;
                m_expandedBorrowList.append(expanded);
                ++assigned;
            }
            if (assigned < sel.quantity) {
                MessageDialog::showWarning(this, QStringLiteral("可用位置不足"),
                    QStringLiteral("工具「%1」需要借用 %2 件，\n"
                                   "但映射表中在库可用位置仅有 %3 个。\n\n"
                                   "请减少借用数量后再试。")
                        .arg(sel.toolName).arg(sel.quantity).arg(assigned));
                return;
            }
        }
    }

    showBorrowConfirmDialog();
}

/**
 * [2026-06-27] 第一步：确认借用信息对话框（改造版）
 *   展示借用详情（工具名、位置、数量、借用人、归还时间），用户确认后进入"抽屉打开中"
 */
void ToolBorrowPage::showBorrowConfirmDialog() {
    QDialog* dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("确认借用"));
    dlg->setFixedSize(560, qMax(460, 340 + m_expandedBorrowList.size() * 40));
    dlg->setStyleSheet(StyleHelper::dialogStyle());
    dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);

    auto* mainLayout = new QVBoxLayout(dlg);
    mainLayout->setContentsMargins(32, 28, 32, 24);
    mainLayout->setSpacing(14);

    // 标题栏
    auto* titleBar = new QHBoxLayout();
    auto* iconLabel = new QLabel(QStringLiteral("📋"));
    iconLabel->setStyleSheet("font-size:28px;background:transparent;");
    auto* titleLabel = new QLabel(QStringLiteral("待借用工具清单"));
    titleLabel->setStyleSheet("font-size:22px;font-weight:bold;color:#1a1a2e;background:transparent;");
    titleBar->addWidget(iconLabel);
    titleBar->addWidget(titleLabel);
    titleBar->addStretch();
    mainLayout->addLayout(titleBar);

    // [V2.03d 2026-06-29] 借用详情表格用展开清单：工具名/位置/数量(每行1件1位置)
    auto* table = new QTableWidget();
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({
        QStringLiteral("工具名称"), QStringLiteral("存放位置"), QStringLiteral("借用数量")
    });
    table->setRowCount(m_expandedBorrowList.size());
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setStyleSheet(QString(
        "QTableWidget { border:1px solid #f0f0f0; background:#fff; border-radius:10px; "
        "  font-family:\"Microsoft YaHei\",sans-serif; font-size:14px; }"
        "QTableWidget::item { padding:10px 12px; color:#333; border:none; "
        "  border-bottom:1px solid #f3f3f3; }"
        "QHeaderView::section { background:#f8f9fb; color:#666; font-weight:600; "
        "  font-size:13px; padding:10px 12px; border:none; border-bottom:1px solid #f0f0f0; }"
    ));
    for (int col = 0; col < 3; ++col) {
        table->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Stretch);
    }
    for (int i = 0; i < m_expandedBorrowList.size(); ++i) {
        const auto& tool = m_expandedBorrowList[i];
        table->setItem(i, 0, new QTableWidgetItem(tool.toolName));
        table->setItem(i, 1, new QTableWidgetItem(tool.position.isEmpty() ? QStringLiteral("--") : tool.position));
        table->setItem(i, 2, new QTableWidgetItem(QStringLiteral("%1 件").arg(tool.quantity)));
        table->setRowHeight(i, 38);
    }
    table->setFixedHeight(qMax(160, m_expandedBorrowList.size() * 38 + 38));
    mainLayout->addWidget(table);

    // 借用人信息 + 归还时间
    auto* infoRow = new QHBoxLayout();
    infoRow->setSpacing(24);
    auto* userInfo = new QLabel(QStringLiteral("借用人：%1（%2）")
        .arg(m_user["realName"].toString(), m_user["workNo"].toString()));
    userInfo->setStyleSheet("font-size:15px;color:#1a1a2e;font-weight:600;background:transparent;");
    auto* retInfo = new QLabel(QStringLiteral("预计归还：%1").arg(m_pendingReturnTime));
    retInfo->setStyleSheet("font-size:15px;color:#f57c00;font-weight:600;background:transparent;");
    infoRow->addWidget(userInfo);
    infoRow->addStretch();
    infoRow->addWidget(retInfo);
    mainLayout->addLayout(infoRow);

    // 流水号
    auto* flowInfo = new QLabel(QStringLiteral("流水单号：%1").arg(m_flowNo));
    flowInfo->setStyleSheet("font-size:13px;color:#999;background:transparent;");
    mainLayout->addWidget(flowInfo);

    mainLayout->addStretch();

    // 底部按钮栏
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(12);
    btnLayout->addStretch();

    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    cancelBtn->setStyleSheet(StyleHelper::buttonDefault());
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setMinimumHeight(44);
    cancelBtn->setMinimumWidth(110);
    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);

    auto* nextBtn = new QPushButton(QStringLiteral("下一步 →"));
    nextBtn->setStyleSheet(StyleHelper::buttonPrimary());
    nextBtn->setCursor(Qt::PointingHandCursor);
    nextBtn->setMinimumHeight(44);
    nextBtn->setMinimumWidth(140);
    connect(nextBtn, &QPushButton::clicked, this, [this, dlg]() {
        dlg->accept();
        showBorrowDrawerOpeningDialog();  // [2026-06-27] 进入步骤2：抽屉打开中
    });
    btnLayout->addWidget(nextBtn);

    mainLayout->addLayout(btnLayout);
    dlg->exec();
    dlg->deleteLater();
}

/**
 * [2026-06-27] 第二步：抽屉打开中（动态旋转框）
 *   提示用户对应抽屉已打开，按清单借用工具
 * [2026-06-27 补充] 加入待借用工具列表，便于用户按列表取用
 */
void ToolBorrowPage::showBorrowDrawerOpeningDialog() {
    QDialog* dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("抽屉打开中"));
    // [2026-06-27] 根据借用工具数量动态调整高度，确保列表完整显示
    // [V2.03d 2026-06-29] 用展开清单显示，每行1件1位置
    int listCount = m_expandedBorrowList.size();
    int dlgHeight = qMax(460, 320 + listCount * 36);
    dlg->setFixedSize(560, dlgHeight);
    dlg->setStyleSheet(StyleHelper::dialogStyle());
    dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);

    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(32, 28, 32, 24);
    layout->setSpacing(14);

    // 标题
    auto* titleLabel = new QLabel(QStringLiteral("抽屉已打开"));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size:22px;font-weight:bold;color:#1a1a2e;background:transparent;");
    layout->addWidget(titleLabel);

    // 动态加载指示器 — 三点跳动动画
    auto* spinnerContainer = new QWidget();
    spinnerContainer->setFixedHeight(48);
    auto* spinnerLayout = new QHBoxLayout(spinnerContainer);
    spinnerLayout->setSpacing(16);
    spinnerLayout->setAlignment(Qt::AlignCenter);
    QList<QLabel*> dots;
    for (int i = 0; i < 3; ++i) {
        auto* dot = new QLabel();
        dot->setFixedSize(16, 16);
        dot->setStyleSheet(QString(
            "QLabel{background:%1;border-radius:8px;}"
        ).arg(StyleHelper::primaryColor()));
        spinnerLayout->addWidget(dot);
        dots.append(dot);
    }
    layout->addWidget(spinnerContainer);

    // [2026-06-27] 三点依次跳动：定时器控制颜色和大小变化
    QTimer* dotTimer = new QTimer(dlg);
    dotTimer->setInterval(300);
    int dotIndex = 0;
    QObject::connect(dotTimer, &QTimer::timeout, dlg, [dots, &dotIndex]() {
        for (int i = 0; i < dots.size(); ++i) {
            if (i == dotIndex) {
                dots[i]->setStyleSheet("background:#4da3ff;border-radius:10px;");
                dots[i]->setFixedSize(20, 20);
            } else {
                dots[i]->setStyleSheet("background:#c8d6e5;border-radius:8px;");
                dots[i]->setFixedSize(16, 16);
            }
        }
        dotIndex = (dotIndex + 1) % dots.size();
    });
    dotTimer->start();

    // 提示信息
    auto* descLabel = new QLabel(QStringLiteral(
        "对应工具抽屉已自动打开，<br/>"
        "请按下方清单取用工具后手动关闭抽屉再点击下一步。"
    ));
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setTextFormat(Qt::RichText);
    descLabel->setStyleSheet("font-size:15px;color:#555;line-height:1.6;background:transparent;");
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    // [2026-06-27] 待借用工具列表，便于用户按列表取用
    if (listCount > 0) {
        auto* table = new QTableWidget();
        table->setColumnCount(4);
        table->setHorizontalHeaderLabels({
            QStringLiteral("工具名称"), QStringLiteral("工具编号"),
            QStringLiteral("存放位置"), QStringLiteral("借用数量")
        });
        table->setRowCount(listCount);
        table->verticalHeader()->setVisible(false);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setStyleSheet(QString(
            "QTableWidget { border:1px solid #f0f0f0; background:#fff; border-radius:10px; "
            "  font-family:\"Microsoft YaHei\",sans-serif; font-size:13px; outline:none; }"
            "QTableWidget::item { padding:8px 12px; color:#333; border:none; "
            "  border-bottom:1px solid #f3f3f3; outline:none; }"
            "QTableWidget::item:selected { background:#e6f0ff; outline:none; }"
            "QHeaderView::section { background:#f8f9fb; color:#666; font-weight:600; "
            "  font-size:12px; padding:8px 12px; border:none; border-bottom:1px solid #f0f0f0; }"
        ));
        // 列宽：名称Stretch，编号Stretch，位置Stretch，数量Fixed窄列
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
        table->setColumnWidth(3, 80);
        table->horizontalHeader()->setStretchLastSection(false);

        for (int i = 0; i < listCount; ++i) {
            const auto& it = m_expandedBorrowList[i];
            table->setItem(i, 0, new QTableWidgetItem(it.toolName));
            table->setItem(i, 1, new QTableWidgetItem(it.toolCode.isEmpty() ? QStringLiteral("--") : it.toolCode));
            table->setItem(i, 2, new QTableWidgetItem(it.position.isEmpty() ? QStringLiteral("--") : it.position));
            table->setItem(i, 3, new QTableWidgetItem(QStringLiteral("%1 件").arg(it.quantity)));
            table->setRowHeight(i, 36);
        }
        table->setFixedHeight(qMax(120, listCount * 36 + 36));
        layout->addWidget(table);
    }

    layout->addStretch();

    // 底部按钮
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    cancelBtn->setStyleSheet(StyleHelper::buttonDefault()) ;
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setMinimumHeight(44);
    cancelBtn->setMinimumWidth(110);
    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);

    auto* nextBtn = new QPushButton(QStringLiteral("下一步 →"));
    nextBtn->setStyleSheet(StyleHelper::buttonPrimary());
    nextBtn->setCursor(Qt::PointingHandCursor);
    nextBtn->setMinimumHeight(44);
    nextBtn->setMinimumWidth(140);
    connect(nextBtn, &QPushButton::clicked, this, [this, dlg]() {
        dlg->accept();
        showToolVerifyDialog();  // [2026-06-27] 进入步骤3：工具核对
    });
    btnRow->addWidget(cancelBtn);
    btnRow->addSpacing(12);
    btnRow->addWidget(nextBtn);
    layout->addLayout(btnRow);

    dlg->exec();
    dlg->deleteLater();
}

/**
 * [2026-06-27] 第三步：工具核对对话框（假异常提示）
 *   提示用户工具未正确放置/数量不符，确认后执行实际借用
 */
void ToolBorrowPage::showToolVerifyDialog() {
    // [V2.03k 2026-06-29] 右上角倒计时+左下角忽略+告警入库+退出系统
    QDialog* dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("工具核对"));
    dlg->setFixedSize(520, 420);
    dlg->setStyleSheet(StyleHelper::dialogStyle());
    dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);

    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(32, 32, 32, 24);
    layout->setSpacing(18);

    // [V2.03k] 顶部行：图标居中 + 右上角倒计时
    auto* topRow = new QHBoxLayout();
    auto* iconLabel = new QLabel(QStringLiteral("⚠️"));
    iconLabel->setStyleSheet("font-size:56px;background:transparent;");
    int bufferMinutes = AppConfig::instance().borrowReturnBuffer();
    if (bufferMinutes <= 0) bufferMinutes = 30;
    auto* countdownLabel = new QLabel(QStringLiteral("⏱ %1:00").arg(bufferMinutes));
    countdownLabel->setStyleSheet("font-size:18px;font-weight:bold;color:#e74c3c;background:#fdecea;border:1px solid #f5c6cb;border-radius:8px;padding:6px 12px;");
    topRow->addStretch();
    topRow->addWidget(iconLabel);
    topRow->addStretch();
    topRow->addWidget(countdownLabel);
    layout->addLayout(topRow);

    // 警告标题
    auto* titleLabel = new QLabel(QStringLiteral("工具核对异常"));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size:22px;font-weight:bold;color:#fa8c16;background:transparent;");
    layout->addWidget(titleLabel);

    // 警告详情（假异常）
    // [V2.03d] 改用展开清单取首个位置展示
    QString firstPosition = m_expandedBorrowList.isEmpty() ? QStringLiteral("A-01")
        : (m_expandedBorrowList.first().position.isEmpty() ? QStringLiteral("A-01") : m_expandedBorrowList.first().position);
    auto* descLabel = new QLabel(QStringLiteral(
        "检测到 <b>%1</b> 位置的工具视觉识别与清单不符，<br/>"
        "可能原因：误取相邻柜位工具、标签损坏或柜位错位。<br/><br/>"
        "请重新核对工具后点击确认，系统将完成借用登记。"
    ).arg(firstPosition));
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setTextFormat(Qt::RichText);
    descLabel->setStyleSheet("font-size:14px;color:#555;line-height:1.7;background:transparent;");
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    layout->addStretch();

    // 倒计时定时器，到0时自动触发告警
    int* remainSeconds = new int(bufferMinutes * 60);
    QTimer* countdownTimer = new QTimer(dlg);
    countdownTimer->setInterval(1000);
    QObject::connect(countdownTimer, &QTimer::timeout, dlg, [countdownLabel, remainSeconds, dlg, countdownTimer]() {
        (*remainSeconds)--;
        if (*remainSeconds <= 0) {
            countdownTimer->stop();
            dlg->done(2);  // 倒计时结束→走忽略流程
        } else {
            int mins = *remainSeconds / 60;
            int secs = *remainSeconds % 60;
            countdownLabel->setText(QStringLiteral("⏱ %1:%2")
                .arg(mins, 2, 10, QChar('0'))
                .arg(secs, 2, 10, QChar('0')));
        }
    });
    countdownTimer->start();

    // 忽略按钮回调：关闭对话框走done(2)流程，告警在exec后统一写入
    auto doIgnore = [this, dlg, countdownTimer, remainSeconds]() mutable {
        countdownTimer->stop();
        delete remainSeconds;
        dlg->done(2);
    };

    // [V2.03k] 按钮区：左下角忽略 + 右下角取消借用 + 确认完成核对
    auto* btnRow = new QHBoxLayout();
    auto* ignoreBtn = new QPushButton(QStringLiteral("忽略"));
    ignoreBtn->setStyleSheet("QPushButton{background:#e74c3c;color:#fff;border:none;border-radius:10px;padding:10px 24px;font-size:14px;font-weight:700;min-height:44px;}QPushButton:hover{background:#c0392b;}");
    ignoreBtn->setCursor(Qt::PointingHandCursor);
    connect(ignoreBtn, &QPushButton::clicked, this, doIgnore);
    btnRow->addWidget(ignoreBtn);
    btnRow->addStretch();

    auto* cancelBtn = new QPushButton(QStringLiteral("取消借用"));
    cancelBtn->setStyleSheet(StyleHelper::buttonDefault());
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setMinimumHeight(44);
    cancelBtn->setMinimumWidth(120);
    connect(cancelBtn, &QPushButton::clicked, dlg, [countdownTimer, remainSeconds]() {
        countdownTimer->stop();
        delete remainSeconds;
        static_cast<QDialog*>(static_cast<QWidget*>(countdownTimer->parent()))->reject();
    });

    auto* confirmBtn = new QPushButton(QStringLiteral("✓ 确认完成核对"));
    confirmBtn->setStyleSheet(StyleHelper::buttonPrimary());
    confirmBtn->setCursor(Qt::PointingHandCursor);
    confirmBtn->setMinimumHeight(44);
    confirmBtn->setMinimumWidth(160);
    connect(confirmBtn, &QPushButton::clicked, this, [this, dlg, countdownTimer, remainSeconds]() {
        countdownTimer->stop();
        delete remainSeconds;
        dlg->accept();
        executeBorrow();
    });
    btnRow->addWidget(cancelBtn);
    btnRow->addSpacing(12);
    btnRow->addWidget(confirmBtn);
    layout->addLayout(btnRow);

    int result = dlg->exec();
    countdownTimer->stop();

    if (result == 2) {
        // 忽略或倒计时结束：写告警日志（闭环）
        // doIgnore已写告警的也是done(2)，但需处理倒计时到0直接done(2)的情况
        db::AlertDAO alertDao;
        AlertLog alert;
        alert.typeId = 2;
        alert.userId = m_user["userId"].toInt();
        if (!m_selectedTools.isEmpty()) {
            alert.toolId = m_selectedTools.first().toolId;
            alert.toolCode = m_selectedTools.first().toolCode;
        }
        alert.message = QStringLiteral("工具核对异常：%1位置工具视觉识别与清单不符，用户忽略告警或倒计时超时").arg(firstPosition);
        alert.status = "unhandled";
        alertDao.insertAlert(alert);

        MessageDialog::showWarning(nullptr, QStringLiteral("告警已记录"),
            QStringLiteral("工具核对异常告警已记录到系统告警，页面已重置。"));
        QMetaObject::invokeMethod(this, "refresh", Qt::QueuedConnection);
        m_selectedToolIds.clear();
        m_selectedTools.clear();
    }
    dlg->deleteLater();
}

/**
 * [V7.1] 第三步：执行借用（DAO层写入数据库）
 * [V7.4] 支持多工具批量借用
 * 通过BorrowService执行实际借用操作，写入tool_borrow_record表
 * 成功后弹出美观的成功提示
 */
void ToolBorrowPage::executeBorrow() {
    BorrowService svc;
    // [2026-06-27] successTools改为存QPair<名称,数量>，修复成功对话框显示数量恒为1的Bug
    QList<QPair<QString, int>> successTools;
    QStringList failTools;
    int successCount = 0;

    // [V7.4] 逐个工具借用
    // [2026-06-27 修复] MySQL flow_no有UNIQUE约束，多工具共用同一flowNo会导致只有第1条插入成功
    // 修复方案：每个工具的flowNo加序号后缀(-01,-02,...)，既保持批次关联性又满足UNIQUE约束
    // [V2.03d 2026-06-29] 改用展开清单(m_expandedBorrowList)，每条对应一个位置，quantity=1
    int totalTools = m_expandedBorrowList.size();
    int seqIndex = 0;
    for (const auto& tool : m_expandedBorrowList) {
        // [2026-06-27] 生成带序号的flowNo：原flowNo + "-01" / "-02" / ...
        QString toolFlowNo = m_flowNo;
        if (totalTools > 1) {
            toolFlowNo = QStringLiteral("%1-%2")
                .arg(m_flowNo)
                .arg(seqIndex + 1, 2, 10, QChar('0'));
        }
        ++seqIndex;
        // [2026-06-27] 使用每个工具自己的 quantity，而非统一的 m_pendingQuantity
        // reason改为用户选中的任务类型名称列表，保持归还页"任务类型"列与借用时一致
        QString reason = m_selectedTypeNames.isEmpty()
            ? QStringLiteral("任务借用")
            : m_selectedTypeNames.join("、");
        // 直接用展开清单中保存的mappingId，不再重新查
        // 根因：原代码查"第一个in_stock位置"可能匹配到错误位置，
        // 导致借用记录mappingId与实际借用的位置不一致→数据混乱
        // 修复：展开清单时已保存mappingId，直接用
        int borrowMappingId = tool.mappingId;
        if (borrowMappingId <= 0) {
            failTools.append(QStringLiteral("%1: 位置分配异常(mappingId=0)").arg(tool.toolName));
            continue;
        }
        auto result = svc.borrowTool(m_user["userId"].toInt(), tool.toolId, borrowMappingId,
            tool.quantity, reason, m_pendingReturnTime, toolFlowNo,
            AppConfig::instance().localMachineGroupId());

        if (result.success) {
            successCount++;
            // [V2.03d 2026-06-29] 展开后每条quantity=1，按工具名汇总数量显示
            bool merged = false;
            for (auto& st : successTools) {
                if (st.first == tool.toolName) { st.second += tool.quantity; merged = true; break; }
            }
            if (!merged) successTools.append(qMakePair(tool.toolName, tool.quantity));
        } else {
            failTools.append(QStringLiteral("%1: %2").arg(tool.toolName, result.message));
        }
    }

    if (successCount > 0) {
        // 借用成功 - 显示美观的成功提示
        QDialog* successDlg = new QDialog(this);
        successDlg->setWindowTitle(QStringLiteral("借用成功"));
        successDlg->setFixedSize(460, qMax(380, 300 + successTools.size() * 30));
        successDlg->setStyleSheet(StyleHelper::dialogStyle());
        successDlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);

        auto* layout = new QVBoxLayout(successDlg);
        layout->setContentsMargins(32, 32, 32, 24);
        layout->setSpacing(16);

        // 成功图标 — [2026-06-27] 添加background:transparent防止灰色底
        auto* iconLabel = new QLabel(QStringLiteral("✅"));
        iconLabel->setAlignment(Qt::AlignCenter);
        iconLabel->setStyleSheet("font-size:56px;background:transparent;");
        layout->addWidget(iconLabel);

        // 成功标题 — [2026-06-27] 添加background:transparent防止灰色底
        auto* titleLabel = new QLabel(QStringLiteral("借用成功！"));
        titleLabel->setAlignment(Qt::AlignCenter);
        titleLabel->setStyleSheet("font-size:24px;font-weight:bold;color:#43a047;background:transparent;");
        layout->addWidget(titleLabel);

        // 借用详情 — [2026-06-27] 去掉绿色背景，纯文本显示工具清单，更清爽
        auto* detailFrame = new QFrame();
        detailFrame->setStyleSheet("background:transparent;border:none;");
        auto* detailLayout = new QVBoxLayout(detailFrame);
        detailLayout->setSpacing(6);
        detailLayout->setContentsMargins(0, 8, 0, 8);

        // [2026-06-27] 显示实际借用数量，而非恒定的m_pendingQuantity
        for (const auto& tool : successTools) {
            auto* toolInfo = new QLabel(QStringLiteral("✓ %1  ×%2件").arg(tool.first).arg(tool.second));
            toolInfo->setStyleSheet("font-size:15px;font-weight:600;color:#333;");
            detailLayout->addWidget(toolInfo);
        }

        if (!failTools.isEmpty()) {
            auto* failTitle = new QLabel(QStringLiteral("以下工具借用失败："));
            failTitle->setStyleSheet("font-size:14px;color:#e53935;font-weight:600;margin-top:8px;");
            detailLayout->addWidget(failTitle);
            for (const auto& fail : failTools) {
                auto* failInfo = new QLabel(QStringLiteral("✗ %1").arg(fail));
                failInfo->setStyleSheet("font-size:13px;color:#e53935;");
                detailLayout->addWidget(failInfo);
            }
        }

        layout->addWidget(detailFrame);

        // 提示
        auto* hintLabel = new QLabel(QStringLiteral("请在预计归还时间前归还，逾期将产生记录"));
        hintLabel->setWordWrap(true);
        hintLabel->setAlignment(Qt::AlignCenter);
        hintLabel->setStyleSheet("font-size:14px;color:#555;background:transparent;");
        layout->addWidget(hintLabel);

        layout->addStretch();

        // 确定按钮 — [2026-06-27] 缩小按钮尺寸，56px→44px，18px字体→15px，去掉过大padding
        auto* okBtn = new QPushButton(QStringLiteral("知道了"));
        okBtn->setStyleSheet(QString(
            "QPushButton{ background:#43a047;color:white;border:none;border-radius:10px;"
            "padding:8px 28px;font-size:15px;font-weight:600;min-height:44px;}"
            "QPushButton:hover{background:#388e3c;}"
            "QPushButton:pressed{background:#2e7d32;}"
        ));
        okBtn->setCursor(Qt::PointingHandCursor);
        connect(okBtn, &QPushButton::clicked, successDlg, &QDialog::accept);
        layout->addWidget(okBtn, 0, Qt::AlignCenter);

        successDlg->exec();
        successDlg->deleteLater();

        // 刷新页面数据
        refresh();
    } else {
        // 全部借用失败
        MessageDialog::showError(this, QStringLiteral("借用失败"),
            QStringLiteral("所有工具借用均失败，请重试。"));
    }
}

/** 任务类型按钮点击（显示下拉面板） */
void ToolBorrowPage::onTaskTypeBtnClicked() {
    if (m_taskTypePopup->isVisible()) {
        m_taskTypePopup->hide();
    } else {
        // 定位到按钮下方
        QPoint pos = m_taskTypeBtn->mapToGlobal(QPoint(0, m_taskTypeBtn->height() + 4));
        m_taskTypePopup->move(pos);
        m_taskTypePopup->show();
    }
}

/** 过滤任务类型 */
void ToolBorrowPage::onFilterTaskTypes() {
    QString keyword = m_taskTypeSearch->text().trimmed().toLower();
    // 显示/隐藏任务类型复选框
    for (auto& pair : m_taskTypeCheckBoxes) {
        bool visible = keyword.isEmpty() || pair.first.toLower().contains(keyword);
        pair.second->setVisible(visible);
    }
}

/** 确认任务类型选择 */
void ToolBorrowPage::onConfirmTaskTypes() {
    m_selectedTypeIds.clear();
    m_selectedTypeNames.clear();
    QStringList selectedCodes;

    for (auto& pair : m_taskTypeCheckBoxes) {
        if (pair.second->isChecked()) {
            m_selectedTypeIds.append(pair.second->property("typeId").toInt());
            m_selectedTypeNames.append(pair.first);
            // 获取typeCode用于流水号生成
            QString code = pair.second->property("typeCode").toString();
            if (!code.isEmpty()) selectedCodes.append(code);
        }
    }

    if (m_selectedTypeNames.isEmpty()) {
        m_taskTypeBtn->setText(QStringLiteral("▼ 请选择或搜索"));
        m_taskTypeBtn->setProperty("selected", false);
        m_flowNoDisplay->setText("--");
    } else if (m_selectedTypeNames.size() <= 2) {
        m_taskTypeBtn->setText(QStringLiteral("已选%1项: %2").arg(m_selectedTypeNames.size()).arg(m_selectedTypeNames.join(", ")));
        m_taskTypeBtn->setProperty("selected", true);
        // [2026-06-27] 基于任务类型生成流水号
        QString typeCode = selectedCodes.first().mid(0, 2).toUpper();
        BorrowService svc;
        m_flowNo = svc.generateFlowNo(typeCode.isEmpty() ? QStringLiteral("借用") : typeCode);
        m_flowNoDisplay->setText(m_flowNo);
    } else {
        m_taskTypeBtn->setText(QStringLiteral("已选%1项").arg(m_selectedTypeNames.size()));
        m_taskTypeBtn->setProperty("selected", true);
        QString typeCode = selectedCodes.first().mid(0, 2).toUpper();
        BorrowService svc;
        m_flowNo = svc.generateFlowNo(typeCode.isEmpty() ? QStringLiteral("借用") : typeCode);
        m_flowNoDisplay->setText(m_flowNo);
    }
    // 强制刷新样式以应用selected属性
    m_taskTypeBtn->style()->unpolish(m_taskTypeBtn);
    m_taskTypeBtn->style()->polish(m_taskTypeBtn);

    m_taskTypePopup->hide();
    m_borrowBtn->setText(QStringLiteral("确认借用(共0件)"));
    loadRecommendedTools();
}

// [V6.6] 工具搜索框软键盘
void ToolBorrowPage::onSearchFieldClicked() {
    // [2026-06-27] 搜索行已移除，无软键盘需求
    if (!m_toolSearchEdit) return;
    if (!m_softKeyboard) {
        m_softKeyboard = new SoftKeyboard(this);
        m_softKeyboard->setMode(SoftKeyboard::ModeEn);
        connect(m_softKeyboard, &SoftKeyboard::confirmed, this, [this]() {
            m_softKeyboard->hide();
        });
    }
    QPoint pos = m_toolSearchEdit->mapToGlobal(QPoint(0, m_toolSearchEdit->height() + 4));
    m_softKeyboard->show(m_toolSearchEdit, pos);
}

// [2026-06-25] 全部工具表分页
void ToolBorrowPage::onToolPrevPage() {
    if (m_toolCurrentPage > 1) {
        m_toolCurrentPage--;
        refreshAllToolTable();
    }
}

void ToolBorrowPage::onToolNextPage() {
    int totalPages = (m_toolTotalRecords + m_toolPageSize - 1) / m_toolPageSize;
    if (m_toolCurrentPage < totalPages) {
        m_toolCurrentPage++;
        refreshAllToolTable();
    }
}

// [2026-06-25] 借用记录表分页
void ToolBorrowPage::onRecordPrevPage() {
    if (m_recordCurrentPage > 1) {
        m_recordCurrentPage--;
        loadBorrowRecords();
    }
}

void ToolBorrowPage::onRecordNextPage() {
    int totalPages = (m_recordTotalRecords + m_recordPageSize - 1) / m_recordPageSize;
    if (m_recordCurrentPage < totalPages) {
        m_recordCurrentPage++;
        loadBorrowRecords();
    }
}
