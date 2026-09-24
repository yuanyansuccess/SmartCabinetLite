/**
 * @file ToolCheckoutPage.cpp
 * @brief 工具出库页面 — 批量出库、出库记录、二次确认、分页
 * @author 袁燕
 */
#include "ToolCheckoutPage.h"
#include "utils/StyleHelper.h"
#include "services/ToolService.h"
#include "services/BorrowService.h"
#include "components/SoftKeyboard.h"
#include "components/MultiSelectFilter.h"
#include "db/ToolDAO.h"               // 出库扣减库存和更新映射表状态
#include "db/RecordDAO.h"             // 读取和写入操作日志
#include "common/AppConfig.h"         // 获取当前用户
#include "common/Constants.h"         // 分页常量
#include "db/AlertDAO.h"              // 写入告警
#include "model/AlertLog.h"           // AlertLog实体
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QGroupBox>
#include <QTabWidget>
#include <QDateTime>
#include <QRegularExpression>
#include <QHBoxLayout>
#include <QHeaderView>
#include "components/MessageDialog.h"
#include <QCheckBox>
#include <QGroupBox>
#include <QDialog>
#include <QFrame>
#include <QTimer>            // [2026-06-27] 抽屉打开动画
#include <algorithm>

ToolCheckoutPage::ToolCheckoutPage(QWidget* parent) : QWidget(parent) {
    setupUI();
}

void ToolCheckoutPage::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    // 标题
    auto* title = new QLabel(QStringLiteral("工具出库管理"));
    title->setStyleSheet("font-size:20px;font-weight:700;color:#1a1a2e;");
    mainLayout->addWidget(title);

    // [2026-06-27] 双选项卡结构：待出库 + 出库记录（参考借用页设计）
    m_tabWidget = new QTabWidget();
    m_tabWidget->setStyleSheet(QString(
        "QTabWidget::pane { border: none; background: transparent; }"
        "QTabBar::tab { background: #f0f2f5; color: #666; padding: 14px 28px; "
        "  font-size: 17px; font-weight: 600; border-radius: 12px 12px 0 0; "
        "  min-height: 48px; margin-right: 4px; }"
        "QTabBar::tab:selected { background: white; color: %1; border-bottom: 3px solid %1; }"
        "QTabBar::tab:hover { background: #e8f0fe; }"
    ).arg(StyleHelper::primaryColor()));

    // Tab1: 待出库
    auto* checkoutWidget = createCheckoutTab();
    m_tabWidget->addTab(checkoutWidget, QStringLiteral("📤 待出库"));

    // Tab2: 出库记录
    auto* recordWidget = createRecordTab();
    m_tabWidget->addTab(recordWidget, QStringLiteral("📋 出库记录"));

    // [2026-06-27] Tab切换时加载对应数据
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &ToolCheckoutPage::onTabChanged);

    mainLayout->addWidget(m_tabWidget);

    // 初始加载
    loadTools();
}

