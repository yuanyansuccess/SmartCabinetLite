/**
 * @file SystemSettingsPageTaskConfig.cpp
 * @brief 系统设置-任务配置（任务类型与工具推荐数量配置）
 * @author 袁燕
 *
 * 本文件实现上述功能，成员函数声明见 SystemSettingsPage.h。
 * 数据流与页面既有约定一致：读查询直调 DAO，写操作经 MaintenanceService/ToolDAO。
 */
#include "SystemSettingsPage.h"
#include "utils/StyleHelper.h"
#include "components/BaseDialog.h"
#include "components/MessageDialog.h"
#include "db/ToolDAO.h"
#include "services/MaintenanceService.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>

/**
 * @brief 创建任务配置面板：任务类型下拉、工具清单表格与保存栏
 * @return 面板控件指针，由调用方加入选项卡堆栈；面板数据在构造时立即加载
 */
QWidget* SystemSettingsPage::createTaskConfigPanel() {
    auto* panel = new QFrame();
    panel->setObjectName("taskConfigPanel");
    panel->setStyleSheet(QString("QFrame#taskConfigPanel{background:white;border-radius:12px;border:none;}"));
    auto* layout = new QVBoxLayout(panel);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 16, 20, 16);

    auto* title = new QLabel(QStringLiteral("任务类型-工具数量配置"));
    title->setStyleSheet("font-size:16px;font-weight:700;color:#1a1a2e;margin-bottom:4px;background:transparent;");
    layout->addWidget(title);

    auto* desc = new QLabel(QStringLiteral(
        "为每种任务类型配置对应的工具及推荐借用数量。借用时选择任务类型将自动推荐此处配置的工具和数量。"));
    desc->setStyleSheet("font-size:13px;color:#888;background:transparent;");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    // 任务类型选择行：下拉 + 新增任务工具按钮
    auto* typeRow = new QHBoxLayout();
    auto* typeLabel = new QLabel(QStringLiteral("任务类型："));
    typeLabel->setFixedWidth(80);
    typeLabel->setStyleSheet("font-size:15px;font-weight:600;color:#333;background:transparent;");
    m_taskTypeCombo = new QComboBox();
    m_taskTypeCombo->setMinimumHeight(StyleHelper::Token::ControlHeight);
    m_taskTypeCombo->setStyleSheet(
        "QComboBox { border: 1px solid #d0d0d0; border-radius: 10px; padding: 0 14px; font-size: 16px; color: #1a1a2e; background: white; min-height: 48px; }\n"
        "QComboBox:focus { border-color: #4da3ff; }\n"
        "QComboBox::drop-down { border: none; width: 36px; }\n"
        "QComboBox QAbstractItemView { font-size: 16px; padding: 8px; }");
    m_addTaskToolBtn = new QPushButton(QStringLiteral("+ 新增任务工具"));
    m_addTaskToolBtn->setMinimumHeight(StyleHelper::Token::ControlHeight);
    m_addTaskToolBtn->setMaximumWidth(140);
    m_addTaskToolBtn->setCursor(Qt::PointingHandCursor);
    m_addTaskToolBtn->setStyleSheet(
        "QPushButton { background: #4da3ff; color: white; border: none; border-radius: 10px; padding: 10px 28px; font-size: 14px; font-weight: 700; min-height: 44px; min-width: 100px; }\n"
        "QPushButton:hover { background: #3d8ae0; }\n"
        "QPushButton:pressed { background: #2e7bd6; }\n"
        "QPushButton:disabled { background: #cccccc; color: #888888; }");
    typeRow->addWidget(typeLabel);
    typeRow->addWidget(m_taskTypeCombo, 1);
    typeRow->addWidget(m_addTaskToolBtn);
    layout->addLayout(typeRow);

    // 工具清单表格：列宽策略前3列拉伸，推荐数量/操作列固定
    m_taskToolTable = new QTableWidget();
    m_taskToolTable->setStyleSheet(
        "QTableWidget{font-size:14px;background:white;border:1px solid #f0f0f0;border-radius:10px;outline:none;}\n"
        "QTableWidget::item{padding:6px 10px;color:#333;border-bottom:1px solid #f3f3f3;}\n"
        "QHeaderView::section{background:#f8f9fb;color:#666;font-weight:600;font-size:14px;padding:8px 10px;border:none;border-bottom:2px solid #f0f0f0;}");
    m_taskToolTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_taskToolTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_taskToolTable->verticalHeader()->setVisible(false);
    m_taskToolTable->setColumnCount(5);
    m_taskToolTable->setHorizontalHeaderLabels(
        {QStringLiteral("工具编号"), QStringLiteral("工具名称"), QStringLiteral("规格"),
         QStringLiteral("推荐数量"), QStringLiteral("操作")});
    m_taskToolTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_taskToolTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_taskToolTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_taskToolTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_taskToolTable->setColumnWidth(3, 110);
    m_taskToolTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_taskToolTable->setColumnWidth(4, 170);  // 操作列
    layout->addWidget(m_taskToolTable);

    // 保存栏：与表格右对齐的保存按钮
    auto* saveBar = new QFrame();
    saveBar->setStyleSheet("QFrame{border-top:1px solid #f0f0f0;background:transparent;}");
    auto* saveLayout = new QHBoxLayout(saveBar);
    saveLayout->setContentsMargins(0, 12, 0, 0);
    saveLayout->addStretch();
    m_taskSaveBtn = new QPushButton(QStringLiteral("保存配置"));
    m_taskSaveBtn->setCursor(Qt::PointingHandCursor);
    m_taskSaveBtn->setStyleSheet(
        "QPushButton { background: #4da3ff; color: white; border: none; border-radius: 10px; padding: 10px 24px; font-size: 14px; font-weight: 700; min-height: 44px; }\n"
        "QPushButton:hover { background: #3d8ae0; }\n"
        "QPushButton:pressed { background: #2e7bd6; }");
    saveLayout->addWidget(m_taskSaveBtn);
    layout->addWidget(saveBar);

    // 先加载数据再连信号：避免加载过程中触发 currentIndexChanged 造成重复加载
    loadTaskTypes();
    connect(m_taskTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        int typeId = m_taskTypeCombo->currentData().toInt();
        if (typeId > 0) loadTaskTools(typeId);
    });
    if (m_taskTypeCombo->count() > 0) {
        loadTaskTools(m_taskTypeCombo->currentData().toInt());
    }
    connect(m_addTaskToolBtn, &QPushButton::clicked, this, &SystemSettingsPage::onAddTaskTool);
    connect(m_taskSaveBtn, &QPushButton::clicked, this, &SystemSettingsPage::saveTaskToolConfig);

    return panel;
}

