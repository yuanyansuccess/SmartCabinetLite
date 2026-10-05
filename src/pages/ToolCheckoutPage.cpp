/**
 * @file ToolCheckoutPage.cpp
 * @brief 工具出库页面 — 批量出库、出库记录、二次确认、分页
 * @author 袁燕
 */
#include "ToolCheckoutPage.h"
#include "ui_ToolCheckoutPage.h"
#include <QTableWidget>  // 待出库/出库记录表格
#include "components/PaginationBar.h"
#include "utils/StyleHelper.h"
#include "components/DrawerOpeningDialog.h"
#include "components/VerifyAlertDialog.h"
#include "components/ResultDialog.h"
#include "services/ToolService.h"
#include "services/BorrowService.h"
#include "components/SoftKeyboard.h"
#include "components/MultiSelectFilter.h"
#include "db/RecordDAO.h"             // 出库记录分页查询
#include "services/CheckoutService.h" // 出库落库（映射表/库存/日志）
#include "common/AppConfig.h"         // 获取当前用户
#include "common/Constants.h"         // 分页常量
#include "services/AlertService.h"    // 告警闭环写入
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
#include <QTimer>  // 抽屉打开动画
#include <algorithm>

ToolCheckoutPage::ToolCheckoutPage(QWidget* parent) : QWidget(parent), ui(new Ui::ToolCheckoutPage) {
    // 静态布局来自ToolCheckoutPage.ui（Qt Designer可视化维护）
    ui->setupUi(this);
    setupUI();
}

ToolCheckoutPage::~ToolCheckoutPage() {
    delete ui;
}

/**
 * @brief 构建页面界面：读取 .ui 静态布局并补充动态控件
 */
void ToolCheckoutPage::setupUI() {
    // 桥接.ui控件（业务逻辑沿用m_成员，零改动）
    m_tabWidget = ui->tabWidget;
    m_searchEdit = ui->searchBar->lineEdit();
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索工具名称..."));
    m_searchBtn = ui->searchBtn;
    m_searchResetBtn = ui->searchResetBtn;
    m_selectedHint = ui->selectedHint;
    m_table = ui->table;
    m_paginationBar = ui->paginationBar;
    m_resetBtn = ui->resetBtn;
    m_batchBtn = ui->batchBtn;
    m_recordTable = ui->recordTable;
    m_recordPaginationBar = ui->recordPaginationBar;

    // 搜索框软键盘事件过滤
    m_searchEdit->installEventFilter(this);
    connect(ui->searchBar->keyboardButton(), &QPushButton::clicked, this, &ToolCheckoutPage::onSearchFieldClicked);

    // 分类多选筛选（自定义组件，动态创建装入.ui槽位）
    m_categoryFilter = new MultiSelectFilter(QStringLiteral("全部类别"), this);
    m_categoryFilter->setOptions({QStringLiteral("电动工具"), QStringLiteral("手动工具"),
        QStringLiteral("测量工具"), QStringLiteral("焊接工具"), QStringLiteral("照明工具")});
    connect(m_categoryFilter, &MultiSelectFilter::selectionChanged, this, [this](const QStringList&) { m_currentPage = 1; loadTools(); });
    ui->categoryFilterSlotLayout->addWidget(m_categoryFilter);

    connect(m_searchBtn, &QPushButton::clicked, this, [this]() { m_currentPage = 1; loadTools(); });
    connect(m_searchResetBtn, &QPushButton::clicked, this, [this]() {
        m_searchEdit->clear();
        m_categoryFilter->selectAll();
        m_selectedSet.clear();
        m_selectedHint->setText(QStringLiteral("（已选 0 件）"));
        m_currentPage = 1;
        loadTools();
    });

    // 待出库表格列宽策略：选择列固定60px，1-6列拉伸，状态列固定130px
    m_table->horizontalHeader()->setStretchLastSection(false);
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

    connect(m_paginationBar, &PaginationBar::prevClicked, this, &ToolCheckoutPage::onPrevPage);
    connect(m_paginationBar, &PaginationBar::nextClicked, this, &ToolCheckoutPage::onNextPage);
    connect(m_resetBtn, &QPushButton::clicked, this, &ToolCheckoutPage::onReset);
    connect(m_batchBtn, &QPushButton::clicked, this, &ToolCheckoutPage::onBatchCheckout);

    // 出库记录表格列宽：时间/名称/编号/原因 Stretch，数量/操作人 Fixed窄列
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
    connect(m_recordPaginationBar, &PaginationBar::prevClicked, this, &ToolCheckoutPage::onRecordPrevPage);
    connect(m_recordPaginationBar, &PaginationBar::nextClicked, this, &ToolCheckoutPage::onRecordNextPage);

    // Tab切换时加载对应数据
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &ToolCheckoutPage::onTabChanged);

    // 初始加载
    loadTools();
}

