/**
 * @file ToolManagementPageDetail.cpp
 * @brief 工具管理-工具详情对话框（详情Tab/操作记录Tab，含ToolDetailCtx定义）
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

// 查看工具详情：美观弹窗展示所有字段
// BaseDialog统一圆角无边框风格
// 接收ToolInfo（含位置信息），操作记录按位置过滤
// 同一工具在多个位置，每个位置的详情只显示该位置相关的操作记录
/** 工具详情对话框会话上下文：聚合对话框控件与权威工具数据 */
struct ToolDetailCtx {
    BaseDialog* dlg = nullptr;
    QTabWidget* tabWidget = nullptr;
    ToolInfo t;               // 补全位置信息后的权威工具数据
    db::RecordDAO recDao;     // 操作记录查询（Tab1最后操作与Tab2记录共用）
};

/** 工具详情 — 统一BaseDialog圆角风格，Tab选项卡式（详情/操作记录/文档） */
void ToolManagementPage::onDetailTool(const ToolInfo& toolInfo) {
    ToolDetailCtx ctx;
    ctx.t = resolveToolInfo(toolInfo);
    if (ctx.t.toolId == 0) return;

    // 对话框加宽到900，确保操作记录表格列宽不截断
    ctx.dlg = new BaseDialog(this, 900);
    ctx.dlg->setDialogTitle(QStringLiteral("工具详情"));
    ctx.dlg->setAttribute(Qt::WA_DeleteOnClose);

    auto* cl = ctx.dlg->contentLayout();
    cl->setSpacing(0);

    buildDetailHeader(cl, ctx.t);
    ctx.tabWidget = createDetailTabWidget();
    buildToolDetailTab(ctx);
    buildOperationRecordTab(ctx);
    finalizeDetailDialog(ctx);
}

/** 补全工具数据：getToolById取基础字段，位置信息以映射表（位置维度查询）为权威覆盖 */
ToolInfo ToolManagementPage::resolveToolInfo(const ToolInfo& toolInfo) {
    ToolController ctrl;
    // toolInfo来自findAllTools位置维度查询，含位置；getToolById补全其他字段
    ToolInfo t = ctrl.getToolById(toolInfo.toolId);
    if (t.toolId == 0) return t;
    // 用位置维度查询的位置信息覆盖（findAllTools的位置来自映射表，是权威数据源）
    // 必须覆盖mappingId，否则getToolById返回mappingId=0
    // → 操作记录走兜底findByToolId → 显示所有位置的借用记录（不按位置过滤）
    t.mappingId = toolInfo.mappingId;
    t.cabinetId = toolInfo.cabinetId;
    t.cabinetName = toolInfo.cabinetName;
    t.layer = toolInfo.layer;
    t.position = toolInfo.position;
    t.status = toolInfo.status;  // 映射表status（位置占用状态）
    return t;
}

/** 构建对话框顶部标题区：工具名称 + 状态标签 + 分隔线 */
void ToolManagementPage::buildDetailHeader(QVBoxLayout* cl, const ToolInfo& t) {
    auto* headerRow = new QHBoxLayout();
    headerRow->setSpacing(16);

    auto* nameLabel = new QLabel(t.toolName.isEmpty() ? QStringLiteral("未命名工具") : t.toolName);
    nameLabel->setStyleSheet("font-size:22px;font-weight:700;color:#1a1a2e;background:transparent;");
    headerRow->addWidget(nameLabel);

    QString statusText = SC::toolStatusText(t.status, true);   // [等价保留] 本页borrowed历史文案"已借用"
    QString statusBg = SC::toolStatusColor(t.status);

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
}

/** 创建详情对话框TabWidget（统一样式） */
QTabWidget* ToolManagementPage::createDetailTabWidget() {
    auto* tabWidget = new QTabWidget();
    tabWidget->setStyleSheet(QString(
        "QTabWidget::pane { border: none; background: transparent; }"
        "QTabBar::tab { background: #f0f2f5; color: #666; padding: 10px 24px; "
        "  font-size: 15px; font-weight: 600; border-radius: 10px 10px 0 0; "
        "  min-height: 40px; margin-right: 4px; }"
        "QTabBar::tab:selected { background: white; color: %1; border-bottom: 3px solid %1; }"
        "QTabBar::tab:hover { background: #e8f0fe; }"
    ).arg(StyleHelper::primaryColor()));
    return tabWidget;
}

