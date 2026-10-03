/**
 * @file ToolCheckinPage.cpp
 * @brief 工具入库页面实现 - 双Tab(入库管理+入库记录) + 四步对话框流程
 * @author 袁燕
 *
 * 去掉步骤条，改为对话框引导四步流程
 *   供应商/柜体/层号/位置/工具类型改为下拉选择
 *   四步：清单确认→柜体打开动画→假异常→入库成功
 * 识别方式选择 + 工具文档上传
 * 模仿出库页面风格，增加双Tab：入库管理 + 入库记录
 *   入库成功写sys_operation_log(operation_type='checkin')，记录Tab展示历史
 */
#include "ToolCheckinPage.h"
#include "ui_ToolCheckinPage.h"
#include <QTableWidget>  // 入库记录表格
#include "components/PaginationBar.h"
#include "utils/StyleHelper.h"
#include "components/DrawerOpeningDialog.h"
#include "components/VerifyAlertDialog.h"
#include "components/ResultDialog.h"
#include "services/ToolService.h"
#include "common/AppConfig.h"
#include "services/AlertService.h"  // 告警闭环写入
#include "model/AlertLog.h"  // AlertLog实体
#include "common/Constants.h"
#include "db/ToolDAO.h"
#include "db/RecordDAO.h"
#include "controller/ToolController.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include "components/MessageDialog.h"
#include <QGroupBox>
#include <QTimer>
#include <QDialog>
#include <QFrame>
#include <QFileDialog>
#include <QStandardPaths>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QRegularExpression>  // 解析content字段
#include <QSet>  // 已占用位置集合

ToolCheckinPage::ToolCheckinPage(QWidget* parent) : QWidget(parent), ui(new Ui::ToolCheckinPage) {
    // 静态布局来自ToolCheckinPage.ui（Qt Designer可视化维护）
    ui->setupUi(this);
    setupUI();
}

ToolCheckinPage::~ToolCheckinPage() {
    delete ui;
}

void ToolCheckinPage::setupUI() {
    // 桥接.ui控件（业务逻辑沿用m_成员，零改动）
    m_tabWidget = ui->tabWidget;
    m_categoryCombo = ui->categoryCombo;
    m_toolSelectCombo = ui->toolSelectCombo;
    m_qtyCombo = ui->qtyCombo;
    m_qtyHintLabel = ui->qtyHintLabel;
    m_machineGroupLabel = ui->machineGroupLabel;
    m_errorLabel = ui->errorLabel;
    m_resetBtn = ui->resetBtn;
    m_submitBtn = ui->submitBtn;
    m_recordTable = ui->recordTable;
    m_recordPaginationBar = ui->recordPaginationBar;

    // 入库数量初始项（选择工具后由loadToolsByCategory重建）
    m_qtyCombo->addItem(QStringLiteral("请先选择工具"), 0);

    // 保留隐藏字段（入库确认时使用，但不显示给用户，不参与布局）
    m_toolNameEdit = new QLineEdit(); m_toolNameEdit->setVisible(false);
    m_toolCodeEdit = new QLineEdit(); m_toolCodeEdit->setVisible(false);
    m_specEdit = new QLineEdit(); m_specEdit->setVisible(false);
    m_cabinetCombo = new QComboBox(); m_cabinetCombo->setVisible(false);
    m_layerCombo = new QComboBox(); m_layerCombo->setVisible(false);
    m_positionCombo = new QComboBox(); m_positionCombo->setVisible(false);

    // 数据加载（顺序与原实现一致）
    loadCategoryOptions();
    loadLocalMachineGroup();
    // 页面初始化时主动加载工具下拉
    // 不依赖工具类型选择，直接显示对照表中有位置记录的pending工具
    loadToolsByCategory(0);

    // 入库记录表格列宽：时间/名称/编号/位置Stretch，供应商/操作人Fixed
    m_recordTable->horizontalHeader()->setStretchLastSection(false);
    for (int i = 0; i < 6; i++) {
        if (i == 4 || i == 5) {
            m_recordTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Fixed);
        } else {
            m_recordTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
        }
    }
    m_recordTable->setColumnWidth(4, 120);  // 供应商
    m_recordTable->setColumnWidth(5, 90);   // 操作人
    m_recordTable->horizontalHeader()->setStretchLastSection(false);
    m_recordTable->verticalHeader()->setMinimumSectionSize(48);

    // 信号槽连接
    connect(m_resetBtn, &QPushButton::clicked, this, &ToolCheckinPage::onReset);
    connect(m_submitBtn, &QPushButton::clicked, this, &ToolCheckinPage::onSubmit);
    // 工具类型切换 → 加载对应工具列表
    connect(m_categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        onCategoryChanged();
    });
    // 工具选择 → 自动填充工具信息
    connect(m_toolSelectCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        onToolSelected();
    });
    // 入库数量变化 → 更新提示
    connect(m_qtyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        m_selectedCheckinQty = m_qtyCombo->currentData().toInt();
    });
    connect(m_recordPaginationBar, &PaginationBar::prevClicked, this, &ToolCheckinPage::onRecordPrevPage);
    connect(m_recordPaginationBar, &PaginationBar::nextClicked, this, &ToolCheckinPage::onRecordNextPage);
    // Tab切换时加载对应数据
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &ToolCheckinPage::onTabChanged);
}