// Tab1: 待出库操作面板（从原setupUI提取）

// Tab2: 出库历史记录

void ToolCheckoutPage::refresh() {
    // 切换菜单回到出库页时，完全还原到初始状态：
    // 1. 切回Tab1(待出库) 2. 清空已选工具 3. 重置页码 4. 清空搜索 5. 分类全选 6. 重新加载
    if (m_tabWidget) m_tabWidget->setCurrentIndex(0);
    m_selectedSet.clear();
    m_currentPage = 1;
    m_searchEdit->clear();
    m_categoryFilter->selectAll();
    m_selectedHint->setText(QStringLiteral("（已选 0 件）"));
    loadTools();
}

/**
 * @brief 统计当前已选待出库工具数量
 * @return 选中数量
 */
int ToolCheckoutPage::selectedCount() const {
    return m_selectedSet.size();
}

/**
 * @brief 加载工具
 */
void ToolCheckoutPage::loadTools() {
    // 性能优化：禁用重绘+信号阻塞，避免重建过程中频繁刷新

    m_table->setUpdatesEnabled(false);
    m_table->blockSignals(true);
    // 客户端全量加载+排序+分页，选中工具自动跳首页（与借用页一致）
    BorrowService svc;
    QJsonObject result = svc.getAllInStockTools(1, SC::PAGE_SIZE_UNLIMITED);
    QJsonArray allTools = result["list"].toArray();
    m_totalRecords = allTools.size();

    // 客户端多选分类过滤 + 关键字搜索过滤
    QString keyword = m_searchEdit->text().trimmed();
    QStringList selCats = m_categoryFilter->selectedOptions();
    bool allSel = selCats.size() == 5 || selCats.isEmpty();
    QSet<QString> catSet(selCats.begin(), selCats.end());

    QJsonArray filteredTools;
    for (const auto& t : allTools) {
        QJsonObject toolObj = t.toObject();
        QString cat = toolObj["category"].toString();
        if (!allSel && !catSet.contains(cat)) continue;
        // 关键字搜索：匹配工具名/编号/规格
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

    // 排序：已选工具 → 未选工具
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
    m_paginationBar->setPageInfo(m_currentPage, totalPages);
    m_paginationBar->setTotalRecords(m_totalRecords);

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
        // 选中标识为mappingId（位置唯一），同一工具不同位置可独立选中
        int mappingId = t["mappingId"].toInt();

        // 复选框 使用统一表格复选框样式
        auto* check = new QCheckBox();
        check->setChecked(m_selectedSet.contains(mappingId));
        check->setStyleSheet(StyleHelper::tableCheckBox());
        m_table->setCellWidget(i, 0, check);
        connect(check, &QCheckBox::toggled, this, [this, mappingId](bool checked) {
            if (checked) m_selectedSet.insert(mappingId);
            else m_selectedSet.remove(mappingId);
            m_selectedHint->setText(QStringLiteral("（已选 %1 件）").arg(selectedCount()));
            // 性能优化：勾选不调用loadTools()全量重建
            // 原逻辑：每次勾选→查DB+重建所有行widget→卡顿
            // 新逻辑：仅更新选中集合+提示文字

        });

        m_table->setItem(i, 1, new QTableWidgetItem(t["toolCode"].toString()));  // 显示工具自身编号，非自增ID
        m_table->setItem(i, 2, new QTableWidgetItem(t["toolName"].toString()));
        m_table->setItem(i, 3, new QTableWidgetItem(t["spec"].toString()));
        m_table->setItem(i, 4, new QTableWidgetItem(t["category"].toString()));
        // 位置直接用DAO已格式化的position（权威数据源）
        // 注意：原代码从DAO格式化后的"A-1-2"再提取数字重新拼接→双重格式化→"A-01-12"
        // DAO层ToolDAO::findAll已LEFT JOIN映射表格式化好position，直接用即可
        m_table->setItem(i, 5, new QTableWidgetItem(t["position"].toString()));

        // 出库原因 使用紧凑表格下拉框样式
        auto* reasonCombo = new QComboBox();
        reasonCombo->addItems({QStringLiteral("请选择原因"), QStringLiteral("报废更换"), QStringLiteral("损坏退役"),
                              QStringLiteral("调拨其他机组"), QStringLiteral("升级替换"), QStringLiteral("超期淘汰"), QStringLiteral("其他原因")});
        reasonCombo->setStyleSheet(StyleHelper::tableComboBox());
        m_table->setCellWidget(i, 6, reasonCombo);

        // 删除"出库数量"列（一个位置=一个工具，数量恒为1）

        // 操作按钮 统一风格：禁用态灰色"待出库"，启用态主色蓝"出库"
        // 注意：重建表格时根据 m_selectedSet 设置初始状态，确保状态列与复选框同步
        // 选中标识为mappingId
        bool isSelected = m_selectedSet.contains(mappingId);
        auto* opBtn = new QPushButton(isSelected ? QStringLiteral("出库") : QStringLiteral("待出库"));
        opBtn->setStyleSheet(StyleHelper::tableActionBtn());
        opBtn->setCursor(Qt::PointingHandCursor);
        opBtn->setEnabled(isSelected);
        opBtn->setFixedHeight(StyleHelper::Token::ControlHeightCompactInput);
        connect(opBtn, &QPushButton::clicked, this, [this, i] { m_table->selectRow(i); });
        // 合并两个toggled信号为一个，减少信号回调开销
        // 原逻辑：check连两个toggled信号（一个更新m_selectedSet，一个更新opBtn）
        // 新逻辑：合并为一个lambda同时更新m_selectedSet和opBtn

        // [注] 第一个toggled连接在上方（更新m_selectedSet），这里仅保留opBtn更新
        connect(check, &QCheckBox::toggled, opBtn, [opBtn](bool checked) {
            opBtn->setEnabled(checked);
            opBtn->setText(checked ? QStringLiteral("出库") : QStringLiteral("待出库"));
        });
        m_table->setCellWidget(i, 7, opBtn);
        m_table->setRowHeight(i, 56);
    }

    // 恢复表格重绘和信号
    m_table->blockSignals(false);
    m_table->setUpdatesEnabled(true);
}

