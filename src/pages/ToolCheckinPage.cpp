/**
 * @file ToolCheckinPage.cpp
 * @brief 工具出入库页面实现 - 三Tab(出入库管理+入库记录+出库记录) + 单据式出入库
 * @author 袁燕
 *
 * 出入库管理Tab：出入库单以列表逐行添加指令（工具名称 + 类型入库/出库），
 *   位置由系统按对照关系自动带出；点击确认先开箱，用户按单据取放工具后
 *   关闭柜门，系统逐项执行并检查，成功项写操作日志，未完成项红字提示。
 * 入库记录Tab：sys_operation_log(operation_type='checkin') 历史。
 * 出库记录Tab：sys_operation_log(operation_type='checkout') 历史。
 */
#include "ToolCheckinPage.h"
#include "ui_ToolCheckinPage.h"
#include <QTableWidget>  // 出入库单表格与记录表格
#include "components/PaginationBar.h"
#include "components/BaseDialog.h"  // 一键导入入库行弹窗容器
#include "components/DrawerOpeningDialog.h"  // 识别等待框（复用开柜弹窗骨架）
#include <QCheckBox>  // 一键导入入库行勾选框
#include "utils/StyleHelper.h"
#include "common/AppConfig.h"
#include "common/Constants.h"
#include "db/ToolDAO.h"
#include "db/RecordDAO.h"
#include "components/MessageDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QComboBox>
#include <QPushButton>
#include <QRegularExpression>  // 解析content字段
#include <QSet>  // 出库位置查重
#include <QDateTime>  // 记录时间简化格式化

namespace {
/// 记录时间简化显示：ISO原串"2026-10-16T17:04:23" → "2026-10-16 17:04"
/// 去掉T与秒：麒麟1024x768下时间列更紧凑易读；解析失败回退原串
QString formatRecordTime(const QString& iso) {
    QDateTime dt = QDateTime::fromString(iso, Qt::ISODate);
    if (!dt.isValid()) dt = QDateTime::fromString(iso, Qt::ISODateWithMs);
    return dt.isValid() ? dt.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : iso;
}
}

/**
 * @brief 构造函数，初始化 ToolCheckinPage 对象并连接信号槽
 * @param parent 父对象指针，作为 Qt 对象树父节点接管所有权
 */
ToolCheckinPage::ToolCheckinPage(QWidget* parent) : QWidget(parent), ui(new Ui::ToolCheckinPage) {
    // 静态布局来自ToolCheckinPage.ui（Qt Designer可视化维护）
    ui->setupUi(this);
    setupUI();
}

ToolCheckinPage::~ToolCheckinPage() {
    delete ui;
}

/**
 * @brief 构建页面界面：桥接 .ui 静态布局控件并连接信号槽
 */
void ToolCheckinPage::setupUI() {
    // 桥接.ui控件（业务逻辑沿用m_成员）
    m_tabWidget = ui->tabWidget;
    m_orderTable = ui->orderTable;
    m_machineGroupLabel = ui->machineGroupLabel;
    m_errorLabel = ui->errorLabel;
    m_resetBtn = ui->resetBtn;
    m_submitBtn = ui->submitBtn;
    m_recordTable = ui->recordTable;
    m_recordPaginationBar = ui->recordPaginationBar;
    m_outRecordTable = ui->checkoutRecordTable;
    m_outRecordPaginationBar = ui->checkoutRecordPaginationBar;

    // 机组信息回填
    loadLocalMachineGroup();

    // 出入库单表格列宽：类型/规格/操作固定，工具名称/位置拉伸
    m_orderTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_orderTable->setColumnWidth(0, 110);   // 类型
    m_orderTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_orderTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_orderTable->setColumnWidth(2, 150);   // 规格（选工具后自动带出）
    m_orderTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_orderTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_orderTable->setColumnWidth(4, 90);    // 操作
    m_orderTable->verticalHeader()->setMinimumSectionSize(56);

    // 入库记录表格列宽：时间按内容自适应(麒麟字体渲染较宽,固定150会截断)，
    // 名称/编号/位置Stretch，供应商/操作人Fixed
    m_recordTable->horizontalHeader()->setStretchLastSection(false);
    for (int i = 0; i < 6; i++) {
        if (i == 4 || i == 5) {
            m_recordTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Fixed);
        } else if (i == 0) {
            m_recordTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
        } else {
            m_recordTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
        }
    }
    m_recordTable->setColumnWidth(4, 120);  // 供应商
    m_recordTable->setColumnWidth(5, 90);   // 操作人
    m_recordTable->verticalHeader()->setMinimumSectionSize(48);

    // 出库记录表格列宽：时间/名称/编号Stretch，操作人Fixed
    m_outRecordTable->horizontalHeader()->setStretchLastSection(false);
    for (int i = 0; i < 4; i++) {
        if (i == 3) {
            m_outRecordTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Fixed);
        } else if (i == 0) {
            // 时间列按内容自适应：与入库记录一致，避免麒麟上固定宽度截断
            m_outRecordTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
        } else {
            m_outRecordTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
        }
    }
    m_outRecordTable->setColumnWidth(3, 90);   // 操作人
    m_outRecordTable->verticalHeader()->setMinimumSectionSize(48);

    // 信号槽连接
    connect(ui->addInBtn, &QPushButton::clicked, this, &ToolCheckinPage::onAddOrderRow);
    connect(ui->addOutBtn, &QPushButton::clicked, this, &ToolCheckinPage::onAddOrderRowOut);
    connect(ui->batchImportInBtn, &QPushButton::clicked, this, &ToolCheckinPage::onBatchImportInRows);
    connect(m_resetBtn, &QPushButton::clicked, this, &ToolCheckinPage::clearOrderTable);
    connect(m_submitBtn, &QPushButton::clicked, this, &ToolCheckinPage::onSubmit);
    connect(m_recordPaginationBar, &PaginationBar::prevClicked, this, &ToolCheckinPage::onRecordPrevPage);
    connect(m_recordPaginationBar, &PaginationBar::nextClicked, this, &ToolCheckinPage::onRecordNextPage);
    connect(m_outRecordPaginationBar, &PaginationBar::prevClicked, this, &ToolCheckinPage::onOutRecordPrevPage);
    connect(m_outRecordPaginationBar, &PaginationBar::nextClicked, this, &ToolCheckinPage::onOutRecordNextPage);
    // Tab切换时加载对应数据
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &ToolCheckinPage::onTabChanged);
}

/**
 * @brief 读取本机组信息并回填到界面（机组隔离：出入库仅限本机组工具）
 */
