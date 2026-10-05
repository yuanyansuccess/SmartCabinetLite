/**
 * @file    SystemMaintenancePage.cpp
 * @author  袁燕
 * @brief   系统维护页面 — 任务配置/工具维护/工具对照关系维护
 *
 * 新建系统维护页面
 * 排布对齐系统设置 + 对照关系支持手动选择
 * 改为Tab选项卡布局（QStackedWidget），去掉滚动
 *   触屏友好：点击Tab切换页面，不用滑轮滚动
 *   样式对齐系统设置：灰底白选中+主色下划线
 *   三个Tab：任务配置 / 工具维护 / 工具对照关系
 */
#include "SystemMaintenancePage.h"
#include "ui_SystemMaintenancePage.h"
#include <QTableWidget>  // 维护页各配置表格
#include "utils/StyleHelper.h"
#include "components/BaseDialog.h"
#include "components/MessageDialog.h"
#include "common/AppConfig.h"
#include "db/ToolDAO.h"
#include "services/MaintenanceService.h"  // 维护域写操作下沉（读查询仍直调DAO）
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QEvent>
#include <QFileDialog>  // 工具文档上传
#include <QStandardPaths>  // 文档存储路径
#include <QFileInfo>  // 文件信息
#include <QDir>  // 目录创建
#include <QDateTime>  // 时间戳文件名
#include "common/Constants.h"  // RECOGNITION_VISION + TOOL_DOC_SUFFIXES

SystemMaintenancePage::SystemMaintenancePage(QWidget* parent) : QWidget(parent), ui(new Ui::SystemMaintenancePage) {
    // 静态布局来自SystemMaintenancePage.ui（Qt Designer可视化维护）
    ui->setupUi(this);

    // 桥接.ui控件（业务逻辑沿用m_成员，零改动）
    m_stackedWidget = ui->stackedWidget;
    m_taskTypeCombo = ui->taskTypeCombo;
    m_taskToolTable = ui->taskToolTable;
    m_toolTable = ui->toolTable;
    m_mappingTable = ui->mappingTable;
    m_posCabinetCombo = ui->posCabinetCombo;
    m_posLayerCombo = ui->posLayerCombo;
    m_posPositionCombo = ui->posPositionCombo;
    m_posToolCombo = ui->posToolCombo;

    // Tab选项卡：点击切换+Hover效果的事件过滤
    m_tabLabels << ui->tabLabel0 << ui->tabLabel1 << ui->tabLabel2;
    for (QLabel* tab : m_tabLabels) {
        tab->installEventFilter(this);
    }
    // 隐藏"工具维护""工具对照关系"两个选项卡（保留索引占位，恢复时删除此两行即可）
    m_tabLabels[1]->hide();
    m_tabLabels[2]->hide();
    updateTabStyles();

    // ── 任务配置面板 ──
    // 表格列宽策略：前3列拉伸，推荐数量/操作列固定
    m_taskToolTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_taskToolTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_taskToolTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_taskToolTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_taskToolTable->setColumnWidth(3, 110);
    m_taskToolTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_taskToolTable->setColumnWidth(4, 170);  // 操作列
    connect(ui->addTaskToolBtn, &QPushButton::clicked, this, &SystemMaintenancePage::onAddTaskTool);
    connect(ui->taskSaveBtn, &QPushButton::clicked, this, &SystemMaintenancePage::saveTaskToolConfig);
    loadTaskTypes();
    connect(m_taskTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        int typeId = m_taskTypeCombo->currentData().toInt();
        if (typeId > 0) loadTaskTools(typeId);
    });
    if (m_taskTypeCombo->count() > 0) {
        loadTaskTools(m_taskTypeCombo->currentData().toInt());
    }

    // ── 工具维护面板 ──
    // 表格列宽策略：前6列拉伸，操作列固定180px
    for (int i = 0; i < 6; i++) {
        m_toolTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
    }
    m_toolTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Fixed);
    m_toolTable->setColumnWidth(6, 180);  // 180操作列更宽
    connect(ui->addToolBtn, &QPushButton::clicked, this, &SystemMaintenancePage::onAddTool);
    connect(ui->toolSaveBtn, &QPushButton::clicked, this, [this]() {
        loadAllTools();
        MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("工具列表已刷新"));
    });
    loadAllTools();

    // ── 工具对照关系面板 ──
    // 表格列宽策略：前3列拉伸，状态/操作列固定
    m_mappingTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_mappingTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_mappingTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_mappingTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_mappingTable->setColumnWidth(3, 90);  // 90状态列更宽
    m_mappingTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_mappingTable->setColumnWidth(4, 120);  // 120操作列更宽
    connect(ui->bindBtn, &QPushButton::clicked, this, &SystemMaintenancePage::onBindPosition);
    connect(ui->mappingSaveBtn, &QPushButton::clicked, this, [this]() {
        loadPositionMappings();
        loadAvailablePositions();
        loadUnboundTools();
        MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("对照关系已刷新"));
    });
    loadAvailablePositions();
    loadUnboundTools();
    loadPositionMappings();

    // 默认显示第一个Tab
    switchTab(0);
}

SystemMaintenancePage::~SystemMaintenancePage() {
    delete ui;
}

/**
 * @brief 按当前选中项刷新各选项卡的样式
 */
void SystemMaintenancePage::updateTabStyles() {
    // Tab样式对齐系统设置：灰底白选中+主色下划线
    for (int i = 0; i < m_tabLabels.size(); ++i) {
        if (i == m_activeTabIndex) {
            m_tabLabels[i]->setStyleSheet(
                QString("font-size:15px;font-weight:600;padding:10px 24px;border-radius:10px 10px 0 0;"
                        "color:%1;background:#fff;min-height:44px;"
                        "border-bottom:3px solid %1;")
                .arg(StyleHelper::primaryColor()));
        } else {
            m_tabLabels[i]->setStyleSheet(StyleHelper::panelTitleBar());
        }
    }
}

