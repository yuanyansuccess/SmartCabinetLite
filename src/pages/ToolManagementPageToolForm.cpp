/**
 * @file ToolManagementPageToolForm.cpp
 * @brief 工具管理-工具新增与编辑表单（新增/编辑/删除/提交）
 * @author 袁燕
 *
 * 本文件实现上述功能，成员函数声明见 ToolManagementPage.h。
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

/**
 * @brief 处理工具
 */
void ToolManagementPage::onAddTool() {
    m_editToolId = 0;
    if (!m_toolDialog) {
        // BaseDialog统一圆角无边框风格
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

/**
 * @brief 处理编辑框工具
 */
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

/**
 * @brief 处理工具
 */
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

/**
 * @brief 处理工具
 */
void ToolManagementPage::onSubmitTool() {
    QString name = m_dlgName->text().trimmed();
    if (name.isEmpty()) { MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("工具名称不能为空")); return; }

    ToolInfo info;
    info.toolName = name;
    info.toolCode = m_dlgCode->text().trimmed();
    info.categoryId = m_dlgCategory->currentData().toInt();
    info.position = m_dlgPosition->text().trimmed();
    // 数量恒为1（一个位置=一个工具），不从输入框读取
    info.totalQty = 1;
    info.currentQty = 1;
    info.spec = m_dlgSpec->text().trimmed();
    info.cabinetId = 0;
    // 从AppConfig读取本机机组ID，系统设置页面配置
    info.machineGroupId = AppConfig::instance().localMachineGroupId();
    info.status = SC::TOOL_IN_STOCK;

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