// 加载工具类型下拉选项（与工具管理页一致，从DB读取）
void ToolCheckinPage::loadCategoryOptions() {
    if (!m_categoryCombo) return;
    m_categoryCombo->clear();
    m_categoryCombo->addItem(QStringLiteral("请选择工具类型"), 0);
    // 注意：data必须存categoryId(数字)，存name会导致onCategoryChanged转int为0
    ToolController ctrl;
    QList<ToolCategory> cats = ctrl.getCategories();
    for (const ToolCategory& cat : cats) {
        m_categoryCombo->addItem(cat.categoryName, cat.categoryId);
    }
}

// 加载入库柜体下拉选项（从DB读取 tool_cabinet 表）
void ToolCheckinPage::loadCabinetOptions() {
    if (!m_cabinetCombo) return;
    m_cabinetCombo->clear();
    m_cabinetCombo->addItem(QStringLiteral("请选择柜体"), 0);
    ToolController ctrl;
    QStringList cabs = ctrl.cabinetNames();
    for (const QString& name : cabs) {
        m_cabinetCombo->addItem(name, name);
    }
    // 如果DB没有柜体数据，提供默认选项
    if (cabs.isEmpty()) {
        m_cabinetCombo->addItem(QStringLiteral("A柜"), QStringLiteral("A柜"));
        m_cabinetCombo->addItem(QStringLiteral("B柜"), QStringLiteral("B柜"));
        m_cabinetCombo->addItem(QStringLiteral("C柜"), QStringLiteral("C柜"));
    }
}

void ToolCheckinPage::refresh() {
    // 刷新时重新加载下拉选项（可能新增了类别/柜体）
    loadCategoryOptions();
    loadCabinetOptions();
    loadLocalMachineGroup();
    // 主动加载工具下拉（不依赖类型选择）
    loadToolsByCategory(0);
    // 切换菜单进入时重置表单数据，避免残留上次输入
    onReset();
}

