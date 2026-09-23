/**
 * @file    SystemMaintenancePage.cpp
 * @author  袁燕
 * @brief   系统维护页面 — 任务配置/工具维护/工具对照关系维护
 *
 * [V2.03g 2026-06-29] 新建系统维护页面
 * [V2.03h 2026-06-29] 排布对齐系统设置 + 对照关系支持手动选择
 * [V2.03i 2026-06-29] 改为Tab选项卡布局（QStackedWidget），去掉滚动
 *   触屏友好：点击Tab切换页面，不用滑轮滚动
 *   样式对齐系统设置：灰底白选中+主色下划线
 *   三个Tab：任务配置 / 工具维护 / 工具对照关系
 */
#include "SystemMaintenancePage.h"
#include "utils/StyleHelper.h"
#include "components/BaseDialog.h"
#include "components/MessageDialog.h"
#include "common/AppConfig.h"
#include "db/ToolDAO.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QEvent>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QScrollArea>
#include <QFileDialog>           // [V2.03j] 工具文档上传
#include <QStandardPaths>        // [V2.03j] 文档存储路径
#include <QFileInfo>             // [V2.03j] 文件信息
#include <QDir>                  // [V2.03j] 目录创建
#include <QDateTime>             // [V2.03j] 时间戳文件名
#include "common/Constants.h"    // [V2.03j] RECOGNITION_RFID/VISION + TOOL_DOC_SUFFIXES

SystemMaintenancePage::SystemMaintenancePage(QWidget* parent) : QWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);  // [V2.03i] 对齐系统设置外边距
    mainLayout->setSpacing(16);

    // [V2.03i] Tab选项卡栏（对齐系统设置风格）
    createTabBar(mainLayout);

    // [V2.03i] QStackedWidget — 三个面板切换显示，不用滚动
    m_stackedWidget = new QStackedWidget();
    m_stackedWidget->setContentsMargins(0, 0, 0, 0);
    m_stackedWidget->addWidget(createTaskTypePanel());
    m_stackedWidget->addWidget(createToolMaintenancePanel());
    m_stackedWidget->addWidget(createPositionMappingPanel());
    mainLayout->addWidget(m_stackedWidget, 1);

    // 默认显示第一个Tab
    switchTab(0);
}

void SystemMaintenancePage::createTabBar(QVBoxLayout* mainLayout) {
    // [V2.03i] Tab栏容器：对齐系统设置（白底圆角+1px边框）
    auto* tabContainer = new QFrame();
    tabContainer->setAttribute(Qt::WA_StyledBackground, true);
    tabContainer->setStyleSheet("QFrame{background:#fff;border-radius:12px;border:1px solid #f0f0f0;}");
    auto* tabBar = new QHBoxLayout(tabContainer);
    tabBar->setSpacing(4);
    tabBar->setContentsMargins(5, 5, 5, 5);

    QStringList tabLabels = {
        QStringLiteral("任务配置"),
        QStringLiteral("工具维护"),
        QStringLiteral("工具对照关系")
    };

    for (int i = 0; i < tabLabels.size(); ++i) {
        auto* tab = new QLabel(tabLabels[i]);
        tab->setProperty("tabIndex", i);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setAlignment(Qt::AlignCenter);
        tab->installEventFilter(this);
        m_tabLabels.append(tab);
        tabBar->addWidget(tab);
    }
    tabBar->addStretch();
    mainLayout->addWidget(tabContainer);
    updateTabStyles();
}

void SystemMaintenancePage::updateTabStyles() {
    // [V2.03i] Tab样式对齐系统设置：灰底白选中+主色下划线
    for (int i = 0; i < m_tabLabels.size(); ++i) {
        if (i == m_activeTabIndex) {
            m_tabLabels[i]->setStyleSheet(
                QString("font-size:15px;font-weight:600;padding:10px 24px;border-radius:10px 10px 0 0;"
                        "color:%1;background:#fff;min-height:44px;"
                        "border-bottom:3px solid %1;")
                .arg(StyleHelper::primaryColor()));
        } else {
            m_tabLabels[i]->setStyleSheet(
                QString("font-size:15px;font-weight:600;padding:10px 24px;border-radius:10px 10px 0 0;"
                        "color:%1;background:#f0f2f5;min-height:44px;")
                .arg(StyleHelper::textSecondary()));
        }
    }
}

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