void ToolCheckinPage::loadLocalMachineGroup() {
    int groupId = AppConfig::instance().localMachineGroupId();
    if (groupId <= 0) {
        m_machineGroupLabel->setText(QStringLiteral("未配置（请在系统设置中配置本机机组）"));
        m_machineGroupLabel->setStyleSheet(QString(
            "" + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + ";color:#e67e22;background:#fff3e0;"
            "border:1px solid #ffcc80;border-radius:10px;padding:8px 14px;"
        ));
        m_localMachineGroupId = 0;
        return;
    }

    db::ToolDAO dao;
    QJsonObject mg = dao.getMachineGroupById(groupId);
    if (mg.isEmpty() || mg["groupName"].toString().isEmpty()) {
        m_machineGroupLabel->setText(QStringLiteral("机组ID %1 不存在于数据库中").arg(groupId));
        m_machineGroupLabel->setStyleSheet(QString(
            "" + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + ";color:#e74c3c;background:#fdecea;"
            "border:1px solid #f5c6cb;border-radius:10px;padding:8px 14px;"
        ));
        m_localMachineGroupId = 0;
        return;
    }

    m_localMachineGroupId = groupId;
    QString groupName = mg["groupName"].toString();
    QString deptName = mg["deptName"].toString();
    QString displayText = groupName;
    if (!deptName.isEmpty()) displayText += QStringLiteral("（%1）").arg(deptName);
    m_machineGroupLabel->setText(displayText);
    m_machineGroupLabel->setStyleSheet(QString(
        "" + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + ";font-weight:600;color:%1;"
        "background:#e8f5e9;border:1px solid #a5d6a7;border-radius:10px;padding:8px 14px;"
    ).arg(StyleHelper::primaryColor()));
}

/**
 * @brief 刷新页面数据：清空单据并重置到第一个Tab
 */
void ToolCheckinPage::refresh() {
    loadLocalMachineGroup();
    if (m_tabWidget) m_tabWidget->setCurrentIndex(0);
    clearOrderTable();
}
// ═══════════ 出入库单据 ═══════════

/**
 * @brief 添加一行入库指令
 */
void ToolCheckinPage::onAddOrderRow() {
    appendOrderRow(QStringLiteral("in"));
}

/**
 * @brief 添加一行出库指令
 */
void ToolCheckinPage::onAddOrderRowOut() {
    appendOrderRow(QStringLiteral("out"));
}

/**
 * @brief 追加一行入库指令并预选指定工具（供一键导入入库行调用）
 * @param toolId 待入库工具ID；该工具不在下拉选项中时该行保持未选状态；
 *               列表中已有该工具的入库行时跳过不添加，避免重复入库同一工具
 */
void ToolCheckinPage::appendInRowForTool(int toolId) {
    // 去重：已有该工具的入库行则跳过（手动添加的行同样计入"已有"）；
    // 只比对入库行——出库行下拉data存的是mappingId（位置标识），与toolId不是同一编号空间，不能混比
    for (int r = 0; r < m_orderTable->rowCount(); ++r) {
        auto* existTypeCombo = qobject_cast<QComboBox*>(m_orderTable->cellWidget(r, 0));
        auto* existToolCombo = qobject_cast<QComboBox*>(m_orderTable->cellWidget(r, 1));
        if (!existTypeCombo || !existToolCombo) continue;
        if (existTypeCombo->currentData().toString() == QStringLiteral("in")
            && existToolCombo->currentData().toInt() == toolId) {
            return;
        }
    }
    appendOrderRow(QStringLiteral("in"));
    int row = m_orderTable->rowCount() - 1;
    auto* toolCombo = qobject_cast<QComboBox*>(m_orderTable->cellWidget(row, 1));
    if (!toolCombo) return;
    int idx = toolCombo->findData(toolId);
    // 选中后触发 currentIndexChanged → 自动带出位置与规格
    if (idx >= 0) toolCombo->setCurrentIndex(idx);
}

/**
 * @brief 一键导入入库行：列出全部待入库工具供勾选，确认后一次生成多条入库指令
 * @note 工具来源与"入库下拉"同源（findPendingTools，仅取有空闲位置的），
 *       默认全选，支持全选/全不选，逐条调用 appendInRowForTool 生成入库行
 */
void ToolCheckinPage::onBatchImportInRows() {
    m_errorLabel->setVisible(false);

    db::ToolDAO toolDao;
    QJsonArray pendingTools = toolDao.findPendingTools(0, m_localMachineGroupId);
    QList<QJsonObject> tools;
    for (int i = 0; i < pendingTools.size(); ++i) {
        QJsonObject t = pendingTools[i].toObject();
        if (t["freePosCount"].toInt() <= 0) continue;
        tools.append(t);
    }
    if (tools.isEmpty()) {
        MessageDialog::showWarning(this, QStringLiteral("无法导入"),
            QStringLiteral("暂无可入库工具（请先在对照关系中配置位置）"));
        return;
    }

    BaseDialog dlg(this, 620);
    dlg.setDialogTitle(QStringLiteral("一键导入入库行"));
    auto* cl = dlg.contentLayout();

    auto* tip = new QLabel(QStringLiteral(
        "勾选需要入库的工具（默认全选），确认后按列表顺序逐条生成入库行。"));
    tip->setWordWrap(true);
    tip->setStyleSheet(QStringLiteral(
        "font-size:14px;color:#1890ff;background:#e6f7ff;"
        "border:1px solid #91d5ff;border-radius:10px;padding:10px 14px;"));
    cl->addWidget(tip);

    auto* table = new QTableWidget();
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels(QStringList()
        << QStringLiteral("选择") << QStringLiteral("工具（编号 - 名称）")
        << QStringLiteral("规格") << QStringLiteral("可入"));
    table->setRowCount(tools.size());
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->setMinimumHeight(300);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table->setColumnWidth(0, 60);    // 选择
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    table->setColumnWidth(2, 150);   // 规格
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    table->setColumnWidth(3, 70);    // 可入数量

    QList<QCheckBox*> checkBoxes;    // 与 tools 同序
    for (int i = 0; i < tools.size(); ++i) {
        const QJsonObject& t = tools[i];

        // 列0：勾选框（居中）
        auto* cbWrap = new QWidget();
        auto* cbLayout = new QHBoxLayout(cbWrap);
        cbLayout->setContentsMargins(0, 0, 0, 0);
        cbLayout->setAlignment(Qt::AlignCenter);
        auto* cb = new QCheckBox();
        cb->setChecked(true);        // 默认全选，真正做到"一键"
        cb->setCursor(Qt::PointingHandCursor);
        cbLayout->addWidget(cb);
        table->setCellWidget(i, 0, cbWrap);
        checkBoxes.append(cb);

        // 列1：编号 - 名称
        table->setItem(i, 1, new QTableWidgetItem(QStringLiteral("%1 - %2")
            .arg(t["toolCode"].toString(), t["toolName"].toString())));

        // 列2：规格
        auto* specItem = new QTableWidgetItem(
            t["spec"].toString().isEmpty() ? QStringLiteral("--") : t["spec"].toString());
        specItem->setTextAlignment(Qt::AlignCenter);
        table->setItem(i, 2, specItem);

        // 列3：可入数量
        auto* cntItem = new QTableWidgetItem(QString::number(t["freePosCount"].toInt()));
        cntItem->setTextAlignment(Qt::AlignCenter);
        table->setItem(i, 3, cntItem);

        table->setRowHeight(i, 48);
    }
    cl->addWidget(table, 1);

    // 全选 / 全不选
    auto* selRow = new QHBoxLayout();
    auto* selAllBtn = new QPushButton(QStringLiteral("全选"));
    auto* selNoneBtn = new QPushButton(QStringLiteral("全不选"));
    for (auto* b : { selAllBtn, selNoneBtn }) {
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
        b->setStyleSheet(QStringLiteral(
            "QPushButton{background:#fff;color:#4da3ff;border:1px solid #4da3ff;"
            "border-radius:10px;font-size:14px;font-weight:600;padding:0 18px;}"
            "QPushButton:hover{background:#e8f0fe;}"));
    }
    connect(selAllBtn, &QPushButton::clicked, this, [&checkBoxes]() {
        for (auto* cb : checkBoxes) cb->setChecked(true);
    });
    connect(selNoneBtn, &QPushButton::clicked, this, [&checkBoxes]() {
        for (auto* cb : checkBoxes) cb->setChecked(false);
    });
    selRow->addWidget(selAllBtn);
    selRow->addWidget(selNoneBtn);
    selRow->addStretch();
    cl->addLayout(selRow);

    // 底部按钮
    auto* bl = dlg.buttonLayout();
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    cancelBtn->setMinimumHeight(StyleHelper::Token::ControlHeightTouch);
    cancelBtn->setMinimumWidth(100);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:#fff;color:#666;border:2px solid #ddd;border-radius:12px;"
        "font-size:15px;font-weight:600;}"
        "QPushButton:hover{background:#f5f5f5;}"));
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    auto* okBtn = new QPushButton(QStringLiteral("确认导入"));
    okBtn->setMinimumHeight(StyleHelper::Token::ControlHeightTouch);
    okBtn->setMinimumWidth(100);
    okBtn->setCursor(Qt::PointingHandCursor);
    okBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:12px;"
        "font-size:16px;font-weight:700;}"
        "QPushButton:hover{background:#3d8ae0;}"));

    bl->addWidget(cancelBtn);
    bl->addWidget(okBtn);

    connect(okBtn, &QPushButton::clicked, &dlg, [this, &dlg, &checkBoxes, &tools]() {
        QList<int> picked;
        for (int i = 0; i < checkBoxes.size() && i < tools.size(); ++i) {
            if (checkBoxes[i]->isChecked()) picked.append(tools[i]["toolId"].toInt());
        }
        if (picked.isEmpty()) {
            MessageDialog::showWarning(this, QStringLiteral("提示"), QStringLiteral("请至少勾选一个工具"));
            return;
        }
        for (int toolId : picked) appendInRowForTool(toolId);
        dlg.accept();
    });

    dlg.adjustSize();  // 按内容重算高度
    dlg.exec();
}