void ToolCheckinPage::loadLocalMachineGroup() {
    int groupId = AppConfig::instance().localMachineGroupId();
    if (groupId <= 0) {
        m_machineGroupLabel->setText(QStringLiteral("未配置（请在系统设置中配置本机机组）"));
        m_machineGroupLabel->setStyleSheet(QString(
            "font-size:16px;color:#e67e22;background:#fff3e0;"
            "border:1px solid #ffcc80;border-radius:10px;padding:10px 16px;min-height:48px;"
        ));
        m_localMachineGroupId = 0;
        return;
    }

    db::ToolDAO dao;
    QJsonObject mg = dao.getMachineGroupById(groupId);
    if (mg.isEmpty() || mg["groupName"].toString().isEmpty()) {
        m_machineGroupLabel->setText(QStringLiteral("机组ID %1 不存在于数据库中").arg(groupId));
        m_machineGroupLabel->setStyleSheet(QString(
            "font-size:16px;color:#e74c3c;background:#fdecea;"
            "border:1px solid #f5c6cb;border-radius:10px;padding:10px 16px;min-height:48px;"
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
        "font-size:16px;font-weight:600;color:%1;"
        "background:#e8f5e9;border:1px solid #a5d6a7;border-radius:10px;padding:10px 16px;min-height:48px;"
    ).arg(StyleHelper::primaryColor()));
}

void ToolCheckinPage::onReset() {
    m_toolNameEdit->clear();
    m_toolCodeEdit->clear();
    m_specEdit->clear();
    m_selectedToolId = 0;
    // 工具选择重置到第一项
    if (m_toolSelectCombo && m_toolSelectCombo->count() > 0) {
        m_toolSelectCombo->setCurrentIndex(0);
    }
    // 数量下拉重置
    m_qtyCombo->clear();
    m_qtyCombo->addItem(QStringLiteral("请先选择工具"), 0);
    m_selectedCheckinQty = 0;
    m_availablePositions = QJsonArray();
    m_qtyHintLabel->setText(QStringLiteral("请先选择工具"));
    m_cabinetCombo->setCurrentIndex(0);
    m_layerCombo->setCurrentIndex(0);
    m_positionCombo->setCurrentIndex(0);
    m_categoryCombo->setCurrentIndex(0);
    m_errorLabel->setVisible(false);
}

bool ToolCheckinPage::validateForm(QString& errorMsg) {
    // 入库校验：必须选择工具、选择数量且有空闲位置
    if (m_selectedToolId <= 0) {
        errorMsg = QStringLiteral("请选择待入库工具");
        return false;
    }
    if (m_toolCodeEdit->text().trimmed().isEmpty()) {
        errorMsg = QStringLiteral("工具编号加载失败，请重新选择工具");
        return false;
    }
    if (m_toolNameEdit->text().trimmed().isEmpty()) {
        errorMsg = QStringLiteral("工具名称加载失败，请重新选择工具");
        return false;
    }
    if (m_availablePositions.isEmpty()) {
        errorMsg = QStringLiteral("该工具没有空闲位置可供入库，请先在对照关系中配置位置");
        return false;
    }
    if (m_selectedCheckinQty <= 0) {
        errorMsg = QStringLiteral("请选择入库数量");
        return false;
    }
    if (m_selectedCheckinQty > m_availablePositions.size()) {
        errorMsg = QStringLiteral("入库数量不能超过空闲位置数(%1)").arg(m_availablePositions.size());
        return false;
    }
    return true;
}

void ToolCheckinPage::onSubmit() {
    QString errorMsg;
    if (!validateForm(errorMsg)) {
        m_errorLabel->setText(errorMsg);
        m_errorLabel->setVisible(true);
        return;
    }
    m_errorLabel->setVisible(false);

    // 入库支持多位置：取前N个空闲位置（N=用户选择的入库数量）
    if (m_availablePositions.isEmpty()) {
        MessageDialog::showWarning(this, QStringLiteral("无法入库"),
            QStringLiteral("该工具在对照关系表中没有空闲位置可供入库。\n请先在「系统维护→工具对照关系」中配置位置。"));
        return;
    }

    // 设置第一个位置到隐藏字段（供四步流程中柜体打开等环节使用）
    QJsonObject firstPos = m_availablePositions[0].toObject();
    m_cabinetCombo->clear();
    m_cabinetCombo->addItem(firstPos["cabinetName"].toString(), firstPos["cabinetName"].toString());
    m_layerCombo->clear();
    m_layerCombo->addItem(firstPos["layer"].toString(), firstPos["layer"].toString());
    m_positionCombo->clear();
    m_positionCombo->addItem(firstPos["position"].toString(), firstPos["position"].toString());

    // 进入四步入库流程：步骤1 入库清单确认
    showCheckinListDialog();
}

// ═══════════ 步骤1: 入库清单确认对话框 ═══════════
void ToolCheckinPage::showCheckinListDialog() {
    QDialog* dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("入库清单"));
    dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dlg->setStyleSheet(StyleHelper::dialogStyle());
    // 对话框尺寸动态调整（取决于位置数）
    // 增大高度计算：标题80+提示60+基本信息表254+位置表+按钮区80+间距
    int checkinQty = m_selectedCheckinQty;
    int posTableHeight = checkinQty * 36 + 38;
    int dlgHeight = 80 + 60 + 254 + posTableHeight + 80 + 40;
    dlg->setFixedSize(620, dlgHeight > 760 ? 760 : dlgHeight);

    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(32, 28, 32, 24);
    layout->setSpacing(12);

    auto* titleRow = new QHBoxLayout();
    auto* iconLabel = new QLabel(QStringLiteral("📦"));
    iconLabel->setStyleSheet("font-size:28px;background:transparent;");
    auto* titleLabel = new QLabel(QStringLiteral("入库清单确认"));
    titleLabel->setStyleSheet("font-size:22px;font-weight:bold;color:#1a1a2e;background:transparent;");
    titleRow->addWidget(iconLabel);
    titleRow->addWidget(titleLabel);
    titleRow->addStretch();
    layout->addLayout(titleRow);

    auto* tipFrame = new QFrame();
    tipFrame->setObjectName("tipFrame");
    tipFrame->setStyleSheet(
        "QFrame#tipFrame{background:#e6f7ff;border:1px solid #91d5ff;"
        "border-radius:10px;padding:10px 14px;}"
    );
    auto* tipLayout = new QHBoxLayout(tipFrame);
    tipLayout->setContentsMargins(12, 6, 12, 6);
    auto* tipIcon = new QLabel(QStringLiteral("📋"));
    tipIcon->setStyleSheet("font-size:20px;background:transparent;");
    auto* tipText = new QLabel(QStringLiteral("请确认以下入库信息后点击下一步"));
    tipText->setStyleSheet("font-size:15px;color:#1890ff;font-weight:600;background:transparent;");
    tipLayout->addWidget(tipIcon);
    tipLayout->addWidget(tipText, 1);
    layout->addWidget(tipFrame);

    // 入库清单为：基本信息+多位置表格
    // 基本信息：工具名称/编号/类型/规格/机组
    // 位置表格：入库数量/入库位置（每个位置一行）
    QString categoryText = m_categoryCombo->currentText();
    QString groupName = m_machineGroupLabel->text();

    // 基本信息表格（2列：项目-内容）
    auto* baseTable = new QTableWidget();
    baseTable->setColumnCount(2);
    baseTable->setHorizontalHeaderLabels({QStringLiteral("项目"), QStringLiteral("内容")});
    baseTable->verticalHeader()->setVisible(false);
    baseTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    baseTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    baseTable->setStyleSheet(QString(
        "QTableWidget { border:1px solid #f0f0f0; background:#fff; border-radius:10px; "
        "  font-family:\"Microsoft YaHei\",sans-serif; font-size:14px; outline:none; }"
        "QTableWidget::item { padding:8px 14px; color:#333; border:none; "
        "  border-bottom:1px solid #f3f3f3; outline:none; }"
        "QHeaderView::section { background:#f8f9fb; color:#666; font-weight:600; "
        "  font-size:13px; padding:10px 14px; border:none; border-bottom:1px solid #f0f0f0; }"
    ));
    baseTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    baseTable->setColumnWidth(0, 140);
    baseTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    QStringList baseItems;
    baseItems << QStringLiteral("工具名称") << m_toolNameEdit->text();
    baseItems << QStringLiteral("工具编号") << m_toolCodeEdit->text();
    baseItems << QStringLiteral("工具类型") << categoryText;
    baseItems << QStringLiteral("规格型号") << (m_specEdit->text().isEmpty() ? QStringLiteral("--") : m_specEdit->text());
    baseItems << QStringLiteral("入库数量") << QStringLiteral("%1 件").arg(checkinQty);
    baseItems << QStringLiteral("所属机组") << groupName;

    baseTable->setRowCount(baseItems.size() / 2);
    for (int i = 0; i < baseItems.size(); i += 2) {
        auto* keyItem = new QTableWidgetItem(baseItems[i]);
        keyItem->setForeground(QColor("#999"));
        baseTable->setItem(i / 2, 0, keyItem);
        auto* valItem = new QTableWidgetItem(baseItems[i + 1]);
        valItem->setForeground(QColor("#333"));
        QFont vf = valItem->font(); vf.setBold(true); valItem->setFont(vf);
        baseTable->setItem(i / 2, 1, valItem);
        baseTable->setRowHeight(i / 2, 36);
    }
    baseTable->setFixedHeight(baseItems.size() / 2 * 36 + 38);
    layout->addWidget(baseTable);

    // 入库位置表格（每个位置一行，多位置多行）
    auto* posTable = new QTableWidget();
    posTable->setColumnCount(3);
    posTable->setHorizontalHeaderLabels({QStringLiteral("序号"), QStringLiteral("入库位置"), QStringLiteral("柜体")});
    posTable->verticalHeader()->setVisible(false);
    posTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    posTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    posTable->setStyleSheet(baseTable->styleSheet());
    posTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    posTable->setColumnWidth(0, 60);
    posTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    posTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

    posTable->setRowCount(checkinQty);
    for (int i = 0; i < checkinQty; ++i) {
        QJsonObject posObj = m_availablePositions[i].toObject();
        auto* noItem = new QTableWidgetItem(QStringLiteral("第%1件").arg(i + 1));
        noItem->setTextAlignment(Qt::AlignCenter);
        noItem->setForeground(QColor("#333"));
        posTable->setItem(i, 0, noItem);
        auto* posItem = new QTableWidgetItem(posObj["posDisplay"].toString());
        posItem->setForeground(QColor("#43a047"));
        QFont pf = posItem->font(); pf.setBold(true); posItem->setFont(pf);
        posItem->setTextAlignment(Qt::AlignCenter);
        posTable->setItem(i, 1, posItem);
        auto* cabItem = new QTableWidgetItem(posObj["cabinetName"].toString());
        cabItem->setForeground(QColor("#555"));
        posTable->setItem(i, 2, cabItem);
        posTable->setRowHeight(i, 36);
    }
    posTable->setFixedHeight(checkinQty * 36 + 38);
    layout->addWidget(posTable);

    layout->addStretch(1);

    // 按钮区增加间距，避免和表格挤在一起
    auto* spacerBeforeBtns = new QFrame();
    spacerBeforeBtns->setFixedHeight(8);
    spacerBeforeBtns->setStyleSheet("background:transparent;");
    layout->addWidget(spacerBeforeBtns);

    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(16);
    btnRow->addStretch(1);
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    cancelBtn->setStyleSheet(StyleHelper::buttonDefault());
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setMinimumHeight(StyleHelper::Token::ControlHeightTouch);
    cancelBtn->setMinimumWidth(120);
    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);

    auto* nextBtn = new QPushButton(QStringLiteral("下一步 →"));
    nextBtn->setStyleSheet(StyleHelper::buttonPrimary());
    nextBtn->setCursor(Qt::PointingHandCursor);
    nextBtn->setMinimumHeight(StyleHelper::Token::ControlHeightTouch);
    nextBtn->setMinimumWidth(160);
    connect(nextBtn, &QPushButton::clicked, this, [this, dlg]() {
        dlg->accept();
        showCheckinDrawerOpeningDialog();
    });
    btnRow->addWidget(cancelBtn);
    btnRow->addSpacing(12);
    btnRow->addWidget(nextBtn);
    layout->addLayout(btnRow);

    dlg->exec();
    delete dlg;
}