/**
 * @brief 处理出库
 */
void ToolCheckoutPage::onBatchCheckout() {
    if (selectedCount() == 0) {
        MessageDialog::showError(this, QStringLiteral("提示"), QStringLiteral("请至少选择一件工具"));
        return;
    }
    // 校验每个已选工具的出库原因和出库数量
    QStringList issues;
    for (int i = 0; i < m_tools.size(); ++i) {
        QJsonObject t = m_tools[i].toObject();
        // 选中标识为mappingId
        int mappingId = t["mappingId"].toInt();
        if (!m_selectedSet.contains(mappingId)) continue;

        QString toolName = t["toolName"].toString();
        auto* reasonCombo = qobject_cast<QComboBox*>(m_table->cellWidget(i, 6));

        // 检查出库原因（索引0为"请选择原因"，视为未选）
        if (reasonCombo && reasonCombo->currentIndex() == 0) {
            issues.append(QStringLiteral("· %1：请选择出库原因").arg(toolName));
        }
        // 删除出库数量校验（数量恒为1）
    }
    if (!issues.isEmpty()) {
        QString msg = QStringLiteral("以下工具信息不完整，请补充后再出库：\n\n") + issues.join("\n");
        MessageDialog::showWarning(this, QStringLiteral("出库信息不完整"), msg);
        return;
    }
    showCheckoutListDialog();
}

// 收集已选工具的详细信息（用于三步出库流程）
QList<CheckoutItem> ToolCheckoutPage::collectSelectedItems() const {
    QList<CheckoutItem> items;
    for (int i = 0; i < m_tools.size(); ++i) {
        QJsonObject t = m_tools[i].toObject();
        // 选中标识为mappingId
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
        item.quantity = 1;  // 数量恒为1（一个位置=一个工具）
        item.reason = reasonCombo ? reasonCombo->currentText() : QStringLiteral("其他原因");
        items.append(item);
    }
    return items;
}


/**
 * @brief 处理重置
 */
void ToolCheckoutPage::onReset() {
    m_selectedSet.clear();
    m_currentPage = 1;  // 重置到第1页
    loadTools();
}

