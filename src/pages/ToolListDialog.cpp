/**
 * @file ToolListDialog.cpp
 * @brief 工具清单对话框实现 - 按状态查询工具列表（在库/已借出）
 * @author 袁燕
 */
#include "ToolListDialog.h"
#include "components/SearchBar.h"
#include "components/MultiSelectFilter.h"
#include "components/SingleSelectFilter.h"
#include "components/PaginationBar.h"
#include "components/SoftKeyboard.h"
#include "controller/ToolController.h"
#include "utils/StyleHelper.h"
#include "common/Constants.h"
#include <QHeaderView>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QFont>
#include <QColor>
#include <QApplication>
#include <QScreen>

/**
 * @brief 依据屏幕可用宽度与父窗口宽度计算卡片宽度，避免对话框超出屏幕边缘被裁切
 * @param parent 父窗口
 * @return 卡片宽度（720~1100 之间）
 */
static int fitCardWidth(QWidget* parent) {
    QScreen* screen = QApplication::primaryScreen();
    int avail = screen ? screen->availableGeometry().width() : 1280;
    if (parent) avail = qMin(avail, parent->window()->width());
    return qBound(720, avail - 80, 1100);
}

/**
 * @brief 构造函数：构建界面并按状态加载第一页数据
 * @param status 过滤的工具状态
 * @param title 对话框标题
 * @param parent 父窗口
 */
ToolListDialog::ToolListDialog(const QString& status, const QString& title, QWidget* parent)
    : BaseDialog(parent, fitCardWidth(parent)), m_status(status) {
    setDialogTitle(title);
    setButtonAreaVisible(false);
    // 状态筛选项：首项「全部状态」不筛选，其余文案与数据库状态值一一对应
    m_statusOptions = { QStringLiteral("全部状态"),
                        SC::toolStatusText(SC::TOOL_IN_STOCK),
                        SC::toolStatusText(SC::TOOL_BORROWED),
                        SC::toolStatusText(SC::TOOL_CHECKED_OUT),
                        SC::toolStatusText(SC::TOOL_PENDING),
                        SC::toolStatusText(SC::TOOL_MAINTENANCE) };
    m_statusValues  = { QString(),
                        SC::TOOL_IN_STOCK, SC::TOOL_BORROWED, SC::TOOL_CHECKED_OUT,
                        SC::TOOL_PENDING, SC::TOOL_MAINTENANCE };
    // 打开对话框时的状态作为初值：从统计卡片进入时默认只看该状态，重置也回到该状态
    m_initialStatusIndex = m_statusValues.indexOf(m_status);
    if (m_initialStatusIndex < 0) m_initialStatusIndex = 0;
    buildContent();
    loadCategories();
    loadMachineGroups();
    loadTools();
}

/**
 * @brief 构建对话框内容：返回行 + 搜索行 + 筛选行 + 工具表格 + 分页栏
 */