/**
 * @brief 向单据表格追加一行指令（默认未选工具，位置列显示待选择）
 * @param type 行类型："in"=入库 / "out"=出库
 */
void ToolCheckinPage::appendOrderRow(const QString& type) {
    m_errorLabel->setVisible(false);
    int row = m_orderTable->rowCount();
    m_orderTable->insertRow(row);

    // 列0：类型下拉（切换时重载该行工具下拉）
    auto* typeCombo = new QComboBox();
    typeCombo->addItem(QStringLiteral("入库"), "in");
    typeCombo->addItem(QStringLiteral("出库"), "out");
    typeCombo->setCurrentIndex(type == "out" ? 1 : 0);
    typeCombo->setStyleSheet(StyleHelper::tableComboBox());
    m_orderTable->setCellWidget(row, 0, typeCombo);
    connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this, typeCombo]() { reloadToolOptions(rowOfWidget(typeCombo)); });

    // 列1：工具下拉（按类型加载可选工具）
    auto* toolCombo = new QComboBox();
    toolCombo->setStyleSheet(StyleHelper::tableComboBox());
    m_orderTable->setCellWidget(row, 1, toolCombo);
    connect(toolCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this, toolCombo]() { updateRowPosition(rowOfWidget(toolCombo)); });

    // 列2：规格（选工具后自动带出，只读展示）
    auto* specItem = new QTableWidgetItem(QStringLiteral("--"));
    specItem->setTextAlignment(Qt::AlignCenter);
    specItem->setForeground(QColor("#666666"));
    m_orderTable->setItem(row, 2, specItem);

    // 列3：位置（选工具后自动带出，只读展示）
    auto* posItem = new QTableWidgetItem(QStringLiteral("--"));
    posItem->setTextAlignment(Qt::AlignCenter);
    posItem->setForeground(QColor("#43a047"));
    m_orderTable->setItem(row, 3, posItem);

    // 列4：删除按钮（点击后按控件反查所在行删除，避免行号漂移）
    auto* delBtn = new QPushButton(QStringLiteral("删除"));
    delBtn->setStyleSheet(StyleHelper::tableActionBtn());
    delBtn->setCursor(Qt::PointingHandCursor);
    m_orderTable->setCellWidget(row, 4, delBtn);
    connect(delBtn, &QPushButton::clicked, this, [this, delBtn]() {
        int r = rowOfWidget(delBtn);
        if (r >= 0) {
            m_orderTable->removeRow(r);
            m_errorLabel->setVisible(false);
        }
    });

    m_orderTable->setRowHeight(row, 56);
    reloadToolOptions(row);
}

/**
 * @brief 查找单元格控件所在的行号
 * @param w 单元格内控件指针
 * @return 行号；未找到返回 -1
 */
int ToolCheckinPage::rowOfWidget(QWidget* w) {
    if (!w) return -1;
    for (int r = 0; r < m_orderTable->rowCount(); ++r) {
        for (int col = 0; col < m_orderTable->columnCount(); ++col) {
            if (m_orderTable->cellWidget(r, col) == w) return r;
        }
    }
    return -1;
}
/**
 * @brief 按行类型重载该行工具下拉：入库=待入库工具，出库=在库工具（位置维度）
 * @param row 目标行号；行号无效时仅清空不报错
 */