/**
 * @brief 切换到指定选项卡
 * @param index 目标选项卡索引
 */
void SystemMaintenancePage::switchTab(int index) {
    if (index < 0 || index >= m_tabLabels.size()) return;
    m_activeTabIndex = index;
    updateTabStyles();
    if (m_stackedWidget) {
        m_stackedWidget->setCurrentIndex(index);
    }
    // 切换时刷新当前Tab数据
    switch (index) {
        case 0: loadTaskTypes(); break;
        case 1: loadAllTools(); break;
        case 2: loadPositionMappings(); loadAvailablePositions(); loadUnboundTools(); break;
        default: break;
    }
}

/**
         * 事件过滤器：拦截控件与窗口事件并转交专用处理
         * @param obj 事件来源控件
         * @param event 事件对象
         * @return true=事件已被处理
         */
bool SystemMaintenancePage::eventFilter(QObject* watched, QEvent* event) {
    // Tab点击切换 + Hover效果
    if (event->type() == QEvent::MouseButtonPress) {
        for (int i = 0; i < m_tabLabels.size(); ++i) {
            if (watched == m_tabLabels[i]) {
                switchTab(i);
                break;
            }
        }
    } else if (event->type() == QEvent::Enter) {
        for (int i = 0; i < m_tabLabels.size(); ++i) {
            if (watched == m_tabLabels[i] && i != m_activeTabIndex) {
                m_tabLabels[i]->setStyleSheet(
                    QString("font-size:15px;font-weight:600;padding:10px 24px;border-radius:10px 10px 0 0;"
                            "color:%1;background:#e8f0fe;min-height:44px;")
                    .arg(StyleHelper::primaryColor()));
                break;
            }
        }
    } else if (event->type() == QEvent::Leave) {
        for (int i = 0; i < m_tabLabels.size(); ++i) {
            if (watched == m_tabLabels[i] && i != m_activeTabIndex) {
                m_tabLabels[i]->setStyleSheet(StyleHelper::panelTitleBar());
                break;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

/**
 * @brief 刷新页面数据与统计显示
 */
void SystemMaintenancePage::refresh() {
    // 菜单切换到系统维护时重置页面状态
    // 1. 切回第一个Tab（任务配置） 2. 重新加载所有数据
    if (m_stackedWidget) m_stackedWidget->setCurrentIndex(0);
    m_activeTabIndex = 0;
    updateTabStyles();
    loadTaskTypes();
    loadAllTools();
    loadPositionMappings();
    loadAvailablePositions();
    loadUnboundTools();
}

// ==================== 任务配置面板 ====================

void SystemMaintenancePage::loadTaskTypes() {
    if (!m_taskTypeCombo) return;
    m_taskTypeCombo->clear();
    db::ToolDAO toolDao;
    QJsonArray types = toolDao.allTaskTypes();
    for (int i = 0; i < types.size(); ++i) {
        QJsonObject obj = types[i].toObject();
        m_taskTypeCombo->addItem(obj["typeName"].toString(), obj["typeId"].toInt());
    }
}

/**
 * @brief 加载任务工具
 */
void SystemMaintenancePage::loadTaskTools(int typeId) {
    if (!m_taskToolTable || typeId <= 0) return;
    m_taskToolTable->setRowCount(0);

    db::ToolDAO toolDao;
    QJsonArray tools = toolDao.findTaskTypeTools(typeId);

    int row = 0;
    for (int i = 0; i < tools.size(); ++i) {
        QJsonObject obj = tools[i].toObject();
        int toolId = obj["toolId"].toInt();
        m_taskToolTable->insertRow(row);
        // 列顺序：工具编号/工具名称/规格/推荐数量/操作
        m_taskToolTable->setItem(row, 0, new QTableWidgetItem(obj["toolCode"].toString()));  // 工具编号
        m_taskToolTable->setItem(row, 1, new QTableWidgetItem(obj["toolName"].toString()));  // 工具名称
        m_taskToolTable->setItem(row, 2, new QTableWidgetItem(obj["spec"].toString()));      // 规格
        auto* qtyItem = new QTableWidgetItem(QString::number(obj["recommendedQty"].toInt())); // 推荐数量
        qtyItem->setTextAlignment(Qt::AlignCenter);
        m_taskToolTable->setItem(row, 3, qtyItem);

        // 操作列：修改 + 删除
        auto* opWidget = new QWidget();
        opWidget->setStyleSheet("background:transparent;");
        auto* opLayout = new QHBoxLayout(opWidget);
        opLayout->setContentsMargins(4, 4, 4, 4);
        opLayout->setSpacing(6);

        auto* editBtn = new QPushButton(QStringLiteral("修改"));
        editBtn->setFixedSize(60, 40);  // 60x40小米标准
        editBtn->setStyleSheet("QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:6px;font-size:14px;font-weight:600;}QPushButton:hover{background:#3d8ae0;}");
        editBtn->setCursor(Qt::PointingHandCursor);
        connect(editBtn, &QPushButton::clicked, this, [this, row] { onEditTaskTool(row); });

        auto* delBtn = new QPushButton(QStringLiteral("删除"));
        delBtn->setFixedSize(60, 40);
        delBtn->setStyleSheet("QPushButton{background:#e74c3c;color:#fff;border:none;border-radius:6px;font-size:14px;font-weight:600;}QPushButton:hover{background:#c0392b;}");
        delBtn->setCursor(Qt::PointingHandCursor);
        connect(delBtn, &QPushButton::clicked, this, [this, row] { onDeleteTaskTool(row); });

        opLayout->addWidget(editBtn);
        opLayout->addWidget(delBtn);
        m_taskToolTable->setCellWidget(row, 4, opWidget);

        // 存储toolId到行数据，供修改/删除使用
        m_taskToolTable->item(row, 0)->setData(Qt::UserRole, toolId);
        m_taskToolTable->setRowHeight(row, 56);  // 56小米工程师标准
        row++;
    }
}

/**
 * @brief 保存任务工具配置
 */
void SystemMaintenancePage::saveTaskToolConfig() {
    int typeId = m_taskTypeCombo ? m_taskTypeCombo->currentData().toInt() : 0;
    if (typeId <= 0) {
        MessageDialog::showWarning(this, QStringLiteral("提示"), QStringLiteral("请先选择任务类型"));
        return;
    }
    db::ToolDAO toolDao;
    bool ok = true;
    for (int i = 0; i < m_taskToolTable->rowCount(); ++i) {
        int toolId = m_taskToolTable->item(i, 0)->data(Qt::UserRole).toInt();
        if (toolId <= 0) continue;
        int qty = m_taskToolTable->item(i, 3)->text().toInt();
        if (qty < 1) qty = 1;
        MaintenanceService maintSvc;
    if (!maintSvc.updateTaskTypeToolQty(typeId, toolId, qty)) { ok = false; break; }
    }
    if (ok) {
        MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("任务类型工具配置已保存"));
    } else {
        MessageDialog::showError(this, QStringLiteral("失败"), QStringLiteral("保存失败，请重试"));
    }
}

// ==================== 工具维护面板 ====================

void SystemMaintenancePage::loadAllTools() {
    if (!m_toolTable) return;
    m_toolTable->setRowCount(0);

    db::ToolDAO toolDao;
    QJsonArray tools = toolDao.allToolsForMaintenance();

    int row = 0;
    for (int i = 0; i < tools.size(); ++i) {
        QJsonObject obj = tools[i].toObject();
        m_toolTable->insertRow(row);
        m_toolTable->setItem(row, 0, new QTableWidgetItem(obj["toolCode"].toString()));
        m_toolTable->setItem(row, 1, new QTableWidgetItem(obj["toolName"].toString()));
        m_toolTable->setItem(row, 2, new QTableWidgetItem(obj["categoryName"].toString()));
        m_toolTable->setItem(row, 3, new QTableWidgetItem(obj["spec"].toString()));
        m_toolTable->setItem(row, 4, new QTableWidgetItem(obj["unit"].toString()));

        QString status = obj["status"].toString();
        QString statusText = SC::toolStatusText(status);
        QColor statusColor(SC::toolStatusColor(status));
        auto* statusItem = new QTableWidgetItem(statusText);
        statusItem->setForeground(statusColor);
        m_toolTable->setItem(row, 5, statusItem);

        int toolId = obj["toolId"].toInt();
        auto* opWidget = new QWidget();
        opWidget->setStyleSheet("background:transparent;");
        auto* opLayout = new QHBoxLayout(opWidget);
        opLayout->setContentsMargins(4, 4, 4, 4);
        opLayout->setSpacing(6);

        auto* editBtn = new QPushButton(QStringLiteral("编辑"));
        editBtn->setFixedSize(64, 40);
        editBtn->setStyleSheet("QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:8px;font-size:14px;font-weight:600;}QPushButton:hover{background:#3d8ae0;}");
        editBtn->setCursor(Qt::PointingHandCursor);
        connect(editBtn, &QPushButton::clicked, this, [this, toolId] { onEditTool(toolId); });

        auto* delBtn = new QPushButton(QStringLiteral("删除"));
        delBtn->setFixedSize(64, 40);
        delBtn->setStyleSheet("QPushButton{background:#e74c3c;color:#fff;border:none;border-radius:8px;font-size:14px;font-weight:600;}QPushButton:hover{background:#c0392b;}");
        delBtn->setCursor(Qt::PointingHandCursor);
        connect(delBtn, &QPushButton::clicked, this, [this, toolId] { onDeleteTool(toolId); });

        opLayout->addWidget(editBtn);
        opLayout->addWidget(delBtn);
        m_toolTable->setCellWidget(row, 6, opWidget);
        m_toolTable->setRowHeight(row, 56);
        row++;
    }
}

/**
 * @brief 处理工具
 */
void SystemMaintenancePage::onAddTool() {
    m_editToolId = 0;
    // 复用对话框创建逻辑
    if (!m_toolDialog) ensureToolDialogCreated();
    m_toolDialog->setDialogTitle(QStringLiteral("新增工具"));
    m_dlgName->clear(); m_dlgCode->clear(); m_dlgCategory->setCurrentIndex(0);
    m_dlgSpec->clear(); m_dlgUnit->setText(QStringLiteral("把"));
    m_dlgSupplier->setCurrentIndex(0); m_dlgRecognition->setCurrentIndex(0);
    m_dlgDocumentPath.clear(); m_dlgDocumentEdit->clear();
    m_toolDialog->exec();
}

// 确保工具对话框已创建（不弹出），onAddTool和onEditTool复用
void SystemMaintenancePage::ensureToolDialogCreated() {
    if (m_toolDialog) return;
    m_toolDialog = new BaseDialog(this, 480);
    m_toolDialog->setDialogTitle(QStringLiteral("新增工具"));
    auto* cl = m_toolDialog->contentLayout();
    cl->setSpacing(12);

    m_dlgName = new QLineEdit(); m_dlgName->setStyleSheet(StyleHelper::lineEdit()); m_dlgName->setMinimumHeight(StyleHelper::Token::ControlHeight);
    m_dlgCode = new QLineEdit(); m_dlgCode->setStyleSheet(StyleHelper::lineEdit()); m_dlgCode->setMinimumHeight(StyleHelper::Token::ControlHeight);
    m_dlgCategory = new QComboBox(); m_dlgCategory->setEditable(true); m_dlgCategory->setStyleSheet(StyleHelper::comboBox()); m_dlgCategory->setMinimumHeight(StyleHelper::Token::ControlHeight);
    m_dlgSpec = new QLineEdit(); m_dlgSpec->setStyleSheet(StyleHelper::lineEdit()); m_dlgSpec->setMinimumHeight(StyleHelper::Token::ControlHeight);
    m_dlgUnit = new QLineEdit(); m_dlgUnit->setStyleSheet(StyleHelper::lineEdit()); m_dlgUnit->setMinimumHeight(StyleHelper::Token::ControlHeight);
    m_dlgSupplier = new QComboBox(); m_dlgSupplier->setEditable(true); m_dlgSupplier->setStyleSheet(StyleHelper::comboBox()); m_dlgSupplier->setMinimumHeight(StyleHelper::Token::ControlHeight);
    m_dlgSupplier->addItem(QStringLiteral("史丹利工具"));
    m_dlgSupplier->addItem(QStringLiteral("博世电动工具"));
    m_dlgSupplier->addItem(QStringLiteral("牧田电动工具"));
    m_dlgSupplier->addItem(QStringLiteral("世达工具"));
    m_dlgSupplier->addItem(QStringLiteral("其他"));
    m_dlgRecognition = new QComboBox(); m_dlgRecognition->setStyleSheet(StyleHelper::comboBox()); m_dlgRecognition->setMinimumHeight(StyleHelper::Token::ControlHeight);
    // 全系统统一为视觉识别，识别方式不提供其它选项
    m_dlgRecognition->addItem(QStringLiteral("视觉识别"), SC::RECOGNITION_VISION);
    m_dlgDocumentEdit = new QLineEdit(); m_dlgDocumentEdit->setStyleSheet(StyleHelper::lineEdit()); m_dlgDocumentEdit->setMinimumHeight(StyleHelper::Token::ControlHeight); m_dlgDocumentEdit->setReadOnly(true);
    m_dlgDocumentEdit->setPlaceholderText(QStringLiteral("支持 .doc / .docx / .pdf，最大50MB"));
    m_dlgUploadBtn = new QPushButton(QStringLiteral("上传"));
    m_dlgUploadBtn->setStyleSheet(StyleHelper::buttonOutline());
    m_dlgUploadBtn->setCursor(Qt::PointingHandCursor);
    m_dlgUploadBtn->setFixedHeight(StyleHelper::Token::ControlHeight);
    m_dlgUploadBtn->setFixedWidth(70);
    connect(m_dlgUploadBtn, &QPushButton::clicked, this, &SystemMaintenancePage::onUploadDocument);

    db::ToolDAO toolDao;
    QList<ToolCategory> categories = toolDao.allCategories();
    for (const auto& cat : categories) {
        m_dlgCategory->addItem(cat.categoryName, cat.categoryId);
    }

    auto addField = [cl](const QString& label, QWidget* w) {
        auto* row = new QHBoxLayout();
        auto* lb = new QLabel(label);
        lb->setFixedWidth(80);
        lb->setStyleSheet(StyleHelper::labelText());
        row->addWidget(lb);
        row->addWidget(w, 1);
        cl->addLayout(row);
    };
    addField(QStringLiteral("工具名称:"), m_dlgName);
    addField(QStringLiteral("工具编号:"), m_dlgCode);
    addField(QStringLiteral("工具类型:"), m_dlgCategory);
    addField(QStringLiteral("工具规格:"), m_dlgSpec);
    addField(QStringLiteral("单位:"), m_dlgUnit);
    addField(QStringLiteral("供应商:"), m_dlgSupplier);
    addField(QStringLiteral("识别方式:"), m_dlgRecognition);

    {
        auto* row = new QHBoxLayout();
        auto* lb = new QLabel(QStringLiteral("工具文档:"));
        lb->setFixedWidth(80);
        lb->setStyleSheet(StyleHelper::labelText());
        row->addWidget(lb);
        row->addWidget(m_dlgDocumentEdit, 1);
        row->addWidget(m_dlgUploadBtn);
        cl->addLayout(row);
    }

    auto* saveBtn = new QPushButton(QStringLiteral("保存"));
    saveBtn->setStyleSheet(StyleHelper::buttonPrimary());
    saveBtn->setCursor(Qt::PointingHandCursor);
    saveBtn->setMinimumHeight(StyleHelper::Token::ControlHeight);
    connect(saveBtn, &QPushButton::clicked, this, &SystemMaintenancePage::onSubmitTool);
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    cancelBtn->setStyleSheet(StyleHelper::buttonDefault());
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setMinimumHeight(StyleHelper::Token::ControlHeight);
    connect(cancelBtn, &QPushButton::clicked, this, [this]() { m_toolDialog->reject(); });
    auto* btnLayout = m_toolDialog->buttonLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(saveBtn);
}

/**
 * @brief 处理编辑框工具
 */
void SystemMaintenancePage::onEditTool(int toolId) {
    m_editToolId = toolId;
    if (!m_toolDialog) ensureToolDialogCreated();
    db::ToolDAO toolDao;
    QJsonObject t = toolDao.findToolForEdit(toolId);
    if (t.isEmpty()) return;
    m_toolDialog->setDialogTitle(QStringLiteral("编辑工具"));
    m_dlgName->setText(t["toolName"].toString());
    m_dlgCode->setText(t["toolCode"].toString());
    int catId = t["categoryId"].toInt();
    for (int i = 0; i < m_dlgCategory->count(); ++i) {
        if (m_dlgCategory->itemData(i).toInt() == catId) { m_dlgCategory->setCurrentIndex(i); break; }
    }
    m_dlgSpec->setText(t["spec"].toString());
    m_dlgUnit->setText(t["unit"].toString());
    QString supplier = t["supplier"].toString();
    if (!supplier.isEmpty()) {
        int idx = m_dlgSupplier->findText(supplier);
        if (idx >= 0) m_dlgSupplier->setCurrentIndex(idx);
        else { m_dlgSupplier->addItem(supplier); m_dlgSupplier->setCurrentText(supplier); }
    }
    QString recognition = t["recognitionMethod"].toString();
    int recIdx = m_dlgRecognition->findData(recognition);
    if (recIdx >= 0) m_dlgRecognition->setCurrentIndex(recIdx);
    m_dlgDocumentPath = t["documentPath"].toString();
    if (!m_dlgDocumentPath.isEmpty()) {
        QFileInfo fi(m_dlgDocumentPath);
        m_dlgDocumentEdit->setText(fi.fileName());
    } else {
        m_dlgDocumentEdit->clear();
    }
    m_toolDialog->exec();
}

/**
 * @brief 处理工具
 */
void SystemMaintenancePage::onDeleteTool(int toolId) {
    // 按工具状态判断是否可删除
    // 在库(in_stock)/已借出(borrowed) → 不能删除（工具还有物理实体在系统中）
    // 待入库(pending)/已出库(checked_out)/维护中(maintenance) → 可以删除
    db::ToolDAO toolDao;
    QJsonObject t = toolDao.findById(toolId);
    if (!t.isEmpty()) {
        QString status = t["status"].toString();
        QString toolName = t["toolName"].toString();
        if (status == SC::TOOL_IN_STOCK) {
            MessageDialog::showError(this, QStringLiteral("无法删除"),
                QStringLiteral("工具「%1」正在库中，不能删除。\n请先出库后再删除。").arg(toolName));
            return;
        }
        if (status == SC::TOOL_BORROWED) {
            MessageDialog::showError(this, QStringLiteral("无法删除"),
                QStringLiteral("工具「%1」正在借用中，不能删除。\n请先归还后再删除。").arg(toolName));
            return;
        }
    }

    bool confirmed = MessageDialog::showQuestion(this, QStringLiteral("确认删除"),
        QStringLiteral("确定要删除此工具吗？\n删除后工具基础信息将永久移除。"));
    if (!confirmed) return;

    if (MaintenanceService().deleteTool(toolId)) {
        MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("工具已删除"));
        loadAllTools();
    } else {
        MessageDialog::showError(this, QStringLiteral("失败"), QStringLiteral("删除失败，请检查数据库连接"));
    }
}

/**
 * @brief 处理工具
 */
void SystemMaintenancePage::onSubmitTool() {
    QString name = m_dlgName->text().trimmed();
    if (name.isEmpty()) {
        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("工具名称不能为空"));
        return;
    }
    // tool_code为空会导致入库页findByCode找不到工具→入库失败
    QString code = m_dlgCode->text().trimmed();
    if (code.isEmpty()) {
        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("工具编号不能为空"));
        return;
    }
    db::ToolDAO toolDao;
    QJsonObject toolData;
    toolData["toolName"] = name;
    toolData["toolCode"] = code;
    toolData["categoryId"] = m_dlgCategory->currentData().toInt();
    toolData["spec"] = m_dlgSpec->text().trimmed();
    toolData["unit"] = m_dlgUnit->text().trimmed();
    toolData["supplier"] = m_dlgSupplier->currentText().trimmed();
    toolData["recognitionMethod"] = m_dlgRecognition->currentData().toString();
    toolData["documentPath"] = m_dlgDocumentPath;
    toolData["machineGroupId"] = AppConfig::instance().localMachineGroupId();

    bool ok;
    MaintenanceService maintSvc;
    if (m_editToolId == 0) {
        ok = maintSvc.createTool(toolData);
    } else {
        ok = maintSvc.updateTool(m_editToolId, toolData);
    }
    if (ok) {
        m_toolDialog->accept();
        MessageDialog::showSuccess(this, QStringLiteral("成功"), m_editToolId == 0 ? QStringLiteral("工具已新增") : QStringLiteral("工具已更新"));
        loadAllTools();
    } else {
        MessageDialog::showError(this, QStringLiteral("失败"), QStringLiteral("保存失败，请检查数据库连接"));
    }
}