bool SystemMaintenancePage::eventFilter(QObject* watched, QEvent* event) {
    // [V2.03i] Tab点击切换 + Hover效果
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
                m_tabLabels[i]->setStyleSheet(
                    QString("font-size:15px;font-weight:600;padding:10px 24px;border-radius:10px 10px 0 0;"
                            "color:%1;background:#f0f2f5;min-height:44px;")
                    .arg(StyleHelper::textSecondary()));
                break;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SystemMaintenancePage::refresh() {
    // [V2.04 2026-06-30 袁燕] 菜单切换到系统维护时重置页面状态
    //   1. 切回第一个Tab（任务配置） 2. 重新加载所有数据
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
QWidget* SystemMaintenancePage::createTaskTypePanel() {
    auto* panel = new QFrame();
    panel->setObjectName("taskPanel");
    panel->setStyleSheet("QFrame#taskPanel{background:white;border-radius:12px;border:none;}");
    auto* layout = new QVBoxLayout(panel);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 16, 20, 16);

    auto* title = new QLabel(QStringLiteral("任务类型-工具数量配置"));
    title->setStyleSheet(QString("font-size:16px;font-weight:700;color:%1;margin-bottom:4px;background:transparent;").arg(StyleHelper::textColor()));
    layout->addWidget(title);

    auto* desc = new QLabel(QStringLiteral("为每种任务类型配置对应的工具及推荐借用数量。借用时选择任务类型将自动推荐此处配置的工具和数量。"));
    desc->setStyleSheet("font-size:13px;color:#888;background:transparent;");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    auto* typeRow = new QHBoxLayout();
    auto* typeLabel = new QLabel(QStringLiteral("任务类型："));
    typeLabel->setStyleSheet("font-size:15px;font-weight:600;color:#333;background:transparent;");
    typeLabel->setFixedWidth(80);
    m_taskTypeCombo = new QComboBox();
    m_taskTypeCombo->setStyleSheet(StyleHelper::comboBox());
    m_taskTypeCombo->setMinimumHeight(44);
    typeRow->addWidget(typeLabel);
    typeRow->addWidget(m_taskTypeCombo, 1);
    // [V2.03k] 新增任务工具按钮（在任务类型旁边）
    auto* addTaskToolBtn = new QPushButton(QStringLiteral("+ 新增任务工具"));
    addTaskToolBtn->setStyleSheet(StyleHelper::buttonPrimary());
    addTaskToolBtn->setCursor(Qt::PointingHandCursor);
    addTaskToolBtn->setMinimumHeight(44);
    addTaskToolBtn->setMaximumWidth(140);
    connect(addTaskToolBtn, &QPushButton::clicked, this, &SystemMaintenancePage::onAddTaskTool);
    typeRow->addWidget(addTaskToolBtn);
    layout->addLayout(typeRow);

    // [V2.03k] 列改为：工具编号/工具名称/规格/推荐数量/操作(修改+删除)
    m_taskToolTable = new QTableWidget();
    m_taskToolTable->setColumnCount(5);
    m_taskToolTable->setHorizontalHeaderLabels({
        QStringLiteral("工具编号"), QStringLiteral("工具名称"), QStringLiteral("规格"),
        QStringLiteral("推荐数量"), QStringLiteral("操作")
    });
    m_taskToolTable->verticalHeader()->setVisible(false);
    m_taskToolTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_taskToolTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    // [V2.03l 2026-06-30] 小米工程师优化：表格字体14px+行高56px，看清楚
    m_taskToolTable->setStyleSheet(
        "QTableWidget{font-size:14px;background:white;border:1px solid #f0f0f0;border-radius:10px;outline:none;}"
        "QTableWidget::item{padding:6px 10px;color:#333;border-bottom:1px solid #f3f3f3;}"
        "QHeaderView::section{background:#f8f9fb;color:#666;font-weight:600;font-size:14px;padding:8px 10px;border:none;border-bottom:2px solid #f0f0f0;}");
    m_taskToolTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_taskToolTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_taskToolTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_taskToolTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_taskToolTable->setColumnWidth(3, 110);
    m_taskToolTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_taskToolTable->setColumnWidth(4, 170);  // [V2.03l] 操作列
    layout->addWidget(m_taskToolTable, 1);

    auto* saveBar = new QFrame();
    saveBar->setStyleSheet("QFrame{border-top:1px solid #f0f0f0;background:transparent;}");
    auto* saveBarLayout = new QHBoxLayout(saveBar);
    saveBarLayout->setContentsMargins(0, 12, 0, 0);
    saveBarLayout->addStretch();
    auto* saveBtn = new QPushButton(QStringLiteral("保存配置"));
    saveBtn->setStyleSheet(StyleHelper::settingSaveBtn());
    saveBtn->setCursor(Qt::PointingHandCursor);
    connect(saveBtn, &QPushButton::clicked, this, &SystemMaintenancePage::saveTaskToolConfig);
    saveBarLayout->addWidget(saveBtn);
    layout->addWidget(saveBar);

    loadTaskTypes();
    connect(m_taskTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        int typeId = m_taskTypeCombo->currentData().toInt();
        if (typeId > 0) loadTaskTools(typeId);
    });
    if (m_taskTypeCombo->count() > 0) {
        loadTaskTools(m_taskTypeCombo->currentData().toInt());
    }

    return panel;
}

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
        // [V2.03k] 列顺序：工具编号/工具名称/规格/推荐数量/操作
        m_taskToolTable->setItem(row, 0, new QTableWidgetItem(obj["toolCode"].toString()));  // 工具编号
        m_taskToolTable->setItem(row, 1, new QTableWidgetItem(obj["toolName"].toString()));  // 工具名称
        m_taskToolTable->setItem(row, 2, new QTableWidgetItem(obj["spec"].toString()));      // 规格
        auto* qtyItem = new QTableWidgetItem(QString::number(obj["recommendedQty"].toInt())); // 推荐数量
        qtyItem->setTextAlignment(Qt::AlignCenter);
        m_taskToolTable->setItem(row, 3, qtyItem);

        // [V2.03k] 操作列：修改 + 删除
        auto* opWidget = new QWidget();
        opWidget->setStyleSheet("background:transparent;");
        auto* opLayout = new QHBoxLayout(opWidget);
        opLayout->setContentsMargins(4, 4, 4, 4);
        opLayout->setSpacing(6);

        auto* editBtn = new QPushButton(QStringLiteral("修改"));
        editBtn->setFixedSize(60, 40);  // [V2.03l] 56x36→60x40小米标准
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

        // [V2.03k] 存储toolId到行数据，供修改/删除使用
        m_taskToolTable->item(row, 0)->setData(Qt::UserRole, toolId);
        m_taskToolTable->setRowHeight(row, 56);  // [V2.03l] 52→56小米工程师标准
        row++;
    }
}

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
        if (!toolDao.updateTaskTypeToolQty(typeId, toolId, qty)) { ok = false; break; }
    }
    if (ok) {
        MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("任务类型工具配置已保存"));
    } else {
        MessageDialog::showError(this, QStringLiteral("失败"), QStringLiteral("保存失败，请重试"));
    }
}