void ToolCheckinPage::reloadToolOptions(int row) {
    if (row < 0 || row >= m_orderTable->rowCount()) return;
    auto* typeCombo = qobject_cast<QComboBox*>(m_orderTable->cellWidget(row, 0));
    auto* toolCombo = qobject_cast<QComboBox*>(m_orderTable->cellWidget(row, 1));
    if (!typeCombo || !toolCombo) return;

    bool isOut = (typeCombo->currentData().toString() == "out");
    toolCombo->blockSignals(true);
    toolCombo->clear();
    toolCombo->addItem(isOut ? QStringLiteral("请选择在库工具") : QStringLiteral("请选择待入库工具"), 0);

    if (isOut) {
        // 出库下拉=在库位置明细（位置维度：一条映射记录=一个位置）；按本机组隔离过滤
        db::ToolDAO toolDao;
        QJsonArray inStock = toolDao.findAllInStockPositions(m_localMachineGroupId);
        for (int i = 0; i < inStock.size(); ++i) {
            QJsonObject t = inStock[i].toObject();
            // 下拉只显示"编号 - 名称"：具体位置在选中后由位置列自动带出，名称里不再重复
            QString text = QStringLiteral("%1 - %2")
                .arg(t["toolCode"].toString(), t["toolName"].toString());
            // data存mappingId（位置唯一标识），出库按位置执行
            toolCombo->addItem(text, t["mappingId"].toInt());
            // 规格随下拉项带出（Qt::UserRole+1），选工具时无需再查库
            toolCombo->setItemData(toolCombo->count() - 1, t["spec"].toString(), Qt::UserRole + 1);
        }
        if (toolCombo->count() <= 1) {
            toolCombo->addItem(QStringLiteral("暂无在库工具"), -1);
        }
    } else {
        // 入库下拉=对照表中有空闲位置的工具（含已出库工具回流）
        db::ToolDAO toolDao;
        QJsonArray pendingTools = toolDao.findPendingTools(0, m_localMachineGroupId);
        for (int i = 0; i < pendingTools.size(); ++i) {
            QJsonObject t = pendingTools[i].toObject();
            int freePosCount = t["freePosCount"].toInt();
            if (freePosCount <= 0) continue;
            QString toolStatus = t["status"].toString();
            QString statusHint;
            if (toolStatus == SC::TOOL_PENDING) statusHint = QStringLiteral("待入库");
            else if (toolStatus == SC::TOOL_CHECKED_OUT) statusHint = QStringLiteral("已出库待放回");
            else statusHint = QStringLiteral("可补位");
            QString text = QStringLiteral("%1 - %2（%3，可入%4件）")
                .arg(t["toolCode"].toString(), t["toolName"].toString(), statusHint)
                .arg(freePosCount);
            toolCombo->addItem(text, t["toolId"].toInt());
            // 规格随下拉项带出（Qt::UserRole+1），选工具时无需再查库
            toolCombo->setItemData(toolCombo->count() - 1, t["spec"].toString(), Qt::UserRole + 1);
        }
        if (toolCombo->count() <= 1) {
            toolCombo->addItem(QStringLiteral("暂无可入库工具（请先在对照关系中配置位置）"), -1);
        }
    }
    toolCombo->blockSignals(false);

    // 类型切换后规格/位置列清空，等待重新选择工具
    auto* specItem = m_orderTable->item(row, 2);
    if (specItem) specItem->setText(QStringLiteral("--"));
    auto* posItem = m_orderTable->item(row, 3);
    if (posItem) {
        posItem->setText(QStringLiteral("--"));
        posItem->setData(Qt::UserRole, QVariant());
    }
    updateRowPosition(row);
}

/**
 * @brief 行内选择工具后，自动带出位置并显示（一个工具只在一个位置）
 * @param row 目标行号；行号无效时直接返回
 */
void ToolCheckinPage::updateRowPosition(int row) {
    if (row < 0 || row >= m_orderTable->rowCount()) return;
    auto* typeCombo = qobject_cast<QComboBox*>(m_orderTable->cellWidget(row, 0));
    auto* toolCombo = qobject_cast<QComboBox*>(m_orderTable->cellWidget(row, 1));
    auto* specItem = m_orderTable->item(row, 2);
    auto* posItem = m_orderTable->item(row, 3);
    if (!typeCombo || !toolCombo || !specItem || !posItem) return;

    int selectedId = toolCombo->currentData().toInt();
    if (selectedId <= 0) {
        specItem->setText(QStringLiteral("--"));
        posItem->setText(QStringLiteral("--"));
        posItem->setData(Qt::UserRole, QVariant());
        return;
    }
    // 规格随所选工具自动带出（下拉项已附带，无需逐行查库）
    const QString spec = toolCombo->itemData(toolCombo->currentIndex(), Qt::UserRole + 1).toString();
    specItem->setText(spec.isEmpty() ? QStringLiteral("--") : spec);

    if (typeCombo->currentData().toString() == "out") {
        // 出库：selectedId=mappingId，位置与toolId从在库位置明细实时查询带出
        db::ToolDAO toolDao;
        QJsonArray inStock = toolDao.findAllInStockPositions(m_localMachineGroupId);
        for (int i = 0; i < inStock.size(); ++i) {
            QJsonObject t = inStock[i].toObject();
            if (t["mappingId"].toInt() == selectedId) {
                posItem->setText(StyleHelper::formatPosition(
                    t["cabinetName"].toString(), t["layer"].toString(), t["position"].toString()));
                posItem->setData(Qt::UserRole, t["toolId"].toInt());
                return;
            }
        }
        posItem->setText(QStringLiteral("--"));
        posItem->setData(Qt::UserRole, QVariant());
    } else {
        // 入库：selectedId=toolId，显示该工具第一个空闲位置（确认时逐行分配）
        db::ToolDAO toolDao;
        QJsonArray positions = toolDao.findPendingPositions(selectedId);
        if (!positions.isEmpty()) {
            QJsonObject p = positions[0].toObject();
            posItem->setText(StyleHelper::formatPosition(
                p["cabinetName"].toString(), p["layer"].toString(), p["position"].toString()));
            posItem->setData(Qt::UserRole, selectedId);
        } else {
            posItem->setText(QStringLiteral("无空闲位置"));
            posItem->setData(Qt::UserRole, QVariant());
        }
    }
}
/**
 * @brief 从单据表格收集全部指令并做行级校验
 * @param errorMsg 校验失败原因（成功时被清空）
 * @return 指令列表；校验失败或单据为空时返回空列表
 */