// [2026-06-27] Tab1: 待出库操作面板（从原setupUI提取）
QWidget* ToolCheckoutPage::createCheckoutTab() {
    auto* widget = new QWidget();
    auto* mainLayout = new QVBoxLayout(widget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    // 搜索筛选行
    auto* searchRow = new QHBoxLayout();
    searchRow->setSpacing(12);

    auto* searchInputWrap = new QFrame();
    searchInputWrap->setAttribute(Qt::WA_StyledBackground, true);
    searchInputWrap->setFixedHeight(48);
    searchInputWrap->setStyleSheet("QFrame{border:2px solid #e0e0e0;border-radius:12px;background:#fff;}");
    auto* searchInputLayout = new QHBoxLayout(searchInputWrap);
    searchInputLayout->setContentsMargins(0, 0, 1, 0);
    searchInputLayout->setSpacing(0);

    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索工具名称..."));
    m_searchEdit->setStyleSheet(
        "QLineEdit{border:none;padding:0 16px;font-size:16px;background:transparent;color:#333;min-height:42px;}"
    );
    m_searchEdit->installEventFilter(this);
    searchInputLayout->addWidget(m_searchEdit, 1);

    auto* kbdBtn = new QPushButton(QStringLiteral("⌨"));
    kbdBtn->setFixedSize(46, 44);
    kbdBtn->setCursor(Qt::PointingHandCursor);
    kbdBtn->setStyleSheet(
        "QPushButton{border:none;border-radius:0 10px 10px 0;"
        "background:#f0f2f5;font-size:22px;color:#888;}"
        "QPushButton:hover{background:#e6f0ff;color:#4da3ff;}"
    );
    connect(kbdBtn, &QPushButton::clicked, this, &ToolCheckoutPage::onSearchFieldClicked);
    searchInputLayout->addWidget(kbdBtn);

    m_categoryFilter = new MultiSelectFilter(QStringLiteral("全部类别"), this);
    m_categoryFilter->setOptions({QStringLiteral("电动工具"), QStringLiteral("手动工具"),
        QStringLiteral("测量工具"), QStringLiteral("焊接工具"), QStringLiteral("照明工具")});
    connect(m_categoryFilter, &MultiSelectFilter::selectionChanged, this, [this](const QStringList&) { m_currentPage = 1; loadTools(); });

    searchRow->addWidget(searchInputWrap, 1);
    searchRow->addWidget(m_categoryFilter);
    searchInputWrap->setMaximumWidth(360);

    // 查询+重置按钮（参考人员管理页风格）
    // 修复：按钮被拉伸，增加setMaximumWidth+末尾addStretch
    m_searchBtn = new QPushButton(QStringLiteral("查询"));
    m_searchBtn->setFixedHeight(48);
    m_searchBtn->setMaximumWidth(100);
    m_searchBtn->setStyleSheet(
        "QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:12px;"
        "padding:0 24px;font-size:16px;font-weight:700;}"
        "QPushButton:hover{background:#3d8ae0;}"
        "QPushButton:pressed{background:#2e7ad6;}"
    );
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    connect(m_searchBtn, &QPushButton::clicked, this, [this]() { m_currentPage = 1; loadTools(); });

    m_searchResetBtn = new QPushButton(QStringLiteral("重置"));
    m_searchResetBtn->setFixedHeight(48);
    m_searchResetBtn->setMaximumWidth(100);
    m_searchResetBtn->setStyleSheet(
        "QPushButton{background:#fff;color:#4da3ff;border:2px solid #4da3ff;border-radius:12px;"
        "padding:0 24px;font-size:16px;font-weight:700;}"
        "QPushButton:hover{background:#f0f7ff;}"
        "QPushButton:pressed{background:#e6f0ff;}"
    );
    m_searchResetBtn->setCursor(Qt::PointingHandCursor);
    connect(m_searchResetBtn, &QPushButton::clicked, this, [this]() {
        m_searchEdit->clear();
        m_categoryFilter->selectAll();
        m_selectedSet.clear();
        m_selectedHint->setText(QStringLiteral("（已选 0 件）"));
        m_currentPage = 1;
        loadTools();
    });

    searchRow->addWidget(m_searchBtn);
    searchRow->addWidget(m_searchResetBtn);
    searchRow->addStretch();
    mainLayout->addLayout(searchRow);

    // 警告提示
    auto* tipBar = new QFrame();
    tipBar->setObjectName("tipBar");
    tipBar->setStyleSheet("QFrame#tipBar{background:#fff8e1;border:1px solid #ffd54f;border-radius:10px;padding:12px 16px;}");
    auto* tipLayout = new QHBoxLayout(tipBar);
    tipLayout->setContentsMargins(12, 8, 12, 8);
    auto* tipIcon = new QLabel("⚠");
    tipIcon->setStyleSheet(QString("font-size:20px;color:%1;background:transparent;").arg(StyleHelper::warningColor()));
    auto* tipText = new QLabel(QStringLiteral("出库操作将从系统中移除该工具识别信息，工具将不再被系统追踪管理。请确认后操作。"));
    tipText->setStyleSheet(QString("font-size:15px;color:%1;background:transparent;").arg(StyleHelper::textColor()));
    tipText->setWordWrap(true);
    tipLayout->addWidget(tipIcon);
    tipLayout->addWidget(tipText, 1);
    mainLayout->addWidget(tipBar);

    // 选择待出库工具面板
    auto* panel = new QGroupBox(QStringLiteral("选择待出库工具"));
    panel->setStyleSheet(QString("QGroupBox{font-size:16px;font-weight:700;color:%1;"
                                "border:1px solid %2;border-radius:12px;margin-top:10px;padding:16px;}"
                                "QGroupBox::title{padding:0 10px;}")
                         .arg(StyleHelper::textColor(), StyleHelper::borderColor()));
    auto* panelLayout = new QVBoxLayout(panel);

    m_selectedHint = new QLabel();
    m_selectedHint->setWordWrap(true);
    m_selectedHint->setStyleSheet(QString("font-size:15px;color:%1;font-weight:600;").arg(StyleHelper::textSecondary()));
    panelLayout->addWidget(m_selectedHint);

    m_table = new QTableWidget();
    // [V2.03g] 删除"出库数量"列（一个位置=一个工具，数量恒为1）
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("选择"), QStringLiteral("编号"), QStringLiteral("工具名称"),
        QStringLiteral("规格"), QStringLiteral("机组"), QStringLiteral("位置"),
        QStringLiteral("出库原因"), QStringLiteral("状态")
    });
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->setAlternatingRowColors(false);
    m_table->setColumnWidth(0, 60);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    for (int i = 1; i < 7; i++) {
        m_table->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
    }
    m_table->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Fixed);
    m_table->setColumnWidth(7, 130);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setMinimumSectionSize(50);
    m_table->verticalHeader()->setMinimumSectionSize(48);
    panelLayout->addWidget(m_table, 1);

    auto* pageSep = new QFrame();
    pageSep->setStyleSheet("QFrame{border-top:1px solid #f0f0f0;}");
    pageSep->setFixedHeight(1);
    panelLayout->addWidget(pageSep);

    auto* pageLayout = new QHBoxLayout();
    pageLayout->setContentsMargins(18, 12, 18, 12);
    pageLayout->setSpacing(6);

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
    connect(m_prevBtn, &QPushButton::clicked, this, &ToolCheckoutPage::onPrevPage);

    m_nextBtn = new QPushButton(QStringLiteral("下一页"));
    m_nextBtn->setStyleSheet(
        "QPushButton{border:1px solid #ddd;border-radius:6px;padding:5px 12px;"
        "font-size:13px;font-weight:600;color:#555;background:#fff;min-height:30px;}"
        "QPushButton:hover{border-color:#4da3ff;color:#4da3ff;}"
        "QPushButton:disabled{opacity:0.35;}"
    );
    m_nextBtn->setCursor(Qt::PointingHandCursor);
    connect(m_nextBtn, &QPushButton::clicked, this, &ToolCheckoutPage::onNextPage);

    m_pageLabel = new QLabel(QStringLiteral("第 1 页"));
    m_pageLabel->setStyleSheet("font-size:13px;color:#999;padding:0 4px;");

    pageLayout->addStretch();
    pageLayout->addWidget(m_prevBtn);
    pageLayout->addWidget(m_pageLabel);
    pageLayout->addWidget(m_nextBtn);
    pageLayout->addWidget(m_totalLabel);
    panelLayout->addLayout(pageLayout);

    mainLayout->addWidget(panel, 1);

    // 底部按钮
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();

    m_resetBtn = new QPushButton(QStringLiteral("重置"));
    m_resetBtn->setStyleSheet(StyleHelper::buttonDefault());
    m_resetBtn->setCursor(Qt::PointingHandCursor);
    m_resetBtn->setMinimumHeight(44);
    m_resetBtn->setMinimumWidth(120);
    connect(m_resetBtn, &QPushButton::clicked, this, &ToolCheckoutPage::onReset);
    btnRow->addWidget(m_resetBtn);

    m_batchBtn = new QPushButton(QStringLiteral("批量出库"));
    m_batchBtn->setStyleSheet(StyleHelper::buttonPrimary());
    m_batchBtn->setCursor(Qt::PointingHandCursor);
    m_batchBtn->setMinimumHeight(44);
    m_batchBtn->setMinimumWidth(140);
    connect(m_batchBtn, &QPushButton::clicked, this, &ToolCheckoutPage::onBatchCheckout);
    btnRow->addWidget(m_batchBtn);

    mainLayout->addLayout(btnRow);
    return widget;
}