/** 构建Tab1工具详情：信息卡片网格（基础字段+最近出入库操作） */
void ToolManagementPage::buildToolDetailTab(ToolDetailCtx& ctx) {
    const ToolInfo& t = ctx.t;

    auto* detailTab = new QWidget();
    auto* detailLayout = new QVBoxLayout(detailTab);
    detailLayout->setContentsMargins(0, 12, 0, 0);
    detailLayout->setSpacing(0);

    // infoCard外包QScrollArea，已出库工具字段多时可滚动
    auto* detailScroll = new QScrollArea();
    detailScroll->setWidgetResizable(true);
    detailScroll->setFrameShape(QFrame::NoFrame);
    detailScroll->setStyleSheet("QScrollArea{background:transparent;border:none;}");

    // 信息卡片区
    auto* infoCard = new QFrame();
    infoCard->setStyleSheet(StyleHelper::cardLight());
    auto* infoLayout = new QGridLayout(infoCard);
    infoLayout->setContentsMargins(20, 20, 20, 20);
    infoLayout->setHorizontalSpacing(40);
    infoLayout->setVerticalSpacing(14);

    int row = 0;
    infoLayout->addWidget(FormFactory::fieldLabel(QStringLiteral("工具编号")), row, 0);
    infoLayout->addWidget(FormFactory::fieldValue(t.toolCode.isEmpty() ? QStringLiteral("-") : t.toolCode), row++, 1);

    infoLayout->addWidget(FormFactory::fieldLabel(QStringLiteral("规格型号")), row, 0);
    infoLayout->addWidget(FormFactory::fieldValue(t.spec.isEmpty() ? QStringLiteral("-") : t.spec), row++, 1);

    infoLayout->addWidget(FormFactory::fieldLabel(QStringLiteral("所属分类")), row, 0);
    infoLayout->addWidget(FormFactory::fieldValue(t.categoryName.isEmpty() ? QStringLiteral("未分类") : t.categoryName), row++, 1);

    infoLayout->addWidget(FormFactory::fieldLabel(QStringLiteral("所属机组")), row, 0);
    infoLayout->addWidget(FormFactory::fieldValue(t.machineGroupName.isEmpty() ? QStringLiteral("未分配") : t.machineGroupName), row++, 1);

    infoLayout->addWidget(FormFactory::fieldLabel(QStringLiteral("存放位置")), row, 0);
    QString posDisplay = StyleHelper::formatPosition(t.cabinetName, t.layer, t.position);
    infoLayout->addWidget(FormFactory::fieldValue(posDisplay), row++, 1);

    // 不显示库存数量字段：一个位置(机组-柜-层-位号)=一个工具，数量恒为1，状态字段已说明在库/借出

    infoLayout->addWidget(FormFactory::fieldLabel(QStringLiteral("视觉标签")), row, 0);
    infoLayout->addWidget(FormFactory::fieldValue(t.visionTag.isEmpty() ? QStringLiteral("未绑定") : t.visionTag), row++, 1);

    // 识别方式显示
    infoLayout->addWidget(FormFactory::fieldLabel(QStringLiteral("识别方式")), row, 0);
    infoLayout->addWidget(FormFactory::fieldValue(QStringLiteral("视觉识别")), row++, 1);

    infoLayout->addWidget(FormFactory::fieldLabel(QStringLiteral("创建时间")), row, 0);
    infoLayout->addWidget(FormFactory::fieldValue(t.createdAt.isValid() ? t.createdAt.toString("yyyy-MM-dd HH:mm") : QStringLiteral("-")), row++, 1);

    infoLayout->addWidget(FormFactory::fieldLabel(QStringLiteral("借用人")), row, 0);
    infoLayout->addWidget(FormFactory::fieldValue(t.latestOpUser.isEmpty() ? QStringLiteral("无") : t.latestOpUser), row++, 1);

    // 已出库/入库工具显示操作人和操作时间
    {
        if (t.status == SC::TOOL_CHECKED_OUT) {
            QJsonObject log = ctx.recDao.findLastOperationLog(t.toolCode, SC::OP_CHECKOUT);
            if (!log.isEmpty()) {
                QString checkoutTime = log["time"].toString();
                QString checkoutUser = log["operator"].toString();
                infoLayout->addWidget(FormFactory::fieldLabel(QStringLiteral("出库人")), row, 0);
                infoLayout->addWidget(FormFactory::fieldValue(checkoutUser.isEmpty() ? QStringLiteral("--") : checkoutUser), row++, 1);
                infoLayout->addWidget(FormFactory::fieldLabel(QStringLiteral("出库时间")), row, 0);
                infoLayout->addWidget(FormFactory::fieldValue(checkoutTime.left(16).isEmpty() ? QStringLiteral("--") : checkoutTime.left(16)), row++, 1);
            }
        } else if (t.status == SC::TOOL_IN_STOCK) {
            QJsonObject log = ctx.recDao.findLastOperationLog(t.toolCode, SC::OP_CHECKIN);
            if (!log.isEmpty()) {
                QString checkinTime = log["time"].toString();
                QString checkinUser = log["operator"].toString();
                infoLayout->addWidget(FormFactory::fieldLabel(QStringLiteral("入库人")), row, 0);
                infoLayout->addWidget(FormFactory::fieldValue(checkinUser.isEmpty() ? QStringLiteral("--") : checkinUser), row++, 1);
                infoLayout->addWidget(FormFactory::fieldLabel(QStringLiteral("入库时间")), row, 0);
                infoLayout->addWidget(FormFactory::fieldValue(checkinTime.left(16).isEmpty() ? QStringLiteral("--") : checkinTime.left(16)), row++, 1);
            }
        }
    }

    infoLayout->addWidget(FormFactory::fieldLabel(QStringLiteral("最近操作")), row, 0);
    QString lastOpStr;
    if (!t.latestOpTime.isEmpty()) {
        QString opType;
        if (t.latestOpType == "borrow") opType = QStringLiteral("借用");
        else if (t.latestOpType == SC::OP_CHECKOUT) opType = QStringLiteral("出库");
        else if (t.latestOpType == SC::OP_CHECKIN) opType = QStringLiteral("入库");
        else opType = QStringLiteral("操作");
        lastOpStr = QStringLiteral("%1 %2").arg(t.latestOpTime, opType);
    } else {
        lastOpStr = QStringLiteral("暂无操作记录");
    }
    infoLayout->addWidget(FormFactory::fieldValue(lastOpStr), row++, 1);

    detailScroll->setWidget(infoCard);
    detailLayout->addWidget(detailScroll);
    ctx.tabWidget->addTab(detailTab, QStringLiteral("📋 工具详情"));
}