// ═══════════ 步骤2: 柜体打开中（三点跳动动画） ═══════════
void ToolCheckinPage::showCheckinDrawerOpeningDialog() {
    // 多位置提示表格
    int checkinQty = m_selectedCheckinQty;

    // 提示文案：单位置用简单文字，多位置带清单
    QString descHtml;
    if (checkinQty == 1) {
        QJsonObject posObj = m_availablePositions[0].toObject();
        QString cabinetName = posObj["cabinetName"].toString();
        QString posDisplay = posObj["posDisplay"].toString();
        descHtml = QStringLiteral(
            "工具柜「%1」已自动打开，<br/>"
            "请将<b>%2</b>放入位置 <b style='color:#43a047;'>%3</b> 后点击下一步。"
        ).arg(cabinetName).arg(m_toolNameEdit->text()).arg(posDisplay);
    } else {
        descHtml = QStringLiteral(
            "请将<b>%1</b>（共<b style='color:#43a047;'>%2</b>件）依次放入以下位置后点击下一步："
        ).arg(m_toolNameEdit->text()).arg(checkinQty);
    }

    DrawerOpeningDialog dlg(QStringLiteral("柜体打开中"), QStringLiteral("柜体已打开"),
        descHtml, 32, 60, this);
    dlg.setFixedSize(560, 420);

    // 多位置用表格展示位置清单
    if (checkinQty > 1) {
        auto* posTable = new QTableWidget();
        posTable->setColumnCount(3);
        posTable->setHorizontalHeaderLabels({QStringLiteral("序号"), QStringLiteral("位置"), QStringLiteral("柜体")});
        posTable->verticalHeader()->setVisible(false);
        posTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        posTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        posTable->setStyleSheet(QString(
            "QTableWidget { border:1px solid #f0f0f0; background:#fff; border-radius:10px; "
            "  font-size:14px; outline:none; }"
            "QTableWidget::item { padding:6px 10px; border:none; outline:none; }"
            "QHeaderView::section { background:#f8f9fb; color:#666; font-weight:600; font-size:13px; padding:8px; border:none; }"
        ));
        posTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
        posTable->setColumnWidth(0, 60);
        posTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        posTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        posTable->setRowCount(checkinQty);
        for (int i = 0; i < checkinQty; ++i) {
            QJsonObject posObj = m_availablePositions[i].toObject();
            posTable->setItem(i, 0, new QTableWidgetItem(QStringLiteral("%1").arg(i + 1)));
            auto* posItem = new QTableWidgetItem(posObj["posDisplay"].toString());
            posItem->setForeground(QColor("#43a047"));
            QFont pf = posItem->font(); pf.setBold(true); posItem->setFont(pf);
            posTable->setItem(i, 1, posItem);
            posTable->setItem(i, 2, new QTableWidgetItem(posObj["cabinetName"].toString()));
            posTable->setRowHeight(i, 36);
        }
        posTable->setFixedHeight(checkinQty * 36 + 38);
        dlg.bodyLayout()->addWidget(posTable);
    }

    if (dlg.exec() == QDialog::Accepted) {
        showCheckinWarningDialog();
    }
}

