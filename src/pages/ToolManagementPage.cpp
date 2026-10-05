/**
 * @file ToolManagementPage.cpp
 * @brief 工具管理页面实现 - 参考工程机组管理风格重做
 * @author 袁燕
 *   1. 顶部统计卡片（全部工具/在库/已借用/维护中）
 *   2. 表格列：编号/名称/规格型号/机组/位置/状态/最近操作/操作
 *   3. 状态用彩色标签（在库绿/已借用橙/维护中灰）
 *   4. 操作按钮改为"详情"
 *   5. 数据使用数据库 machine_group + tool_info 关联查询
 */
#include "ToolManagementPage.h"
#include "ui_ToolManagementPage.h"
#include "components/PaginationBar.h"
#include "utils/StyleHelper.h"
#include "components/FormFactory.h"  // 表单控件工厂（收敛重复lambda）
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

ToolManagementPage::ToolManagementPage(QWidget* parent) : QWidget(parent), ui(new Ui::ToolManagementPage),
    m_toolDialog(nullptr), m_editToolId(0), m_currentPage(1), m_pageSize(20), m_totalRecords(0) {
    // 静态布局来自ToolManagementPage.ui（Qt Designer可视化维护）
    ui->setupUi(this);
    setupUI();
    loadStats();  // 加载统计
}

ToolManagementPage::~ToolManagementPage() {
    delete ui;
}

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

/**
 * @brief 初始化统计统计卡片
 */
