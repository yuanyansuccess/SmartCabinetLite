/**
 * @file ToolManagementPage.cpp
 * @brief 工具管理页面实现 - 参考工程机组管理风格重做
 * @author 袁燕
 * @修改说明 V7.0 2026-06-24 全面改造：
 *   1. 顶部统计卡片（全部工具/在库/已借用/维护中）
 *   2. 表格列：编号/名称/规格型号/机组/位置/状态/最近操作/操作
 *   3. 状态用彩色标签（在库绿/已借用橙/维护中灰）
 *   4. 操作按钮改为"详情"
 *   5. 数据使用数据库 machine_group + tool_info 关联查询
 */
#include "ToolManagementPage.h"
#include "utils/StyleHelper.h"
#include "controller/ToolController.h"
#include "components/SoftKeyboard.h"
#include "components/MultiSelectFilter.h"
#include "components/BaseDialog.h"     // 统一圆角对话框
#include "common/AppConfig.h"          // 读取本机机组ID
#include "common/Constants.h"          // 识别方式常量
#include "db/RecordDAO.h"              // 查询工具的借用记录
#include <QApplication>                // 屏幕高度自适应
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFormLayout>
#include <QRegularExpression>     // 位置格式化提取数字
#include "components/MessageDialog.h"
#include <QDebug>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QTabWidget>          // 工具详情对话框Tab选项卡
#include <QScrollArea>         // 借用记录Tab滚动支持
#include <QDesktopServices>    // 文档在线浏览(调用系统默认程序)
#include <QUrl>                // 文档URL
#include <QFileInfo>           // 文档文件信息
#include <QFileDialog>         // 文档下载(另存为)
#include <QStandardPaths>      // 文档上传存储路径
#include <QDateTime>           // 文档文件名时间戳
#include <QDir>                // 创建文档存储目录
#include <QTimer>              // 上传后延迟重新打开详情对话框

ToolManagementPage::ToolManagementPage(QWidget* parent) : QWidget(parent),
    m_toolDialog(nullptr), m_editToolId(0), m_currentPage(1), m_pageSize(20), m_totalRecords(0) {
    setupUI();
    loadStats();  // 加载统计
}

ToolManagementPage::~ToolManagementPage() = default;

// 创建单个统计卡片
static QFrame* createStatCard(const QString& title, const QString& icon, const QString& color, QLabel*& countLabel) {
    auto* card = new QFrame();
    card->setFixedHeight(SC::STAT_CARD_HEIGHT);
    card->setStyleSheet(QString(
        "QFrame{background:#fff;border-radius:%1px;}"
    ).arg(SC::STAT_CARD_ICON_RADIUS));
    auto* layout = new QHBoxLayout(card);
    layout->setContentsMargins(20, SC::STAT_CARD_SPACING, 20, SC::STAT_CARD_SPACING);
    layout->setSpacing(SC::STAT_CARD_SPACING);

    // 图标
    auto* iconLabel = new QLabel(icon);
    iconLabel->setFixedSize(SC::STAT_CARD_ICON_SIZE, SC::STAT_CARD_ICON_SIZE);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet(QString(
        "QLabel{background:%1;border-radius:%2px;font-size:%3px;color:#fff;}"
    ).arg(color).arg(SC::STAT_CARD_ICON_RADIUS).arg(SC::STAT_CARD_ICON_FONT));

    // 文字区域
    auto* textLayout = new QVBoxLayout();
    textLayout->setSpacing(4);
    auto* titleLabel = new QLabel(title);
    titleLabel->setStyleSheet(QString("font-size:%1px;color:#999;background:transparent;")
                              .arg(SC::FONT_SIZE_SMALL));
    countLabel = new QLabel("0");
    countLabel->setStyleSheet(QString("font-size:%1px;font-weight:700;color:%2;background:transparent;")
                              .arg(SC::STAT_CARD_VALUE_FONT).arg(color));
    textLayout->addWidget(titleLabel);
    textLayout->addWidget(countLabel);

    layout->addWidget(iconLabel);
    layout->addLayout(textLayout);
    layout->addStretch();
    return card;
}

void ToolManagementPage::setupStatsCards() {
    // 统计卡片行 [V7.0]
    // 统计按"种类+位置"唯一标识排列
    // 工具总数=在库+已借出（不含出库/待入库）
    // 新增"待入库"卡片，pending=配置层尚未物理入库
    // 举一反三：Dashboard统计也需同步调整
    m_statsCardAll         = createStatCard(QStringLiteral("工具总数"), QStringLiteral("📦"), "#4da3ff", m_labelTotalTools);
    m_statsCardInStock     = createStatCard(QStringLiteral("在库工具"),     QStringLiteral("✅"), "#43a047", m_labelInStock);
    m_statsCardBorrowed    = createStatCard(QStringLiteral("已借出"),   QStringLiteral("📤"), "#f57c00", m_labelBorrowed);
    m_statsCardCheckedOut  = createStatCard(QStringLiteral("已出库"),   QStringLiteral("📤"), "#7c3aed", m_labelCheckedOut);
    m_statsCardPending     = createStatCard(QStringLiteral("待入库"),   QStringLiteral("📥"), "#ff9800", m_labelPending);  // 新增
    m_statsCardMaintenance = createStatCard(QStringLiteral("维护中"),   QStringLiteral("🔧"), "#999999", m_labelMaintenance);
}