// ═══════════ 步骤3: 假异常告警对话框 ═══════════
void ToolCheckinPage::showCheckinWarningDialog() {
    // 增加倒计时+忽略+告警入库+重置页面（不退出系统）
    // 多位置校验提示
    int checkinQty = m_selectedCheckinQty;
    QString posInfo;
    if (checkinQty == 1) {
        QJsonObject posObj = m_availablePositions[0].toObject();
        posInfo = QStringLiteral("%1-%2").arg(posObj["cabinetName"].toString(), posObj["posDisplay"].toString());
    } else {
        posInfo = QStringLiteral("%1个位置").arg(checkinQty);
    }

    VerifyAlertDialog dlg(QStringLiteral("入库校验"), QStringLiteral("入库校验异常"),
        QStringLiteral(
        "检测到 <b>%1</b> 的 <b>%2</b> 位置传感器未检测到工具放置，<br/>"
        "或工具未正确放入指定位置。<br/>"
        "请检查工具是否放好后重新校验。"
    ).arg(posInfo.contains("个位置") ? QStringLiteral("多个柜体") : m_availablePositions[0].toObject()["cabinetName"].toString()).arg(posInfo),
        "font-size:15px;color:#555;line-height:1.6;background:transparent;",
        QStringLiteral("取消入库"), QStringLiteral("🔄 重新校验"), 150,
        AppConfig::instance().borrowReturnBuffer(), this);
    dlg.setFixedSize(520, 380);

    int result = dlg.exec();
    if (result == QDialog::Accepted) {
        showCheckinSuccessDialog();
    } else if (result == 2) {
        // 忽略或倒计时结束：写告警日志（闭环）
        AlertService alertSvc;
        alertSvc.recordVerifyAlert(m_user["userId"].toInt(), m_selectedToolId, m_toolCodeEdit->text().trimmed(),
            QStringLiteral("入库校验异常：%1位置传感器未检测到工具放置，用户忽略告警或倒计时超时").arg(posInfo));

        MessageDialog::showWarning(nullptr, QStringLiteral("告警已记录"),
            QStringLiteral("入库校验异常告警已记录到系统告警，页面已重置。"));
        onReset();
    }
}