void ToolManagementPage::setupStatsCards() {
    // 统计卡片行 
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

/**
 * @brief 构建页面界面：读取 .ui 静态布局并补充动态控件
 */
void ToolManagementPage::setupUI() {
    // 桥接.ui控件（业务逻辑沿用m_成员，零改动）
    m_searchEdit = ui->searchBar->lineEdit();
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索工具名称/编号..."));
    m_searchBtn = ui->searchBtn;
    m_resetBtn = ui->resetBtn;
    m_table = ui->table;
    m_paginationBar = ui->paginationBar;

    // 统计卡片（SC常量样式动态构建装入.ui容器）
    setupStatsCards();
    ui->statsLayout->addWidget(m_statsCardAll, 1);
    ui->statsLayout->addWidget(m_statsCardInStock, 1);
    ui->statsLayout->addWidget(m_statsCardBorrowed, 1);
    ui->statsLayout->addWidget(m_statsCardCheckedOut, 1);
    ui->statsLayout->addWidget(m_statsCardPending, 1);
    ui->statsLayout->addWidget(m_statsCardMaintenance, 1);

    // 搜索框软键盘按钮
    connect(ui->searchBar->keyboardButton(), &QPushButton::clicked, this, &ToolManagementPage::onSearchKeyboardClicked);

    // 类别筛选——从DB动态获取（自定义组件装入.ui槽位）
    m_categoryFilter = new MultiSelectFilter(QStringLiteral("全部类别"), this);
    connect(m_categoryFilter, &MultiSelectFilter::selectionChanged, this, &ToolManagementPage::onSearch);
    ui->categoryFilterSlotLayout->addWidget(m_categoryFilter);

    // 机组筛选——从machine_group表动态获取
    m_machineGroupFilter = new MultiSelectFilter(QStringLiteral("全部机组"), this);
    connect(m_machineGroupFilter, &MultiSelectFilter::selectionChanged, this, &ToolManagementPage::onSearch);
    ui->machineGroupFilterSlotLayout->addWidget(m_machineGroupFilter);

    connect(m_searchBtn, &QPushButton::clicked, this, &ToolManagementPage::onSearch);
    connect(m_resetBtn, &QPushButton::clicked, this, &ToolManagementPage::onReset);

    // 表格列宽策略：数据列Stretch均分，操作列Fixed紧凑130px（触屏按钮）
    m_table->horizontalHeader()->setStretchLastSection(false);
    for (int i = 0; i < 9; i++) {
        m_table->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
    }
    m_table->horizontalHeader()->setSectionResizeMode(9, QHeaderView::Fixed);
    m_table->setColumnWidth(9, 130);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setMinimumSectionSize(60);

    // 分页
    connect(m_paginationBar, &PaginationBar::prevClicked, this, &ToolManagementPage::onPrevPage);
    connect(m_paginationBar, &PaginationBar::nextClicked, this, &ToolManagementPage::onNextPage);

    loadCategories();
    loadMachineGroups();
}

/**
 * @brief 刷新页面数据与统计显示
 */
void ToolManagementPage::refresh() {
    // 切换到本页面时重置为默认筛选条件和第1页
    m_searchEdit->clear();
    m_categoryFilter->selectAll();
    m_machineGroupFilter->selectAll();
    m_currentPage = 1;
    loadStats();
    loadTools();
}

/**
 * @brief 处理搜索
 */
void ToolManagementPage::onSearch() {
    m_currentPage = 1;  // 筛选查询必须重置到第1页
    loadTools();
}

/**
 * @brief 处理重置
 */
void ToolManagementPage::onReset() {
    m_searchEdit->clear();
    m_categoryFilter->selectAll();
    m_machineGroupFilter->selectAll();
    m_currentPage = 1;  // 重置筛选必须重置到第1页
    loadTools();
}

/**
 * @brief 加载工具分类到分类下拉框
 */
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
// 按工具件数统计（total_qty/current_qty/borrowed_qty），不按种类数
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

/**
 * @brief 加载工具
 */
void ToolManagementPage::loadTools() {
    QString kw = m_searchEdit->text().trimmed();
    QStringList cats = m_categoryFilter->selectedOptions();
    QString cat = cats.join(",");
    // 动态判断全选（不硬编码5）
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
    m_paginationBar->setPageInfo(m_currentPage, totalPages);
    m_paginationBar->setTotalRecords(m_totalRecords);

    // 填充表格：编号/类别/名称/规格型号/机组/位置/状态/借用中/借用人/最近操作/操作
    // 无"库存"列：一个位置(机组-柜-层-位号)=一个工具，位置为唯一标识格式
    m_table->setRowCount(pageResult.list.size());
    for (int i = 0; i < pageResult.list.size(); ++i) {
        const ToolInfo& t = pageResult.list[i];

        m_table->setItem(i, 0, new QTableWidgetItem(t.toolCode));               // 编号
        m_table->setItem(i, 1, new QTableWidgetItem(t.categoryName.isEmpty() ? QStringLiteral("--") : t.categoryName));  // 类别
        m_table->setItem(i, 2, new QTableWidgetItem(t.toolName));               // 名称
        m_table->setItem(i, 3, new QTableWidgetItem(t.spec));                   // 规格型号

        // 机组 
        QString groupName = t.machineGroupName.isEmpty() ? QStringLiteral("未分配") : t.machineGroupName;
        m_table->setItem(i, 4, new QTableWidgetItem(groupName));

        // 位置格式：柜号-层号-位号（两位补零，如A-01-03）
        QString posDisplay = StyleHelper::formatPosition(t.cabinetName, t.layer, t.position);
        auto* posItem = new QTableWidgetItem(posDisplay);
        posItem->setTextAlignment(Qt::AlignCenter);
        posItem->setFont(QFont(posItem->font().family(), 12, QFont::DemiBold));
        posItem->setForeground(QColor("#333333"));
        m_table->setItem(i, 5, posItem);

        // 状态 - 彩色标签样式 
        QString statusText;
        QString statusColor;
        if (t.status == SC::TOOL_IN_STOCK) {
            statusText = QStringLiteral("在库");
            statusColor = "#43a047";
        } else if (t.status == SC::TOOL_BORROWED) {
            statusText = QStringLiteral("已借用");
            statusColor = "#f57c00";
        } else if (t.status == SC::TOOL_CHECKED_OUT) {           // 新增已出库状态
            statusText = QStringLiteral("已出库");          // 区别于已借用，表示永久出库消耗
            statusColor = "#e53935";  //   红色警示，作者：袁燕
        } else if (t.status == SC::TOOL_MAINTENANCE) {
            statusText = QStringLiteral("维护中");
            statusColor = "#999999";
        } else if (t.status == SC::TOOL_PENDING) {               // 新增待入库状态
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



        // 借用人 显示最近借用人 列索引7
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

        // 最近操作 列索引8
        QString lastOp;
        if (!t.latestOpTime.isEmpty()) {
            QString opType;
            if (t.latestOpType == "borrow") opType = QStringLiteral("借用");
            else if (t.latestOpType == SC::OP_CHECKOUT) opType = QStringLiteral("出库");
            else if (t.latestOpType == SC::OP_CHECKIN) opType = QStringLiteral("入库");
            else opType = QStringLiteral("操作");
            lastOp = t.latestOpTime + "\n" + opType;
        } else {
            lastOp = QStringLiteral("暂无");
        }
        auto* opItem = new QTableWidgetItem(lastOp);
        opItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        opItem->setForeground(QColor("#888888"));
        m_table->setItem(i, 8, opItem);

        // 操作列 - 详情按钮 去掉编辑按钮，只保留详情
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

/**
 * @brief 处理页面
 */
void ToolManagementPage::onPrevPage() {
    if (m_currentPage > 1) { m_currentPage--; loadTools(); }
}

/**
 * @brief 处理页面
 */
void ToolManagementPage::onNextPage() {
    int totalPages = (m_totalRecords + m_pageSize - 1) / m_pageSize;
    if (m_currentPage < totalPages) { m_currentPage++; loadTools(); }
}




/**
 * @brief 处理搜索键盘点击事件
 */
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