QList<ToolCheckinPage::OrderRow> ToolCheckinPage::collectOrderItems(QString& errorMsg) {
    errorMsg.clear();
    QList<OrderRow> items;
    if (m_orderTable->rowCount() == 0) {
        errorMsg = QStringLiteral("请先添加出入库指令");
        return items;
    }

    db::ToolDAO toolDao;
    QSet<int> outMappingIds;    // 出库位置查重
    QMap<int, bool> toolInOut;  // 工具是否同时出现入库(true)与出库(false)

    for (int row = 0; row < m_orderTable->rowCount(); ++row) {
        auto* typeCombo = qobject_cast<QComboBox*>(m_orderTable->cellWidget(row, 0));
        auto* toolCombo = qobject_cast<QComboBox*>(m_orderTable->cellWidget(row, 1));
        if (!typeCombo || !toolCombo) continue;

        QString type = typeCombo->currentData().toString();
        int selectedId = toolCombo->currentData().toInt();
        if (selectedId <= 0) {
            errorMsg = QStringLiteral("第 %1 行请选择工具").arg(row + 1);
            return QList<OrderRow>();
        }

        OrderRow item;
        item.type = type;
        if (type == "out") {
            // 出库：selectedId=mappingId，toolId从位置列UserRole取
            item.mappingId = selectedId;
            auto* posItem = m_orderTable->item(row, 3);
            item.toolId = posItem ? posItem->data(Qt::UserRole).toInt() : 0;
            if (outMappingIds.contains(item.mappingId)) {
                errorMsg = QStringLiteral("第 %1 行位置重复出库").arg(row + 1);
                return QList<OrderRow>();
            }
            outMappingIds.insert(item.mappingId);
        } else {
            item.toolId = selectedId;
            item.mappingId = 0;
        }

        // 从库中补全工具基础信息（编号/名称）
        QJsonObject info = toolDao.findToolBasicInfo(item.toolId);
        if (info.isEmpty()) {
            errorMsg = QStringLiteral("第 %1 行工具信息加载失败，请重新选择").arg(row + 1);
            return QList<OrderRow>();
        }
        item.toolCode = info["toolCode"].toString();
        item.toolName = info["toolName"].toString();

        // 同一工具不允许同时出现入库与出库指令（状态冲突）
        bool isIn = (type != "out");
        if (toolInOut.contains(item.toolId) && toolInOut.value(item.toolId) != isIn) {
            errorMsg = QStringLiteral("工具「%1」不能同时出现在入库和出库指令中").arg(item.toolName);
            return QList<OrderRow>();
        }
        toolInOut.insert(item.toolId, isIn);

        items.append(item);
    }
    return items;
}

/**
 * @brief 执行前最终核对：入库行实时分配空闲位置，出库行实时核对位置仍在库
 * @param items 待核对的指令列表（入库行的位置信息会被写回）
 * @return true=全部通过；失败时通过 MessageDialog 提示原因
 */
bool ToolCheckinPage::validateOrderItems(const QList<OrderRow>& items) {
    db::ToolDAO toolDao;

    // 入库行：按工具实时查询空闲位置（同一工具多行已在onSubmit入口拦截，此处为防御性逐行分配）
    QMap<int, QJsonArray> freePositionsByTool;  // toolId -> 实时空闲位置列表
    QMap<int, int> assignedCountByTool;         // toolId -> 已分配行数
    for (const OrderRow& it : items) {
        if (it.type != "in") continue;
        if (!freePositionsByTool.contains(it.toolId)) {
            freePositionsByTool[it.toolId] = toolDao.findPendingPositions(it.toolId);
            assignedCountByTool[it.toolId] = 0;
        }
    }

    // 出库行：实时核对位置仍在库（页面打开期间可能被其他操作出库）
    QJsonArray inStockList = toolDao.findAllInStockPositions(m_localMachineGroupId);
    QSet<int> validMappingIds;
    for (int i = 0; i < inStockList.size(); ++i) {
        validMappingIds.insert(inStockList[i].toObject()["mappingId"].toInt());
    }

    for (const OrderRow& it : items) {
        if (it.type == "in") {
            QJsonArray freePositions = freePositionsByTool.value(it.toolId);
            int used = assignedCountByTool.value(it.toolId);
            if (used >= freePositions.size()) {
                MessageDialog::showWarning(this, QStringLiteral("无法执行"),
                    QStringLiteral("工具「%1」空闲位置不足（需 %2 个，仅剩 %3 个）\n请减少该工具的入库行数。")
                        .arg(it.toolName).arg(used + 1).arg(freePositions.size()));
                return false;
            }
            QJsonObject p = freePositions[used].toObject();
            // 分配结果写回指令（posInfo供执行环节直接使用）
            OrderRow& mutableItem = const_cast<OrderRow&>(it);
            mutableItem.posInfo = p;
            mutableItem.position = StyleHelper::formatPosition(
                p["cabinetName"].toString(), p["layer"].toString(), p["position"].toString());
            assignedCountByTool[it.toolId] = used + 1;
        } else if (!validMappingIds.contains(it.mappingId)) {
            // 出库位置已不在库（页面打开期间被其他操作出库）
            MessageDialog::showWarning(this, QStringLiteral("无法执行"),
                QStringLiteral("工具「%1」的位置已不在库（可能刚被出库），请删除该行后重新添加。")
                    .arg(it.toolName));
            return false;
        }
    }
    return true;
}

/**
 * @brief 清空单据并隐藏错误提示
 */
void ToolCheckinPage::clearOrderTable() {
    m_orderTable->setRowCount(0);
    m_errorLabel->setVisible(false);
}

/**
 * @brief 确认出入库：收集校验单据 → 查重 → 实时核对位置 → 进入开箱环节
 */
void ToolCheckinPage::onSubmit() {
    // 第一步：从表格收集指令。行级格式错误（未选工具、工具信息加载失败、
    // 出库位置重复、同一工具同时入+出）由 collectOrderItems 写入 errorMsg，此处只负责展示
    QString errorMsg;
    QList<OrderRow> items = collectOrderItems(errorMsg);
    if (!errorMsg.isEmpty()) {
        m_errorLabel->setText(errorMsg);
        m_errorLabel->setVisible(true);
        return;
    }
    // 第二步：空单据拦截。collectOrderItems 对空表已设置提示并被上一步拦下，
    // 此处为防御性兜底：即使收集逻辑将来调整，空单据也绝不会进入后续开箱环节
    if (items.isEmpty()) {
        m_errorLabel->setText(QStringLiteral("请先添加出入库指令"));
        m_errorLabel->setVisible(true);
        return;
    }
    // 校验通过前隐藏历史错误提示，避免旧红字与后续弹窗同时出现误导用户
    m_errorLabel->setVisible(false);

    // 入库行查重：同一工具只允许一行（一行一件）；有重复则提示用户删除多余行后再确认，
    // 不进入出入库逻辑。用 QSet 记录已出现的工具（与出库行查重同一模式）
    QSet<int> seenInToolIds;
    for (const OrderRow& it : items) {
        if (it.type != QStringLiteral("in")) continue;
        if (seenInToolIds.contains(it.toolId)) {
            MessageDialog::showWarning(this, QStringLiteral("无法执行"),
                QStringLiteral("工具「%1」已有多行入库指令\n请删除多余的行（每行一件），只保留一行后再确认。")
                    .arg(it.toolName));
            return;
        }
        seenInToolIds.insert(it.toolId);
    }

    if (!validateOrderItems(items)) return;

    // 离线禁止开箱（协议"离线禁止开锁"）：链路不可用时不进入开箱环节，
    // 避免"门开了但没人记录"——离线时柜体实际无法开锁，UI 也不应谎称已打开
    if (!m_edgeOnline) {
        MessageDialog::showWarning(this, QStringLiteral("无法开箱"),
            QStringLiteral("边缘机链路不可用，无法开箱/开锁。\n请检查边缘计算机连接后重试。"));
        return;
    }

    // 发起真实开箱指令（TCP open_req），进入"正在开柜"等待态；ack 成功才进开箱环节
    m_pendingOpenItems = items;
    emit openRequested();
}

