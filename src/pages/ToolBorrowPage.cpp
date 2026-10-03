/**
 * @file ToolBorrowPage.cpp
 * @brief 工具借用页面 — 任务类型多选、工具推荐、借用确认、位置分配
 * @author 袁燕
 */
#include "ToolBorrowPage.h"
#include "ui_ToolBorrowPage.h"
#include <QTableWidget>  // 工具/借用记录表格
#include "components/PaginationBar.h"
#include "utils/StyleHelper.h"
#include "components/DrawerOpeningDialog.h"
#include "components/VerifyAlertDialog.h"
#include "components/ResultDialog.h"
#include "services/BorrowService.h"
#include "common/AppConfig.h"       // 获取本机机组ID
#include "common/Constants.h"       // 分页常量
#include "services/AlertService.h"    // 告警闭环写入
#include "model/AlertLog.h"         // AlertLog实体
#include "db/ToolDAO.h"            // 查询机组名称和位置映射
#include "components/SoftKeyboard.h"
// 通过BorrowService访问数据
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
#include <QSet>  // 任务类型默认勾选
#include <QSpinBox>  // 数量选择
#include <QStyle>  // style()->unpolish/polish 刷新QSS属性
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QTimer>  // 勾选跳页延迟重建，避免信号回调中销毁widget
#include <QApplication>  // 退出系统
#include <QCoreApplication>  // QCoreApplication::quit

ToolBorrowPage::ToolBorrowPage(QWidget* parent) : QWidget(parent), ui(new Ui::ToolBorrowPage) {
    // 静态布局来自ToolBorrowPage.ui（Qt Designer可视化维护）
    ui->setupUi(this);
    setupUI();
}

ToolBorrowPage::~ToolBorrowPage() {
    delete ui;
}

void ToolBorrowPage::setUser(const QJsonObject& user) {
    m_user = user;
    refresh();
}

void ToolBorrowPage::setupUI() {
    // 桥接.ui控件（业务逻辑沿用m_成员，零改动）
    m_tabWidget = ui->tabWidget;
    m_userInfoLabel = ui->userInfoLabel;
    m_workNoLabel = ui->workNoLabel;
    m_deptLabel = ui->deptLabel;
    m_dateLabel = ui->dateLabel;
    m_taskTypeBtn = ui->taskTypeBtn;
    m_flowNoDisplay = ui->flowNoDisplay;
    m_returnTimeLabel = ui->returnTimeLabel;
    m_recommendHint = ui->recommendHint;
    m_allToolTable = ui->allToolTable;
    m_toolPaginationBar = ui->toolPaginationBar;
    m_borrowBtn = ui->borrowBtn;
    m_recordTable = ui->recordTable;
    m_recordPaginationBar = ui->recordPaginationBar;

    // 不创建独立的推荐表格，全部工具统一在 m_allToolTable 中显示

    // 日期初始值
    m_dateLabel->setText(QDate::currentDate().toString("yyyy-MM-dd"));

    // 预计归还时间：从AppConfig读取默认借用期限(小时)，自动计算归还时间
    int defaultPeriodHours = AppConfig::instance().borrowDefaultPeriod();
    if (defaultPeriodHours <= 0) defaultPeriodHours = 168; // 默认7天=168小时
    QDateTime returnTime = QDateTime::currentDateTime().addSecs(defaultPeriodHours * 3600);
    m_returnTimeLabel->setText(returnTime.toString("yyyy-MM-dd HH:mm"));

    // 信号槽连接
    connect(m_taskTypeBtn, &QPushButton::clicked, this, &ToolBorrowPage::onTaskTypeBtnClicked);
    connect(m_allToolTable, &QTableWidget::cellClicked, this, &ToolBorrowPage::onToolSelected);
    connect(m_toolPaginationBar, &PaginationBar::prevClicked, this, &ToolBorrowPage::onToolPrevPage);
    connect(m_toolPaginationBar, &PaginationBar::nextClicked, this, &ToolBorrowPage::onToolNextPage);
    connect(m_borrowBtn, &QPushButton::clicked, this, &ToolBorrowPage::onBorrowConfirm);
    connect(m_recordPaginationBar, &PaginationBar::prevClicked, this, &ToolBorrowPage::onRecordPrevPage);
    connect(m_recordPaginationBar, &PaginationBar::nextClicked, this, &ToolBorrowPage::onRecordNextPage);

    // 工具表格列宽策略：选择列固定60px，1-6列拉伸，操作列固定130px
    m_allToolTable->horizontalHeader()->setStretchLastSection(false);
    m_allToolTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_allToolTable->setColumnWidth(0, 60);
    for (int c = 1; c < 7; c++) {
        m_allToolTable->horizontalHeader()->setSectionResizeMode(c, QHeaderView::Stretch);
    }
    m_allToolTable->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Fixed);
    m_allToolTable->setColumnWidth(7, 130);
    m_allToolTable->horizontalHeader()->setMinimumSectionSize(50);

    // 记录表格列宽策略：数据列Stretch，操作列Fixed 130px
    m_recordTable->horizontalHeader()->setStretchLastSection(false);
    for (int i = 0; i < 4; i++) {
        m_recordTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
    }
    m_recordTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_recordTable->setColumnWidth(4, 130);
    m_recordTable->horizontalHeader()->setStretchLastSection(false);
    m_recordTable->horizontalHeader()->setMinimumSectionSize(60);

    // ────── 任务类型下拉面板（popup，动态构建）──────
    // QFrame→QDialog+Popup，解决checkbox选不中致命Bug
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

    // 底部按钮：取消（清空全部勾选）+ 确定
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
    // 取消=清空全部勾选+关闭面板
    connect(cancelBtn, &QPushButton::clicked, this, [this]() {
        for (auto& pair : m_taskTypeCheckBoxes) {
            pair.second->setChecked(false);
        }
        m_taskTypePopup->hide();
    });
    btnRow->addWidget(cancelBtn);

    // 确定按钮颜色统一为蓝色实底#4da3ff，与底部"确认借用"按钮一致
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
}