// ==================== 工具维护面板 ====================
QWidget* SystemMaintenancePage::createToolMaintenancePanel() {
    auto* panel = new QFrame();
    panel->setObjectName("toolPanel");
    panel->setStyleSheet("QFrame#toolPanel{background:white;border-radius:12px;border:none;}");
    auto* layout = new QVBoxLayout(panel);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 16, 20, 16);

    auto* title = new QLabel(QStringLiteral("工具维护"));
    title->setStyleSheet(QString("font-size:16px;font-weight:700;color:%1;margin-bottom:4px;background:transparent;").arg(StyleHelper::textColor()));
    layout->addWidget(title);

    auto* desc = new QLabel(QStringLiteral("管理系统所有工具的基础信息。删除工具前需确保该工具无未归还的借用记录。"));
    desc->setStyleSheet("font-size:13px;color:#888;background:transparent;");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    auto* btnRow = new QHBoxLayout();
    auto* addBtn = new QPushButton(QStringLiteral("+ 新增工具"));
    addBtn->setStyleSheet(StyleHelper::buttonPrimary());
    addBtn->setCursor(Qt::PointingHandCursor);
    addBtn->setMinimumHeight(44);
    addBtn->setMaximumWidth(160);
    btnRow->addWidget(addBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    m_toolTable = new QTableWidget();
    m_toolTable->setColumnCount(7);
    m_toolTable->setHorizontalHeaderLabels({
        QStringLiteral("编号"), QStringLiteral("名称"), QStringLiteral("分类"),
        QStringLiteral("规格"), QStringLiteral("单位"), QStringLiteral("状态"), QStringLiteral("操作")
    });
    m_toolTable->verticalHeader()->setVisible(false);
    m_toolTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_toolTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    // [V2.03l] 小米工程师优化：表格字体14px+行高56px
    m_toolTable->setStyleSheet(
        "QTableWidget{font-size:14px;background:white;border:1px solid #f0f0f0;border-radius:10px;outline:none;}"
        "QTableWidget::item{padding:6px 10px;color:#333;border-bottom:1px solid #f3f3f3;}"
        "QHeaderView::section{background:#f8f9fb;color:#666;font-weight:600;font-size:14px;padding:8px 10px;border:none;border-bottom:2px solid #f0f0f0;}");
    for (int i = 0; i < 6; i++) {
        m_toolTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
    }
    m_toolTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Fixed);
    m_toolTable->setColumnWidth(6, 180);  // [V2.03k] 160→180操作列更宽
    layout->addWidget(m_toolTable, 1);

    auto* saveBar = new QFrame();
    saveBar->setStyleSheet("QFrame{border-top:1px solid #f0f0f0;background:transparent;}");
    auto* saveBarLayout = new QHBoxLayout(saveBar);
    saveBarLayout->setContentsMargins(0, 12, 0, 0);
    saveBarLayout->addStretch();
    auto* refreshBtn = new QPushButton(QStringLiteral("保存配置"));
    refreshBtn->setStyleSheet(StyleHelper::settingSaveBtn());
    refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(refreshBtn, &QPushButton::clicked, this, [this]() {
        loadAllTools();
        MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("工具列表已刷新"));
    });
    saveBarLayout->addWidget(refreshBtn);
    layout->addWidget(saveBar);

    loadAllTools();
    connect(addBtn, &QPushButton::clicked, this, &SystemMaintenancePage::onAddTool);

    return panel;
}

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
        QString statusText;
        QColor statusColor("#999999");
        if (status == "in_stock")      { statusText = QStringLiteral("在库");   statusColor = QColor("#43a047"); }
        else if (status == "borrowed")   { statusText = QStringLiteral("已借出"); statusColor = QColor("#f57c00"); }
        else if (status == "checked_out"){ statusText = QStringLiteral("已出库"); statusColor = QColor("#e53935"); }
        else if (status == "maintenance"){ statusText = QStringLiteral("维护中"); statusColor = QColor("#999999"); }
        else if (status == "pending")    { statusText = QStringLiteral("待入库"); statusColor = QColor("#1890ff"); }
        else statusText = status;
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