// ==================== 工具对照关系维护面板 ====================

void SystemMaintenancePage::loadAvailablePositions() {
    if (!m_posCabinetCombo) return;

    m_posCabinetCombo->blockSignals(true);
    m_posCabinetCombo->clear();
    db::ToolDAO toolDao;
    QList<ToolCabinet> cabinets = toolDao.allCabinets();
    for (const auto& cab : cabinets) {
        m_posCabinetCombo->addItem(cab.cabinetName, cab.cabinetId);
    }

    m_posLayerCombo->blockSignals(true);
    m_posLayerCombo->clear();
    for (int i = 1; i <= 12; ++i) {
        m_posLayerCombo->addItem(QString::number(i).rightJustified(2, '0'), i);
    }

    m_posPositionCombo->blockSignals(true);
    m_posPositionCombo->clear();
    for (int i = 1; i <= 15; ++i) {
        m_posPositionCombo->addItem(QString::number(i).rightJustified(2, '0'), i);
    }

    m_posCabinetCombo->blockSignals(false);
    m_posLayerCombo->blockSignals(false);
    m_posPositionCombo->blockSignals(false);
}

/**
 * @brief 加载尚未配置位置对照的工具列表
 */
void SystemMaintenancePage::loadUnboundTools() {
    if (!m_posToolCombo) return;
    m_posToolCombo->blockSignals(true);
    m_posToolCombo->clear();
    db::ToolDAO toolDao;
    QJsonArray tools = toolDao.allToolsSimple();
    for (int i = 0; i < tools.size(); ++i) {
        QJsonObject obj = tools[i].toObject();
        QString text = QStringLiteral("%1 - %2").arg(obj["toolCode"].toString(), obj["toolName"].toString());
        m_posToolCombo->addItem(text, obj["toolId"].toInt());
    }
    m_posToolCombo->blockSignals(false);
}