// [2026-06-27] Tab2: 出库历史记录
QWidget* ToolCheckoutPage::createRecordTab() {
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("出库历史记录"));
    title->setStyleSheet(QString("font-size:20px;font-weight:bold;color:%1;").arg(StyleHelper::textColor()));
    layout->addWidget(title);

    // 出库记录表格：出库时间/工具名称/工具编号/出库数量/出库原因/操作人
    m_recordTable = new QTableWidget();
    m_recordTable->setColumnCount(6);
    m_recordTable->setHorizontalHeaderLabels({
        QStringLiteral("出库时间"), QStringLiteral("工具名称"), QStringLiteral("工具编号"),
        QStringLiteral("出库数量"), QStringLiteral("出库原因"), QStringLiteral("操作人")
    });
    m_recordTable->horizontalHeader()->setStretchLastSection(false);
    m_recordTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_recordTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_recordTable->verticalHeader()->setVisible(false);
    // [V2.02 2026-06-28] 移除内联表格QSS，使用全局QSS统一表格样式
    // 列宽：时间/名称/编号/原因 Stretch，数量/操作人 Fixed窄列
    for (int i = 0; i < 6; i++) {
        if (i == 3 || i == 5) {
            m_recordTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Fixed);
        } else {
            m_recordTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
        }
    }
    m_recordTable->setColumnWidth(3, 90);
    m_recordTable->setColumnWidth(5, 100);
    m_recordTable->horizontalHeader()->setStretchLastSection(false);
    m_recordTable->verticalHeader()->setMinimumSectionSize(48);
    layout->addWidget(m_recordTable, 1);

    // 分页栏
    auto* pageLayout = new QHBoxLayout();
    pageLayout->setContentsMargins(0, 8, 0, 0);
    pageLayout->setSpacing(6);

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
    connect(m_recordPrevBtn, &QPushButton::clicked, this, &ToolCheckoutPage::onRecordPrevPage);

    m_recordNextBtn = new QPushButton(QStringLiteral("下一页"));
    m_recordNextBtn->setStyleSheet(
        "QPushButton{border:1px solid #ddd;border-radius:6px;padding:5px 12px;"
        "font-size:13px;font-weight:600;color:#555;background:#fff;min-height:30px;}"
        "QPushButton:hover{border-color:#4da3ff;color:#4da3ff;}"
        "QPushButton:disabled{opacity:0.35;}"
    );
    m_recordNextBtn->setCursor(Qt::PointingHandCursor);
    connect(m_recordNextBtn, &QPushButton::clicked, this, &ToolCheckoutPage::onRecordNextPage);

    m_recordPageLabel = new QLabel(QStringLiteral("第 1 页"));
    m_recordPageLabel->setStyleSheet("font-size:13px;color:#999;padding:0 4px;");

    pageLayout->addStretch();
    pageLayout->addWidget(m_recordPrevBtn);
    pageLayout->addWidget(m_recordPageLabel);
    pageLayout->addWidget(m_recordNextBtn);
    pageLayout->addWidget(m_recordTotalLabel);
    layout->addLayout(pageLayout);

    return widget;
}

void ToolCheckoutPage::refresh() {
    // [V2.03 2026-06-27] 切换菜单回到出库页时，完全还原到初始状态：
    // 1. 切回Tab1(待出库) 2. 清空已选工具 3. 重置页码 4. 清空搜索 5. 分类全选 6. 重新加载
    if (m_tabWidget) m_tabWidget->setCurrentIndex(0);
    m_selectedSet.clear();
    m_currentPage = 1;
    m_searchEdit->clear();
    m_categoryFilter->selectAll();
    m_selectedHint->setText(QStringLiteral("（已选 0 件）"));
    loadTools();
}

int ToolCheckoutPage::selectedCount() const {
    return m_selectedSet.size();
}