void SystemMaintenancePage::onAddTool() {
    m_editToolId = 0;
    // [V2.03k] 复用对话框创建逻辑
    if (!m_toolDialog) ensureToolDialogCreated();
    m_toolDialog->setDialogTitle(QStringLiteral("新增工具"));
    m_dlgName->clear(); m_dlgCode->clear(); m_dlgCategory->setCurrentIndex(0);
    m_dlgSpec->clear(); m_dlgUnit->setText(QStringLiteral("把"));
    m_dlgSupplier->setCurrentIndex(0); m_dlgRecognition->setCurrentIndex(0);
    m_dlgDocumentPath.clear(); m_dlgDocumentEdit->clear();
    m_toolDialog->exec();
}

// [V2.03k 2026-06-29] 确保工具对话框已创建（不弹出），onAddTool和onEditTool复用
void SystemMaintenancePage::ensureToolDialogCreated() {
    if (m_toolDialog) return;
    m_toolDialog = new BaseDialog(this, 480);
    m_toolDialog->setDialogTitle(QStringLiteral("新增工具"));
    auto* cl = m_toolDialog->contentLayout();
    cl->setSpacing(12);

    m_dlgName = new QLineEdit(); m_dlgName->setStyleSheet(StyleHelper::lineEdit()); m_dlgName->setMinimumHeight(44);
    m_dlgCode = new QLineEdit(); m_dlgCode->setStyleSheet(StyleHelper::lineEdit()); m_dlgCode->setMinimumHeight(44);
    m_dlgCategory = new QComboBox(); m_dlgCategory->setEditable(true); m_dlgCategory->setStyleSheet(StyleHelper::comboBox()); m_dlgCategory->setMinimumHeight(44);
    m_dlgSpec = new QLineEdit(); m_dlgSpec->setStyleSheet(StyleHelper::lineEdit()); m_dlgSpec->setMinimumHeight(44);
    m_dlgUnit = new QLineEdit(); m_dlgUnit->setStyleSheet(StyleHelper::lineEdit()); m_dlgUnit->setMinimumHeight(44);
    m_dlgSupplier = new QComboBox(); m_dlgSupplier->setEditable(true); m_dlgSupplier->setStyleSheet(StyleHelper::comboBox()); m_dlgSupplier->setMinimumHeight(44);
    m_dlgSupplier->addItem(QStringLiteral("史丹利工具"));
    m_dlgSupplier->addItem(QStringLiteral("博世电动工具"));
    m_dlgSupplier->addItem(QStringLiteral("牧田电动工具"));
    m_dlgSupplier->addItem(QStringLiteral("世达工具"));
    m_dlgSupplier->addItem(QStringLiteral("其他"));
    m_dlgRecognition = new QComboBox(); m_dlgRecognition->setStyleSheet(StyleHelper::comboBox()); m_dlgRecognition->setMinimumHeight(44);
    m_dlgRecognition->addItem(QStringLiteral("RFID识别"), SC::RECOGNITION_RFID);
    m_dlgRecognition->addItem(QStringLiteral("视觉识别"), SC::RECOGNITION_VISION);
    m_dlgDocumentEdit = new QLineEdit(); m_dlgDocumentEdit->setStyleSheet(StyleHelper::lineEdit()); m_dlgDocumentEdit->setMinimumHeight(44); m_dlgDocumentEdit->setReadOnly(true);
    m_dlgDocumentEdit->setPlaceholderText(QStringLiteral("支持 .doc / .docx / .pdf，最大50MB"));
    m_dlgUploadBtn = new QPushButton(QStringLiteral("上传"));
    m_dlgUploadBtn->setStyleSheet(StyleHelper::buttonOutline());
    m_dlgUploadBtn->setCursor(Qt::PointingHandCursor);
    m_dlgUploadBtn->setFixedHeight(44);
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
        lb->setStyleSheet("font-size:15px;font-weight:600;color:#333;background:transparent;");
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
        lb->setStyleSheet("font-size:15px;font-weight:600;color:#333;background:transparent;");
        row->addWidget(lb);
        row->addWidget(m_dlgDocumentEdit, 1);
        row->addWidget(m_dlgUploadBtn);
        cl->addLayout(row);
    }

    auto* saveBtn = new QPushButton(QStringLiteral("保存"));
    saveBtn->setStyleSheet(StyleHelper::buttonPrimary());
    saveBtn->setCursor(Qt::PointingHandCursor);
    saveBtn->setMinimumHeight(44);
    connect(saveBtn, &QPushButton::clicked, this, &SystemMaintenancePage::onSubmitTool);
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    cancelBtn->setStyleSheet(StyleHelper::buttonDefault());
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setMinimumHeight(44);
    connect(cancelBtn, &QPushButton::clicked, this, [this]() { m_toolDialog->reject(); });
    auto* btnLayout = m_toolDialog->buttonLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(saveBtn);
}

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