// ═══════════ 边缘机开箱（开锁）流程 ═══════════

/**
 * @brief 开箱指令回传：发起开柜等待态（一段：只发起，不写库）
 * @param requestId 请求编号；空串=未发出（链路离线或写入失败）
 * @note  非空则弹出模态等待框（三点动画+取消），open_ack 回来/失败在槽里关闭本框
 */
void ToolCheckinPage::onOpenStarted(const QString& requestId) {
    if (requestId.isEmpty()) {
        // 未发出：拒绝本次（EdgeService 已同步发过 openFailed，此处兜底提示）
        m_pendingOpenItems.clear();
        MessageDialog::showWarning(this, QStringLiteral("无法开箱"),
            QStringLiteral("边缘机链路不可用，无法开箱/开锁。\n请检查边缘计算机连接后重试。"));
        return;
    }
    m_openRequestId = requestId;

    // 模态等待框：正在开柜（三点动画+取消），ack 回来或失败在槽里关闭本框
    auto* waitDlg = new DrawerOpeningDialog(QStringLiteral("开柜中"),
        QStringLiteral("正在打开柜体"),
        QStringLiteral("系统正在向边缘计算机发送开箱指令，请稍候…"), 32, 48, this);
    waitDlg->setAttribute(Qt::WA_DeleteOnClose);
    waitDlg->setNextButtonText(QStringLiteral("开柜中…"));
    waitDlg->setNextEnabled(false);
    waitDlg->setFixedSize(520, 380);
    m_openWaitDialog = waitDlg;
    // 用户点取消：退出开柜等待态（清状态+提示，零写库）
    connect(waitDlg, &QDialog::rejected, this, &ToolCheckinPage::cancelOpen);
    waitDlg->exec();
    // 模态结束（ack 关闭/用户取消）后对象已随 WA_DeleteOnClose 删除，指针必须清空
    m_openWaitDialog = nullptr;
}

/**
 * @brief 开箱成功（柜体已真实打开）：进入开箱环节（showOrderExecuteDialog）
 * @param requestId 请求编号（非当前在途请求的回应直接丢弃：已取消/已过期）
 * @param success   true=开锁成功（柜体已真实打开）
 * @param message   失败原因（success=false 时有效）
 */
void ToolCheckinPage::onOpenFinished(const QString& requestId, bool success, const QString& message) {
    if (requestId != m_openRequestId) return;   // 已取消/已过期，丢弃迟到回应
    if (m_openWaitDialog) {
        m_openWaitDialog->accept();
        m_openWaitDialog = nullptr;
    }
    m_openRequestId.clear();

    if (!success) {
        m_pendingOpenItems.clear();
        MessageDialog::showWarning(this, QStringLiteral("开箱失败"),
            message.isEmpty() ? QStringLiteral("边缘机拒绝开箱或开箱失败。") : message);
        return;
    }

    // 开箱成功：真实进入开箱环节（此时"柜体已打开"才为真）
    QList<OrderRow> items = m_pendingOpenItems;
    m_pendingOpenItems.clear();
    showOrderExecuteDialog(items);
}

/**
 * @brief 开箱失败（超时/离线拒发/补发放弃）：终止本次
 * @param requestId 失败请求编号；空串=离线拒发（由 onOpenStarted 分支统一提示，此处跳过）
 * @param reason    失败原因（人读）
 */
void ToolCheckinPage::onOpenFailed(const QString& requestId, const QString& reason) {
    // 离线拒发（requestId 空串）时 EdgeService 会同时发本信号与回传空串，提示统一由 onOpenStarted 做
    if (requestId.isEmpty()) return;
    if (m_openRequestId.isEmpty()) return;
    if (requestId != m_openRequestId) return;

    if (m_openWaitDialog) {
        m_openWaitDialog->accept();
        m_openWaitDialog = nullptr;
    }
    m_openRequestId.clear();
    m_pendingOpenItems.clear();
    MessageDialog::showWarning(this, QStringLiteral("开箱失败"), reason);
}

/**
 * @brief 开柜等待中用户取消：清在途状态并提示（零写库）
 */
void ToolCheckinPage::cancelOpen() {
    m_openRequestId.clear();
    m_pendingOpenItems.clear();
    MessageDialog::showWarning(this, QStringLiteral("已取消"),
        QStringLiteral("已取消开箱，未执行任何出入库。"));
}

// ═══════════ 边缘机识别两段式流程 ═══════════

/**
 * @brief 发起边缘机识别并进入等待态（一段：只发起，不写库）
 * @param items 已通过校验的单据指令（识别期间暂存，结果回来再比对写库）
 * @note  链路在线检查在 EdgeService 内部（离线拒发并通知上层），本方法只暂存与转发请求
 */
void ToolCheckinPage::startEdgeCheck(const QList<OrderRow>& items) {
    m_errorLabel->setVisible(false);
    m_pendingItems = items;
    m_lastSpots.clear();
    // 请求经 MainWindow 适配层转到 EdgeService（页面不直接持有通信层）
    emit recognitionRequested();
}

/**
 * @brief MainWindow 回传识别请求结果
 * @param requestId 请求编号；空串=未发出（链路离线或写入失败）
 * @note  非空则弹出模态等待框（三点动画+取消），识别结果/失败在槽里关闭本框
 */
void ToolCheckinPage::onRecognitionStarted(const QString& requestId) {
    if (requestId.isEmpty()) {
        // 未发出：拒绝本次，不写库（EdgeService 已同步发过 recognizeFailed，此处兜底提示）
        m_pendingItems.clear();
        MessageDialog::showWarning(this, QStringLiteral("无法核对"),
            QStringLiteral("边缘机链路不可用，本次不出入库。\n请检查边缘计算机连接后重试。"));
        return;
    }
    m_currentRequestId = requestId;

    // 模态等待框：识别通常1~3秒；取消随时可放弃本次（零写库）
    auto* waitDlg = new DrawerOpeningDialog(QStringLiteral("核对中"),
        QStringLiteral("正在核对柜内工具"),
        QStringLiteral("系统正在与边缘计算机核对柜内工具，请稍候…"), 32, 48, this);
    // 关闭即删除：模态返回后对象不残留（onRecognitionStarted 末尾统一清指针）
    waitDlg->setAttribute(Qt::WA_DeleteOnClose);
    waitDlg->setNextButtonText(QStringLiteral("核对中…"));
    waitDlg->setNextEnabled(false);
    waitDlg->setFixedSize(520, 380);
    m_waitDialog = waitDlg;
    // 用户点取消：退出等待态（清状态+提示，零写库）
    connect(waitDlg, &QDialog::rejected, this, &ToolCheckinPage::exitWaitState);
    waitDlg->exec();
    // 模态结束（结果关闭/用户取消）后对象已随 WA_DeleteOnClose 删除，指针必须清空
    m_waitDialog = nullptr;
}