void ToolManagementPage::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    // 标题
    auto* title = new QLabel(QStringLiteral("工具机组管理"));
    title->setStyleSheet("font-size:20px;font-weight:700;color:#1a1a2e;background:transparent;");
    mainLayout->addWidget(title);

    // ==================== 统计卡片 [V7.0] ====================
    setupStatsCards();
    auto* statsRow = new QHBoxLayout();
    statsRow->setSpacing(16);
    statsRow->addWidget(m_statsCardAll, 1);
    statsRow->addWidget(m_statsCardInStock, 1);
    statsRow->addWidget(m_statsCardBorrowed, 1);
    statsRow->addWidget(m_statsCardCheckedOut, 1);
    statsRow->addWidget(m_statsCardPending, 1);  // 待入库卡片
    statsRow->addWidget(m_statsCardMaintenance, 1);
    mainLayout->addLayout(statsRow);

    // ==================== 搜索筛选行 ====================
    auto* searchRow = new QHBoxLayout();
    searchRow->setSpacing(12);

    // 搜索框
    auto* searchInputWrap = new QFrame();
    searchInputWrap->setAttribute(Qt::WA_StyledBackground, true);
    searchInputWrap->setFixedWidth(280);
    searchInputWrap->setFixedHeight(48);
    searchInputWrap->setStyleSheet(
        "QFrame{border:2px solid #e0e0e0;border-radius:12px;background:#fff;}"
    );
    auto* searchInputLayout = new QHBoxLayout(searchInputWrap);
    // 右内边距1px防止按钮覆盖QFrame右下角边框
    searchInputLayout->setContentsMargins(0, 0, 1, 0);
    searchInputLayout->setSpacing(0);

    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索工具名称/编号..."));
    m_searchEdit->setStyleSheet(
        "QLineEdit{border:none;padding:0 16px;font-size:16px;background:transparent;color:#333;min-height:42px;}"
    );
    searchInputLayout->addWidget(m_searchEdit, 1);

    auto* searchKbdBtn = new QPushButton(QStringLiteral("⌨"));
    searchKbdBtn->setFixedSize(46, 44);
    searchKbdBtn->setCursor(Qt::PointingHandCursor);
    // 按钮圆角10px对齐QFrame内边距(12px外框-2px边框=10px内径)
    searchKbdBtn->setStyleSheet(
        "QPushButton{border:none;border-radius:0 10px 10px 0;"
        "background:#f0f2f5;font-size:22px;color:#888;}"
        "QPushButton:hover{background:#e6f0ff;color:#4da3ff;}"
    );
    connect(searchKbdBtn, &QPushButton::clicked, this, &ToolManagementPage::onSearchKeyboardClicked);
    searchInputLayout->addWidget(searchKbdBtn);

    // 类别筛选——从DB动态获取
    m_categoryFilter = new MultiSelectFilter(QStringLiteral("全部类别"), this);
    connect(m_categoryFilter, &MultiSelectFilter::selectionChanged, this, &ToolManagementPage::onSearch);

    // 机组筛选——从machine_group表动态获取
    m_machineGroupFilter = new MultiSelectFilter(QStringLiteral("全部机组"), this);
    connect(m_machineGroupFilter, &MultiSelectFilter::selectionChanged, this, &ToolManagementPage::onSearch);

    m_searchBtn = new QPushButton(QStringLiteral("查询"));
    m_searchBtn->setFixedHeight(48);
    m_searchBtn->setStyleSheet(
        "QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:12px;"
        "padding:0 24px;font-size:16px;font-weight:700;}"
        "QPushButton:hover{background:#3d8ae0;}"
        "QPushButton:pressed{transform:scale(0.96);}"
    );
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    connect(m_searchBtn, &QPushButton::clicked, this, &ToolManagementPage::onSearch);

    m_resetBtn = new QPushButton(QStringLiteral("重置"));
    m_resetBtn->setFixedHeight(48);
    m_resetBtn->setStyleSheet(
        "QPushButton{background:#fff;color:#4da3ff;border:2px solid #4da3ff;border-radius:12px;"
        "padding:0 24px;font-size:16px;font-weight:700;}"
        "QPushButton:hover{background:#f0f7ff;}"
        "QPushButton:pressed{transform:scale(0.96);}"
    );
    m_resetBtn->setCursor(Qt::PointingHandCursor);
    connect(m_resetBtn, &QPushButton::clicked, this, &ToolManagementPage::onReset);

    searchRow->addWidget(searchInputWrap, 1);
    searchRow->addWidget(m_categoryFilter);
    searchRow->addWidget(m_machineGroupFilter);
    searchRow->addWidget(m_searchBtn);
    searchRow->addWidget(m_resetBtn);
    searchRow->addStretch();
    mainLayout->addLayout(searchRow);

    // ==================== 表格 [V2.03e 2026-06-29] 列：编号/类别/名称/规格型号/机组/位置/状态/借用人/最近操作/操作 ====================
    // 删除"库存"列（某机组某柜某层某位只有一个库存，位置即唯一标识）
    // 位置列改为唯一标识格式：柜名-层号-位号
    // 删除"借用中"列（一个位置=一个工具，状态列已说明在库/借出）
    m_table = new QTableWidget();
    m_table->setColumnCount(10);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("编号"), QStringLiteral("类别"), QStringLiteral("名称"), QStringLiteral("规格型号"),
        QStringLiteral("机组"), QStringLiteral("位置"), QStringLiteral("状态"),
        QStringLiteral("借用人"), QStringLiteral("最近操作"), QStringLiteral("操作")
    });
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->setAlternatingRowColors(false);
    // 移除内联表格QSS，使用全局QSS统一表格样式（小米设计语言）
    // 数据列Stretch均分，操作列Fixed紧凑（触屏按钮~130px）
    // 列：编号(0) 类别(1) 名称(2) 规格型号(3) 机组(4) 位置(5) 状态(6) 借用人(7) 最近操作(8) 操作(9)
    for (int i = 0; i < 9; i++) {
        m_table->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
    }
    m_table->horizontalHeader()->setSectionResizeMode(9, QHeaderView::Fixed);
    m_table->setColumnWidth(9, 130);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setMinimumSectionSize(60);
    mainLayout->addWidget(m_table, 1);

    // ==================== 分页 ====================
    // 统一分页样式：按钮文字改为"上一页""下一页"，与其他页面一致
    auto* pageRow = new QHBoxLayout();
    pageRow->setContentsMargins(12, 12, 12, 12);
    pageRow->setSpacing(6);

    m_prevBtn = new QPushButton(QStringLiteral("上一页"));
    m_prevBtn->setStyleSheet(
        "QPushButton{border:1px solid #ddd;border-radius:6px;padding:5px 12px;"
        "font-size:13px;font-weight:600;color:#555;background:#fff;min-height:30px;}"
        "QPushButton:hover{border-color:#4da3ff;color:#4da3ff;}"
        "QPushButton:disabled{opacity:0.35;}"
    );
    m_prevBtn->setCursor(Qt::PointingHandCursor);
    connect(m_prevBtn, &QPushButton::clicked, this, &ToolManagementPage::onPrevPage);

    m_nextBtn = new QPushButton(QStringLiteral("下一页"));
    m_nextBtn->setStyleSheet(
        "QPushButton{border:1px solid #ddd;border-radius:6px;padding:5px 12px;"
        "font-size:13px;font-weight:600;color:#555;background:#fff;min-height:30px;}"
        "QPushButton:hover{border-color:#4da3ff;color:#4da3ff;}"
        "QPushButton:disabled{opacity:0.35;}"
    );
    m_nextBtn->setCursor(Qt::PointingHandCursor);
    connect(m_nextBtn, &QPushButton::clicked, this, &ToolManagementPage::onNextPage);

    m_pageLabel = new QLabel(QStringLiteral("第 1 页"));
    m_pageLabel->setStyleSheet("font-size:13px;color:#999;padding:0 4px;");

    m_totalLabel = new QLabel(QStringLiteral("共 0 条"));
    m_totalLabel->setStyleSheet("font-size:13px;color:#999;");

    // 布局顺序：stretch | 上一页 | 第X页 | 下一页 | 共N条
    pageRow->addStretch();
    pageRow->addWidget(m_prevBtn);
    pageRow->addWidget(m_pageLabel);
    pageRow->addWidget(m_nextBtn);
    pageRow->addWidget(m_totalLabel);
    mainLayout->addLayout(pageRow);

    loadCategories();
    loadMachineGroups();
}

void ToolManagementPage::refresh() {
    // 切换到本页面时重置为默认筛选条件和第1页
    m_searchEdit->clear();
    m_categoryFilter->selectAll();
    m_machineGroupFilter->selectAll();
    m_currentPage = 1;
    loadStats();
    loadTools();
}

void ToolManagementPage::onSearch() {
    m_currentPage = 1;  // 筛选查询必须重置到第1页
    loadTools();
}

void ToolManagementPage::onReset() {
    m_searchEdit->clear();
    m_categoryFilter->selectAll();
    m_machineGroupFilter->selectAll();
    m_currentPage = 1;  // 重置筛选必须重置到第1页
    loadTools();
}

void ToolManagementPage::loadCategories() {
    // 从DB动态获取类别列表
    ToolController ctrl;
    m_allCategories = ctrl.categoryNames();
    m_categoryFilter->setOptions(m_allCategories);
}

// 加载机组筛选选项
void ToolManagementPage::loadMachineGroups() {
    ToolController ctrl;
    m_allMachineGroups.clear();
    QList<QJsonObject> groups = ctrl.getMachineGroups();
    for (const auto& g : groups) {
        m_allMachineGroups << g["groupName"].toString();
    }
    m_machineGroupFilter->setOptions(m_allMachineGroups);
}

// 加载统计数据
// 改为按工具件数统计（total_qty/current_qty/borrowed_qty），不再按种类数
void ToolManagementPage::loadStats() {
    ToolController ctrl;
    QJsonObject stats = ctrl.getToolStats();
    // 统计按位置维度计算
    // 工具总数 = 在库 + 已借出 + 待入库 + 已出库（四种状态）
    // 待入库 = 映射表中空闲位置数（有位置记录但无工具占用）
    //int total = stats["inStockCount"].toInt() + stats["borrowedCount"].toInt()
    // + stats["pendingCount"].toInt() + stats["checkedOutCount"].toInt();


    int total = stats["inStockCount"].toInt() + stats["borrowedCount"].toInt();

    m_labelTotalTools->setText(QString::number(total));
    m_labelInStock->setText(QString::number(stats["inStockCount"].toInt()));
    m_labelBorrowed->setText(QString::number(stats["borrowedCount"].toInt()));
    m_labelCheckedOut->setText(QString::number(stats["checkedOutCount"].toInt()));
    m_labelPending->setText(QString::number(stats["pendingCount"].toInt()));
    m_labelMaintenance->setText(QString::number(stats["maintenanceCount"].toInt()));
}