// onCheck不无条件loadTools()全量重建
// 原逻辑：忽略参数直接loadTools()→全量重建
// 新逻辑：空实现，checkbox的toggled信号已在loadTools中处理选择逻辑
void ToolCheckoutPage::onCheck(int row, bool checked) {
    Q_UNUSED(row);
    Q_UNUSED(checked);
    // 不调用loadTools()，选择逻辑由checkbox toggled回调处理
}

/**
 * @brief 处理全部
 */
void ToolCheckoutPage::onToggleAll(bool checked) {
    // #19优化：全选/取消后仍需loadTools更新UI，但已有setUpdatesEnabled优化
    if (checked) {
        for (int i = 0; i < m_tools.size(); ++i)
            m_selectedSet.insert(m_tools[i].toObject()["toolId"].toInt());
    } else {
        m_selectedSet.clear();
    }
    m_currentPage = 1;
    loadTools();
}

// 搜索框点击弹出软键盘
void ToolCheckoutPage::onSearchFieldClicked() {
    if (!m_softKeyboard) {
        m_softKeyboard = new SoftKeyboard(this);
        m_softKeyboard->setMode(SoftKeyboard::ModeEn);
        connect(m_softKeyboard, &SoftKeyboard::confirmed, this, [this]() {
            m_softKeyboard->hide();
            loadTools();  // 软键盘确认后刷新工具列表
        });
    }
    QPoint pos = m_searchEdit->mapToGlobal(QPoint(0, m_searchEdit->height() + 4));
    m_softKeyboard->show(m_searchEdit, pos);
}

// 事件过滤：搜索框点击弹出软键盘
bool ToolCheckoutPage::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_searchEdit && event->type() == QEvent::MouseButtonPress) {
        onSearchFieldClicked();
        return false;  // 不拦截事件，让QLineEdit正常处理焦点
    }
    return QWidget::eventFilter(obj, event);
}

// 分页：上一页
void ToolCheckoutPage::onPrevPage() {
    if (m_currentPage > 1) {
        m_currentPage--;
        loadTools();
    }
}

// 分页：下一页
void ToolCheckoutPage::onNextPage() {
    int totalPages = (m_totalRecords + m_pageSize - 1) / m_pageSize;
    if (m_currentPage < totalPages) {
        m_currentPage++;
        loadTools();
    }
}

// ═══════════ 出库记录相关 ═══════════

// Tab切换时加载对应数据
void ToolCheckoutPage::onTabChanged(int index) {
    if (index == 1) {
        // 切到"出库记录"Tab时加载记录
        m_recordCurrentPage = 1;
        loadCheckoutRecords();
    }
}

// 加载出库历史记录（从 sys_operation_log 查 operation_type='checkout'）
void ToolCheckoutPage::loadCheckoutRecords() {
    if (!m_recordTable) return;

    // 从 RecordDAO 获取分页数据（含总数）
    db::RecordDAO recDao;
    QJsonObject result = recDao.findOperationLogsByType(SC::OP_CHECKOUT, m_recordCurrentPage, m_recordPageSize);
    int total = result["total"].toInt();
    QJsonArray records = result["list"].toArray();

    m_recordTotalRecords = total;

    // 分页
    int totalPages = (total + m_recordPageSize - 1) / m_recordPageSize;
    if (totalPages < 1) totalPages = 1;
    if (m_recordCurrentPage > totalPages) m_recordCurrentPage = totalPages;
    if (m_recordCurrentPage < 1) m_recordCurrentPage = 1;

    m_recordPaginationBar->setPageInfo(m_recordCurrentPage, totalPages);
    m_recordPaginationBar->setTotalRecords(total);

    m_recordTable->setRowCount(records.size());
    for (int i = 0; i < records.size(); ++i) {
        QJsonObject r = records[i].toObject();
        QString content = r["content"].toString();
        QString time = r["createdAt"].toString();

        // 列0：出库时间
        m_recordTable->setItem(i, 0, new QTableWidgetItem(time));

        // 列1-4：从content解析
        // content格式："出库工具「工具名」编号[工具编号]×数量，原因：原因"
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

// 出库记录分页
void ToolCheckoutPage::onRecordPrevPage() {
    if (m_recordCurrentPage > 1) {
        m_recordCurrentPage--;
        loadCheckoutRecords();
    }
}

/**
 * @brief 处理页面
 */
void ToolCheckoutPage::onRecordNextPage() {
    int totalPages = (m_recordTotalRecords + m_recordPageSize - 1) / m_recordPageSize;
    if (m_recordCurrentPage < totalPages) {
        m_recordCurrentPage++;
        loadCheckoutRecords();
    }
}