void ToolListDialog::buildContent() {
    auto* cl = contentLayout();
    cl->setSpacing(12);

    // 返回行：关闭清单对话框，回到工具管理页面
    auto* backRow = new QHBoxLayout();
    auto* backBtn = new QPushButton(QStringLiteral("← 返回"));
    backBtn->setStyleSheet(StyleHelper::buttonOutline());
    backBtn->setMinimumHeight(StyleHelper::Token::ControlHeightTouch);
    backBtn->setCursor(Qt::PointingHandCursor);
    connect(backBtn, &QPushButton::clicked, this, &QDialog::accept);
    backRow->addWidget(backBtn);
    backRow->addStretch(1);
    cl->addLayout(backRow);

    // 搜索行：搜索栏占据整行剩余宽度 + 查询/重置（不与筛选器挤在同一行，避免被压窄）
    auto* searchRow = new QHBoxLayout();
    searchRow->setSpacing(12);

    m_searchBar = new SearchBar(this);
    m_searchBar->setPlaceholderText(QStringLiteral("搜索工具名称/编号..."));
    m_searchBar->setMinimumSize(320, StyleHelper::Token::ControlHeightTouch);
    m_searchBar->lineEdit()->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
    searchRow->addWidget(m_searchBar, 1);
    connect(m_searchBar->keyboardButton(), &QPushButton::clicked,
            this, &ToolListDialog::onSearchKeyboardClicked);

    m_searchBtn = new QPushButton(QStringLiteral("查询"));
    m_searchBtn->setStyleSheet(StyleHelper::buttonPrimary());
    m_searchBtn->setMinimumHeight(StyleHelper::Token::ControlHeightTouch);
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    connect(m_searchBtn, &QPushButton::clicked, this, &ToolListDialog::onSearch);
    searchRow->addWidget(m_searchBtn);

    m_resetBtn = new QPushButton(QStringLiteral("重置"));
    m_resetBtn->setStyleSheet(StyleHelper::buttonOutline());
    m_resetBtn->setMinimumHeight(StyleHelper::Token::ControlHeightTouch);
    m_resetBtn->setCursor(Qt::PointingHandCursor);
    connect(m_resetBtn, &QPushButton::clicked, this, &ToolListDialog::onReset);
    searchRow->addWidget(m_resetBtn);

    cl->addLayout(searchRow);

    // 筛选行：类别/机组/状态三项等宽平分整行
    auto* filterRow = new QHBoxLayout();
    filterRow->setSpacing(12);

    m_categoryFilter = new MultiSelectFilter(QStringLiteral("全部类别"), this);
    connect(m_categoryFilter, &MultiSelectFilter::selectionChanged, this, &ToolListDialog::onSearch);
    filterRow->addWidget(m_categoryFilter, 1);

    m_machineGroupFilter = new MultiSelectFilter(QStringLiteral("全部机组"), this);
    connect(m_machineGroupFilter, &MultiSelectFilter::selectionChanged, this, &ToolListDialog::onSearch);
    filterRow->addWidget(m_machineGroupFilter, 1);

    // 状态单选筛选：在库/已借出/已出库/待入库/维护中，初值由打开对话框时的状态决定
    m_statusFilter = new SingleSelectFilter(QStringLiteral("全部状态"), this);
    m_statusFilter->setOptions(m_statusOptions);
    m_statusFilter->setSelectedIndex(m_initialStatusIndex);
    connect(m_statusFilter, &SingleSelectFilter::selectionChanged, this, &ToolListDialog::onSearch);
    filterRow->addWidget(m_statusFilter, 1);

    cl->addLayout(filterRow);

    // 工具表格（编号/名称/规格型号/机组/位置/状态/借用人/操作）
    m_table = new QTableWidget(this);
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("编号"), QStringLiteral("名称"),
        QStringLiteral("规格型号"), QStringLiteral("机组"), QStringLiteral("位置"),
        QStringLiteral("状态"), QStringLiteral("借用人"), QStringLiteral("操作")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->setStyleSheet(StyleHelper::listTableStyle());
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setMinimumSectionSize(56);
    // 文本列（编号/名称/规格/机组）拉伸均分，取值列固定宽度并适度收窄，
    // 把省下的宽度让给文本列，避免"机组""规格型号"等较长文字折行
    for (int i = 0; i < 4; i++) {
        m_table->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
    }
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_table->setColumnWidth(4, 100);   // 位置
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    m_table->setColumnWidth(5, 80);    // 状态
    m_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Fixed);
    m_table->setColumnWidth(6, 90);    // 借用人
    m_table->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Fixed);
    m_table->setColumnWidth(7, 100);   // 操作
    // 表体高度下调，为新增的筛选行腾出纵向空间，保证整卡在 768 高屏幕上完整显示
    m_table->setMinimumHeight(340);
    cl->addWidget(m_table, 1);

    // 分页栏
    m_paginationBar = new PaginationBar(this);
    connect(m_paginationBar, &PaginationBar::prevClicked, this, &ToolListDialog::onPrevPage);
    connect(m_paginationBar, &PaginationBar::nextClicked, this, &ToolListDialog::onNextPage);
    cl->addWidget(m_paginationBar);
}

/**
 * @brief 加载类别筛选选项（从数据库动态获取）
 */