void ToolCheckoutPage::loadTools() {
    // [V8.0 2026-06-28] 性能优化：禁用重绘+信号阻塞，避免重建过程中频繁刷新
    //   作者：袁燕
    m_table->setUpdatesEnabled(false);
    m_table->blockSignals(true);
    // [2026-06-27] 改为客户端全量加载+排序+分页，选中工具自动跳首页（与借用页一致）
    BorrowService svc;
    QJsonObject result = svc.getAllInStockTools(1, SC::PAGE_SIZE_UNLIMITED);
    QJsonArray allTools = result["list"].toArray();
    m_totalRecords = allTools.size();

    // [2026-06-24v8] 客户端多选分类过滤 + [V2.04] 关键字搜索过滤
    QString keyword = m_searchEdit->text().trimmed();
    QStringList selCats = m_categoryFilter->selectedOptions();
    bool allSel = selCats.size() == 5 || selCats.isEmpty();
    QSet<QString> catSet(selCats.begin(), selCats.end());

    QJsonArray filteredTools;
    for (const auto& t : allTools) {
        QJsonObject toolObj = t.toObject();
        QString cat = toolObj["category"].toString();
        if (!allSel && !catSet.contains(cat)) continue;
        // [V2.04] 关键字搜索：匹配工具名/编号/规格
        if (!keyword.isEmpty()) {
            QString name = toolObj["toolName"].toString();
            QString code = toolObj["toolCode"].toString();
            QString spec = toolObj["spec"].toString();
            if (!name.contains(keyword, Qt::CaseInsensitive) &&
                !code.contains(keyword, Qt::CaseInsensitive) &&
                !spec.contains(keyword, Qt::CaseInsensitive)) continue;
        }
        filteredTools.append(t);
    }
    m_totalRecords = filteredTools.size();

    // [2026-06-27] 排序：已选工具 → 未选工具
    QJsonArray selectedList;
    QJsonArray normalList;
    for (int i = 0; i < filteredTools.size(); ++i) {
        QJsonObject t = filteredTools[i].toObject();
        int toolId = t["toolId"].toInt();
        if (m_selectedSet.contains(toolId)) {
            selectedList.append(t);
        } else {
            normalList.append(t);
        }
    }
    QJsonArray sortedList;
    for (const auto& t : selectedList) sortedList.append(t);
    for (const auto& t : normalList) sortedList.append(t);

    // 分页切片
    int totalPages = qMax(1, (m_totalRecords + m_pageSize - 1) / m_pageSize);
    if (m_currentPage > totalPages) m_currentPage = totalPages;
    if (m_currentPage < 1) m_currentPage = 1;
    m_pageLabel->setText(QStringLiteral("第 %1/%2 页").arg(m_currentPage).arg(totalPages));
    m_totalLabel->setText(QStringLiteral("共 %1 条").arg(m_totalRecords));
    m_prevBtn->setEnabled(m_currentPage > 1);
    m_nextBtn->setEnabled(m_currentPage < totalPages);

    // 更新已选提示
    m_selectedHint->setText(QStringLiteral("（已选 %1 件）").arg(selectedCount()));

    int startIdx = (m_currentPage - 1) * m_pageSize;
    int endIdx = qMin(startIdx + m_pageSize, sortedList.size());
    m_tools = QJsonArray();
    for (int i = startIdx; i < endIdx; ++i) {
        m_tools.append(sortedList[i].toObject());
    }

    // 更新表格
    m_table->setRowCount(m_tools.size());

    for (int i = 0; i < m_tools.size(); ++i) {
        QJsonObject t = m_tools[i].toObject();
        // 选中标识改为mappingId（位置唯一），同一工具不同位置可独立选中
        int mappingId = t["mappingId"].toInt();

        // 复选框 [V7.1] 使用统一表格复选框样式
        auto* check = new QCheckBox();
        check->setChecked(m_selectedSet.contains(mappingId));
        check->setStyleSheet(StyleHelper::tableCheckBox());
        m_table->setCellWidget(i, 0, check);
        connect(check, &QCheckBox::toggled, this, [this, mappingId](bool checked) {
            if (checked) m_selectedSet.insert(mappingId);
            else m_selectedSet.remove(mappingId);
            m_selectedHint->setText(QStringLiteral("（已选 %1 件）").arg(selectedCount()));
            // [V8.0 2026-06-28] 性能优化：勾选不再调用loadTools()全量重建
            // 原逻辑：每次勾选→查DB+重建所有行widget→卡顿
            // 新逻辑：仅更新选中集合+提示文字
            //   作者：袁燕
        });

        m_table->setItem(i, 1, new QTableWidgetItem(t["toolCode"].toString()));  // [2026-06-27] 显示工具自身编号，非自增ID
        m_table->setItem(i, 2, new QTableWidgetItem(t["toolName"].toString()));
        m_table->setItem(i, 3, new QTableWidgetItem(t["spec"].toString()));
        m_table->setItem(i, 4, new QTableWidgetItem(t["category"].toString()));
        // 位置直接用DAO已格式化的position（权威数据源）
        // Bug修复：原代码从DAO格式化后的"A-1-2"再提取数字重新拼接→双重格式化→"A-01-12"
        // DAO层ToolDAO::findAll已LEFT JOIN映射表格式化好position，直接用即可
        m_table->setItem(i, 5, new QTableWidgetItem(t["position"].toString()));

        // 出库原因 [V7.1] 使用紧凑表格下拉框样式
        auto* reasonCombo = new QComboBox();
        reasonCombo->addItems({QStringLiteral("请选择原因"), QStringLiteral("报废更换"), QStringLiteral("损坏退役"),
                              QStringLiteral("调拨其他机组"), QStringLiteral("升级替换"), QStringLiteral("超期淘汰"), QStringLiteral("其他原因")});
        reasonCombo->setStyleSheet(StyleHelper::tableComboBox());
        m_table->setCellWidget(i, 6, reasonCombo);

        // [V2.03g] 删除"出库数量"列（一个位置=一个工具，数量恒为1）

        // 操作按钮 [2026-06-27] 统一风格：禁用态灰色"待出库"，启用态主色蓝"出库"
        // [2026-06-27] 修复：重建表格时根据 m_selectedSet 设置初始状态，确保状态列与复选框同步
        // 选中标识改为mappingId
        bool isSelected = m_selectedSet.contains(mappingId);
        auto* opBtn = new QPushButton(isSelected ? QStringLiteral("出库") : QStringLiteral("待出库"));
        opBtn->setStyleSheet(StyleHelper::tableActionBtn());
        opBtn->setCursor(Qt::PointingHandCursor);
        opBtn->setEnabled(isSelected);
        opBtn->setFixedHeight(40);
        connect(opBtn, &QPushButton::clicked, this, [this, i] { m_table->selectRow(i); });
        // [V8.0 2026-06-28] #16修复：合并两个toggled信号为一个，减少信号回调开销
        // 原逻辑：check连两个toggled信号（一个更新m_selectedSet，一个更新opBtn）
        // 新逻辑：合并为一个lambda同时更新m_selectedSet和opBtn
        //   作者：袁燕
        // [注] 第一个toggled连接在上方（更新m_selectedSet），这里仅保留opBtn更新
        connect(check, &QCheckBox::toggled, opBtn, [opBtn](bool checked) {
            opBtn->setEnabled(checked);
            opBtn->setText(checked ? QStringLiteral("出库") : QStringLiteral("待出库"));
        });
        m_table->setCellWidget(i, 7, opBtn);
        m_table->setRowHeight(i, 56);
    }

    // [V8.0 2026-06-28] 恢复表格重绘和信号
    m_table->blockSignals(false);
    m_table->setUpdatesEnabled(true);
}

void ToolCheckoutPage::onBatchCheckout() {
    if (selectedCount() == 0) {
        MessageDialog::showError(this, QStringLiteral("提示"), QStringLiteral("请至少选择一件工具"));
        return;
    }
    // [2026-06-27] 校验每个已选工具的出库原因和出库数量
    QStringList issues;
    for (int i = 0; i < m_tools.size(); ++i) {
        QJsonObject t = m_tools[i].toObject();
        // 选中标识改为mappingId
        int mappingId = t["mappingId"].toInt();
        if (!m_selectedSet.contains(mappingId)) continue;

        QString toolName = t["toolName"].toString();
        auto* reasonCombo = qobject_cast<QComboBox*>(m_table->cellWidget(i, 6));

        // 检查出库原因（索引0为"请选择原因"，视为未选）
        if (reasonCombo && reasonCombo->currentIndex() == 0) {
            issues.append(QStringLiteral("· %1：请选择出库原因").arg(toolName));
        }
        // [V2.03g] 删除出库数量校验（数量恒为1）
    }
    if (!issues.isEmpty()) {
        QString msg = QStringLiteral("以下工具信息不完整，请补充后再出库：\n\n") + issues.join("\n");
        MessageDialog::showWarning(this, QStringLiteral("出库信息不完整"), msg);
        return;
    }
    showCheckoutListDialog();
}