void ToolManagementPage::loadTools() {
    QString kw = m_searchEdit->text().trimmed();
    QStringList cats = m_categoryFilter->selectedOptions();
    QString cat = cats.join(",");
    // 动态判断全选（不再硬编码5）
    bool allCatSel = (cats.size() == m_allCategories.size());
    if (allCatSel || cats.isEmpty()) cat = "";

    // 机组筛选
    QStringList mgs = m_machineGroupFilter->selectedOptions();
    QString mg = mgs.join(",");
    // 全选则传空（筛选所有）
    if (mgs.isEmpty() || mgs.size() == m_allMachineGroups.size()) mg = "";

    ToolController ctrl;
    auto pageResult = ctrl.getToolList(m_currentPage, m_pageSize, kw, cat, "", "", mg);
    m_totalRecords = pageResult.total;

    // 分页
    int totalPages = (m_totalRecords + m_pageSize - 1) / m_pageSize;
    m_pageLabel->setText(QStringLiteral("第 %1/%2 页").arg(m_currentPage).arg(qMax(1, totalPages)));
    m_totalLabel->setText(QStringLiteral("共 %1 条").arg(m_totalRecords));
    m_prevBtn->setEnabled(m_currentPage > 1);
    m_nextBtn->setEnabled(m_currentPage < totalPages);

    // 填充表格：编号/类别/名称/规格型号/机组/位置/状态/借用中/借用人/最近操作/操作
    // 删除了"库存"列，位置改为唯一标识格式
    m_table->setRowCount(pageResult.list.size());
    for (int i = 0; i < pageResult.list.size(); ++i) {
        const ToolInfo& t = pageResult.list[i];

        m_table->setItem(i, 0, new QTableWidgetItem(t.toolCode));               // 编号
        m_table->setItem(i, 1, new QTableWidgetItem(t.categoryName.isEmpty() ? QStringLiteral("--") : t.categoryName));  // 类别
        m_table->setItem(i, 2, new QTableWidgetItem(t.toolName));               // 名称
        m_table->setItem(i, 3, new QTableWidgetItem(t.spec));                   // 规格型号

        // 机组 [V7.0]
        QString groupName = t.machineGroupName.isEmpty() ? QStringLiteral("未分配") : t.machineGroupName;
        m_table->setItem(i, 4, new QTableWidgetItem(groupName));

        // 位置格式：柜号-层号-位号（两位补零，如A-01-03）
        QString posDisplay = StyleHelper::formatPosition(t.cabinetName, t.layer, t.position);
        auto* posItem = new QTableWidgetItem(posDisplay);
        posItem->setTextAlignment(Qt::AlignCenter);
        posItem->setFont(QFont(posItem->font().family(), 12, QFont::DemiBold));
        posItem->setForeground(QColor("#333333"));
        m_table->setItem(i, 5, posItem);

        // 状态 - 彩色标签样式 [V7.0]
        QString statusText;
        QString statusColor;
        if (t.status == "in_stock") {
            statusText = QStringLiteral("在库");
            statusColor = "#43a047";
        } else if (t.status == "borrowed") {
            statusText = QStringLiteral("已借用");
            statusColor = "#f57c00";
        } else if (t.status == "checked_out") {           // 新增已出库状态
            statusText = QStringLiteral("已出库");          // 区别于已借用，表示永久出库消耗
            statusColor = "#e53935";                       //   红色警示，作者：袁燕
        } else if (t.status == "maintenance") {
            statusText = QStringLiteral("维护中");
            statusColor = "#999999";
        } else if (t.status == "pending") {               // 新增待入库状态
            statusText = QStringLiteral("待入库");          // 系统维护新建工具未入库
            statusColor = "#1890ff";
        } else {
            statusText = t.status;
            statusColor = "#999999";
        }
        auto* statusLabel = new QLabel(statusText);
        statusLabel->setAlignment(Qt::AlignCenter);
        statusLabel->setFixedSize(64, 28);
        statusLabel->setStyleSheet(QString(
            "QLabel{background:%1;color:#fff;border-radius:6px;font-size:13px;font-weight:600;}"
        ).arg(statusColor));
        m_table->setCellWidget(i, 6, statusLabel);

        // 已删除"借用中"列（一个位置=一个工具，状态列已说明）

        // 借用人 [2026-06-27] 显示最近借用人 [V2.03e] 列索引7
        QString borrowerName = t.latestOpUser;
        auto* borrowerItem = new QTableWidgetItem(borrowerName.isEmpty() ? QStringLiteral("--") : borrowerName);
        borrowerItem->setTextAlignment(Qt::AlignCenter);
        if (!borrowerName.isEmpty() && t.latestOpType == "borrow") {
            borrowerItem->setForeground(QColor("#1890ff"));
            QFont borrowerFont = borrowerItem->font();
            borrowerFont.setBold(true);
            borrowerItem->setFont(borrowerFont);
        } else {
            borrowerItem->setForeground(QColor("#bfbfbf"));
        }
        m_table->setItem(i, 7, borrowerItem);

        // 最近操作 [V7.0] [V2.03e] 列索引8
        QString lastOp;
        if (!t.latestOpTime.isEmpty()) {
            QString opType;
            if (t.latestOpType == "borrow") opType = QStringLiteral("借用");
            else if (t.latestOpType == "checkout") opType = QStringLiteral("出库");
            else if (t.latestOpType == "checkin") opType = QStringLiteral("入库");
            else opType = QStringLiteral("操作");
            lastOp = t.latestOpTime + "\n" + opType;
        } else {
            lastOp = QStringLiteral("暂无");
        }
        auto* opItem = new QTableWidgetItem(lastOp);
        opItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        opItem->setForeground(QColor("#888888"));
        m_table->setItem(i, 8, opItem);

        // 操作列 - 详情按钮 [V7.3 2026-06-24] 去掉编辑按钮，只保留详情
        int toolId = t.toolId;
        auto* opWidget = new QWidget();
        opWidget->setStyleSheet("background:transparent;");
        auto* opLayout = new QHBoxLayout(opWidget);
        opLayout->setContentsMargins(4, 4, 4, 4);
        opLayout->setSpacing(0);
        opLayout->setAlignment(Qt::AlignCenter);

        // 详情按钮
        auto* detailBtn = new QPushButton(QStringLiteral("详情"));
        detailBtn->setFixedSize(64, 34);
        detailBtn->setStyleSheet(
            "QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:8px;"
            "font-size:14px;font-weight:600;}"
            "QPushButton:hover{background:#3d8ae0;}"
            "QPushButton:pressed{background:#2b6cdf;}"
        );
        detailBtn->setCursor(Qt::PointingHandCursor);
        // 详情传入ToolInfo（含位置信息），用于按位置过滤操作记录
        connect(detailBtn, &QPushButton::clicked, this, [this, t, i] { m_table->selectRow(i); onDetailTool(t); });

        opLayout->addWidget(detailBtn);
        m_table->setCellWidget(i, 9, opWidget);  // 列索引9
        m_table->setRowHeight(i, 60);  // 行高增大以确保最近操作列两行内容完整显示
    }
}

void ToolManagementPage::onPrevPage() {
    if (m_currentPage > 1) { m_currentPage--; loadTools(); }
}

void ToolManagementPage::onNextPage() {
    int totalPages = (m_totalRecords + m_pageSize - 1) / m_pageSize;
    if (m_currentPage < totalPages) { m_currentPage++; loadTools(); }
}