void ToolListDialog::loadCategories() {
    ToolController ctrl;
    m_allCategories = ctrl.categoryNames();
    m_categoryFilter->setOptions(m_allCategories);
}

/**
 * @brief 加载机组筛选选项（从数据库动态获取）
 */
void ToolListDialog::loadMachineGroups() {
    ToolController ctrl;
    m_allMachineGroups.clear();
    QList<QJsonObject> groups = ctrl.getMachineGroups();
    for (const auto& g : groups) {
        m_allMachineGroups << g["groupName"].toString();
    }
    m_machineGroupFilter->setOptions(m_allMachineGroups);
}

/**
 * @brief 应用筛选条件并回到第1页
 */
void ToolListDialog::onSearch() {
    m_page = 1;  // 筛选查询必须重置到第1页
    loadTools();
}

/**
 * @brief 清空筛选条件并重新加载
 */
void ToolListDialog::onReset() {
    m_searchBar->lineEdit()->clear();
    m_categoryFilter->selectAll();
    m_machineGroupFilter->selectAll();
    m_statusFilter->setSelectedIndex(m_initialStatusIndex);  // 状态回到打开对话框时的筛选
    m_page = 1;
    loadTools();
}

/**
 * @brief 上一页
 */
void ToolListDialog::onPrevPage() {
    if (m_page > 1) { m_page--; loadTools(); }
}

/**
 * @brief 下一页
 */
void ToolListDialog::onNextPage() {
    int totalPages = (m_total + m_pageSize - 1) / m_pageSize;
    if (m_page < totalPages) { m_page++; loadTools(); }
}

/**
 * @brief 在搜索框下方弹出拼音软键盘（工具名称以中文为主，数字/编号可直接输入）
 */
void ToolListDialog::onSearchKeyboardClicked() {
    if (!m_softKeyboard) {
        m_softKeyboard = new SoftKeyboard(this);
        m_softKeyboard->setMode(SoftKeyboard::ModeCn);
        connect(m_softKeyboard, &SoftKeyboard::confirmed, this, [this]() {
            m_softKeyboard->hide();
            onSearch();  // 键盘"搜索"确认键直接出结果
        });
    }
    QLineEdit* edit = m_searchBar->lineEdit();
    QPoint pos = edit->mapToGlobal(QPoint(0, edit->height() + 4));
    m_softKeyboard->show(edit, pos);
}

/**
 * @brief 按当前筛选条件加载工具列表（分页），并填充表格
 */