void SystemMaintenancePage::onDeleteTool(int toolId) {
    // 按工具状态判断是否可删除
    //   在库(in_stock)/已借出(borrowed) → 不能删除（工具还有物理实体在系统中）
    //   待入库(pending)/已出库(checked_out)/维护中(maintenance) → 可以删除
    db::ToolDAO toolDao;
    QJsonObject t = toolDao.findById(toolId);
    if (!t.isEmpty()) {
        QString status = t["status"].toString();
        QString toolName = t["toolName"].toString();
        if (status == "in_stock") {
            MessageDialog::showError(this, QStringLiteral("无法删除"),
                QStringLiteral("工具「%1」正在库中，不能删除。\n请先出库后再删除。").arg(toolName));
            return;
        }
        if (status == "borrowed") {
            MessageDialog::showError(this, QStringLiteral("无法删除"),
                QStringLiteral("工具「%1」正在借用中，不能删除。\n请先归还后再删除。").arg(toolName));
            return;
        }
    }

    bool confirmed = MessageDialog::showQuestion(this, QStringLiteral("确认删除"),
        QStringLiteral("确定要删除此工具吗？\n删除后工具基础信息将永久移除。"));
    if (!confirmed) return;

    if (toolDao.deleteToolFully(toolId)) {
        MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("工具已删除"));
        loadAllTools();
    } else {
        MessageDialog::showError(this, QStringLiteral("失败"), QStringLiteral("删除失败，请检查数据库连接"));
    }
}

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
    if (m_editToolId == 0) {
        ok = toolDao.insertToolFull(toolData);
    } else {
        ok = toolDao.updateToolFull(m_editToolId, toolData);
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
QWidget* SystemMaintenancePage::createPositionMappingPanel() {
    auto* panel = new QFrame();
    panel->setObjectName("mappingPanel");
    panel->setStyleSheet("QFrame#mappingPanel{background:white;border-radius:12px;border:none;}");
    auto* layout = new QVBoxLayout(panel);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 16, 20, 16);

    auto* title = new QLabel(QStringLiteral("工具对照关系维护"));
    title->setStyleSheet(QString("font-size:16px;font-weight:700;color:%1;margin-bottom:4px;background:transparent;").arg(StyleHelper::textColor()));
    layout->addWidget(title);

    auto* desc = new QLabel(QStringLiteral("建立工具与存放位置的对照关系。一个工具可对应多个位置，一个位置只对应一个工具。绑定后到「工具入库」完成入库。"));
    desc->setStyleSheet("font-size:13px;color:#888;background:transparent;");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    // 位置选择行：柜/层/位 下拉 + 工具下拉 + 绑定按钮
    auto* bindRow1 = new QHBoxLayout();
    bindRow1->setSpacing(8);

    auto* cabLabel = new QLabel(QStringLiteral("柜体:"));
    cabLabel->setStyleSheet("font-size:15px;font-weight:600;color:#333;background:transparent;");
    cabLabel->setFixedWidth(50);
    m_posCabinetCombo = new QComboBox();
    m_posCabinetCombo->setStyleSheet(StyleHelper::comboBox());
    m_posCabinetCombo->setMinimumHeight(44);
    bindRow1->addWidget(cabLabel);
    bindRow1->addWidget(m_posCabinetCombo, 1);

    auto* layerLabel = new QLabel(QStringLiteral("层号:"));
    layerLabel->setStyleSheet("font-size:15px;font-weight:600;color:#333;background:transparent;");
    layerLabel->setFixedWidth(50);
    m_posLayerCombo = new QComboBox();
    m_posLayerCombo->setStyleSheet(StyleHelper::comboBox());
    m_posLayerCombo->setMinimumHeight(44);
    bindRow1->addWidget(layerLabel);
    bindRow1->addWidget(m_posLayerCombo, 1);

    auto* posLabel = new QLabel(QStringLiteral("位号:"));
    posLabel->setStyleSheet("font-size:15px;font-weight:600;color:#333;background:transparent;");
    posLabel->setFixedWidth(50);
    m_posPositionCombo = new QComboBox();
    m_posPositionCombo->setStyleSheet(StyleHelper::comboBox());
    m_posPositionCombo->setMinimumHeight(44);
    bindRow1->addWidget(posLabel);
    bindRow1->addWidget(m_posPositionCombo, 1);

    layout->addLayout(bindRow1);

    auto* bindRow2 = new QHBoxLayout();
    bindRow2->setSpacing(8);

    auto* toolLabel = new QLabel(QStringLiteral("工具:"));
    toolLabel->setStyleSheet("font-size:15px;font-weight:600;color:#333;background:transparent;");
    toolLabel->setFixedWidth(50);
    m_posToolCombo = new QComboBox();
    m_posToolCombo->setStyleSheet(StyleHelper::comboBox());
    m_posToolCombo->setMinimumHeight(44);
    bindRow2->addWidget(toolLabel);
    bindRow2->addWidget(m_posToolCombo, 1);

    auto* bindBtn = new QPushButton(QStringLiteral("绑定对照"));
    bindBtn->setStyleSheet(StyleHelper::buttonPrimary());
    bindBtn->setCursor(Qt::PointingHandCursor);
    bindBtn->setMinimumHeight(44);
    bindBtn->setMaximumWidth(140);
    connect(bindBtn, &QPushButton::clicked, this, &SystemMaintenancePage::onBindPosition);
    bindRow2->addWidget(bindBtn);

    layout->addLayout(bindRow2);

    // 对照关系表格
    m_mappingTable = new QTableWidget();
    m_mappingTable->setColumnCount(5);
    m_mappingTable->setHorizontalHeaderLabels({
        QStringLiteral("位置(柜-层-位)"), QStringLiteral("当前工具"), QStringLiteral("工具编号"),
        QStringLiteral("状态"), QStringLiteral("操作")
    });
    m_mappingTable->verticalHeader()->setVisible(false);
    m_mappingTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_mappingTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    // [V2.03l] 小米工程师优化
    m_mappingTable->setStyleSheet(
        "QTableWidget{font-size:14px;background:white;border:1px solid #f0f0f0;border-radius:10px;outline:none;}"
        "QTableWidget::item{padding:6px 10px;color:#333;border-bottom:1px solid #f3f3f3;}"
        "QHeaderView::section{background:#f8f9fb;color:#666;font-weight:600;font-size:14px;padding:8px 10px;border:none;border-bottom:2px solid #f0f0f0;}");
    m_mappingTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_mappingTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_mappingTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_mappingTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_mappingTable->setColumnWidth(3, 90);  // [V2.03k] 80→90状态列更宽
    m_mappingTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_mappingTable->setColumnWidth(4, 120);  // [V2.03k] 100→120操作列更宽
    layout->addWidget(m_mappingTable, 1);

    auto* saveBar = new QFrame();
    saveBar->setStyleSheet("QFrame{border-top:1px solid #f0f0f0;background:transparent;}");
    auto* saveBarLayout = new QHBoxLayout(saveBar);
    saveBarLayout->setContentsMargins(0, 12, 0, 0);
    saveBarLayout->addStretch();
    auto* refreshBtn = new QPushButton(QStringLiteral("保存配置"));
    refreshBtn->setStyleSheet(StyleHelper::settingSaveBtn());
    refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(refreshBtn, &QPushButton::clicked, this, [this]() {
        loadPositionMappings();
        loadAvailablePositions();
        loadUnboundTools();
        MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("对照关系已刷新"));
    });
    saveBarLayout->addWidget(refreshBtn);
    layout->addWidget(saveBar);

    loadAvailablePositions();
    loadUnboundTools();
    loadPositionMappings();

    return panel;
}

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
    if (toolDao.insertPositionMapping(toolId, cabinetId, layer, position, "pending")) {
        MessageDialog::showSuccess(this, QStringLiteral("绑定成功"),
            QStringLiteral("已建立位置 %1-%2-%3 的对照关系。\n请到「工具入库」完成入库。")
                .arg(m_posCabinetCombo->currentText().left(1), layer, position));
        loadPositionMappings();
    } else {
        MessageDialog::showError(this, QStringLiteral("绑定失败"),
            QStringLiteral("绑定失败，请检查数据库连接"));
    }
}

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

    if (toolDao.deletePositionMapping(mappingId)) {
        MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("对照关系已清除"));
        loadPositionMappings();
    } else {
        MessageDialog::showError(this, QStringLiteral("失败"), QStringLiteral("清除失败，请检查数据库连接"));
    }
}

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
        QString statusText;
        QColor posColor("#999999");
        if (posStatus.isEmpty())           { statusText = QStringLiteral("待入库"); posColor = QColor("#1890ff"); }
        else if (posStatus == "in_stock")  { statusText = QStringLiteral("在库");   posColor = QColor("#43a047"); }
        else if (posStatus == "borrowed")   { statusText = QStringLiteral("已借出"); posColor = QColor("#f57c00"); }
        else if (posStatus == "checked_out"){ statusText = QStringLiteral("已出库"); posColor = QColor("#e53935"); }
        else if (posStatus == "pending")   { statusText = QStringLiteral("待入库"); posColor = QColor("#1890ff"); }
        else statusText = posStatus;
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
    catLabel->setStyleSheet("font-size:15px;font-weight:600;color:#333;background:transparent;");
    auto* catCombo = new QComboBox();
    catCombo->setStyleSheet(StyleHelper::comboBox());
    catCombo->setMinimumHeight(44);
    catRow->addWidget(catLabel);
    catRow->addWidget(catCombo, 1);
    cl->addLayout(catRow);

    // 工具选择下拉
    auto* toolRow = new QHBoxLayout();
    auto* toolLabel = new QLabel(QStringLiteral("工具:"));
    toolLabel->setFixedWidth(80);
    toolLabel->setStyleSheet("font-size:15px;font-weight:600;color:#333;background:transparent;");
    auto* toolCombo = new QComboBox();
    toolCombo->setStyleSheet(StyleHelper::comboBox());
    toolCombo->setMinimumHeight(44);
    toolRow->addWidget(toolLabel);
    toolRow->addWidget(toolCombo, 1);
    cl->addLayout(toolRow);

    // 推荐数量
    auto* qtyRow = new QHBoxLayout();
    auto* qtyLabel = new QLabel(QStringLiteral("推荐数量:"));
    qtyLabel->setFixedWidth(80);
    qtyLabel->setStyleSheet("font-size:15px;font-weight:600;color:#333;background:transparent;");
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
    cancelBtn->setMinimumHeight(44);
    auto* confirmBtn = new QPushButton(QStringLiteral("确认"));
    confirmBtn->setStyleSheet(StyleHelper::buttonPrimary());
    confirmBtn->setCursor(Qt::PointingHandCursor);
    confirmBtn->setMinimumHeight(44);
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
    infoLabel->setStyleSheet("font-size:15px;font-weight:600;color:#333;background:transparent;");
    auto* infoValue = new QLabel(QStringLiteral("%1 - %2").arg(
        m_taskToolTable->item(row, 0)->text(), m_taskToolTable->item(row, 1)->text()));
    infoValue->setStyleSheet("font-size:15px;color:#555;background:transparent;");
    infoRow->addWidget(infoLabel);
    infoRow->addWidget(infoValue, 1);
    cl->addLayout(infoRow);

    // 推荐数量
    auto* qtyRow = new QHBoxLayout();
    auto* qtyLabel = new QLabel(QStringLiteral("推荐数量:"));
    qtyLabel->setFixedWidth(80);
    qtyLabel->setStyleSheet("font-size:15px;font-weight:600;color:#333;background:transparent;");
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
    cancelBtn->setMinimumHeight(44);
    auto* confirmBtn = new QPushButton(QStringLiteral("确认"));
    confirmBtn->setStyleSheet(StyleHelper::buttonPrimary());
    confirmBtn->setCursor(Qt::PointingHandCursor);
    confirmBtn->setMinimumHeight(44);
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

// [V2.03j 2026-06-29] 上传工具文档 — 选择doc/docx/pdf文件，复制到AppData目录
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