// ═══════════ 步骤4: 入库成功对话框 ═══════════
void ToolCheckinPage::showCheckinSuccessDialog() {
    // 入库只更新映射表status，不新建tool_info记录
    // 一个工具可以在多个位置入库，传入positions数组一次性处理
    int checkinQty = m_selectedCheckinQty;
    ToolService svc;

    // 构建positions数组
    QJsonArray positions;
    for (int i = 0; i < checkinQty; ++i) {
        QJsonObject posObj = m_availablePositions[i].toObject();
        positions.append(posObj);
    }

    QJsonObject data;
    data["selectedToolId"] = m_selectedToolId;
    data["positions"] = positions;
    data["toolCode"] = m_toolCodeEdit->text().trimmed();

    qInfo() << "[Checkin] 入库" << checkinQty << "件，工具ID=" << m_selectedToolId;
    bool success = svc.checkinTool(data);

    ResultDialog dlg(success ? QStringLiteral("入库成功") : QStringLiteral("入库失败"),
        success ? QStringLiteral("✅") : QStringLiteral("❌"),
        success ? QStringLiteral("入库成功！") : QStringLiteral("入库失败"),
        success ? QStringLiteral("#43a047") : QStringLiteral("#e53935"), 14, this);
    dlg.setFixedSize(480, 380);

    if (success) {
        auto* summaryLabel = new QLabel(QStringLiteral(
            "工具「<b style='color:#43a047;'>%1</b>」已成功入库 <b style='color:#43a047;'>%2</b> 件"
        ).arg(m_toolNameEdit->text()).arg(checkinQty));
        summaryLabel->setAlignment(Qt::AlignCenter);
        summaryLabel->setStyleSheet("font-size:15px;color:#555;background:transparent;");
        summaryLabel->setTextFormat(Qt::RichText);
        summaryLabel->setWordWrap(true);
        dlg.bodyLayout()->addWidget(summaryLabel);

        // 多位置时显示位置列表
        if (checkinQty > 1) {
            QString posList;
            for (int i = 0; i < checkinQty; ++i) {
                QJsonObject posObj = m_availablePositions[i].toObject();
                posList += QStringLiteral("位置%1：%2").arg(i + 1).arg(posObj["posDisplay"].toString());
                if (i < checkinQty - 1) posList += QStringLiteral("<br/>");
            }
            auto* posLabel = new QLabel(posList);
            posLabel->setAlignment(Qt::AlignCenter);
            posLabel->setStyleSheet("font-size:14px;color:#43a047;background:transparent;");
            posLabel->setTextFormat(Qt::RichText);
            posLabel->setWordWrap(true);
            dlg.bodyLayout()->addWidget(posLabel);
        } else {
            QJsonObject posObj = m_availablePositions[0].toObject();
            auto* posLabel = new QLabel(QStringLiteral(
                "入库位置：<b style='color:#43a047;'>%1</b>"
            ).arg(posObj["posDisplay"].toString()));
            posLabel->setAlignment(Qt::AlignCenter);
            posLabel->setStyleSheet("font-size:15px;color:#555;background:transparent;");
            posLabel->setTextFormat(Qt::RichText);
            dlg.bodyLayout()->addWidget(posLabel);
        }
    } else {
        QString failReason;
        QString tn = m_toolNameEdit->text().trimmed();
        // 查询DB看工具当前状态，给出针对性提示
        db::ToolDAO toolDao;
        QJsonObject toolInfo = toolDao.findById(m_selectedToolId);
        if (!toolInfo.isEmpty()) {
            QString dbStatus = toolInfo["status"].toString();
            if (dbStatus == SC::TOOL_IN_STOCK) {
                failReason = QStringLiteral("工具「%1」已在库，不能重复入库").arg(tn);
            } else if (dbStatus == "borrowed") {
                failReason = QStringLiteral("工具「%1」正在借用中，请先归还再入库").arg(tn);
            } else {
                failReason = QStringLiteral("工具「%1」当前状态为%2，入库操作失败").arg(tn, dbStatus);
            }
        } else {
            failReason = QStringLiteral("工具不存在，请检查后重试");
        }
        auto* descLabel = new QLabel(failReason);
        descLabel->setAlignment(Qt::AlignCenter);
        descLabel->setStyleSheet("font-size:15px;color:#555;background:transparent;line-height:1.6;");
        descLabel->setWordWrap(true);
        dlg.bodyLayout()->addWidget(descLabel);
    }

    dlg.addFinishButtonRow(QStringLiteral("完成"), StyleHelper::buttonPrimary(), 140);
    dlg.exec();

    // 入库成功写操作日志
    if (success) {
        int userId = m_user["userId"].toInt();
        if (userId <= 0) userId = 1;

        // 多位置入库写一条日志，包含所有位置
        QString posSummary;
        for (int i = 0; i < checkinQty; ++i) {
            QJsonObject posObj = m_availablePositions[i].toObject();
            posSummary += posObj["posDisplay"].toString();
            if (i < checkinQty - 1) posSummary += QStringLiteral(",");
        }

        QString content = QStringLiteral("入库工具「%1」编号[%2]×%3件 位置%4")
            .arg(m_toolNameEdit->text().trimmed())
            .arg(m_toolCodeEdit->text().trimmed())
            .arg(checkinQty)
            .arg(posSummary);

        db::RecordDAO recDao;
        recDao.insertOperationLog(userId, SC::OP_CHECKIN, "tool",
                                   m_toolCodeEdit->text().trimmed(), content);

        m_recordCurrentPage = 1;
        loadCheckinRecords();
    }

    if (success) {
        onReset();
    }
}