void ToolBorrowPage::refresh() {
    // 切换到本页面时默认显示"借用任务"选项卡
    if (m_tabWidget) m_tabWidget->setCurrentIndex(0);
    // 切换账号时重置所有借用状态，避免残留上个账号的数据
    m_selectedTools.clear();
    m_selectedToolIds.clear();  // QSet同步清空
    m_recommendedToolIds.clear();
    m_selectedTypeIds.clear();
    m_flowNo.clear();
    m_toolCurrentPage = 1;
    // 更新顶部信息栏
    m_userInfoLabel->setText(m_user["realName"].toString());
    m_workNoLabel->setText(m_user["workNo"].toString());
    // 注意：所属机组应显示本机机组名，而非用户部门
    int groupId = AppConfig::instance().localMachineGroupId();
    if (groupId > 0) {
        db::ToolDAO dao;
        QJsonObject mg = dao.getMachineGroupById(groupId);
        m_deptLabel->setText(mg["groupName"].toString("--"));
    } else {
        m_deptLabel->setText(QStringLiteral("未配置"));
    }
    m_dateLabel->setText(QDate::currentDate().toString("yyyy-MM-dd"));

    // 流水号在用户选择任务类型后才生成，默认显示--
    m_flowNoDisplay->setText("--");

    // 性能优化：任务类型列表仅首次加载或列表为空时重建（避免每次切换页面重复DB查询）
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
        m_selectedToolIds.clear();  // QSet同步
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

    // 默认不勾选任何任务类型，由用户主动选择
    QSet<int> defaultCheckedIds;

    for (int i = 0; i < types.size(); ++i) {
        QJsonObject t = types[i].toObject();
        QString typeName = t["typeName"].toString();
        int typeId = t["typeId"].toInt();
        QString typeCode = t["typeCode"].toString();

        auto* checkBox = new QCheckBox(typeName);
        checkBox->setProperty("typeId", typeId);
        checkBox->setProperty("typeCode", typeCode);  // 存储typeCode用于流水号
        checkBox->setStyleSheet("font-size:16px; padding:8px;");
        // 默认不勾选
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

    // 默认不勾选，按钮显示提示文本，重置所有状态
    m_taskTypeBtn->setText(QStringLiteral("▼ 请选择任务类型"));
    m_taskTypeBtn->setProperty("selected", false);
    m_taskTypeBtn->style()->unpolish(m_taskTypeBtn);
    m_taskTypeBtn->style()->polish(m_taskTypeBtn);
    m_flowNoDisplay->setText("--");
    m_recommendHint->setText(QStringLiteral("💡 请先选择任务类型以查看推荐工具"));
    m_borrowBtn->setText(QStringLiteral("确认借用(共0件)"));
    // 重置选中工具和推荐列表
    m_selectedTools.clear();
    m_selectedToolIds.clear();  // QSet同步
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

    // 合并推荐工具到统一表格：推荐工具默认勾选
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
        info.stock = availableQty;  // 可用位置数
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

    // 总件数 = quantity之和
    int totalQty = 0;
    for (const auto& s : m_selectedTools) totalQty += s.quantity;
    m_borrowBtn->setText(QStringLiteral("确认借用(共%1件)").arg(totalQty));
    m_toolCurrentPage = 1;
    refreshAllToolTable();
}

void ToolBorrowPage::loadAllTools() {
    refreshAllToolTable();
}

// 刷新全部工具表格：推荐工具排最前面 + "已添加"标记
// 添加服务端分页
// 8列布局 + 库存totalQty/currentQty + 状态列 + 操作列
// 客户端全量加载+排序+分页，支持已选工具自动跳首页
// 拆分：查DB+缓存 → rebuildToolTableFromCache（排序+分页+填充）
//   勾选跳页时用rebuildToolTableFromCache不查DB，效率高
void ToolBorrowPage::refreshAllToolTable() {
    // 一次性加载本机组全部在库工具，缓存供后续跳页使用
    BorrowService svc;
    int groupId = AppConfig::instance().localMachineGroupId();
    QJsonObject result = svc.getAllInStockTools(1, SC::PAGE_SIZE_UNLIMITED, groupId);
    m_allToolsCache = result["list"].toArray();
    // 从缓存重建表格（排序+分页+填充行）
    rebuildToolTableFromCache();
}

// 从缓存重建工具表格 — 不查DB，仅排序+分页+填充行
//   供勾选/添加按钮跳页使用，O(n)排序效率
void ToolBorrowPage::rebuildToolTableFromCache() {
    // 性能优化：禁用重绘+信号阻塞，避免重建过程中频繁刷新
    m_allToolTable->setUpdatesEnabled(false);
    m_allToolTable->blockSignals(true);
    QJsonArray allList = m_allToolsCache;
    m_toolTotalRecords = allList.size();

    int totalPages = (m_toolTotalRecords + m_toolPageSize - 1) / m_toolPageSize;
    if (totalPages == 0) totalPages = 1;
    if (m_toolCurrentPage > totalPages) m_toolCurrentPage = totalPages;
    if (m_toolCurrentPage < 1) m_toolCurrentPage = 1;
    m_toolPaginationBar->setPageInfo(m_toolCurrentPage, totalPages);
    m_toolPaginationBar->setTotalRecords(m_toolTotalRecords);

    // 第一步：对全量数据做三类排序（先排序，再分页）
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

    // 第二步：对排序后的数据做分页切片
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
        // 推荐行浅蓝底，对齐 #4da3ff 蓝色主调
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
        // checkbox选中整体填充蓝色（非边框），圆角4px对齐StyleHelper::tableCheckBox
        auto* checkBox = new QCheckBox();
        checkBox->setStyleSheet(
            "QCheckBox { background: transparent; }"
            "QCheckBox::indicator { width: 22px; height: 22px; border-radius: 4px; "
            "  border: 2px solid #d0d0d0; background: white; }"
            "QCheckBox::indicator:hover { border-color: #4da3ff; }"
            "QCheckBox::indicator:checked { background: #4da3ff; border-color: #4da3ff; }"
        );
        checkBox->setChecked(isSelected);

        // 辅助函数：计算当前已选总件数
        auto calcTotalSelected = [this]() {
            int total = 0;
            for (const auto& s : m_selectedTools) total += s.quantity;
            return total;
        };

        // 辅助函数：刷新底部按钮"共X件"
        auto refreshBorrowBtn = [this, calcTotalSelected]() {
            m_borrowBtn->setText(QStringLiteral("确认借用(共%1件)").arg(calcTotalSelected()));
        };

        // Col 4: 数量下拉框（1/availableQty, 2/availableQty...格式）
        // 数量上限为availableQty（可用位置数）
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
            // 恢复跳首页：已选工具排序靠前，跳到第一页可见
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

        // Col 7: 操作按钮 — 配色对齐人员管理蓝色 #4da3ff，缩小尺寸
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
                    m_selectedToolIds.insert(toolId);  // QSet同步
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
                    // 恢复跳首页：已选工具排序靠前，跳到第一页可见
                    // 从缓存重建不查DB，QTimer延迟避免信号回调中销毁widget
                    m_toolCurrentPage = 1;
                    QTimer::singleShot(0, this, [this]() { rebuildToolTableFromCache(); });
                });
            }
            opBtn->setMinimumHeight(StyleHelper::Token::ControlHeightCompactInput);
            m_allToolTable->setCellWidget(row, 7, opBtn);
        }

        // 行高加大到64px，确保操作按钮完整显示
        m_allToolTable->setRowHeight(row, 64);
    };

    // 填充当前页数据（已排序+分页后的 list）
    for (int i = 0; i < list.size(); ++i) {
        QJsonObject t = list[i].toObject();
        int toolId = t["toolId"].toInt();  // 用toolId判断推荐
        bool isRecommended = m_recommendedToolIds.contains(toolId);
        populateRow(i, t, isRecommended);
    }

    // 更新底部按钮 — 总件数 = 所有已选工具 quantity 之和
    int totalQty = 0;
    for (const auto& s : m_selectedTools) totalQty += s.quantity;
    m_borrowBtn->setText(QStringLiteral("确认借用(共%1件)").arg(totalQty));

    // 恢复表格重绘和信号
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
    m_recordPaginationBar->setPageInfo(m_recordCurrentPage, totalPages);
    m_recordPaginationBar->setTotalRecords(m_recordTotalRecords);

    m_recordTable->setRowCount(records.size());
    for (int i = 0; i < records.size(); ++i) {
        QJsonObject r = records[i].toObject();
        m_recordTable->setItem(i, 0, new QTableWidgetItem(r["borrowTime"].toString()));
        m_recordTable->setItem(i, 1, new QTableWidgetItem(r["toolName"].toString()));
        // 借用数量为位置（以借用位置维度显示）
        // 位置格式A-01-01，空值显示--，同一工具可多位置借用（每条记录对应一个位置）
        QString posDisplay = r["position"].toString();
        auto* posItem = new QTableWidgetItem(posDisplay.isEmpty() ? QStringLiteral("--") : posDisplay);
        posItem->setTextAlignment(Qt::AlignCenter);
        m_recordTable->setItem(i, 2, posItem);
        QString status = r["status"].toString();
        // 状态英文转中文显示
        QString statusText;
        QColor statusColor;
        if (status == SC::RECORD_BORROWING || status == QStringLiteral("借用中")) {
            statusText = QStringLiteral("借用中");
            statusColor = QColor("#fa8c16");  // 橙色
        } else if (status == SC::RECORD_RETURNED || status == QStringLiteral("已归还")) {
            statusText = QStringLiteral("已归还");
            statusColor = QColor("#52c41a");  // 绿色
        } else if (status == SC::RECORD_OVERDUE || status == QStringLiteral("已逾期")) {
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
        // 操作按钮（对齐UserManagementPage实色块风格 64x36）
        // 已归还状态按钮灰色禁用，其他状态点击跳转归还页面
        bool isReturned = (status == SC::RECORD_RETURNED || status == QStringLiteral("已归还"));
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
            // 颜色对齐人员管理编辑按钮#4da3ff
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
        m_recordTable->setRowHeight(i, 64);  // 行高64px，36px按钮+8px边距完整显示
    }
}