/** 收集操作记录：借用/入库/出库合并为统一列表，按时间降序排序 */
QList<QJsonObject> ToolManagementPage::collectOperationRecords(ToolDetailCtx& ctx) {
    const ToolInfo& t = ctx.t;

    // 借用记录按位置过滤：用mappingId查，只显示当前位置的借用记录
    QJsonArray borrowRecords;
    if (t.mappingId > 0) {
        borrowRecords = ctx.recDao.findByMappingId(t.mappingId, 20);
    } else {
        // 旧数据无mappingId，兜底用toolId查
        borrowRecords = ctx.recDao.findByToolId(t.toolId, 20);
    }

    // 入库/出库记录从DAO层查询（通过sys_operation_log，按位置过滤）
    QString posKey = StyleHelper::formatPosition(t.cabinetName, t.layer, t.position);

    QJsonArray checkinLogs = ctx.recDao.findOperationLogs(t.toolCode, SC::OP_CHECKIN, posKey, 10);
    QJsonArray checkinRecords;
    for (const auto& log : checkinLogs) {
        QJsonObject obj = log.toObject();
        obj["type"] = SC::OP_CHECKIN;
        obj["status"] = "completed";
        obj["reason"] = QStringLiteral("入库");
        // 从content解析供应商
        QRegularExpression reSupplier("供应商：(.+)");
        QRegularExpressionMatch mSupplier = reSupplier.match(obj["content"].toString());
        if (mSupplier.hasMatch()) obj["supplier"] = mSupplier.captured(1);
        checkinRecords.append(obj);
    }

    QJsonArray checkoutLogs = ctx.recDao.findOperationLogs(t.toolCode, SC::OP_CHECKOUT, posKey, 10);
    QJsonArray checkoutRecords;
    for (const auto& log : checkoutLogs) {
        QJsonObject obj = log.toObject();
        obj["type"] = SC::OP_CHECKOUT;
        obj["status"] = "completed";
        // 从content解析出库原因
        QRegularExpression reReason("原因：(.+)");
        QRegularExpressionMatch mReason = reReason.match(obj["content"].toString());
        obj["reason"] = mReason.hasMatch() ? mReason.captured(1) : QStringLiteral("出库");
        checkoutRecords.append(obj);
    }

    // 合并所有记录到统一列表
    // 一条借用记录(returned)拆分为"借用"和"归还"两条，完整展示工具生命周期
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
        if (borrowStatus == SC::RECORD_RETURNED) {
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
    return allRecords;
}

/** 填充操作记录表格：操作人/操作时间/操作类型/任务类型/状态 */
void ToolManagementPage::fillRecordTable(QTableWidget* recordTable, const QList<QJsonObject>& allRecords) {
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
        else if (opType == SC::OP_CHECKIN) { typeText = QStringLiteral("入库"); typeColor = "#43a047"; }
        else if (opType == SC::OP_CHECKOUT) { typeText = QStringLiteral("出库"); typeColor = "#e53935"; }
        else { typeText = QStringLiteral("操作"); typeColor = "#999"; }
        auto* typeLabel = new QLabel(typeText);
        typeLabel->setAlignment(Qt::AlignCenter);
        typeLabel->setFixedSize(64, 26);
        typeLabel->setStyleSheet(QString(
            "QLabel{background:%1;color:#fff;border-radius:6px;font-size:12px;font-weight:600;}"
        ).arg(typeColor));
        recordTable->setCellWidget(i, 2, typeLabel);

        // 任务类型（借用=任务类型/借用原因，入库=供应商，出库=出库原因）
        QString remark;
        if (opType == "borrow") remark = rec["borrowReason"].toString();
        else if (opType == SC::OP_CHECKIN) remark = rec["supplier"].toString();
        else if (opType == SC::OP_CHECKOUT) remark = rec["reason"].toString();
        else remark = QStringLiteral("--");
        recordTable->setItem(i, 3, new QTableWidgetItem(remark.isEmpty() ? QStringLiteral("--") : remark));

        // 状态（借用按借用状态着色，入库/出库固定"已完成"）
        QString status = rec["status"].toString();
        QString statusText;
        QString statusColor;
        if (opType == "borrow") {
            if (status == SC::RECORD_BORROWING) { statusText = QStringLiteral("借用中"); statusColor = "#fa8c16"; }
            else if (status == SC::RECORD_RETURNED) { statusText = QStringLiteral("已归还"); statusColor = "#43a047"; }
            else if (status == SC::RECORD_OVERDUE) { statusText = QStringLiteral("已逾期"); statusColor = "#e53935"; }
            else { statusText = status.isEmpty() ? QStringLiteral("--") : status; statusColor = "#999"; }
        } else {
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
}

/** 构建Tab2操作记录：综合借用/入库/出库记录表格 */
void ToolManagementPage::buildOperationRecordTab(ToolDetailCtx& ctx) {
    auto* recordTab = new QWidget();
    auto* recordLayout = new QVBoxLayout(recordTab);
    recordLayout->setContentsMargins(0, 12, 0, 0);
    recordLayout->setSpacing(0);

    QList<QJsonObject> allRecords = collectOperationRecords(ctx);

    if (allRecords.isEmpty()) {
        auto* emptyLabel = new QLabel(QStringLiteral("暂无操作记录"));
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setStyleSheet("font-size:14px;color:#999;padding:40px;background:transparent;");
        recordLayout->addWidget(emptyLabel);
        recordLayout->addStretch();
    } else {
        auto* recordTable = new QTableWidget();
        fillRecordTable(recordTable, allRecords);
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
    ctx.tabWidget->addTab(recordTab, QStringLiteral("📝 操作记录"));
}

/** 详情对话框收尾：文档Tab、自适应尺寸、关闭按钮与模态执行 */
void ToolManagementPage::finalizeDetailDialog(ToolDetailCtx& ctx) {
    // Tab3: 工具文档 下载+在线浏览
    QWidget* docTab = createDocumentTab(ctx.t.toolId, ctx.t.documentPath);
    ctx.tabWidget->addTab(docTab, QStringLiteral("📄 工具文档"));

    // 对话框高度自适应屏幕，避免内容增加后溢出看不到关闭按钮
    auto* cl = ctx.dlg->contentLayout();
    int screenHeight = QApplication::primaryScreen()->availableGeometry().height();
    int dlgHeight = qMin(screenHeight * 88 / 100, 820);
    ctx.dlg->setMinimumHeight(500);
    ctx.dlg->resize(900, dlgHeight);
    cl->addWidget(ctx.tabWidget);
    cl->addSpacing(20);

    // 关闭按钮 统一弹窗按钮风格：44px高/12px圆角/16px字体，居中显示
    auto* closeBtn = new QPushButton(QStringLiteral("关闭"));
    closeBtn->setFixedHeight(StyleHelper::Token::ControlHeight);
    closeBtn->setMinimumWidth(100);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QPushButton{background:#f0f0f0;color:#555;border:1px solid #ddd;border-radius:12px;"
        "font-size:16px;font-weight:600;}"
        "QPushButton:hover{background:#e0e0e0;}"
    );
    connect(closeBtn, &QPushButton::clicked, ctx.dlg, &QDialog::accept);

    auto* btnLayout = ctx.dlg->buttonLayout();
    // 清除默认stretch后重新居中布局
    QLayoutItem* item;
    while ((item = btnLayout->takeAt(0)) != nullptr) delete item;
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    btnLayout->addStretch();

    ctx.dlg->exec();
}