/**
 * @brief 加载任务类型下拉选项（进入页面与切换选项卡时刷新）
 */
void SystemSettingsPage::loadTaskTypes() {
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
 * @brief 加载指定任务类型已配置的工具清单
 * @param typeId 任务类型ID；小于等于0时忽略
 */
void SystemSettingsPage::loadTaskTools(int typeId) {
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
 * @brief 保存任务工具配置：将表格中各工具的推荐数量写回数据库
 */
void SystemSettingsPage::saveTaskToolConfig() {
    int typeId = m_taskTypeCombo ? m_taskTypeCombo->currentData().toInt() : 0;
    if (typeId <= 0) {
        MessageDialog::showWarning(this, QStringLiteral("提示"), QStringLiteral("请先选择任务类型"));
        return;
    }
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

/**
 * @brief 新增任务工具：选择工具类型/工具并填写推荐数量后写入配置
 */
void SystemSettingsPage::onAddTaskTool() {
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
 * @brief 修改任务工具：调整指定行的推荐数量
 * @param row 表格行号（该行第0列存有toolId）
 */
void SystemSettingsPage::onEditTaskTool(int row) {
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
 * @brief 删除任务工具：从当前任务类型中移除指定工具
 * @param row 表格行号（该行第0列存有toolId）
 */
void SystemSettingsPage::onDeleteTaskTool(int row) {
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