// [2026-06-27] 收集已选工具的详细信息（用于三步出库流程）
QList<ToolCheckoutPage::CheckoutItem> ToolCheckoutPage::collectSelectedItems() const {
    QList<CheckoutItem> items;
    for (int i = 0; i < m_tools.size(); ++i) {
        QJsonObject t = m_tools[i].toObject();
        // 选中标识改为mappingId
        int mappingId = t["mappingId"].toInt();
        if (!m_selectedSet.contains(mappingId)) continue;

        // 读取表格中的出库原因
        auto* reasonCombo = qobject_cast<QComboBox*>(m_table->cellWidget(i, 6));

        CheckoutItem item;
        item.mappingId = mappingId;
        item.toolId = t["toolId"].toInt();
        item.toolCode = t["toolCode"].toString();
        item.toolName = t["toolName"].toString();
        item.position = t["position"].toString();
        item.quantity = 1;  // [V2.03g] 数量恒为1（一个位置=一个工具）
        item.reason = reasonCombo ? reasonCombo->currentText() : QStringLiteral("其他原因");
        items.append(item);
    }
    return items;
}

// ═══════════ 步骤1: 待出库清单对话框 ═══════════
void ToolCheckoutPage::showCheckoutListDialog() {
    QList<CheckoutItem> items = collectSelectedItems();
    if (items.isEmpty()) return;

    QDialog* dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("出库清单"));
    dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dlg->setStyleSheet(StyleHelper::dialogStyle());
    int dlgHeight = qMax(440, 320 + items.size() * 40);
    dlg->setFixedSize(560, dlgHeight);

    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(32, 28, 32, 24);
    layout->setSpacing(16);

    // 标题
    auto* titleRow = new QHBoxLayout();
    auto* iconLabel = new QLabel(QStringLiteral("📦"));
    iconLabel->setStyleSheet("font-size:28px;background:transparent;");
    auto* titleLabel = new QLabel(QStringLiteral("待出库清单"));
    titleLabel->setStyleSheet("font-size:22px;font-weight:bold;color:#1a1a2e;background:transparent;");
    titleRow->addWidget(iconLabel);
    titleRow->addWidget(titleLabel);
    titleRow->addStretch();
    layout->addLayout(titleRow);

    // 提示条：抽屉已打开
    auto* tipFrame = new QFrame();
    tipFrame->setObjectName("tipFrame");
    tipFrame->setStyleSheet(
        "QFrame#tipFrame{background:#e6f7ff;border:1px solid #91d5ff;"
        "border-radius:10px;padding:10px 14px;}"
    );
    auto* tipLayout = new QHBoxLayout(tipFrame);
    tipLayout->setContentsMargins(12, 8, 12, 8);
    auto* tipIcon = new QLabel(QStringLiteral("🔓"));
    tipIcon->setStyleSheet("font-size:20px;background:transparent;");
    auto* tipText = new QLabel(QStringLiteral("对应抽屉/箱子已打开，请进行出库操作"));
    tipText->setStyleSheet("font-size:15px;color:#1890ff;font-weight:600;background:transparent;");
    tipLayout->addWidget(tipIcon);
    tipLayout->addWidget(tipText, 1);
    layout->addWidget(tipFrame);

    // 清单表格
    auto* table = new QTableWidget();
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({
        QStringLiteral("工具名称"), QStringLiteral("出库数量"),
        QStringLiteral("存放地点"), QStringLiteral("出库原因")
    });
    table->setRowCount(items.size());
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
    for (int col = 0; col < 4; ++col) {
        table->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Stretch);
    }
    for (int i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        table->setItem(i, 0, new QTableWidgetItem(it.toolName));
        table->setItem(i, 1, new QTableWidgetItem(QStringLiteral("%1 件").arg(it.quantity)));
        table->setItem(i, 2, new QTableWidgetItem(it.position.isEmpty() ? QStringLiteral("--") : it.position));
        table->setItem(i, 3, new QTableWidgetItem(it.reason));
        table->setRowHeight(i, 38);
    }
    table->setFixedHeight(qMax(160, items.size() * 38 + 38));
    layout->addWidget(table);

    layout->addStretch();

    // 按钮区
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    cancelBtn->setStyleSheet(StyleHelper::buttonDefault());
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setMinimumHeight(44);
    cancelBtn->setMinimumWidth(110);
    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);

    auto* nextBtn = new QPushButton(QStringLiteral("下一步 →"));
    nextBtn->setStyleSheet(StyleHelper::buttonPrimary());
    nextBtn->setCursor(Qt::PointingHandCursor);
    nextBtn->setMinimumHeight(44);
    nextBtn->setMinimumWidth(140);
    connect(nextBtn, &QPushButton::clicked, this, [dlg]() { dlg->accept(); });
    btnRow->addWidget(cancelBtn);
    btnRow->addSpacing(12);
    btnRow->addWidget(nextBtn);
    layout->addLayout(btnRow);

    // 模态执行
    if (dlg->exec() == QDialog::Accepted) {
        delete dlg;
        showCheckoutDrawerOpeningDialog();  // [2026-06-27] 进入步骤2：抽屉打开中
    } else {
        delete dlg;
    }
}