/**
 * @brief 处理位置
 */
void SystemMaintenancePage::onBindPosition() {
    if (!m_posCabinetCombo || !m_posLayerCombo || !m_posPositionCombo || !m_posToolCombo) return;

    int cabinetId = m_posCabinetCombo->currentData().toInt();
    QString layer = m_posLayerCombo->currentText();
    QString position = m_posPositionCombo->currentText();
    int toolId = m_posToolCombo->currentData().toInt();

    if (cabinetId <= 0 || toolId <= 0) {
        MessageDialog::showWarning(this, QStringLiteral("提示"), QStringLiteral("请选择柜体和工具"));
        return;
    }

    // 绑定对照 = INSERT映射记录（一工具可对应多位置）
    db::ToolDAO toolDao;

    // 校验该位置是否已存在映射
    QJsonObject exist = toolDao.checkPositionMappingExists(cabinetId, layer, position);
    if (!exist.isEmpty()) {
        QString occupier = exist["occupier"].toString();
        MessageDialog::showError(this, QStringLiteral("位置已绑定"),
            QStringLiteral("位置「%1-%2-%3」已绑定工具「%4」，不能重复绑定。\n请选择其他位置。")
                .arg(m_posCabinetCombo->currentText().left(1), layer, position, occupier));
        return;
    }

    // INSERT映射记录
    if (toolDao.insertPositionMapping(toolId, cabinetId, layer, position, SC::TOOL_PENDING)) {
        MessageDialog::showSuccess(this, QStringLiteral("绑定成功"),
            QStringLiteral("已建立位置 %1-%2-%3 的对照关系。\n请到「工具入库」完成入库。")
                .arg(m_posCabinetCombo->currentText().left(1), layer, position));
        loadPositionMappings();
    } else {
        MessageDialog::showError(this, QStringLiteral("绑定失败"),
            QStringLiteral("绑定失败，请检查数据库连接"));
    }
}

