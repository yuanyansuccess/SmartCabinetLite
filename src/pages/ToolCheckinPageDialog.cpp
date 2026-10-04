/**
 * @file ToolCheckinPageDialog.cpp
 * @brief 工具入库-弹窗流程（清单确认/开柜引导/告警/成功结果对话框）
 * @author 袁燕
 *
 * 本文件实现上述功能，成员函数声明见 ToolCheckinPage.h。
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

    titleLabel->setStyleSheet(StyleHelper::dialogTitleText());

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

        summaryLabel->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontLabel, StyleHelper::textField()));

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

            posLabel->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontLabel, StyleHelper::textField()));

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