// 恢复表格重绘和信号
void ToolBorrowPage::onTaskTypeChanged() {
    loadRecommendedTools();
}

void ToolBorrowPage::onToolSelected(int row, int col) {
    // 由checkbox处理选择
}

void ToolBorrowPage::onSearchTool() {
    // 搜索行已移除，此方法保留为空避免头文件引用错误
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
    // 必须先选择任务类型才能借用（引导用户）
    if (m_selectedTypeIds.isEmpty()) {
        MessageDialog::showWarning(this, QStringLiteral("请选择任务类型"),
            QStringLiteral("请先点击「任务类型」下拉框选择对应的任务类型，\n系统将根据任务类型智能推荐工具。"));
        return;
    }
    if (m_selectedTools.isEmpty()) {
        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("请先选择要借用的工具"));
        return;
    }

    // 从本机组在库工具列表补全选中工具的 toolCode 和 position
    // 集中在此处补全，避免改动3处分散的lambda捕获列表
    // 使用refreshAllToolTable缓存的数据，不重复查库

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

    // 每件工具默认借1件，m_pendingQuantity用于兼容旧逻辑
    m_pendingQuantity = 1;
    // 从系统参数自动计算归还时间（不从用户编辑的DateTimeEdit读取）
    int defaultPeriodHours = AppConfig::instance().borrowDefaultPeriod();
    if (defaultPeriodHours <= 0) defaultPeriodHours = 168;
    m_pendingReturnTime = QDateTime::currentDateTime().addSecs(defaultPeriodHours * 3600)
                              .toString("yyyy-MM-dd HH:mm:ss");

    // 校验借用总数量不超过系统设置的单次最大借出数量
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

    // 构建按位置展开的借用清单
    // 设计理念：一个位置(机组-柜-层-位号)=一个工具，quantity>1时需找到同名同规格的多个位置
    //   展开后每条记录对应一个具体位置，quantity恒为1。作者：袁燕
    // 从映射表自动分配in_stock位置（不从缓存匹配）
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
 * 第一步：确认借用信息对话框（改造版）
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

    // 借用详情表格用展开清单：工具名/位置/数量(每行1件1位置)
    auto* table = new QTableWidget();
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({
        QStringLiteral("工具名称"), QStringLiteral("存放位置"), QStringLiteral("借用数量")
    });
    table->setRowCount(m_expandedBorrowList.size());
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setStyleSheet(StyleHelper::listTableStyle());
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
    cancelBtn->setMinimumHeight(StyleHelper::Token::ControlHeight);
    cancelBtn->setMinimumWidth(110);
    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);

    auto* nextBtn = new QPushButton(QStringLiteral("下一步 →"));
    nextBtn->setStyleSheet(StyleHelper::buttonPrimary());
    nextBtn->setCursor(Qt::PointingHandCursor);
    nextBtn->setMinimumHeight(StyleHelper::Token::ControlHeight);
    nextBtn->setMinimumWidth(140);
    connect(nextBtn, &QPushButton::clicked, this, [this, dlg]() {
        dlg->accept();
        showBorrowDrawerOpeningDialog();  // 进入步骤2：抽屉打开中
    });
    btnLayout->addWidget(nextBtn);

    mainLayout->addLayout(btnLayout);
    dlg->exec();
    dlg->deleteLater();
}