/**
 * @brief 处理位置
 */
void SystemMaintenancePage::onClearPosition(int mappingId) {
    // 按映射记录ID删除（一工具可有多条映射）
    db::ToolDAO toolDao;

    // 查询该映射的位置和关联工具信息
    QJsonObject info = toolDao.findPositionMappingDetail(mappingId);
    if (info.isEmpty()) return;
    QString layer = info["layer"].toString(), position = info["position"].toString();
    QString toolName = info["toolName"].toString();

    // 获取柜体名称（用于错误提示）
    QString cabName;
    {
        QList<ToolCabinet> cabinets = toolDao.allCabinets();
        for (const auto& cab : cabinets) {
            if (cab.cabinetId == info["cabinetId"].toInt()) { cabName = cab.cabinetName; break; }
        }
    }

    // 校验：该映射表status是否为in_stock/borrowed（已入库/已借出不能清除）
    if (toolDao.isPositionMappingOccupied(mappingId)) {
        MessageDialog::showError(this, QStringLiteral("无法清除"),
            QStringLiteral("位置 %1-%2-%3 当前状态为在库或已借出，不能清除对照关系。").arg(cabName, layer, position));
        return;
    }

    bool confirmed = MessageDialog::showQuestion(this, QStringLiteral("确认清除"),
        QStringLiteral("确定要清除位置 %1-%2-%3 与工具「%4」的对照关系吗？").arg(cabName, layer, position, toolName));
    if (!confirmed) return;

    if (MaintenanceService().removePositionMapping(mappingId)) {
        MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("对照关系已清除"));
        loadPositionMappings();
    } else {
        MessageDialog::showError(this, QStringLiteral("失败"), QStringLiteral("清除失败，请检查数据库连接"));
    }
}