// 查看工具详情：美观弹窗展示所有字段
// 改用BaseDialog统一圆角无边框风格
// 改为接收ToolInfo（含位置信息），操作记录按位置过滤
// 同一工具在多个位置，每个位置的详情只显示该位置相关的操作记录
void ToolManagementPage::onDetailTool(const ToolInfo& toolInfo) {
    ToolController ctrl;
    // 补全基础信息（toolInfo来自findAllTools位置维度查询，含位置；getToolById补全其他字段）
    ToolInfo t = ctrl.getToolById(toolInfo.toolId);
    if (t.toolId == 0) return;
    // 用位置维度查询的位置信息覆盖（findAllTools的位置来自映射表，是权威数据源）
    // 必须覆盖mappingId，否则getToolById返回mappingId=0
    // → onDetailTool走兜底findByToolId → 显示所有位置的借用记录（不按位置过滤）
    t.mappingId = toolInfo.mappingId;
    t.cabinetId = toolInfo.cabinetId;
    t.cabinetName = toolInfo.cabinetName;
    t.layer = toolInfo.layer;
    t.position = toolInfo.position;
    t.status = toolInfo.status;  // 映射表status（位置占用状态）

    // 对话框加宽到900，确保操作记录表格列宽不截断
    auto* dlg = new BaseDialog(this, 900);
    dlg->setDialogTitle(QStringLiteral("工具详情"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    auto* cl = dlg->contentLayout();
    cl->setSpacing(0);

    // 顶部标题区：工具名称 + 状态标签
    auto* headerRow = new QHBoxLayout();
    headerRow->setSpacing(16);

    auto* nameLabel = new QLabel(t.toolName.isEmpty() ? QStringLiteral("未命名工具") : t.toolName);
    nameLabel->setStyleSheet("font-size:22px;font-weight:700;color:#1a1a2e;background:transparent;");
    headerRow->addWidget(nameLabel);

    QString statusText;
    QString statusBg;
    if (t.status == "in_stock") { statusText = QStringLiteral("在库"); statusBg = "#43a047"; }
    else if (t.status == "borrowed") { statusText = QStringLiteral("已借用"); statusBg = "#f57c00"; }
    else if (t.status == "checked_out") { statusText = QStringLiteral("已出库"); statusBg = "#e53935"; }  // 已出库红色
    else if (t.status == "maintenance") { statusText = QStringLiteral("维护中"); statusBg = "#999999"; }
    else if (t.status == "pending") { statusText = QStringLiteral("待入库"); statusBg = "#1890ff"; }  // 待入库
    else { statusText = t.status; statusBg = "#999999"; }

    auto* statusLabel = new QLabel(statusText);
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setFixedSize(72, 30);
    statusLabel->setStyleSheet(QString(
        "QLabel{background:%1;color:#fff;border-radius:8px;font-size:14px;font-weight:700;}"
    ).arg(statusBg));
    headerRow->addWidget(statusLabel);
    headerRow->addStretch();
    cl->addLayout(headerRow);

    // 分隔线
    auto* divider = new QFrame();
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet("QFrame{color:#e8e8e8;margin:16px 0;}");
    cl->addWidget(divider);

    // 改为Tab选项卡式：工具详情 + 借用记录 分两个Tab
    // 避免借用记录过多撑大对话框，分开显示更清晰
    auto* tabWidget = new QTabWidget();
    tabWidget->setStyleSheet(QString(
        "QTabWidget::pane { border: none; background: transparent; }"
        "QTabBar::tab { background: #f0f2f5; color: #666; padding: 10px 24px; "
        "  font-size: 15px; font-weight: 600; border-radius: 10px 10px 0 0; "
        "  min-height: 40px; margin-right: 4px; }"
        "QTabBar::tab:selected { background: white; color: %1; border-bottom: 3px solid %1; }"
        "QTabBar::tab:hover { background: #e8f0fe; }"
    ).arg(StyleHelper::primaryColor()));

    // ====== Tab1: 工具详情 ======
    auto* detailTab = new QWidget();
    auto* detailLayout = new QVBoxLayout(detailTab);
    detailLayout->setContentsMargins(0, 12, 0, 0);
    detailLayout->setSpacing(0);

    // infoCard外包QScrollArea，已出库工具字段多时可滚动
    //   作者：袁燕 — 修复已出库工具详情页关闭按钮被内容挤压截断的问题
    auto* detailScroll = new QScrollArea();
    detailScroll->setWidgetResizable(true);
    detailScroll->setFrameShape(QFrame::NoFrame);
    detailScroll->setStyleSheet("QScrollArea{background:transparent;border:none;}");

    // 信息卡片区
    auto* infoCard = new QFrame();
    infoCard->setStyleSheet("QFrame{background:#f8f9fb;border-radius:12px;}");
    auto* infoLayout = new QGridLayout(infoCard);
    infoLayout->setContentsMargins(20, 20, 20, 20);
    infoLayout->setHorizontalSpacing(40);
    infoLayout->setVerticalSpacing(14);

    auto makeFieldLabel = [](const QString& text) -> QLabel* {
        auto* l = new QLabel(text);
        l->setStyleSheet("font-size:14px;color:#999;background:transparent;");
        return l;
    };
    auto makeFieldValue = [](const QString& text) -> QLabel* {
        auto* l = new QLabel(text);
        l->setStyleSheet("font-size:15px;color:#333;font-weight:600;background:transparent;");
        l->setWordWrap(true);
        return l;
    };

    int row = 0;
    infoLayout->addWidget(makeFieldLabel(QStringLiteral("工具编号")), row, 0);
    infoLayout->addWidget(makeFieldValue(t.toolCode.isEmpty() ? QStringLiteral("-") : t.toolCode), row++, 1);

    infoLayout->addWidget(makeFieldLabel(QStringLiteral("规格型号")), row, 0);
    infoLayout->addWidget(makeFieldValue(t.spec.isEmpty() ? QStringLiteral("-") : t.spec), row++, 1);

    infoLayout->addWidget(makeFieldLabel(QStringLiteral("所属分类")), row, 0);
    infoLayout->addWidget(makeFieldValue(t.categoryName.isEmpty() ? QStringLiteral("未分类") : t.categoryName), row++, 1);

    infoLayout->addWidget(makeFieldLabel(QStringLiteral("所属机组")), row, 0);
    infoLayout->addWidget(makeFieldValue(t.machineGroupName.isEmpty() ? QStringLiteral("未分配") : t.machineGroupName), row++, 1);

    infoLayout->addWidget(makeFieldLabel(QStringLiteral("存放位置")), row, 0);
    QString posDisplay = StyleHelper::formatPosition(t.cabinetName, t.layer, t.position);
    infoLayout->addWidget(makeFieldValue(posDisplay), row++, 1);

    // 删除"库存总数"和"当前在库"字段
    // 设计理念：一个位置(机组-柜-层-位号)=一个工具，数量恒为1
    //   状态字段已说明在库/借出，无需重复显示数量。作者：袁燕

    infoLayout->addWidget(makeFieldLabel(QStringLiteral("视觉标签")), row, 0);
    infoLayout->addWidget(makeFieldValue(t.visionTag.isEmpty() ? QStringLiteral("未绑定") : t.visionTag), row++, 1);

    // 识别方式显示
    infoLayout->addWidget(makeFieldLabel(QStringLiteral("识别方式")), row, 0);
    QString recogText = (t.recognitionMethod == SC::RECOGNITION_VISION)
                        ? QStringLiteral("视觉识别") : QStringLiteral("视觉识别");
    infoLayout->addWidget(makeFieldValue(recogText), row++, 1);

    infoLayout->addWidget(makeFieldLabel(QStringLiteral("创建时间")), row, 0);
    infoLayout->addWidget(makeFieldValue(t.createdAt.isValid() ? t.createdAt.toString("yyyy-MM-dd HH:mm") : QStringLiteral("-")), row++, 1);

    infoLayout->addWidget(makeFieldLabel(QStringLiteral("借用人")), row, 0);
    infoLayout->addWidget(makeFieldValue(t.latestOpUser.isEmpty() ? QStringLiteral("无") : t.latestOpUser), row++, 1);

    // 已出库/入库工具显示操作人和操作时间
    db::RecordDAO recDao;
    {
        if (t.status == "checked_out") {
            QJsonObject log = recDao.findLastOperationLog(t.toolCode, "checkout");
            if (!log.isEmpty()) {
                QString checkoutTime = log["time"].toString();
                QString checkoutUser = log["operator"].toString();
                infoLayout->addWidget(makeFieldLabel(QStringLiteral("出库人")), row, 0);
                infoLayout->addWidget(makeFieldValue(checkoutUser.isEmpty() ? QStringLiteral("--") : checkoutUser), row++, 1);
                infoLayout->addWidget(makeFieldLabel(QStringLiteral("出库时间")), row, 0);
                infoLayout->addWidget(makeFieldValue(checkoutTime.left(16).isEmpty() ? QStringLiteral("--") : checkoutTime.left(16)), row++, 1);
            }
        } else if (t.status == "in_stock") {
            QJsonObject log = recDao.findLastOperationLog(t.toolCode, "checkin");
            if (!log.isEmpty()) {
                QString checkinTime = log["time"].toString();
                QString checkinUser = log["operator"].toString();
                infoLayout->addWidget(makeFieldLabel(QStringLiteral("入库人")), row, 0);
                infoLayout->addWidget(makeFieldValue(checkinUser.isEmpty() ? QStringLiteral("--") : checkinUser), row++, 1);
                infoLayout->addWidget(makeFieldLabel(QStringLiteral("入库时间")), row, 0);
                infoLayout->addWidget(makeFieldValue(checkinTime.left(16).isEmpty() ? QStringLiteral("--") : checkinTime.left(16)), row++, 1);
            }
        }
    }

    infoLayout->addWidget(makeFieldLabel(QStringLiteral("最近操作")), row, 0);
    QString lastOpStr;
    if (!t.latestOpTime.isEmpty()) {
        QString opType;
        if (t.latestOpType == "borrow") opType = QStringLiteral("借用");
        else if (t.latestOpType == "checkout") opType = QStringLiteral("出库");
        else if (t.latestOpType == "checkin") opType = QStringLiteral("入库");
        else opType = QStringLiteral("操作");
        lastOpStr = QStringLiteral("%1 %2").arg(t.latestOpTime, opType);
    } else {
        lastOpStr = QStringLiteral("暂无操作记录");
    }
    infoLayout->addWidget(makeFieldValue(lastOpStr), row++, 1);

    detailScroll->setWidget(infoCard);
    detailLayout->addWidget(detailScroll);
    tabWidget->addTab(detailTab, QStringLiteral("📋 工具详情"));

    // ====== Tab2: 操作记录 [V2.03b 2026-06-29] ======
    // 合并借用记录+入库记录+出库记录，统一展示全部操作历史
    // 按时间降序排列，类型标签区分（借用橙/入库绿/出库红）
    auto* recordTab = new QWidget();
    auto* recordLayout = new QVBoxLayout(recordTab);
    recordLayout->setContentsMargins(0, 12, 0, 0);
    recordLayout->setSpacing(0);

    // 综合操作记录：借用/归还/入库/出库 4种类型，按时间降序排列
    // borrow记录根据status拆分：borrowing/overdue→"借用"，returned→"归还"
    // 借用记录按位置过滤：用mappingId查，只显示当前位置的借用记录
    QJsonArray borrowRecords;
    if (t.mappingId > 0) {
        borrowRecords = recDao.findByMappingId(t.mappingId, 20);
    } else {
        // 旧数据无mappingId，兜底用toolId查
        borrowRecords = recDao.findByToolId(t.toolId, 20);
    }

    // 入库/出库记录从DAO层查询（通过sys_operation_log）
    QString posKey = StyleHelper::formatPosition(t.cabinetName, t.layer, t.position);

    // 查询入库记录（按位置过滤）
    QJsonArray checkinLogs = recDao.findOperationLogs(t.toolCode, "checkin", posKey, 10);
    QJsonArray checkinRecords;
    for (const auto& log : checkinLogs) {
        QJsonObject obj = log.toObject();
        obj["type"] = "checkin";
        obj["status"] = "completed";
        obj["reason"] = QStringLiteral("入库");
        // 从content解析供应商
        QRegularExpression reSupplier("供应商：(.+)");
        QRegularExpressionMatch mSupplier = reSupplier.match(obj["content"].toString());
        if (mSupplier.hasMatch()) obj["supplier"] = mSupplier.captured(1);
        checkinRecords.append(obj);
    }

    // 查询出库记录（按位置过滤）
    QJsonArray checkoutLogs = recDao.findOperationLogs(t.toolCode, "checkout", posKey, 10);
    QJsonArray checkoutRecords;
    for (const auto& log : checkoutLogs) {
        QJsonObject obj = log.toObject();
        obj["type"] = "checkout";
        obj["status"] = "completed";
        // 从content解析出库原因
        QRegularExpression reReason("原因：(.+)");
        QRegularExpressionMatch mReason = reReason.match(obj["content"].toString());
        obj["reason"] = mReason.hasMatch() ? mReason.captured(1) : QStringLiteral("出库");
        checkoutRecords.append(obj);
    }

    // 合并所有记录到统一列表
    // 一条借用记录(returned)拆分为"借用"和"归还"两条，完整展示工具生命周期
    // borrowing/overdue只显示"借用"，returned显示"借用"+"归还"
    QList<QJsonObject> allRecords;
    for (const auto& r : borrowRecords) {
        QJsonObject obj = r.toObject();
        QString borrowStatus = obj["status"].toString();

        // 借用记录（借用时间作为操作时间）
        QJsonObject borrowObj = obj;
        borrowObj["type"] = "borrow";
        borrowObj["reason"] = obj["borrowReason"].toString();
        borrowObj["time"] = obj["borrowTime"].toString();
        allRecords.append(borrowObj);

        // 已归还的记录追加一条"归还"记录（归还时间作为操作时间）
        if (borrowStatus == "returned") {
            QJsonObject returnObj = obj;
            returnObj["type"] = "return";
            returnObj["reason"] = QStringLiteral("归还");
            returnObj["time"] = obj["actualReturnTime"].toString().isEmpty()
                ? obj["borrowTime"].toString() : obj["actualReturnTime"].toString();
            allRecords.append(returnObj);
        }
    }
    for (const auto& r : checkinRecords) allRecords.append(r.toObject());
    for (const auto& r : checkoutRecords) allRecords.append(r.toObject());
    // 按时间降序排序
    std::sort(allRecords.begin(), allRecords.end(),
        [](const QJsonObject& a, const QJsonObject& b) {
            return a["time"].toString() > b["time"].toString();
        });

    if (allRecords.isEmpty()) {
        auto* emptyLabel = new QLabel(QStringLiteral("暂无操作记录"));
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setStyleSheet("font-size:14px;color:#999;padding:40px;background:transparent;");
        recordLayout->addWidget(emptyLabel);
        recordLayout->addStretch();
    } else {
        // 操作记录表格：操作人/操作时间/操作类型/任务类型/状态
        // 删除"数量"列（一个位置=一个工具，数量恒为1无意义）
        // "备注"改为"任务类型"
        auto* recordTable = new QTableWidget();
        recordTable->setColumnCount(5);
        recordTable->setHorizontalHeaderLabels({
            QStringLiteral("操作人"), QStringLiteral("操作时间"), QStringLiteral("操作类型"),
            QStringLiteral("任务类型"), QStringLiteral("状态")
        });
        recordTable->setRowCount(allRecords.size());
        recordTable->verticalHeader()->setVisible(false);
        recordTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        recordTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        recordTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);  // 操作人
        recordTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);    // 操作时间
        recordTable->setColumnWidth(1, 170);  // 加宽确保 yyyy-MM-dd HH:mm 完整显示
        recordTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);    // 操作类型
        recordTable->setColumnWidth(2, 80);
        recordTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);  // 任务类型
        recordTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);    // 状态
        recordTable->setColumnWidth(4, 90);
        recordTable->horizontalHeader()->setStretchLastSection(false);

        for (int i = 0; i < allRecords.size(); ++i) {
            const QJsonObject& rec = allRecords[i];
            QString opType = rec["type"].toString();

            // 操作人（借用/归还记录使用userName+workNo，入库/出库记录使用operator）
            QString operatorName;
            if (opType == "borrow" || opType == "return") {
                QString userName = rec["userName"].toString();
                QString workNo = rec["userWorkNo"].toString();
                operatorName = userName.isEmpty() ? QStringLiteral("--")
                    : (workNo.isEmpty() ? userName : QStringLiteral("%1(%2)").arg(userName, workNo));
            } else {
                operatorName = rec["operator"].toString();
                if (operatorName.isEmpty()) operatorName = QStringLiteral("--");
            }
            auto* opItem = new QTableWidgetItem(operatorName);
            QFont opFont = opItem->font();
            opFont.setBold(true);
            opItem->setFont(opFont);
            recordTable->setItem(i, 0, opItem);

            // 操作时间（截取到分钟）
            QString timeStr = rec["time"].toString();
            if (timeStr.length() > 16) timeStr = timeStr.left(16);
            recordTable->setItem(i, 1, new QTableWidgetItem(timeStr.isEmpty() ? QStringLiteral("--") : timeStr));

            // 操作类型标签（4种彩色）：借用橙/归还蓝/入库绿/出库红
            QString typeText;
            QString typeColor;
            if (opType == "borrow") { typeText = QStringLiteral("借用"); typeColor = "#fa8c16"; }
            else if (opType == "return") { typeText = QStringLiteral("归还"); typeColor = "#4da3ff"; }
            else if (opType == "checkin") { typeText = QStringLiteral("入库"); typeColor = "#43a047"; }
            else if (opType == "checkout") { typeText = QStringLiteral("出库"); typeColor = "#e53935"; }
            else { typeText = QStringLiteral("操作"); typeColor = "#999"; }
            auto* typeLabel = new QLabel(typeText);
            typeLabel->setAlignment(Qt::AlignCenter);
            typeLabel->setFixedSize(64, 26);
            typeLabel->setStyleSheet(QString(
                "QLabel{background:%1;color:#fff;border-radius:6px;font-size:12px;font-weight:600;}"
            ).arg(typeColor));
            recordTable->setCellWidget(i, 2, typeLabel);

            // 删除"数量"列（一个位置=一个工具，数量恒为1）

            // 任务类型（借用=任务类型/借用原因，入库=供应商，出库=出库原因）
            QString remark;
            if (opType == "borrow") remark = rec["borrowReason"].toString();
            else if (opType == "checkin") remark = rec["supplier"].toString();
            else if (opType == "checkout") remark = rec["reason"].toString();
            else remark = QStringLiteral("--");
            recordTable->setItem(i, 3, new QTableWidgetItem(remark.isEmpty() ? QStringLiteral("--") : remark));

            // 状态
            QString status = rec["status"].toString();
            QString statusText;
            QString statusColor;
            if (opType == "borrow") {
                if (status == "borrowing") { statusText = QStringLiteral("借用中"); statusColor = "#fa8c16"; }
                else if (status == "returned") { statusText = QStringLiteral("已归还"); statusColor = "#43a047"; }
                else if (status == "overdue") { statusText = QStringLiteral("已逾期"); statusColor = "#e53935"; }
                else { statusText = status.isEmpty() ? QStringLiteral("--") : status; statusColor = "#999"; }
            } else {
                // 入库/出库状态固定为"已完成"
                statusText = QStringLiteral("已完成");
                statusColor = "#43a047";
            }
            auto* statusItem = new QTableWidgetItem(statusText);
            statusItem->setForeground(QColor(statusColor));
            QFont sf = statusItem->font();
            sf.setBold(true);
            statusItem->setFont(sf);
            statusItem->setTextAlignment(Qt::AlignCenter);
            recordTable->setItem(i, 4, statusItem);

            recordTable->setRowHeight(i, 44);
        }
        // 将表格放入滚动区域，让滚动条拉满可用空间
        auto* recordScroll = new QScrollArea();
        recordScroll->setWidgetResizable(true);
        recordScroll->setFrameShape(QFrame::NoFrame);
        recordScroll->setStyleSheet("QScrollArea{background:transparent;border:none;}QScrollBar:vertical{width:8px;background:transparent;}QScrollBar::handle:vertical{background:#c0c0c0;border-radius:4px;min-height:30px;}");
        recordScroll->setWidget(recordTable);
        recordLayout->addWidget(recordScroll, 1);  // stretch=1，让滚动条拉满
        recordLayout->addStretch();
    }

    // Tab名称"操作记录"，涵盖借用/出库/入库全部操作
    tabWidget->addTab(recordTab, QStringLiteral("📝 操作记录"));

    // ====== Tab3: 工具文档 [V2.01 2026-06-27] 下载+在线浏览 ======
    QWidget* docTab = createDocumentTab(t.toolId, t.documentPath);
    tabWidget->addTab(docTab, QStringLiteral("📄 工具文档"));

    // 对话框高度自适应屏幕，避免内容增加后溢出看不到关闭按钮
    int screenHeight = QApplication::primaryScreen()->availableGeometry().height();
    int dlgHeight = qMin(screenHeight * 88 / 100, 820);
    dlg->setMinimumHeight(500);
    dlg->resize(900, dlgHeight);
    cl->addWidget(tabWidget);
    cl->addSpacing(20);

    // 关闭按钮 [2026-06-26] 统一弹窗按钮风格：44px高/12px圆角/16px字体，居中显示
    auto* closeBtn = new QPushButton(QStringLiteral("关闭"));
    closeBtn->setFixedHeight(44);
    closeBtn->setMinimumWidth(100);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QPushButton{background:#f0f0f0;color:#555;border:1px solid #ddd;border-radius:12px;"
        "font-size:16px;font-weight:600;}"
        "QPushButton:hover{background:#e0e0e0;}"
    );
    connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::accept);

    auto* btnLayout = dlg->buttonLayout();
    // 清除默认stretch后重新居中布局
    QLayoutItem* item;
    while ((item = btnLayout->takeAt(0)) != nullptr) delete item;
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    btnLayout->addStretch();

    dlg->exec();
}