/**
 * @brief 识别成功：配对在途请求 → 比对 → 写库 → 结果页 → 关闭等待框
 * @param requestId 请求编号（非当前在途请求的回应直接丢弃：已取消/已过期）
 * @param spots     全柜实况清单（页面中立结构，MainWindow 从通信层转换）
 */
void ToolCheckinPage::onRecognitionResult(const QString& requestId,
                                          const QList<ToolService::RecogSpot>& spots) {
    if (requestId != m_currentRequestId || m_pendingItems.isEmpty()) return;
    m_lastSpots = spots;

    applyRecognitionOutcome();   // 比对+写库+结果页

    if (m_waitDialog) {
        m_waitDialog->accept();  // 关闭等待框（退出其模态循环）
        m_waitDialog = nullptr;
    }
    m_currentRequestId.clear();
}

/**
 * @brief 识别失败（超时/链路中断/补发放弃）：关闭等待框并询问是否重新核对
 * @param requestId 失败请求编号；空串=离线拒发（由 onRecognitionStarted 分支统一提示，此处跳过）
 * @param reason    失败原因（人读，直接提示）
 */
void ToolCheckinPage::onRecognitionFailed(const QString& requestId, const QString& reason) {
    // 离线拒发（requestId 空串）时 EdgeService 会同时发本信号与回传空串，
    // 提示统一由 onRecognitionStarted 的空串分支做，这里直接跳过，避免弹两个框
    if (requestId.isEmpty()) return;
    if (m_pendingItems.isEmpty()) return;
    if (requestId != m_currentRequestId) return;

    if (m_waitDialog) {
        m_waitDialog->accept();  // 仅作关闭手段（不触发取消语义）
        m_waitDialog = nullptr;
    }
    m_currentRequestId.clear();

    // 柜门已关、工具已放好，只是没识别上：提供【重新核对】重发请求，不用重新开箱
    const bool retry = MessageDialog::showQuestion(this, QStringLiteral("核对失败"),
        QStringLiteral("%1\n\n是否重新核对？（不用重新开箱）").arg(reason));
    if (retry) {
        emit recognitionRequested();   // 重发：onRecognitionStarted 会再次弹出等待框
    } else {
        exitWaitState();
    }
}

/**
 * @brief 边缘机链路心跳状态变化（周期帧驱动）：缓存在线态，等待中掉线则终止本次
 * @param online true=链路恢复；false=判离线（周期帧15秒未达）
 */
void ToolCheckinPage::onEdgeOnlineChanged(bool online) {
    m_edgeOnline = online;               // 缓存当前在线态，供开箱前离线闸门使用

    // 等待核对期间链路掉线：关等待框并终止本次（随后到达的识别失败因请求已清不会重复提示）
    if (!online && m_waitDialog) {
        m_waitDialog->accept();
        m_waitDialog = nullptr;
        m_currentRequestId.clear();
        m_pendingItems.clear();
        m_lastSpots.clear();
        MessageDialog::showWarning(this, QStringLiteral("链路中断"),
            QStringLiteral("边缘机链路已断开，本次不出入库。\n请检查边缘计算机连接后重试。"));
    }

    // 等待开柜期间链路掉线：关开柜等待框并终止本次
    if (!online && m_openWaitDialog) {
        m_openWaitDialog->accept();
        m_openWaitDialog = nullptr;
        m_openRequestId.clear();
        m_pendingOpenItems.clear();
        MessageDialog::showWarning(this, QStringLiteral("链路中断"),
            QStringLiteral("边缘机链路已断开，开箱已终止。\n请检查边缘计算机连接后重试。"));
    }
}

/**
 * @brief 退出等待态：清在途状态并提示（用户取消/失败后放弃都走这里，零写库）
 */
void ToolCheckinPage::exitWaitState() {
    m_currentRequestId.clear();
    m_pendingItems.clear();
    m_lastSpots.clear();
    MessageDialog::showWarning(this, QStringLiteral("已取消"),
        QStringLiteral("本次核对已终止，未写入任何记录。"));
}

/**
 * @brief 识别结果处理（二段）：比对单据与实况 → 通过项写库 → 结果页展示
 * @note  整单语义：比对未过的项不写库（红字+原因），通过项照常写库并写操作日志；
 *        轮次闭环（重新开柜整理/3轮兜底）在后续步骤实现
 */
void ToolCheckinPage::applyRecognitionOutcome() {
    if (m_pendingItems.isEmpty()) return;

    // 页面单据行 → services 比对入参（中立结构，不碰通信层类型）
    QList<ToolService::RecogOrderItem> orderItems;
    for (const OrderRow& it : m_pendingItems) {
        ToolService::RecogOrderItem o;
        o.type = it.type;
        o.toolId = it.toolId;
        o.mappingId = it.mappingId;
        o.toolCode = it.toolCode;
        o.toolName = it.toolName;
        o.position = it.position;
        orderItems.append(o);
    }

    auto outcome = ToolService::compareOrderWithRecognition(orderItems, m_lastSpots);
    showCheckResultDialog(m_pendingItems, outcome.items, outcome.misplaced);
    m_pendingItems.clear();
    m_lastSpots.clear();
}
// ═══════════ 记录相关 ═══════════

/**
 * @brief Tab切换时加载对应记录：1=入库记录，2=出库记录
 * @param index 目标Tab索引
 */
void ToolCheckinPage::onTabChanged(int index) {
    if (index == 1) {
        m_recordCurrentPage = 1;
        loadCheckinRecords();
    } else if (index == 2) {
        m_outRecordCurrentPage = 1;
        loadCheckoutRecords();
    }
}

/**
 * @brief 加载入库历史记录（RecordDAO查询 sys_operation_log + 位置JOIN）
 */