/**
 * 第二步：抽屉打开中（动态旋转框）
 *   提示用户对应抽屉已打开，按清单借用工具
 * 加入待借用工具列表，便于用户按列表取用
 */
void ToolBorrowPage::showBorrowDrawerOpeningDialog() {
    // 根据借用工具数量动态调整高度，确保列表完整显示
    // 用展开清单显示，每行1件1位置
    int listCount = m_expandedBorrowList.size();

    DrawerOpeningDialog dlg(QStringLiteral("抽屉打开中"), QStringLiteral("抽屉已打开"),
        QStringLiteral("对应工具抽屉已自动打开，<br/>"
                       "请按下方清单取用工具后手动关闭抽屉再点击下一步。"),
        28, 48, this);  // [等价保留] 本页历史边距28/动画区高48
    dlg.setFixedSize(560, qMax(460, 320 + listCount * 36));

    // 待借用工具列表，便于用户按列表取用
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
        dlg.bodyLayout()->addWidget(table);
    }

    if (dlg.exec() == QDialog::Accepted) {
        showToolVerifyDialog();  // 进入步骤3：工具核对
    }
}

/**
 * 第三步：工具核对对话框（假异常提示）
 *   提示用户工具未正确放置/数量不符，确认后执行实际借用
 */
void ToolBorrowPage::showToolVerifyDialog() {
    // 右上角倒计时+左下角忽略+告警入库+退出系统
    // 展开清单取首个位置展示
    QString firstPosition = m_expandedBorrowList.isEmpty() ? QStringLiteral("A-01")
        : (m_expandedBorrowList.first().position.isEmpty() ? QStringLiteral("A-01") : m_expandedBorrowList.first().position);

    VerifyAlertDialog dlg(QStringLiteral("工具核对"), QStringLiteral("工具核对异常"),
        QStringLiteral(
        "检测到 <b>%1</b> 位置的工具视觉识别与清单不符，<br/>"
        "可能原因：误取相邻柜位工具、标签损坏或柜位错位。<br/><br/>"
        "请重新核对工具后点击确认，系统将完成借用登记。").arg(firstPosition),
        "font-size:14px;color:#555;line-height:1.7;background:transparent;",
        QStringLiteral("取消借用"), QStringLiteral("✓ 确认完成核对"), 160,
        AppConfig::instance().borrowReturnBuffer(), this);
    dlg.setFixedSize(520, 420);

    int result = dlg.exec();

    if (result == 2) {
        // 忽略或倒计时结束：写告警日志（闭环）
        int alertToolId = 0;
        QString alertToolCode;
        if (!m_selectedTools.isEmpty()) {
            alertToolId = m_selectedTools.first().toolId;
            alertToolCode = m_selectedTools.first().toolCode;
        }
        AlertService alertSvc;
        alertSvc.recordVerifyAlert(m_user["userId"].toInt(), alertToolId, alertToolCode,
            QStringLiteral("工具核对异常：%1位置工具视觉识别与清单不符，用户忽略告警或倒计时超时").arg(firstPosition));

        MessageDialog::showWarning(nullptr, QStringLiteral("告警已记录"),
            QStringLiteral("工具核对异常告警已记录到系统告警，页面已重置。"));
        QMetaObject::invokeMethod(this, "refresh", Qt::QueuedConnection);
        m_selectedToolIds.clear();
        m_selectedTools.clear();
    } else if (result == QDialog::Accepted) {
        executeBorrow();  // 进入步骤4：执行借用
    }
}