// 构建详情对话框的工具文档Tab
// 输入: toolId - 工具ID(预留,可用于后续文档替换), docPath - 文档本地路径
// 输出: QWidget* - 文档Tab页面，含下载/在线浏览按钮
// 功能: 无文档时显示上传提示；有文档时显示文件信息+下载+在线浏览按钮
QWidget* ToolManagementPage::createDocumentTab(int toolId, const QString& docPath) {
    Q_UNUSED(toolId);  // 预留：后续可用于文档替换/删除功能
    auto* tab = new QWidget();
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(0, 16, 0, 0);
    layout->setSpacing(16);
    layout->setAlignment(Qt::AlignTop);

    if (docPath.isEmpty() || !QFileInfo::exists(docPath)) {
        // 无文档 — 友好提示 + 上传入口 [V2.02 2026-06-28]
        //  作者：袁燕 — 要求详情页可直接上传文档，无需进入入库流程
        auto* emptyCard = new QFrame();
        emptyCard->setStyleSheet("QFrame{background:#f8f9fb;border-radius:12px;}");
        auto* emptyLayout = new QVBoxLayout(emptyCard);
        emptyLayout->setContentsMargins(40, 50, 40, 50);
        emptyLayout->setAlignment(Qt::AlignCenter);

        auto* iconLabel = new QLabel(QStringLiteral("📄"));
        iconLabel->setAlignment(Qt::AlignCenter);
        iconLabel->setStyleSheet("font-size:48px;background:transparent;");
        emptyLayout->addWidget(iconLabel);

        auto* tipLabel = new QLabel(QStringLiteral("该工具暂未上传文档"));
        tipLabel->setAlignment(Qt::AlignCenter);
        tipLabel->setStyleSheet("font-size:16px;color:#999;font-weight:600;background:transparent;");
        emptyLayout->addWidget(tipLabel);

        auto* subLabel = new QLabel(QStringLiteral("支持 doc / docx / pdf 格式，单个文件不超过 50MB"));
        subLabel->setAlignment(Qt::AlignCenter);
        subLabel->setStyleSheet("font-size:13px;color:#bbb;background:transparent;");
        subLabel->setWordWrap(true);
        emptyLayout->addWidget(subLabel);

        // 上传文档按钮 — 详情页直接上传，无需进入入库流程
        auto* uploadBtn = new QPushButton(QStringLiteral("⬆ 上传文档"));
        uploadBtn->setStyleSheet(StyleHelper::buttonPrimary());
        uploadBtn->setCursor(Qt::PointingHandCursor);
        uploadBtn->setFixedHeight(48);
        uploadBtn->setMinimumWidth(160);
        uploadBtn->setMaximumWidth(240);
        // 上传成功后关闭详情对话框并重新打开，刷新文档Tab
        connect(uploadBtn, &QPushButton::clicked, this, [this, toolId, uploadBtn]() {
            if (onUploadDocument(toolId)) {
                QWidget* w = uploadBtn;
                while (w && !w->isWindow()) w = w->parentWidget();
                if (auto* dlg = qobject_cast<QDialog*>(w)) dlg->accept();
                // 上传后重新打开详情——用toolId重新查询位置维度数据
                QTimer::singleShot(0, this, [this, toolId]() {
                    ToolController ctrl;
                    auto pageResult = ctrl.getToolList(1, SC::PAGE_SIZE_UNLIMITED, "", "", "", "", "");
                    for (const auto& ti : pageResult.list) {
                        if (ti.toolId == toolId) { onDetailTool(ti); break; }
                    }
                });
            }
        });
        emptyLayout->addSpacing(16);
        emptyLayout->addWidget(uploadBtn, 0, Qt::AlignCenter);

        layout->addWidget(emptyCard);
        layout->addStretch();
        return tab;
    }

    // 有文档 — 显示文件信息卡片 + 操作按钮
    QFileInfo docInfo(docPath);

    auto* infoCard = new QFrame();
    infoCard->setStyleSheet("QFrame{background:#f8f9fb;border-radius:12px;}");
    auto* cardLayout = new QVBoxLayout(infoCard);
    cardLayout->setContentsMargins(24, 24, 24, 24);
    cardLayout->setSpacing(14);

    // 文件图标 + 文件名
    auto* fileRow = new QHBoxLayout();
    fileRow->setSpacing(14);
    auto* fileIcon = new QLabel(QStringLiteral("📄"));
    fileIcon->setStyleSheet("font-size:36px;background:transparent;");
    fileRow->addWidget(fileIcon);

    auto* fileInfoWidget = new QWidget();
    auto* fileInfoLayout = new QVBoxLayout(fileInfoWidget);
    fileInfoLayout->setContentsMargins(0, 0, 0, 0);
    fileInfoLayout->setSpacing(4);

    auto* nameLabel = new QLabel(docInfo.fileName());
    nameLabel->setStyleSheet("font-size:17px;font-weight:700;color:#1a1a2e;background:transparent;");
    nameLabel->setWordWrap(true);
    fileInfoLayout->addWidget(nameLabel);

    // 文件信息行：格式 + 大小
    QString suffix = docInfo.suffix().toUpper();
    qint64 sizeKb = docInfo.size() / 1024;
    QString sizeText = sizeKb > 1024
                       ? QStringLiteral("%1 MB").arg(QString::number(sizeKb / 1024.0, 'f', 1))
                       : QStringLiteral("%1 KB").arg(sizeKb);
    auto* metaLabel = new QLabel(QStringLiteral("%1 格式 · %2").arg(suffix, sizeText));
    metaLabel->setStyleSheet("font-size:13px;color:#999;background:transparent;");
    fileInfoLayout->addWidget(metaLabel);

    fileRow->addWidget(fileInfoWidget, 1);
    cardLayout->addLayout(fileRow);

    layout->addWidget(infoCard);

    // 操作按钮区
    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(12);

    // 下载文档（另存为）
    auto* downloadBtn = new QPushButton(QStringLiteral("⬇ 下载文档"));
    downloadBtn->setStyleSheet(StyleHelper::buttonPrimary());
    downloadBtn->setCursor(Qt::PointingHandCursor);
    downloadBtn->setFixedHeight(48);
    downloadBtn->setMinimumWidth(160);
    connect(downloadBtn, &QPushButton::clicked, this, [docPath, docInfo, this]() {
        QString defaultName = docInfo.fileName();
        QString savePath = QFileDialog::getSaveFileName(
            this, QStringLiteral("保存文档"), defaultName,
            QStringLiteral("文档文件 (*.%1)").arg(docInfo.suffix())
        );
        if (savePath.isEmpty()) return;
        if (QFile::exists(savePath)) QFile::remove(savePath);
        if (QFile::copy(docPath, savePath)) {
            MessageDialog::showSuccess(this, QStringLiteral("下载成功"),
                QStringLiteral("文档已保存到：%1").arg(savePath));
        } else {
            MessageDialog::showError(this, QStringLiteral("下载失败"),
                QStringLiteral("文件复制失败，请检查目标路径权限"));
        }
    });
    btnRow->addWidget(downloadBtn);

    // 在线浏览（调用系统默认程序打开）
    auto* viewBtn = new QPushButton(QStringLiteral("👁 在线浏览"));
    viewBtn->setStyleSheet(StyleHelper::buttonOutline());
    viewBtn->setCursor(Qt::PointingHandCursor);
    viewBtn->setFixedHeight(48);
    viewBtn->setMinimumWidth(160);
    connect(viewBtn, &QPushButton::clicked, this, [docPath, this]() {
        // QDesktopServices::openUrl 调用系统默认程序打开文档
        // PDF → 系统PDF阅读器, Word → Word/WPS, 跨平台兼容
        bool ok = QDesktopServices::openUrl(QUrl::fromLocalFile(docPath));
        if (!ok) {
            MessageDialog::showError(this, QStringLiteral("打开失败"),
                QStringLiteral("无法打开文档，请检查系统是否安装对应的阅读软件"));
        }
    });
    btnRow->addWidget(viewBtn);

    btnRow->addStretch();
    layout->addLayout(btnRow);

    // 提示说明
    auto* tipLabel = new QLabel(QStringLiteral(
        "💡 在线浏览将调用系统默认程序打开文档；下载文档可另存到指定位置"
    ));
    tipLabel->setStyleSheet("font-size:12px;color:#bbb;padding:8px 4px;background:transparent;");
    tipLabel->setWordWrap(true);
    layout->addWidget(tipLabel);

    layout->addStretch();
    return tab;
}