void ToolCheckinPage::loadCheckinRecords() {
    if (!m_recordTable) return;

    db::RecordDAO recDao;
    QJsonObject result = recDao.findCheckinLogs(m_recordCurrentPage, m_recordPageSize);
    int total = result["total"].toInt();
    QJsonArray list = result["list"].toArray();

    m_recordTotalRecords = total;

    // 分页
    int totalPages = (total + m_recordPageSize - 1) / m_recordPageSize;
    if (totalPages < 1) totalPages = 1;
    if (m_recordCurrentPage > totalPages) m_recordCurrentPage = totalPages;
    if (m_recordCurrentPage < 1) m_recordCurrentPage = 1;

    m_recordPaginationBar->setPageInfo(m_recordCurrentPage, totalPages);
    m_recordPaginationBar->setTotalRecords(total);

    // 从content解析位置信息（新旧格式兼容）
    QJsonArray records;
    for (int i = 0; i < list.size(); ++i) {
        QJsonObject r = list[i].toObject();
        QString contentStr = r["content"].toString();
        QRegularExpression rePos("位置([A-Z]-\\d+-\\d+)");
        QRegularExpressionMatch mPos = rePos.match(contentStr);
        if (mPos.hasMatch()) {
            r["posFromContent"] = mPos.captured(1);
        }
        records.append(r);
    }

    m_recordTable->setRowCount(records.size());
    for (int i = 0; i < records.size(); ++i) {
        QJsonObject r = records[i].toObject();
        QString content = r["content"].toString();
        QString time = formatRecordTime(r["createdAt"].toString());

        // 列顺序：入库时间(0)/工具名称(1)/工具编号(2)/位置(3)/供应商(4)/操作人(5)
        m_recordTable->setItem(i, 0, new QTableWidgetItem(time));

        // 从content解析工具名/供应商
        QString toolName, supplier;
        QRegularExpression re1("入库工具「(.+?)」");
        QRegularExpression re3("供应商：(.+)");
        QRegularExpressionMatch m1 = re1.match(content);
        QRegularExpressionMatch m3 = re3.match(content);
        if (m1.hasMatch()) toolName = m1.captured(1);
        if (m3.hasMatch()) supplier = m3.captured(1);

        m_recordTable->setItem(i, 1, new QTableWidgetItem(toolName.isEmpty() ? QStringLiteral("--") : toolName));
        QString toolCode = r["targetId"].toString();
        if (toolCode.isEmpty()) {
            QRegularExpression reCode("编号\\[(.+?)\\]");
            QRegularExpressionMatch mc = reCode.match(content);
            if (mc.hasMatch()) toolCode = mc.captured(1);
        }
        m_recordTable->setItem(i, 2, new QTableWidgetItem(toolCode.isEmpty() ? QStringLiteral("--") : toolCode));
        // 列3：位置（三级优先：content解析→映射表→tool_info）
        {
            QString posDisplay;
            QRegularExpression rePosContent("位置(.+)");
            QRegularExpressionMatch mPosContent = rePosContent.match(content);
            if (mPosContent.hasMatch()) {
                posDisplay = mPosContent.captured(1);
            } else {
                int mpmCabId = r["mpmCabId"].toInt();
                if (mpmCabId > 0) {
                    posDisplay = StyleHelper::formatPosition(r["mpmCabName"].toString(), r["mpmLayer"].toString(), r["mpmPos"].toString());
                } else if (r["tiCabId"].toInt() > 0) {
                    posDisplay = StyleHelper::formatPosition(r["tiCabName"].toString(), r["tiLayer"].toString(), r["tiPos"].toString());
                } else {
                    posDisplay = QStringLiteral("--");
                }
            }
            auto* posItem = new QTableWidgetItem(posDisplay);
            posItem->setTextAlignment(Qt::AlignCenter);
            m_recordTable->setItem(i, 3, posItem);
        }
        m_recordTable->setItem(i, 4, new QTableWidgetItem(supplier.isEmpty() ? QStringLiteral("--") : supplier));
        QString userName = r["realName"].toString();
        if (userName.isEmpty()) userName = QStringLiteral("--");
        m_recordTable->setItem(i, 5, new QTableWidgetItem(userName));

        m_recordTable->setRowHeight(i, 48);
    }
}

/**
 * @brief 入库记录分页：上一页
 */
void ToolCheckinPage::onRecordPrevPage() {
    if (m_recordCurrentPage > 1) {
        m_recordCurrentPage--;
        loadCheckinRecords();
    }
}

/**
 * @brief 入库记录分页：下一页
 */
void ToolCheckinPage::onRecordNextPage() {
    int totalPages = (m_recordTotalRecords + m_recordPageSize - 1) / m_recordPageSize;
    if (m_recordCurrentPage < totalPages) {
        m_recordCurrentPage++;
        loadCheckinRecords();
    }
}
/**
 * @brief 加载出库历史记录（RecordDAO查询 operation_type='checkout'）
 */
void ToolCheckinPage::loadCheckoutRecords() {
    if (!m_outRecordTable) return;

    db::RecordDAO recDao;
    QJsonObject result = recDao.findOperationLogsByType(SC::OP_CHECKOUT, m_outRecordCurrentPage, m_outRecordPageSize);
    int total = result["total"].toInt();
    QJsonArray records = result["list"].toArray();

    m_outRecordTotalRecords = total;

    int totalPages = (total + m_outRecordPageSize - 1) / m_outRecordPageSize;
    if (totalPages < 1) totalPages = 1;
    if (m_outRecordCurrentPage > totalPages) m_outRecordCurrentPage = totalPages;
    if (m_outRecordCurrentPage < 1) m_outRecordCurrentPage = 1;

    m_outRecordPaginationBar->setPageInfo(m_outRecordCurrentPage, totalPages);
    m_outRecordPaginationBar->setTotalRecords(total);

    m_outRecordTable->setRowCount(records.size());
    for (int i = 0; i < records.size(); ++i) {
        QJsonObject r = records[i].toObject();
        QString content = r["content"].toString();
        QString time = formatRecordTime(r["createdAt"].toString());

        // 列顺序：出库时间(0)/工具名称(1)/工具编号(2)/操作人(3)
        m_outRecordTable->setItem(i, 0, new QTableWidgetItem(time));

        // content格式："出库工具「工具名」编号[工具编号]×数量 位置X，原因：原因"
        QString toolName;
        QRegularExpression re1("出库工具「(.+?)」");
        QRegularExpressionMatch m1 = re1.match(content);
        if (m1.hasMatch()) toolName = m1.captured(1);

        m_outRecordTable->setItem(i, 1, new QTableWidgetItem(toolName.isEmpty() ? QStringLiteral("--") : toolName));
        QString toolCode = r["toolCode"].toString();
        if (toolCode.isEmpty()) {
            QRegularExpression reCode("编号\\[(.+?)\\]");
            QRegularExpressionMatch mc = reCode.match(content);
            if (mc.hasMatch()) toolCode = mc.captured(1);
        }
        m_outRecordTable->setItem(i, 2, new QTableWidgetItem(toolCode.isEmpty() ? QStringLiteral("--") : toolCode));
        QString userName = r["realName"].toString();
        if (userName.isEmpty()) userName = QStringLiteral("--");
        m_outRecordTable->setItem(i, 3, new QTableWidgetItem(userName));

        m_outRecordTable->setRowHeight(i, 48);
    }
}

/**
 * @brief 出库记录分页：上一页
 */
void ToolCheckinPage::onOutRecordPrevPage() {
    if (m_outRecordCurrentPage > 1) {
        m_outRecordCurrentPage--;
        loadCheckoutRecords();
    }
}

/**
 * @brief 出库记录分页：下一页
 */
void ToolCheckinPage::onOutRecordNextPage() {
    int totalPages = (m_outRecordTotalRecords + m_outRecordPageSize - 1) / m_outRecordPageSize;
    if (m_outRecordCurrentPage < totalPages) {
        m_outRecordCurrentPage++;
        loadCheckoutRecords();
    }
}