// ═══════════ 步骤2: 抽屉打开中（三点跳动动画） ═══════════
void ToolCheckoutPage::showCheckoutDrawerOpeningDialog() {
    QList<CheckoutItem> items = collectSelectedItems();
    if (items.isEmpty()) return;

    QDialog* dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("抽屉打开中"));
    dlg->setFixedSize(480, qMax(460, 320 + items.size() * 32));
    dlg->setStyleSheet(StyleHelper::dialogStyle());
    dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);

    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(32, 32, 32, 24);
    layout->setSpacing(14);

    // 标题
    auto* titleLabel = new QLabel(QStringLiteral("柜子已打开"));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size:22px;font-weight:bold;color:#1a1a2e;background:transparent;");
    layout->addWidget(titleLabel);

    // 动态加载指示器 — 三点跳动动画（与借用/归还页完全一致）
    auto* spinnerContainer = new QWidget();
    spinnerContainer->setFixedHeight(60);
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

    // 三点跳动动画
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
        "请照下方清单从指定位置取用工具并手动关闭抽屉后点击下一步。"
    ));
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setTextFormat(Qt::RichText);
    descLabel->setStyleSheet("font-size:15px;color:#555;line-height:1.6;background:transparent;");
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    // 出库清单（精简版表格：工具名/位置/数量）
    auto* table = new QTableWidget();
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({
        QStringLiteral("工具名称"), QStringLiteral("存放位置"), QStringLiteral("出库数量")
    });
    table->setRowCount(items.size());
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setStyleSheet(QString(
        "QTableWidget { border:1px solid #f0f0f0; background:#fff; border-radius:10px; "
        "  font-family:\"Microsoft YaHei\",sans-serif; font-size:13px; }"
        "QTableWidget::item { padding:8px 12px; color:#333; border:none; "
        "  border-bottom:1px solid #f3f3f3; }"
        "QHeaderView::section { background:#f8f9fb; color:#666; font-weight:600; "
        "  font-size:12px; padding:8px 12px; border:none; border-bottom:1px solid #f0f0f0; }"
    ));
    for (int col = 0; col < 3; ++col) {
        table->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Stretch);
    }
    for (int i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        table->setItem(i, 0, new QTableWidgetItem(it.toolName));
        table->setItem(i, 1, new QTableWidgetItem(it.position.isEmpty() ? QStringLiteral("--") : it.position));
        table->setItem(i, 2, new QTableWidgetItem(QStringLiteral("%1 件").arg(it.quantity)));
        table->setRowHeight(i, 32);
    }
    table->setFixedHeight(qMax(120, items.size() * 32 + 32));
    layout->addWidget(table);

    layout->addStretch();

    // 底部按钮
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    cancelBtn->setStyleSheet(StyleHelper::buttonDefault());
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
        showCheckoutWarningDialog();  // [2026-06-27] 进入步骤3：异常校验
    });
    btnRow->addWidget(cancelBtn);
    btnRow->addSpacing(12);
    btnRow->addWidget(nextBtn);
    layout->addLayout(btnRow);

    dlg->exec();
    dlg->deleteLater();
}