// 详情页上传工具文档
// 输入: toolId - 工具ID
// 输出: bool - true=上传成功, false=用户取消或失败
// 功能: 选文件→校验格式大小→复制到AppData→更新DB document_path
//  作者：袁燕 — 校验规则与入库页完全一致，保证一致性
bool ToolManagementPage::onUploadDocument(int toolId) {
    // 文件过滤器：Word文档 + PDF（与入库页一致）
    QString filter = QStringLiteral(
        "文档文件 (*.doc *.docx *.pdf);;"
        "Word 97-2003 文档 (*.doc);;"
        "Word 文档 (*.docx);;"
        "PDF 文件 (*.pdf);;"
        "所有文件 (*.*)"
    );
    QString srcPath = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择工具文档"), QString(), filter
    );
    if (srcPath.isEmpty()) return false;  // 用户取消

    // 校验文件后缀
    QFileInfo srcInfo(srcPath);
    QString suffix = srcInfo.suffix().toLower();
    if (!SC::TOOL_DOC_SUFFIXES.contains(suffix)) {
        MessageDialog::showWarning(this, QStringLiteral("格式不支持"),
            QStringLiteral("仅支持 %1 格式的文档").arg(SC::TOOL_DOC_SUFFIXES.join(" / ")));
        return false;
    }

    // 校验文件大小
    qint64 sizeMb = srcInfo.size() / (1024 * 1024);
    if (sizeMb > SC::TOOL_DOC_MAX_SIZE_MB) {
        MessageDialog::showWarning(this, QStringLiteral("文件过大"),
            QStringLiteral("文档大小不能超过 %1MB，当前 %2MB").arg(SC::TOOL_DOC_MAX_SIZE_MB).arg(sizeMb));
        return false;
    }

    // 存储目录：AppData/SmartCabinet/QtSmartCabinet/tool_documents/（与入库页统一）
    QString docDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                     + "/tool_documents";
    QDir().mkpath(docDir);

    // 生成唯一文件名：时间戳_原文件名，避免冲突
    QString destName = QString("%1_%2").arg(
        QDateTime::currentDateTime().toString("yyyyMMddHHmmss"), srcInfo.fileName()
    );
    QString destPath = docDir + "/" + destName;

    // 复制文件
    if (QFile::exists(destPath)) QFile::remove(destPath);
    if (!QFile::copy(srcPath, destPath)) {
        MessageDialog::showError(this, QStringLiteral("上传失败"),
            QStringLiteral("文件复制失败，请检查磁盘空间或权限"));
        return false;
    }

    // 更新DB document_path字段
    ToolController ctrl;
    if (!ctrl.updateToolDocument(toolId, destPath)) {
        MessageDialog::showError(this, QStringLiteral("上传失败"),
            QStringLiteral("文档路径更新失败，请稍后重试"));
        QFile::remove(destPath);  // 回滚已复制的文件
        return false;
    }

    MessageDialog::showSuccess(this, QStringLiteral("上传成功"),
        QStringLiteral("工具文档已上传，可在此Tab下载或在线浏览"));
    return true;
}