/**
 * @brief 加载位置对照关系列表
 */
void SystemMaintenancePage::loadPositionMappings() {
    if (!m_mappingTable) return;
    m_mappingTable->setRowCount(0);

    // 位置状态用映射表status判断：pending=待入库 in_stock=在库 borrowed=已借出
    db::ToolDAO toolDao;
    QJsonArray mappings = toolDao.findAllPositionMappings();

    int row = 0;
    for (int i = 0; i < mappings.size(); ++i) {
        QJsonObject obj = mappings[i].toObject();
        int mappingId = obj["mappingId"].toInt();
        m_mappingTable->insertRow(row);
        QString posDisplay = StyleHelper::formatPosition(
            obj["cabinetName"].toString(), obj["layer"].toString(), obj["position"].toString());
        m_mappingTable->setItem(row, 0, new QTableWidgetItem(posDisplay));

        m_mappingTable->setItem(row, 1, new QTableWidgetItem(obj["toolName"].toString()));
        m_mappingTable->setItem(row, 2, new QTableWidgetItem(obj["toolCode"].toString()));

        QString posStatus = obj["positionStatus"].toString();
        QString statusText = SC::positionStatusText(posStatus);
        QColor posColor(SC::toolStatusColor(posStatus));
        auto* posStatusItem = new QTableWidgetItem(statusText);
        posStatusItem->setForeground(posColor);
        m_mappingTable->setItem(row, 3, posStatusItem);

        auto* clearBtn = new QPushButton(QStringLiteral("清除"));
        clearBtn->setFixedSize(68, 40);
        clearBtn->setStyleSheet("QPushButton{background:#e74c3c;color:#fff;border:none;border-radius:8px;font-size:14px;font-weight:600;}QPushButton:hover{background:#c0392b;}");
        clearBtn->setCursor(Qt::PointingHandCursor);
        connect(clearBtn, &QPushButton::clicked, this, [this, mappingId] { onClearPosition(mappingId); });
        m_mappingTable->setCellWidget(row, 4, clearBtn);

        m_mappingTable->setRowHeight(row, 56);
        row++;
    }
}