void ToolListDialog::loadTools() {
    QString kw = m_searchBar->lineEdit()->text().trimmed();

    // 类别筛选：全选或未选则不过滤
    QStringList cats = m_categoryFilter->selectedOptions();
    QString cat = cats.join(",");
    if (cats.isEmpty() || cats.size() == m_allCategories.size()) cat = "";

    // 机组筛选：全选或未选则不过滤
    QStringList mgs = m_machineGroupFilter->selectedOptions();
    QString mg = mgs.join(",");
    if (mgs.isEmpty() || mgs.size() == m_allMachineGroups.size()) mg = "";

    // 状态筛选：取当前选中项对应的数据库状态值，首项为空串表示全部状态
    const int statusIdx = m_statusFilter->selectedIndex();
    QString status = (statusIdx >= 0 && statusIdx < m_statusValues.size())
                     ? m_statusValues[statusIdx] : QString();

    ToolController ctrl;
    auto pageResult = ctrl.getToolList(m_page, m_pageSize, kw, cat, "", status, mg);
    m_total = pageResult.total;

    int totalPages = (m_total + m_pageSize - 1) / m_pageSize;
    m_paginationBar->setPageInfo(m_page, totalPages);
    m_paginationBar->setTotalRecords(m_total);

    m_table->setRowCount(pageResult.list.size());
    for (int i = 0; i < pageResult.list.size(); ++i) {
        const ToolInfo& t = pageResult.list[i];

        m_table->setItem(i, 0, new QTableWidgetItem(t.toolCode));
        m_table->setItem(i, 1, new QTableWidgetItem(t.toolName));
        m_table->setItem(i, 2, new QTableWidgetItem(t.spec));

        // 机组
        QString groupName = t.machineGroupName.isEmpty() ? QStringLiteral("未分配") : t.machineGroupName;
        m_table->setItem(i, 3, new QTableWidgetItem(groupName));

        // 位置格式：柜号-层号-位号（两位补零，如A-01-03）
        QString posDisplay = StyleHelper::formatPosition(t.cabinetName, t.layer, t.position);
        auto* posItem = new QTableWidgetItem(posDisplay);
        posItem->setTextAlignment(Qt::AlignCenter);
        posItem->setFont(QFont(posItem->font().family(), 12, QFont::DemiBold));
        posItem->setForeground(QColor(StyleHelper::textBody()));
        m_table->setItem(i, 4, posItem);

        // 状态彩色标签
        QString statusText;
        QString statusColor;
        if (t.status == SC::TOOL_IN_STOCK) {
            statusText = QStringLiteral("在库");
            statusColor = StyleHelper::successColor();
        } else if (t.status == SC::TOOL_BORROWED) {
            statusText = QStringLiteral("已借用");
            statusColor = StyleHelper::warningColor();
        } else if (t.status == SC::TOOL_CHECKED_OUT) {
            statusText = QStringLiteral("已出库");
            statusColor = StyleHelper::dangerColor();
        } else if (t.status == SC::TOOL_MAINTENANCE) {
            statusText = QStringLiteral("维护中");
            statusColor = StyleHelper::textMuted();
        } else if (t.status == SC::TOOL_PENDING) {
            statusText = QStringLiteral("待入库");
            statusColor = StyleHelper::primaryColor();
        } else {
            statusText = t.status;
            statusColor = StyleHelper::textMuted();
        }
        auto* statusLabel = new QLabel(statusText);
        statusLabel->setAlignment(Qt::AlignCenter);
        statusLabel->setFixedSize(64, 28);
        statusLabel->setStyleSheet(QString(
            "QLabel{background:%1;color:#fff;border-radius:6px;" + StyleHelper::fontSize(StyleHelper::Token::FontSmall) + ";font-weight:600;}"
        ).arg(statusColor));
        m_table->setCellWidget(i, 5, statusLabel);

        // 借用人：最近操作人为借用操作时加粗标蓝
        QString borrowerName = t.latestOpUser;
        auto* borrowerItem = new QTableWidgetItem(
            borrowerName.isEmpty() ? QStringLiteral("--") : borrowerName);
        borrowerItem->setTextAlignment(Qt::AlignCenter);
        if (!borrowerName.isEmpty() && t.latestOpType == "borrow") {
            borrowerItem->setForeground(QColor(StyleHelper::primaryColor()));
            QFont borrowerFont = borrowerItem->font();
            borrowerFont.setBold(true);
            borrowerItem->setFont(borrowerFont);
        } else {
            borrowerItem->setForeground(QColor(StyleHelper::textDisabled()));
        }
        m_table->setItem(i, 6, borrowerItem);

        // 操作列：详情按钮（详情对话框由工具管理页构建）
        auto* opWidget = new QWidget();
        opWidget->setStyleSheet("background:transparent;");
        auto* opLayout = new QHBoxLayout(opWidget);
        opLayout->setContentsMargins(4, 4, 4, 4);
        opLayout->setSpacing(0);
        opLayout->setAlignment(Qt::AlignCenter);

        auto* detailBtn = new QPushButton(QStringLiteral("详情"));
        detailBtn->setFixedSize(64, 34);
        detailBtn->setStyleSheet(
            "QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:8px;"
            "" + StyleHelper::fontSize(StyleHelper::Token::FontBody) + ";font-weight:600;}"
            "QPushButton:hover{background:#3d8ae0;}"
            "QPushButton:pressed{background:#2b6cdf;}"
        );
        detailBtn->setCursor(Qt::PointingHandCursor);
        connect(detailBtn, &QPushButton::clicked, this, [this, t] { emit detailRequested(t); });

        opLayout->addWidget(detailBtn);
        m_table->setCellWidget(i, 7, opWidget);
        m_table->setRowHeight(i, 60);
    }
}