void ToolManagementPage::onAddTool() {
    m_editToolId = 0;
    if (!m_toolDialog) {
        // 改用BaseDialog统一圆角无边框风格
        m_toolDialog = new BaseDialog(this, 460);
        m_toolDialog->setDialogTitle(QStringLiteral("添加工具"));

        auto* cl = m_toolDialog->contentLayout();
        cl->setSpacing(12);

        QString labelStyle = QString("font-size:16px;font-weight:bold;color:%1;background:transparent;").arg(StyleHelper::textColor());
        auto makeLabel = [&](const QString& text) {
            auto* l = new QLabel(text);
            l->setStyleSheet(labelStyle);
            return l;
        };

        m_dlgName = new QLineEdit(); m_dlgName->setStyleSheet(StyleHelper::lineEdit());
        m_dlgCode = new QLineEdit(); m_dlgCode->setStyleSheet(StyleHelper::lineEdit());
        m_dlgCategory = new QComboBox(); m_dlgCategory->setEditable(true); m_dlgCategory->setStyleSheet(StyleHelper::comboBox());
        m_dlgPosition = new QLineEdit(); m_dlgPosition->setStyleSheet(StyleHelper::lineEdit());
        m_dlgUnit = new QLineEdit(); m_dlgUnit->setStyleSheet(StyleHelper::lineEdit());
        m_dlgSpec = new QLineEdit(); m_dlgSpec->setStyleSheet(StyleHelper::lineEdit());

        ToolController ctrl;
        QList<ToolCategory> cats = ctrl.getCategories();
        for (const auto& catObj : cats) {
            m_dlgCategory->addItem(catObj.categoryName, catObj.categoryId);
        }

        // 使用HBox布局替代QFormLayout
        auto addField = [&](const QString& label, QWidget* w) {
            auto* row = new QHBoxLayout();
            row->setSpacing(12);
            auto* lb = makeLabel(label);
            lb->setFixedWidth(80);
            row->addWidget(lb);
            row->addWidget(w, 1);
            cl->addLayout(row);
        };

        addField(QStringLiteral("工具名称:"), m_dlgName);
        addField(QStringLiteral("编码:"), m_dlgCode);
        addField(QStringLiteral("分类:"), m_dlgCategory);
        addField(QStringLiteral("位置:"), m_dlgPosition);
        addField(QStringLiteral("单位:"), m_dlgUnit);
        addField(QStringLiteral("规格:"), m_dlgSpec);
        // 删除"库存数量"输入框，数量恒为1（一个位置=一个工具）

        cl->addSpacing(8);

        m_dlgSaveBtn = new QPushButton(QStringLiteral("保存"));
        m_dlgSaveBtn->setStyleSheet(StyleHelper::buttonPrimary());
        m_dlgSaveBtn->setCursor(Qt::PointingHandCursor);
        connect(m_dlgSaveBtn, &QPushButton::clicked, this, &ToolManagementPage::onSubmitTool);

        // 有保存就有取消（要求）
        auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
        cancelBtn->setStyleSheet(StyleHelper::buttonDefault());
        cancelBtn->setCursor(Qt::PointingHandCursor);
        connect(cancelBtn, &QPushButton::clicked, this, [this]() { m_toolDialog->reject(); });

        auto* btnLayout = m_toolDialog->buttonLayout();
        btnLayout->addStretch();
        btnLayout->addWidget(cancelBtn);
        btnLayout->addWidget(m_dlgSaveBtn);
    }
    m_toolDialog->setDialogTitle(QStringLiteral("添加工具"));
    m_dlgName->clear(); m_dlgCode->clear(); m_dlgCategory->setCurrentIndex(0);
    m_dlgPosition->clear(); m_dlgUnit->setText(QStringLiteral("把"));
    m_dlgSpec->clear();
    m_toolDialog->exec();
}