// 任务工具增删改 — 新增/修改对话框(工具类型选择+工具选择+推荐数量)
void SystemMaintenancePage::onAddTaskTool() {
    int typeId = m_taskTypeCombo ? m_taskTypeCombo->currentData().toInt() : 0;
    if (typeId <= 0) {
        MessageDialog::showWarning(this, QStringLiteral("提示"), QStringLiteral("请先选择任务类型"));
        return;
    }

    BaseDialog dlg(this, 460);
    dlg.setDialogTitle(QStringLiteral("新增任务工具"));
    auto* cl = dlg.contentLayout();
    cl->setSpacing(12);

    // 工具类型下拉
    auto* catRow = new QHBoxLayout();
    auto* catLabel = new QLabel(QStringLiteral("工具类型:"));
    catLabel->setFixedWidth(80);
    catLabel->setStyleSheet(StyleHelper::labelText());
    auto* catCombo = new QComboBox();
    catCombo->setStyleSheet(StyleHelper::comboBox());
    catCombo->setMinimumHeight(StyleHelper::Token::ControlHeight);
    catRow->addWidget(catLabel);
    catRow->addWidget(catCombo, 1);
    cl->addLayout(catRow);

    // 工具选择下拉
    auto* toolRow = new QHBoxLayout();
    auto* toolLabel = new QLabel(QStringLiteral("工具:"));
    toolLabel->setFixedWidth(80);
    toolLabel->setStyleSheet(StyleHelper::labelText());
    auto* toolCombo = new QComboBox();
    toolCombo->setStyleSheet(StyleHelper::comboBox());
    toolCombo->setMinimumHeight(StyleHelper::Token::ControlHeight);
    toolRow->addWidget(toolLabel);
    toolRow->addWidget(toolCombo, 1);
    cl->addLayout(toolRow);

    // 推荐数量
    auto* qtyRow = new QHBoxLayout();
    auto* qtyLabel = new QLabel(QStringLiteral("推荐数量:"));
    qtyLabel->setFixedWidth(80);
    qtyLabel->setStyleSheet(StyleHelper::labelText());
    auto* qtySpin = new QSpinBox();
    qtySpin->setRange(1, 99);
    qtySpin->setValue(1);
    qtySpin->setStyleSheet("QSpinBox{font-size:15px;padding:4px 8px;border:1px solid #e0e0e0;border-radius:8px;min-height:36px;}");
    qtyRow->addWidget(qtyLabel);
    qtyRow->addWidget(qtySpin, 1);
    cl->addLayout(qtyRow);

    // 加载工具类型
    db::ToolDAO toolDao;
    QList<ToolCategory> categories = toolDao.allCategories();
    for (const auto& cat : categories) {
        catCombo->addItem(cat.categoryName, cat.categoryId);
    }

    // 工具类型切换 → 加载对应工具
    auto loadTools = [toolCombo](int categoryId) {
        toolCombo->clear();
        if (categoryId <= 0) return;
        db::ToolDAO dao;
        QJsonArray tools = dao.allToolsSimple();
        for (int i = 0; i < tools.size(); ++i) {
            QJsonObject obj = tools[i].toObject();
            // 只加载对应分类的工具
            toolCombo->addItem(QStringLiteral("%1 - %2").arg(obj["toolCode"].toString(), obj["toolName"].toString()), obj["toolId"].toInt());
        }
    };
    if (catCombo->count() > 0) loadTools(catCombo->currentData().toInt());
    connect(catCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [loadTools, catCombo](int) {
        loadTools(catCombo->currentData().toInt());
    });

    // 按钮
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    cancelBtn->setStyleSheet(StyleHelper::buttonDefault());
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setMinimumHeight(StyleHelper::Token::ControlHeight);
    auto* confirmBtn = new QPushButton(QStringLiteral("确认"));
    confirmBtn->setStyleSheet(StyleHelper::buttonPrimary());
    confirmBtn->setCursor(Qt::PointingHandCursor);
    confirmBtn->setMinimumHeight(StyleHelper::Token::ControlHeight);
    auto* btnLayout = dlg.buttonLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(confirmBtn);

    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(confirmBtn, &QPushButton::clicked, this, [this, &dlg, typeId, toolCombo, qtySpin]() {
        int toolId = toolCombo->currentData().toInt();
        if (toolId <= 0) {
            MessageDialog::showWarning(this, QStringLiteral("提示"), QStringLiteral("请选择工具"));
            return;
        }
        int qty = qtySpin->value();

        db::ToolDAO dao;
        // 检查是否已存在
        if (dao.checkTaskTypeToolExists(typeId, toolId)) {
            MessageDialog::showWarning(this, QStringLiteral("提示"), QStringLiteral("该工具已存在于此任务类型中"));
            return;
        }

        if (dao.addTaskTypeTool(typeId, toolId, qty)) {
            MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("任务工具已新增"));
            dlg.accept();
            loadTaskTools(typeId);
        } else {
            MessageDialog::showError(this, QStringLiteral("失败"), QStringLiteral("新增失败，请检查数据库连接"));
        }
    });

    dlg.exec();
}

/**
 * @brief 处理编辑框任务工具
 */