/**
 * 第三步：执行借用（DAO层写入数据库）
 * 支持多工具批量借用
 * 通过BorrowService执行实际借用操作，写入tool_borrow_record表
 * 成功后弹出美观的成功提示
 */
void ToolBorrowPage::executeBorrow() {
    BorrowService svc;
    // successTools存QPair<名称,数量>，成功对话框按件数显示
    QList<QPair<QString, int>> successTools;
    QStringList failTools;
    int successCount = 0;

    // 逐个工具借用
    // MySQL flow_no有UNIQUE约束，多工具共用同一flowNo会导致只有第1条插入成功
    // 修复方案：每个工具的flowNo加序号后缀(-01,-02,...)，既保持批次关联性又满足UNIQUE约束
    // 展开清单(m_expandedBorrowList)，每条对应一个位置，quantity=1
    int totalTools = m_expandedBorrowList.size();
    int seqIndex = 0;
    for (const auto& tool : m_expandedBorrowList) {
        // 生成带序号的flowNo：原flowNo + "-01" / "-02" / ...
        QString toolFlowNo = m_flowNo;
        if (totalTools > 1) {
            toolFlowNo = QStringLiteral("%1-%2")
                .arg(m_flowNo)
                .arg(seqIndex + 1, 2, 10, QChar('0'));
        }
        ++seqIndex;
        // 使用每个工具自己的 quantity，而非统一的 m_pendingQuantity
        // reason为用户选中的任务类型名称列表，保持归还页"任务类型"列与借用时一致
        QString reason = m_selectedTypeNames.isEmpty()
            ? QStringLiteral("任务借用")
            : m_selectedTypeNames.join("、");
        // 直接用展开清单中保存的mappingId，不重新查
        // 根因：原代码查"第一个in_stock位置"可能匹配到错误位置，
        // 导致借用记录mappingId与实际借用的位置不一致→数据混乱
        // 注意：展开清单时已保存mappingId，直接用
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
            // 展开后每条quantity=1，按工具名汇总数量显示
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
        // 借用成功 - 显示美观的成功提示
        ResultDialog successDlg(QStringLiteral("借用成功"), QStringLiteral("✅"),
            QStringLiteral("借用成功！"), QStringLiteral("#43a047"), 16, this);
        successDlg.setFixedSize(460, qMax(380, 300 + successTools.size() * 30));

        // 借用详情 — 去掉绿色背景，纯文本显示工具清单，更清爽
        auto* detailFrame = new QFrame();
        detailFrame->setStyleSheet("background:transparent;border:none;");
        auto* detailLayout = new QVBoxLayout(detailFrame);
        detailLayout->setSpacing(6);
        detailLayout->setContentsMargins(0, 8, 0, 8);

        // 显示实际借用数量，而非恒定的m_pendingQuantity
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
        successDlg.bodyLayout()->addWidget(detailFrame);

        // 提示
        successDlg.setHint(QStringLiteral("请在预计归还时间前归还，逾期将产生记录"));

        // 确定按钮 — 缩小按钮尺寸，44px，18px字体→15px，去掉过大padding
        successDlg.addFinishButtonCentered(QStringLiteral("知道了"), QString(
            "QPushButton{ background:#43a047;color:white;border:none;border-radius:10px;"
            "padding:8px 28px;font-size:15px;font-weight:600;min-height:44px;}"
            "QPushButton:hover{background:#388e3c;}"
            "QPushButton:pressed{background:#2e7d32;}"
        ));

        successDlg.exec();

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
        // 基于任务类型生成流水号
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


// 全部工具表分页
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

// 借用记录表分页
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