void ToolManagementPage::onEditTool(int toolId) {
    m_editToolId = toolId;
    ToolController ctrl;
    ToolInfo t = ctrl.getToolById(toolId);
    if (t.toolId == 0) return;
    if (!m_toolDialog) { onAddTool(); return; }
    m_toolDialog->setDialogTitle(QStringLiteral("编辑工具"));
    m_dlgName->setText(t.toolName);
    m_dlgCode->setText(t.toolCode);
    m_dlgCategory->setCurrentText(t.categoryName);
    m_dlgPosition->setText(t.position);
    m_dlgSpec->setText(t.spec);
    m_toolDialog->exec();
}

void ToolManagementPage::onDeleteTool(int toolId) {
    if (!MessageDialog::showQuestion(this, QStringLiteral("确认删除"),
        QStringLiteral("确定要删除该工具吗？"))) return;
    ToolController ctrl;
    if (ctrl.deleteTool(toolId)) {
        loadTools();
        loadStats();  // 刷新统计
    } else {
        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("删除失败"));
    }
}

void ToolManagementPage::onSubmitTool() {
    QString name = m_dlgName->text().trimmed();
    if (name.isEmpty()) { MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("工具名称不能为空")); return; }

    ToolInfo info;
    info.toolName = name;
    info.toolCode = m_dlgCode->text().trimmed();
    info.categoryId = m_dlgCategory->currentData().toInt();
    info.position = m_dlgPosition->text().trimmed();
    // 数量恒为1（一个位置=一个工具），不再从输入框读取
    info.totalQty = 1;
    info.currentQty = 1;
    info.spec = m_dlgSpec->text().trimmed();
    info.cabinetId = 0;
    // 从AppConfig读取本机机组ID，系统设置页面配置
    info.machineGroupId = AppConfig::instance().localMachineGroupId();
    info.status = "in_stock";

    ToolController ctrl;
    bool ok;
    if (m_editToolId == 0) {
        ok = (ctrl.addTool(info) > 0);
    } else {
        info.toolId = m_editToolId;
        ok = ctrl.updateTool(info);
    }
    if (ok) { m_toolDialog->accept(); loadTools(); loadStats(); }
    else { MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("保存失败")); }
}

void ToolManagementPage::onSearchKeyboardClicked() {
    if (!m_softKeyboard) {
        m_softKeyboard = new SoftKeyboard(this);
        m_softKeyboard->setMode(SoftKeyboard::ModeEn);
        connect(m_softKeyboard, &SoftKeyboard::confirmed, this, [this]() {
            m_softKeyboard->hide();
        });
    }
    QPoint pos = m_searchEdit->mapToGlobal(QPoint(0, m_searchEdit->height() + 4));
    m_softKeyboard->show(m_searchEdit, pos);
}