void SystemMaintenancePage::onEditTaskTool(int row) {
    int typeId = m_taskTypeCombo ? m_taskTypeCombo->currentData().toInt() : 0;
    if (typeId <= 0) return;
    int toolId = m_taskToolTable->item(row, 0)->data(Qt::UserRole).toInt();
    if (toolId <= 0) return;

    // 读取当前推荐数量
    int currentQty = m_taskToolTable->item(row, 3)->text().toInt();

    BaseDialog dlg(this, 460);
    dlg.setDialogTitle(QStringLiteral("修改任务工具"));
    auto* cl = dlg.contentLayout();
    cl->setSpacing(12);

    // 显示当前工具（只读）
    auto* infoRow = new QHBoxLayout();
    auto* infoLabel = new QLabel(QStringLiteral("当前工具:"));
    infoLabel->setFixedWidth(80);
    infoLabel->setStyleSheet(StyleHelper::labelText());
    auto* infoValue = new QLabel(QStringLiteral("%1 - %2").arg(
        m_taskToolTable->item(row, 0)->text(), m_taskToolTable->item(row, 1)->text()));
    infoValue->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontLabel, StyleHelper::textField()));
    infoRow->addWidget(infoLabel);
    infoRow->addWidget(infoValue, 1);
    cl->addLayout(infoRow);

    // 推荐数量
    auto* qtyRow = new QHBoxLayout();
    auto* qtyLabel = new QLabel(QStringLiteral("推荐数量:"));
    qtyLabel->setFixedWidth(80);
    qtyLabel->setStyleSheet(StyleHelper::labelText());
    auto* qtySpin = new QSpinBox();
    qtySpin->setRange(1, 99);
    qtySpin->setValue(currentQty);
    qtySpin->setStyleSheet("QSpinBox{font-size:15px;padding:4px 8px;border:1px solid #e0e0e0;border-radius:8px;min-height:36px;}");
    qtyRow->addWidget(qtyLabel);
    qtyRow->addWidget(qtySpin, 1);
    cl->addLayout(qtyRow);

    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    cancelBtn->setStyleSheet(StyleHelper::buttonDefault());
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setMinimumHeight(StyleHelper::Token::ControlHeight);
    auto* confirmBtn = new QPushButton(QStringLiteral("确认"));
    confirmBtn->setStyleSheet(StyleHelper::buttonPrimary());
    confirmBtn->setCursor(Qt::PointingHandCursor);
    confirmBtn->setMinimumHeight(StyleHelper::Token::ControlHeight);
    auto* btnLayout = dlg.buttonLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(confirmBtn);

    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(confirmBtn, &QPushButton::clicked, this, [this, &dlg, typeId, toolId, qtySpin]() {
        int qty = qtySpin->value();
        db::ToolDAO dao;
        if (dao.updateTaskTypeToolQty(typeId, toolId, qty)) {
            MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("任务工具已修改"));
            dlg.accept();
            loadTaskTools(typeId);
        } else {
            MessageDialog::showError(this, QStringLiteral("失败"), QStringLiteral("修改失败，请检查数据库连接"));
        }
    });

    dlg.exec();
}

/**
 * @brief 处理工具
 */
void SystemMaintenancePage::onDeleteTaskTool(int row) {
    int typeId = m_taskTypeCombo ? m_taskTypeCombo->currentData().toInt() : 0;
    if (typeId <= 0) return;
    int toolId = m_taskToolTable->item(row, 0)->data(Qt::UserRole).toInt();
    if (toolId <= 0) return;

    bool confirmed = MessageDialog::showQuestion(this, QStringLiteral("确认删除"),
        QStringLiteral("确定要删除工具「%1」的任务配置吗？").arg(m_taskToolTable->item(row, 1)->text()));
    if (!confirmed) return;

    db::ToolDAO toolDao;
    if (toolDao.deleteTaskTypeTool(typeId, toolId)) {
        MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("任务工具已删除"));
        loadTaskTools(typeId);
    } else {
        MessageDialog::showError(this, QStringLiteral("失败"), QStringLiteral("删除失败，请检查数据库连接"));
    }
}

// 上传工具文档 — 选择doc/docx/pdf文件，复制到AppData目录
void SystemMaintenancePage::onUploadDocument() {
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
    if (srcPath.isEmpty()) return;

    QFileInfo srcInfo(srcPath);
    QString suffix = srcInfo.suffix().toLower();
    if (!SC::TOOL_DOC_SUFFIXES.contains(suffix)) {
        MessageDialog::showWarning(this, QStringLiteral("格式不支持"),
            QStringLiteral("仅支持 %1 格式的文档").arg(SC::TOOL_DOC_SUFFIXES.join(" / ")));
        return;
    }

    qint64 sizeMb = srcInfo.size() / (1024 * 1024);
    if (sizeMb > SC::TOOL_DOC_MAX_SIZE_MB) {
        MessageDialog::showWarning(this, QStringLiteral("文件过大"),
            QStringLiteral("文档大小不能超过 %1MB，当前 %2MB").arg(SC::TOOL_DOC_MAX_SIZE_MB).arg(sizeMb));
        return;
    }

    QString docDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/tool_documents";
    QDir().mkpath(docDir);

    QString destName = QString("%1_%2").arg(
        QDateTime::currentDateTime().toString("yyyyMMddHHmmss"), srcInfo.fileName()
    );
    QString destPath = docDir + "/" + destName;

    if (QFile::exists(destPath)) QFile::remove(destPath);
    if (!QFile::copy(srcPath, destPath)) {
        MessageDialog::showError(this, QStringLiteral("上传失败"),
            QStringLiteral("文件复制失败，请检查磁盘空间或权限"));
        return;
    }

    m_dlgDocumentPath = destPath;
    m_dlgDocumentEdit->setText(srcInfo.fileName() + QStringLiteral("  (%1MB)").arg(sizeMb));
}