// ═══════════ 步骤2: 抽屉异常警告对话框 ═══════════
void ToolCheckoutPage::showCheckoutWarningDialog() {
    // [V2.03l 2026-06-29] 增加倒计时+忽略+告警入库+重置页面（不退出系统）
    QList<CheckoutItem> items = collectSelectedItems();
    if (items.isEmpty()) return;

    QDialog* dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("出库校验"));
    dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dlg->setStyleSheet(StyleHelper::dialogStyle());
    dlg->setFixedSize(520, 380);

    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(32, 32, 32, 24);
    layout->setSpacing(18);

    // [V2.03l] 顶部行：图标居中 + 右上角倒计时
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
    auto* titleLabel = new QLabel(QStringLiteral("出库校验异常"));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size:22px;font-weight:bold;color:#fa8c16;background:transparent;");
    layout->addWidget(titleLabel);

    // 警告详情
    QString firstPosition = items.first().position.isEmpty() ? QStringLiteral("A-01") : items.first().position;
    auto* descLabel = new QLabel(QStringLiteral(
        "检测到 <b>%1</b> 抽屉未关好，或工具未正确放入回收位。<br/>"
        "请检查抽屉状态后重新校验。"
    ).arg(firstPosition));
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setStyleSheet("font-size:15px;color:#555;line-height:1.6;background:transparent;");
    descLabel->setTextFormat(Qt::RichText);
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    layout->addStretch();

    // [V2.03l] 倒计时定时器
    int* remainSeconds = new int(bufferMinutes * 60);
    QTimer* countdownTimer = new QTimer(dlg);
    countdownTimer->setInterval(1000);
    QObject::connect(countdownTimer, &QTimer::timeout, dlg, [countdownLabel, remainSeconds, dlg, countdownTimer]() {
        (*remainSeconds)--;
        if (*remainSeconds <= 0) {
            countdownTimer->stop();
            dlg->done(2);
        } else {
            int mins = *remainSeconds / 60;
            int secs = *remainSeconds % 60;
            countdownLabel->setText(QStringLiteral("⏱ %1:%2")
                .arg(mins, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0')));
        }
    });
    countdownTimer->start();

    // 忽略按钮回调：关闭对话框走done(2)流程，告警在exec后统一写入
    auto doIgnore = [this, dlg, countdownTimer, remainSeconds]() {
        countdownTimer->stop();
        delete remainSeconds;
        dlg->done(2);
    };

    // 按钮区：左下角忽略 + 右下角取消出库 + 重新校验
    auto* btnRow = new QHBoxLayout();
    auto* ignoreBtn = new QPushButton(QStringLiteral("忽略"));
    ignoreBtn->setStyleSheet("QPushButton{background:#e74c3c;color:#fff;border:none;border-radius:10px;padding:10px 24px;font-size:14px;font-weight:700;min-height:44px;}QPushButton:hover{background:#c0392b;}");
    ignoreBtn->setCursor(Qt::PointingHandCursor);
    connect(ignoreBtn, &QPushButton::clicked, this, doIgnore);
    btnRow->addWidget(ignoreBtn);
    btnRow->addStretch();

    auto* cancelBtn = new QPushButton(QStringLiteral("取消出库"));
    cancelBtn->setStyleSheet(StyleHelper::buttonDefault());
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setMinimumHeight(44);
    cancelBtn->setMinimumWidth(120);
    connect(cancelBtn, &QPushButton::clicked, dlg, [countdownTimer, remainSeconds]() {
        countdownTimer->stop(); delete remainSeconds;
        static_cast<QDialog*>(static_cast<QWidget*>(countdownTimer->parent()))->reject();
    });

    auto* retryBtn = new QPushButton(QStringLiteral("🔄 重新校验"));
    retryBtn->setStyleSheet(StyleHelper::buttonPrimary());
    retryBtn->setCursor(Qt::PointingHandCursor);
    retryBtn->setMinimumHeight(44);
    retryBtn->setMinimumWidth(150);
    connect(retryBtn, &QPushButton::clicked, this, [dlg, countdownTimer, remainSeconds]() {
        countdownTimer->stop(); delete remainSeconds; dlg->accept();
    });
    btnRow->addWidget(cancelBtn);
    btnRow->addSpacing(12);
    btnRow->addWidget(retryBtn);
    layout->addLayout(btnRow);

    int result = dlg->exec();
    countdownTimer->stop();
    if (result == QDialog::Accepted) {
        delete dlg;
        showCheckoutSuccessDialog();
    } else if (result == 2) {
        // 忽略或倒计时结束：写告警日志（闭环）
        db::AlertDAO alertDao;
        AlertLog alert;
        alert.typeId = 2;
        alert.userId = m_user["userId"].toInt();
        if (!m_selectedSet.isEmpty()) {
            int firstMappingId = *m_selectedSet.constBegin();
            for (const QJsonValue& v : m_tools) {
                QJsonObject t = v.toObject();
                if (t["mappingId"].toInt() == firstMappingId) {
                    alert.toolId = t["toolId"].toInt();
                    alert.toolCode = t["toolCode"].toString();
                    break;
                }
            }
        }
        alert.message = QStringLiteral("出库校验异常：%1抽屉未关好或工具未正确放入回收位，用户忽略告警或倒计时超时").arg(firstPosition);
        alert.status = "unhandled";
        alertDao.insertAlert(alert);

        delete dlg;
        MessageDialog::showWarning(nullptr, QStringLiteral("告警已记录"),
            QStringLiteral("出库校验异常告警已记录到系统告警，页面已重置。"));
        m_selectedSet.clear();
        if (m_table) {
            for (int i = 0; i < m_table->rowCount(); ++i) {
                auto* cb = qobject_cast<QCheckBox*>(m_table->cellWidget(i, 0));
                if (cb) cb->setChecked(false);
            }
        }
    } else {
        delete dlg;
    }
}

// ═══════════ 步骤3: 出库成功对话框 ═══════════
void ToolCheckoutPage::showCheckoutSuccessDialog() {
    QList<CheckoutItem> items = collectSelectedItems();
    int totalQty = 0;
    for (const auto& it : items) totalQty += it.quantity;

    QDialog* dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("出库成功"));
    dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dlg->setStyleSheet(StyleHelper::dialogStyle());
    int dlgHeight = qMax(380, 280 + items.size() * 30);
    dlg->setFixedSize(480, dlgHeight);

    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(32, 32, 32, 24);
    layout->setSpacing(14);

    // 成功图标
    auto* iconLabel = new QLabel(QStringLiteral("✅"));
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet("font-size:56px;background:transparent;");
    layout->addWidget(iconLabel);

    // 成功标题
    auto* titleLabel = new QLabel(QStringLiteral("出库成功！"));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size:24px;font-weight:bold;color:#43a047;background:transparent;");
    layout->addWidget(titleLabel);

    // 汇总信息
    auto* summaryLabel = new QLabel(QStringLiteral(
        "共出库 <b style='color:#43a047;font-size:18px;'>%1</b> 件工具，"
        "涉及 <b style='color:#43a047;font-size:18px;'>%2</b> 种工具"
    ).arg(totalQty).arg(items.size()));
    summaryLabel->setAlignment(Qt::AlignCenter);
    summaryLabel->setStyleSheet("font-size:15px;color:#555;background:transparent;");
    summaryLabel->setTextFormat(Qt::RichText);
    layout->addWidget(summaryLabel);

    // 工具清单
    auto* detailFrame = new QFrame();
    detailFrame->setStyleSheet("background:transparent;border:none;");
    auto* detailLayout = new QVBoxLayout(detailFrame);
    detailLayout->setSpacing(6);
    detailLayout->setContentsMargins(20, 8, 20, 8);
    for (const auto& it : items) {
        auto* toolInfo = new QLabel(QStringLiteral("✓ %1  ×%2件").arg(it.toolName).arg(it.quantity));
        toolInfo->setStyleSheet("font-size:14px;font-weight:600;color:#333;");
        detailLayout->addWidget(toolInfo);
    }
    layout->addWidget(detailFrame);

    layout->addStretch();

    // 完成按钮
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* finishBtn = new QPushButton(QStringLiteral("完成"));
    finishBtn->setStyleSheet(StyleHelper::buttonPrimary());
    finishBtn->setCursor(Qt::PointingHandCursor);
    finishBtn->setMinimumHeight(44);
    finishBtn->setMinimumWidth(140);
    connect(finishBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    btnRow->addWidget(finishBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    dlg->exec();
    delete dlg;

    // 出库成功后写入数据库：扣减库存 + 写操作日志 + 更新映射表
    {
        db::ToolDAO toolDao;
        db::RecordDAO recDao;
        for (const auto& it : items) {
            // 更新映射表status='checked_out'（位置维度出库，通过mappingId）
            if (it.mappingId > 0) {
                toolDao.updateMappingStatus(it.mappingId, "checked_out");
            }
            // 扣减库存（current_qty 和 total_qty 都减）
            toolDao.updateStock(it.toolId, -it.quantity);
            // [V8.0 2026-06-28] 出库后current_qty=0则状态设为checked_out(已出库)
            QJsonObject updatedTool = toolDao.findById(it.toolId);
            if (updatedTool["currentQty"].toInt() <= 0) {
                toolDao.updateStatus(it.toolId, "checked_out");
            }

            // 写入操作日志（target_id存tool_code与入库一致）
            QString content = QStringLiteral("出库工具「%1」编号[%2]×%3 位置%4，原因：%5")
                .arg(it.toolName)
                .arg(it.toolCode.isEmpty() ? QStringLiteral("--") : it.toolCode)
                .arg(it.quantity)
                .arg(it.position)
                .arg(it.reason);
            int userId = m_user["userId"].toInt();
            recDao.insertOperationLog(userId, "checkout", "tool", it.toolCode, content);
        }
    }

    // [2026-06-27] 成功出库后清空选中状态并刷新
    m_selectedSet.clear();
    m_currentPage = 1;
    loadTools();
}

void ToolCheckoutPage::onReset() {
    m_selectedSet.clear();
    m_currentPage = 1;  // [2026-06-25] 重置到第1页
    loadTools();
}

// [V8.0 2026-06-28] #18修复：onCheck不再无条件loadTools()全量重建
// 原逻辑：忽略参数直接loadTools()→全量重建
// 新逻辑：空实现，checkbox的toggled信号已在loadTools中处理选择逻辑
//   作者：袁燕
void ToolCheckoutPage::onCheck(int row, bool checked) {
    Q_UNUSED(row);
    Q_UNUSED(checked);
    // 不再调用loadTools()，选择逻辑由checkbox toggled回调处理
}

void ToolCheckoutPage::onToggleAll(bool checked) {
    // [V8.0 2026-06-28] #19优化：全选/取消后仍需loadTools更新UI，但已有setUpdatesEnabled优化
    if (checked) {
        for (int i = 0; i < m_tools.size(); ++i)
            m_selectedSet.insert(m_tools[i].toObject()["toolId"].toInt());
    } else {
        m_selectedSet.clear();
    }
    m_currentPage = 1;
    loadTools();
}

// [V6.6] 搜索框点击弹出软键盘
void ToolCheckoutPage::onSearchFieldClicked() {
    if (!m_softKeyboard) {
        m_softKeyboard = new SoftKeyboard(this);
        m_softKeyboard->setMode(SoftKeyboard::ModeEn);
        connect(m_softKeyboard, &SoftKeyboard::confirmed, this, [this]() {
            m_softKeyboard->hide();
            loadTools();  // [V6.6] 软键盘确认后刷新工具列表
        });
    }
    QPoint pos = m_searchEdit->mapToGlobal(QPoint(0, m_searchEdit->height() + 4));
    m_softKeyboard->show(m_searchEdit, pos);
}

// [V6.6] 事件过滤：搜索框点击弹出软键盘
bool ToolCheckoutPage::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_searchEdit && event->type() == QEvent::MouseButtonPress) {
        onSearchFieldClicked();
        return false;  // 不拦截事件，让QLineEdit正常处理焦点
    }
    return QWidget::eventFilter(obj, event);
}