// ═══════════ 入库记录相关 ═══════════

// Tab切换时加载对应数据
void ToolCheckinPage::onTabChanged(int index) {
    if (index == 1) {
        // 切到"入库记录"Tab时加载记录
        m_recordCurrentPage = 1;
        loadCheckinRecords();
    }
}

// 加载入库历史记录（通过RecordDAO查询sys_operation_log+位置JOIN）
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
        QString time = r["createdAt"].toString();

        // 列0：入库时间
        m_recordTable->setItem(i, 0, new QTableWidgetItem(time));

        // 从content解析工具名/数量/位置/供应商
        // 新格式："入库工具「工具名」编号[编号]×1件 位置A-01-01"
        // 兼容旧格式："入库工具「工具名」编号[编号]×数量，供应商：xxx"
        QString toolName, supplier;
        QRegularExpression re1("入库工具「(.+?)」");
        QRegularExpression re3("供应商：(.+)");
        QRegularExpressionMatch m1 = re1.match(content);
        QRegularExpressionMatch m3 = re3.match(content);
        if (m1.hasMatch()) toolName = m1.captured(1);
        if (m3.hasMatch()) supplier = m3.captured(1);

        // 入库记录6列（去掉数量列，恒为1件无需显示）
        // 列顺序：入库时间(0)/工具名称(1)/工具编号(2)/位置(3)/供应商(4)/操作人(5)

        // 列1：工具名称
        m_recordTable->setItem(i, 1, new QTableWidgetItem(toolName.isEmpty() ? QStringLiteral("--") : toolName));
        // 列2：工具编号
        QString toolCode = r["targetId"].toString();
        if (toolCode.isEmpty()) {
            QRegularExpression reCode("编号\\[(.+?)\\]");
            QRegularExpressionMatch mc = reCode.match(content);
            if (mc.hasMatch()) toolCode = mc.captured(1);
        }
        m_recordTable->setItem(i, 2, new QTableWidgetItem(toolCode.isEmpty() ? QStringLiteral("--") : toolCode));
        // 列3：位置（三级优先：content解析→映射表→tool_info）
        // 新格式支持多位置："位置A-01-01,A-01-02"，用正则匹配全部
        {
            QString posDisplay;
            // 匹配"位置"关键字后的所有位置（逗号分隔）
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
        // 列4：供应商
        m_recordTable->setItem(i, 4, new QTableWidgetItem(supplier.isEmpty() ? QStringLiteral("--") : supplier));
        // 列5：操作人
        QString userName = r["realName"].toString();
        if (userName.isEmpty()) userName = QStringLiteral("--");
        m_recordTable->setItem(i, 5, new QTableWidgetItem(userName));

        m_recordTable->setRowHeight(i, 48);
    }
}

// 入库记录分页：上一页
void ToolCheckinPage::onRecordPrevPage() {
    if (m_recordCurrentPage > 1) {
        m_recordCurrentPage--;
        loadCheckinRecords();
    }
}

// 入库记录分页：下一页
void ToolCheckinPage::onRecordNextPage() {
    int totalPages = (m_recordTotalRecords + m_recordPageSize - 1) / m_recordPageSize;
    if (m_recordCurrentPage < totalPages) {
        m_recordCurrentPage++;
        loadCheckinRecords();
    }
}

// ═══════════ 入库流程简化：工具类型→工具选择→位置自动给出 ═══════════