// [2026-06-25] 分页：上一页
void ToolCheckoutPage::onPrevPage() {
    if (m_currentPage > 1) {
        m_currentPage--;
        loadTools();
    }
}

// [2026-06-25] 分页：下一页
void ToolCheckoutPage::onNextPage() {
    int totalPages = (m_totalRecords + m_pageSize - 1) / m_pageSize;
    if (m_currentPage < totalPages) {
        m_currentPage++;
        loadTools();
    }
}

// ═══════════ [2026-06-27] 出库记录相关 ═══════════

// [2026-06-27] Tab切换时加载对应数据
void ToolCheckoutPage::onTabChanged(int index) {
    if (index == 1) {
        // 切到"出库记录"Tab时加载记录
        m_recordCurrentPage = 1;
        loadCheckoutRecords();
    }
}

// [2026-06-27] 加载出库历史记录（从 sys_operation_log 查 operation_type='checkout'）
void ToolCheckoutPage::loadCheckoutRecords() {
    if (!m_recordTable) return;

    // 从 RecordDAO 获取分页数据（含总数）
    db::RecordDAO recDao;
    QJsonObject result = recDao.findOperationLogsByType("checkout", m_recordCurrentPage, m_recordPageSize);
    int total = result["total"].toInt();
    QJsonArray records = result["list"].toArray();

    m_recordTotalRecords = total;

    // 分页
    int totalPages = (total + m_recordPageSize - 1) / m_recordPageSize;
    if (totalPages < 1) totalPages = 1;
    if (m_recordCurrentPage > totalPages) m_recordCurrentPage = totalPages;
    if (m_recordCurrentPage < 1) m_recordCurrentPage = 1;

    m_recordPageLabel->setText(QStringLiteral("第 %1/%2 页").arg(m_recordCurrentPage).arg(totalPages));
    m_recordTotalLabel->setText(QStringLiteral("共 %1 条").arg(total));
    m_recordPrevBtn->setEnabled(m_recordCurrentPage > 1);
    m_recordNextBtn->setEnabled(m_recordCurrentPage < totalPages);

    m_recordTable->setRowCount(records.size());
    for (int i = 0; i < records.size(); ++i) {
        QJsonObject r = records[i].toObject();
        QString content = r["content"].toString();
        QString time = r["createdAt"].toString();

        // 列0：出库时间
        m_recordTable->setItem(i, 0, new QTableWidgetItem(time));

        // 列1-4：从content解析
        // [2026-06-27] content格式："出库工具「工具名」编号[工具编号]×数量，原因：原因"
        QString toolName, quantity, reason;
        QRegularExpression re1("出库工具「(.+?)」");
        QRegularExpression re2("×(\\d+)");
        QRegularExpression re3("原因：(.+)");
        QRegularExpressionMatch m1 = re1.match(content);
        QRegularExpressionMatch m2 = re2.match(content);
        QRegularExpressionMatch m3 = re3.match(content);
        if (m1.hasMatch()) toolName = m1.captured(1);
        if (m2.hasMatch()) quantity = m2.captured(1) + " 件";
        if (m3.hasMatch()) reason = m3.captured(1);

        m_recordTable->setItem(i, 1, new QTableWidgetItem(toolName.isEmpty() ? QStringLiteral("--") : toolName));
        // 列2：工具编号（优先用JOIN获取的tool_code，降级从content解析）
        QString toolCode = r["toolCode"].toString();
        if (toolCode.isEmpty()) {
            QRegularExpression reCode("编号\\[(.+?)\\]");
            QRegularExpressionMatch mc = reCode.match(content);
            if (mc.hasMatch()) toolCode = mc.captured(1);
        }
        m_recordTable->setItem(i, 2, new QTableWidgetItem(toolCode.isEmpty() ? QStringLiteral("--") : toolCode));
        // 列3：出库数量
        auto* qtyItem = new QTableWidgetItem(quantity.isEmpty() ? QStringLiteral("--") : quantity);
        qtyItem->setTextAlignment(Qt::AlignCenter);
        m_recordTable->setItem(i, 3, qtyItem);
        // 列4：出库原因
        m_recordTable->setItem(i, 4, new QTableWidgetItem(reason.isEmpty() ? QStringLiteral("--") : reason));
        // 列5：操作人
        QString userName = r["realName"].toString();
        if (userName.isEmpty()) userName = QStringLiteral("--");
        m_recordTable->setItem(i, 5, new QTableWidgetItem(userName));

        m_recordTable->setRowHeight(i, 48);
    }
}

// [2026-06-27] 出库记录分页
void ToolCheckoutPage::onRecordPrevPage() {
    if (m_recordCurrentPage > 1) {
        m_recordCurrentPage--;
        loadCheckoutRecords();
    }
}

void ToolCheckoutPage::onRecordNextPage() {
    int totalPages = (m_recordTotalRecords + m_recordPageSize - 1) / m_recordPageSize;
    if (m_recordCurrentPage < totalPages) {
        m_recordCurrentPage++;
        loadCheckoutRecords();
    }
}