// 入库页工具下拉=显示对照表中有空闲位置的工具
// 核心设计：入库是将工具放到指定位置，工具在映射表中有空闲位置即可入库
// 映射表status='pending'表示待入库的空闲位置
void ToolCheckinPage::loadToolsByCategory(int categoryId) {
    if (!m_toolSelectCombo) return;
    m_toolSelectCombo->blockSignals(true);
    m_toolSelectCombo->clear();
    m_toolSelectCombo->addItem(QStringLiteral("请选择待入库工具"), 0);

    db::ToolDAO toolDao;
    QJsonArray pendingTools = toolDao.findPendingTools(categoryId, m_localMachineGroupId);
    for (int i = 0; i < pendingTools.size(); ++i) {
        QJsonObject t = pendingTools[i].toObject();
        int freePosCount = t["freePosCount"].toInt();
        if (freePosCount > 0) {
            QString toolStatus = t["status"].toString();
            QString statusHint;
            if (toolStatus == SC::TOOL_PENDING) statusHint = QStringLiteral("待入库");
            else if (toolStatus == SC::TOOL_IN_STOCK) statusHint = QStringLiteral("可补位");
            else if (toolStatus == SC::TOOL_CHECKED_OUT) statusHint = QStringLiteral("可重新入库");
            else statusHint = toolStatus;
            QString text = QStringLiteral("%1 - %2（%3，可入库%4件）")
                .arg(t["toolCode"].toString(), t["toolName"].toString(), statusHint)
                .arg(freePosCount);
            m_toolSelectCombo->addItem(text, t["toolId"].toInt());
        }
    }
    if (m_toolSelectCombo->count() <= 1) {
        m_toolSelectCombo->addItem(QStringLiteral("暂无待入库工具（请先在对照关系表中配置位置）"), -1);
    }
    m_toolSelectCombo->blockSignals(false);
}

// generateUniqueToolCode已移除
// 入库不创建新tool_info记录，不需要生成新编号

// 工具类型切换时刷新工具下拉
void ToolCheckinPage::onCategoryChanged() {
    if (!m_categoryCombo) return;
    int categoryId = m_categoryCombo->currentData().toInt();
    loadToolsByCategory(categoryId);
    m_selectedToolId = 0;
}

// 选择工具后自动填充信息 + 查找所有空闲位置
// 入库数量可选1~N，N=空闲位置数
void ToolCheckinPage::onToolSelected() {
    if (!m_toolSelectCombo) return;
    m_selectedToolId = m_toolSelectCombo->currentData().toInt();
    if (m_selectedToolId <= 0) {
        m_qtyCombo->clear();
        m_qtyCombo->addItem(QStringLiteral("请先选择工具"), 0);
        m_qtyHintLabel->setText(QStringLiteral("请先选择工具"));
        m_selectedCheckinQty = 0;
        m_availablePositions = QJsonArray();
        return;
    }

    db::ToolDAO toolDao;
    QJsonObject info = toolDao.findToolBasicInfo(m_selectedToolId);
    if (info.isEmpty()) return;

    m_toolNameEdit->setText(info["toolName"].toString());
    m_toolCodeEdit->setText(info["toolCode"].toString());
    m_specEdit->setText(info["spec"].toString());

    // 查找此工具在映射表中status='pending'的位置（待入库）
    m_availablePositions = QJsonArray();
    QJsonArray positions = toolDao.findPendingPositions(m_selectedToolId);
    for (int i = 0; i < positions.size(); ++i) {
        QJsonObject p = positions[i].toObject();
        QJsonObject posObj;
        posObj["cabinetName"] = p["cabinetName"].toString();
        posObj["cabinetCode"] = p["cabinetCode"].toString();
        posObj["layer"]       = p["layer"].toString();
        posObj["position"]    = p["position"].toString();
        posObj["posDisplay"]  = StyleHelper::formatPosition(
            p["cabinetName"].toString(), p["layer"].toString(), p["position"].toString());
        posObj["cabinetId"]   = p["cabinetId"].toInt();
        m_availablePositions.append(posObj);
    }

    int freePosCount = m_availablePositions.size();
    // 入库数量下拉：1~空闲位置数
    m_qtyCombo->blockSignals(true);
    m_qtyCombo->clear();
    if (freePosCount <= 0) {
        m_qtyCombo->addItem(QStringLiteral("无空闲位置"), 0);
        m_qtyHintLabel->setText(QStringLiteral("该工具没有空闲位置，请先在对照关系中配置"));
        m_selectedCheckinQty = 0;
    } else {
        for (int i = 1; i <= freePosCount; ++i) {
            m_qtyCombo->addItem(QStringLiteral("%1 件").arg(i), i);
        }
        m_qtyCombo->setCurrentIndex(0);  // 默认选1件
        m_selectedCheckinQty = 1;
        if (freePosCount == 1) {
            m_qtyHintLabel->setText(QStringLiteral("入库位置：%1").arg(
                m_availablePositions[0].toObject()["posDisplay"].toString()));
        } else {
            m_qtyHintLabel->setText(QStringLiteral("最多可入库%1件（%1个空闲位置）").arg(freePosCount));
        }
    }
    m_qtyCombo->blockSignals(false);
}
