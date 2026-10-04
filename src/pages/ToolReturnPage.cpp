/**
 * @file ToolReturnPage.cpp
 * @brief 工具归还页面 — 多选归还、异常告警、归还清单、分页（UI与逻辑分离：静态布局在ToolReturnPage.ui）
 * @author 袁燕
 */
#include "ToolReturnPage.h"
#include "ui_ToolReturnPage.h"
#include "components/PaginationBar.h"
#include "utils/StyleHelper.h"
#include "components/DrawerOpeningDialog.h"
#include "components/VerifyAlertDialog.h"
#include "components/ResultDialog.h"
#include "services/ReturnService.h"
#include "components/MessageDialog.h"
#include "common/AppConfig.h"   // 获取本机机组ID
#include "db/ToolDAO.h"         // 查询机组名称
#include "db/RecordDAO.h"       // 查询借用记录关联工具信息
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QCheckBox>
#include <QDateTime>
#include <QDate>
#include <QDebug>
#include <QFrame>
#include <QDialog>
#include <QPushButton>
#include <QLabel>
#include <QAbstractItemView>  // scrollToItem需要
#include <QTimer>  // 抽屉打开动画
#include <QTableWidget>  // 归还清单表格
#include "services/AlertService.h"  // 告警闭环写入
#include "model/AlertLog.h"  // AlertLog实体
#include "common/Constants.h"

ToolReturnPage::ToolReturnPage(QWidget* parent) : QWidget(parent), ui(new Ui::ToolReturnPage) {
    ui->setupUi(this);  // 静态布局来自ToolReturnPage.ui（Qt Designer可视化维护）
    setupUI();          // 动态部分：列宽策略、信号槽连接
}

ToolReturnPage::~ToolReturnPage() {
    delete ui;
}

void ToolReturnPage::setUser(const QJsonObject& user) {
    m_user = user;
    refresh();
}

void ToolReturnPage::refresh() {
    if (!m_user.isEmpty()) {
        m_userNameLabel->setText(m_user["realName"].toString("--"));
        m_workNoLabel->setText(m_user["workNo"].toString("--"));
        // 所属机组显示本机机组名（与借用页一致），而非用户部门
        int groupId = AppConfig::instance().localMachineGroupId();
        if (groupId > 0) {
            db::ToolDAO dao;
            QJsonObject mg = dao.getMachineGroupById(groupId);
            m_deptLabel->setText(mg["groupName"].toString("--"));
        } else {
            m_deptLabel->setText(QStringLiteral("未配置"));
        }
    }
    m_dateLabel->setText(QDate::currentDate().toString("yyyy-MM-dd"));
    loadRecords();
}

void ToolReturnPage::setupUI() {
    // 桥接.ui控件（业务逻辑沿用m_成员，零改动）
    m_userNameLabel = ui->userNameLabel;
    m_workNoLabel = ui->workNoLabel;
    m_deptLabel = ui->deptLabel;
    m_dateLabel = ui->dateLabel;
    m_warningLabel = ui->warningLabel;
    m_table = ui->table;
    m_paginationBar = ui->paginationBar;
    m_selectAllBtn = ui->selectAllBtn;

    // 日期初始值（refresh()中随用户信息更新）
    m_dateLabel->setText(QDate::currentDate().toString("yyyy-MM-dd"));

    // 表格列宽策略：选择列固定60px，其余拉伸
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->setColumnWidth(0, 60);
    for (int col = 1; col < 8; col++) {
        m_table->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Stretch);
    }

    // 表格行点击勾选逻辑
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int /*col*/) {
        if (row < 0 || row >= m_records.size()) return;
        QJsonObject rec = m_records[row].toObject();
        int recordId = rec["recordId"].toInt();
        if (m_checkedRecordIds.contains(recordId)) {
            m_checkedRecordIds.remove(recordId);
        } else {
            m_checkedRecordIds.insert(recordId);
        }
        updateReturnBtn();
        auto* cb = qobject_cast<QCheckBox*>(m_table->cellWidget(row, 0));
        if (cb) cb->setChecked(m_checkedRecordIds.contains(recordId));
    });

    // 分页
    connect(m_paginationBar, &PaginationBar::prevClicked, this, &ToolReturnPage::onPrevPage);
    connect(m_paginationBar, &PaginationBar::nextClicked, this, &ToolReturnPage::onNextPage);

    // 确认归还
    connect(m_selectAllBtn, &QPushButton::clicked, this, &ToolReturnPage::onReturnSelected);
}

void ToolReturnPage::loadRecords() {
    // 显示所有位置的待归还记录（不限当前用户）
    // 设计理念：管理员需要看到全部借用情况，不只看自己的
    ReturnService svc;
    QJsonObject result = svc.getAllBorrowingRecords(m_currentPage, m_pageSize);

    m_records = result["list"].toArray();
    m_totalRecords = result["total"].toInt(0);

    m_paginationBar->setTotalRecords(m_totalRecords);
    int totalPages = qMax(1, (m_totalRecords + m_pageSize - 1) / m_pageSize);
    m_paginationBar->setPageInfo(m_currentPage, totalPages);

    m_checkedRecordIds.clear();
    updateReturnBtn();

    m_table->setRowCount(m_records.size());
    for (int i = 0; i < m_records.size(); ++i) {
        QJsonObject r = m_records[i].toObject();
        int recordId = r["recordId"].toInt();

        // checkbox选中整体填充蓝色（非边框），与借用页样式统一
        auto* checkBox = new QCheckBox();
        checkBox->setChecked(false);
        checkBox->setStyleSheet(
            "QCheckBox { background: transparent; }"
            "QCheckBox::indicator { width: 22px; height: 22px; border-radius: 4px; "
            "  border: 2px solid #d0d0d0; background: white; }"
            "QCheckBox::indicator:hover { border-color: #4da3ff; }"
            "QCheckBox::indicator:checked { background: #4da3ff; border-color: #4da3ff; }"
        );
        connect(checkBox, &QCheckBox::toggled, this, [this, recordId](bool checked) {
            if (checked) {
                m_checkedRecordIds.insert(recordId);
            } else {
                m_checkedRecordIds.remove(recordId);
            }
            updateReturnBtn();
        });
        m_table->setCellWidget(i, 0, checkBox);

        QString borrowTime = r["borrowTime"].toString();
        m_table->setItem(i, 1, new QTableWidgetItem(borrowTime));

        m_table->setItem(i, 2, new QTableWidgetItem(r["toolName"].toString()));

        m_table->setItem(i, 3, new QTableWidgetItem(r["position"].toString("--")));

        m_table->setItem(i, 4, new QTableWidgetItem(QString::number(r["borrowQty"].toInt())));

        // 任务类型：字段名对齐DAO返回的borrowReason（原误用reason导致永远显示--）
        // 空值显示"--"（不兜底"任务借用"，让数据真实性可见）
        QString borrowReason = r["borrowReason"].toString();
        if (borrowReason.isEmpty()) borrowReason = QStringLiteral("--");
        m_table->setItem(i, 5, new QTableWidgetItem(borrowReason));

        // 流水号：空值显示--
        QString flowNo = r["flowNo"].toString();
        m_table->setItem(i, 6, new QTableWidgetItem(flowNo.isEmpty() ? QStringLiteral("--") : flowNo));

        // 状态英文转中文
        QString status = r["status"].toString();
        QString statusText;
        QColor statusColor;
        if (status == SC::RECORD_BORROWING || status == QStringLiteral("借用中")) {
            statusText = QStringLiteral("借用中");
            statusColor = QColor("#fa8c16");
        } else if (status == SC::RECORD_OVERDUE || status == QStringLiteral("已逾期")) {
            statusText = QStringLiteral("已逾期");
            statusColor = QColor("#f5222d");
        } else if (status == SC::RECORD_RETURNED || status == QStringLiteral("已归还")) {
            statusText = QStringLiteral("已归还");
            statusColor = QColor("#43a047");
        } else {
            statusText = status.isEmpty() ? QStringLiteral("--") : status;
            statusColor = QColor("#999");
        }
        auto* statusItem = new QTableWidgetItem(statusText);
        statusItem->setForeground(statusColor);
        QFont sf = statusItem->font();
        sf.setBold(true);
        statusItem->setFont(sf);
        m_table->setItem(i, 7, statusItem);

        m_table->setRowHeight(i, 52);
    }

    // 如果是从借用记录跳转过来的，自动选中对应记录
    if (m_pendingReturnRecordId > 0) {
        for (int i = 0; i < m_records.size(); ++i) {
            QJsonObject r = m_records[i].toObject();
            if (r["recordId"].toInt() == m_pendingReturnRecordId) {
                m_checkedRecordIds.insert(m_pendingReturnRecordId);
                auto* cb = qobject_cast<QCheckBox*>(m_table->cellWidget(i, 0));
                if (cb) cb->setChecked(true);
                updateReturnBtn();
                // 滚动到对应行
                m_table->scrollToItem(m_table->item(i, 0), QAbstractItemView::PositionAtCenter);
                break;
            }
        }
        m_pendingReturnRecordId = 0;  // 清除标记，避免下次刷新重复选中
    }
}

// 从借用记录跳转时，设置待归还记录ID
void ToolReturnPage::setPendingReturnRecordId(int recordId) {
    m_pendingReturnRecordId = recordId;
}

void ToolReturnPage::onSelectAll(bool checked) {
    m_checkedRecordIds.clear();
    if (checked) {
        for (int i = 0; i < m_records.size(); ++i) {
            QJsonObject r = m_records[i].toObject();
            m_checkedRecordIds.insert(r["recordId"].toInt());
        }
    }
    for (int i = 0; i < m_table->rowCount(); ++i) {
        auto* cb = qobject_cast<QCheckBox*>(m_table->cellWidget(i, 0));
        if (cb) cb->setChecked(checked);
    }
    updateReturnBtn();
}

void ToolReturnPage::onReturnSelected() {
    if (m_checkedRecordIds.isEmpty()) {
        MessageDialog::showWarning(this, QStringLiteral("提示"), QStringLiteral("请先勾选需要归还的工具"));
        return;
    }
    // 改造为四步流程：清单确认 → 抽屉打开 → 异常校验 → 归还成功
    showReturnConfirmDialog();
}

void ToolReturnPage::onReturnAll() {
    for (int i = 0; i < m_records.size(); ++i) {
        QJsonObject r = m_records[i].toObject();
        m_checkedRecordIds.insert(r["recordId"].toInt());
    }
    for (int i = 0; i < m_table->rowCount(); ++i) {
        auto* cb = qobject_cast<QCheckBox*>(m_table->cellWidget(i, 0));
        if (cb) cb->setChecked(true);
    }
    updateReturnBtn();
}

void ToolReturnPage::onPrevPage() {
    if (m_currentPage > 1) {
        m_currentPage--;
        loadRecords();
    }
}

void ToolReturnPage::onNextPage() {
    int totalPages = qMax(1, (m_totalRecords + m_pageSize - 1) / m_pageSize);
    if (m_currentPage < totalPages) {
        m_currentPage++;
        loadRecords();
    }
}

void ToolReturnPage::updateReturnBtn() {
    m_selectAllBtn->setEnabled(!m_checkedRecordIds.isEmpty());
}

// ═══════════ 步骤1：归还清单确认对话框 ═══════════
void ToolReturnPage::showReturnConfirmDialog() {
    // 收集已选归还记录
    QList<QJsonObject> selectedRecords;
    int totalQty = 0;
    for (int i = 0; i < m_records.size(); ++i) {
        QJsonObject r = m_records[i].toObject();
        if (m_checkedRecordIds.contains(r["recordId"].toInt())) {
            selectedRecords.append(r);
            totalQty += r["borrowQty"].toInt();
        }
    }
    if (selectedRecords.isEmpty()) return;

    QDialog* dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("确认归还"));
    dlg->setFixedSize(560, qMax(460, 320 + selectedRecords.size() * 40));
    dlg->setStyleSheet(StyleHelper::dialogStyle());
    dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);

    auto* mainLayout = new QVBoxLayout(dlg);
    mainLayout->setContentsMargins(32, 28, 32, 24);
    mainLayout->setSpacing(14);

    // 标题栏
    auto* titleBar = new QHBoxLayout();
    auto* iconLabel = new QLabel(QStringLiteral("📋"));
    iconLabel->setStyleSheet("font-size:28px;background:transparent;");
    auto* titleLabel = new QLabel(QStringLiteral("待归还工具清单"));
    titleLabel->setStyleSheet(StyleHelper::dialogTitleText());
    titleBar->addWidget(iconLabel);
    titleBar->addWidget(titleLabel);
    titleBar->addStretch();
    mainLayout->addLayout(titleBar);

    // 归还详情表格：工具名/位置/数量
    auto* table = new QTableWidget();
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({
        QStringLiteral("工具名称"), QStringLiteral("存放位置"), QStringLiteral("归还数量")
    });
    table->setRowCount(selectedRecords.size());
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setStyleSheet(StyleHelper::listTableStyle());
    for (int col = 0; col < 3; ++col) {
        table->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Stretch);
    }
    for (int i = 0; i < selectedRecords.size(); ++i) {
        const QJsonObject& r = selectedRecords[i];
        table->setItem(i, 0, new QTableWidgetItem(r["toolName"].toString()));
        table->setItem(i, 1, new QTableWidgetItem(r["position"].toString().isEmpty() ? QStringLiteral("--") : r["position"].toString()));
        table->setItem(i, 2, new QTableWidgetItem(QStringLiteral("%1 件").arg(r["borrowQty"].toInt())));
        table->setRowHeight(i, 38);
    }
    table->setFixedHeight(qMax(160, selectedRecords.size() * 38 + 38));
    mainLayout->addWidget(table);

    // 归还人信息
    auto* infoRow = new QHBoxLayout();
    infoRow->setSpacing(24);
    auto* userInfo = new QLabel(QStringLiteral("归还人：%1（%2）")
        .arg(m_user["realName"].toString(), m_user["workNo"].toString()));
    userInfo->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontLabel, StyleHelper::textColor(), 600));
    auto* totalInfo = new QLabel(QStringLiteral("共归还 %1 件工具").arg(totalQty));
    totalInfo->setStyleSheet("font-size:15px;color:#43a047;font-weight:600;background:transparent;");
    infoRow->addWidget(userInfo);
    infoRow->addStretch();
    infoRow->addWidget(totalInfo);
    mainLayout->addLayout(infoRow);

    mainLayout->addStretch();

    // 底部按钮栏
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(12);
    btnLayout->addStretch();

    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    cancelBtn->setStyleSheet(StyleHelper::buttonDefault());
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setMinimumHeight(StyleHelper::Token::ControlHeight);
    cancelBtn->setMinimumWidth(110);
    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);

    auto* nextBtn = new QPushButton(QStringLiteral("下一步 →"));
    nextBtn->setStyleSheet(StyleHelper::buttonPrimary());
    nextBtn->setCursor(Qt::PointingHandCursor);
    nextBtn->setMinimumHeight(StyleHelper::Token::ControlHeight);
    nextBtn->setMinimumWidth(140);
    connect(nextBtn, &QPushButton::clicked, this, [this, dlg]() {
        dlg->accept();
        showReturnDrawerOpeningDialog();  // 进入步骤2：抽屉打开中
    });
    btnLayout->addWidget(nextBtn);

    mainLayout->addLayout(btnLayout);
    dlg->exec();
    dlg->deleteLater();
}

// ═══════════ 步骤2：抽屉打开中（三点跳动动画） ═══════════
void ToolReturnPage::showReturnDrawerOpeningDialog() {
    // 宽度加到560，高度根据归还数量动态调整
    QList<QJsonObject> previewRecords;
    for (int i = 0; i < m_records.size(); ++i) {
        QJsonObject r = m_records[i].toObject();
        if (m_checkedRecordIds.contains(r["recordId"].toInt())) {
            previewRecords.append(r);
        }
    }

    DrawerOpeningDialog dlg(QStringLiteral("抽屉打开中"), QStringLiteral("抽屉已打开"),
        QStringLiteral("对应工具抽屉已自动打开，<br/>"
                       "请按清单将工具放回指定位置手动关闭抽屉后再点击下一步。"), 32, 60, this);
    dlg.setFixedSize(560, qMax(460, 320 + previewRecords.size() * 36));

    // 归还清单（精简版表格）
    // 列从2列扩展为4列：工具名称/归还位置/归还数量/借用人，便于用户按清单归还
    QList<QJsonObject> selectedRecords;
    for (int i = 0; i < m_records.size(); ++i) {
        QJsonObject r = m_records[i].toObject();
        if (m_checkedRecordIds.contains(r["recordId"].toInt())) {
            selectedRecords.append(r);
        }
    }
    auto* table = new QTableWidget();
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({
        QStringLiteral("工具名称"), QStringLiteral("归还位置"),
        QStringLiteral("归还数量"), QStringLiteral("借用人")
    });
    table->setRowCount(selectedRecords.size());
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setStyleSheet(QString(
        "QTableWidget { border:1px solid #f0f0f0; background:#fff; border-radius:10px; "
        "  font-family:\"Microsoft YaHei\",sans-serif; font-size:13px; outline:none; }"
        "QTableWidget::item { padding:8px 12px; color:#333; border:none; "
        "  border-bottom:1px solid #f3f3f3; outline:none; }"
        "QTableWidget::item:selected { background:#e6f0ff; outline:none; }"
        "QHeaderView::section { background:#f8f9fb; color:#666; font-weight:600; "
        "  font-size:12px; padding:8px 12px; border:none; border-bottom:1px solid #f0f0f0; }"
    ));
    // 列宽：名称/位置/借用人Stretch均分，数量Fixed窄列
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    table->setColumnWidth(2, 80);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->horizontalHeader()->setStretchLastSection(false);
    for (int i = 0; i < selectedRecords.size(); ++i) {
        const QJsonObject& r = selectedRecords[i];
        table->setItem(i, 0, new QTableWidgetItem(r["toolName"].toString()));
        table->setItem(i, 1, new QTableWidgetItem(r["position"].toString().isEmpty() ? QStringLiteral("--") : r["position"].toString()));
        // 归还数量：显示借用的数量
        int qty = r["borrowQty"].toInt();
        auto* qtyItem = new QTableWidgetItem(QStringLiteral("%1 件").arg(qty > 0 ? qty : 1));
        qtyItem->setTextAlignment(Qt::AlignCenter);
        table->setItem(i, 2, qtyItem);
        // 借用人：显示借用人姓名
        QString userName = r["userName"].toString();
        table->setItem(i, 3, new QTableWidgetItem(userName.isEmpty() ? QStringLiteral("--") : userName));
        table->setRowHeight(i, 36);
    }
    table->setFixedHeight(qMax(120, selectedRecords.size() * 36 + 36));
    dlg.bodyLayout()->addWidget(table);

    if (dlg.exec() == QDialog::Accepted) {
        showReturnErrorDialog();  // 进入步骤3：工具核对异常
    }
}

void ToolReturnPage::showReturnErrorDialog() {
    // 步骤3：工具核对异常 — 右上角倒计时+左下角忽略+告警入库+退出系统
    // 警告详情（假异常：取出第一个选中工具的位置）
    QString firstPosition = QStringLiteral("A-01");
    for (int i = 0; i < m_records.size(); ++i) {
        QJsonObject r = m_records[i].toObject();
        if (m_checkedRecordIds.contains(r["recordId"].toInt())) {
            QString pos = r["position"].toString();
            if (!pos.isEmpty()) { firstPosition = pos; break; }
        }
    }

    VerifyAlertDialog dlg(QStringLiteral("归还校验"), QStringLiteral("工具核对异常"),
        QStringLiteral(
        "检测到 <b>%1</b> 位置的工具未正确放入柜位，<br/>"
        "可能原因：工具放置位置错误、视觉识别未通过或柜门未完全关闭。<br/><br/>"
        "请重新核对工具后点击确认，系统将完成归还登记。").arg(firstPosition),
        "font-size:14px;color:#555;line-height:1.7;background:transparent;",
        QStringLiteral("取消归还"), QStringLiteral("✓ 确认完成核对"), 160,
        AppConfig::instance().borrowReturnBuffer(), this);
    dlg.setFixedSize(520, 420);

    int result = dlg.exec();

    if (result == 2) {
        // 忽略或倒计时结束：写告警日志（闭环）
        int alertToolId = 0;
        QString alertToolCode;
        if (!m_checkedRecordIds.isEmpty()) {
            db::RecordDAO recDao;
            QJsonObject toolInfo = recDao.findToolInfoByRecordId(*m_checkedRecordIds.constBegin());
            alertToolId = toolInfo["toolId"].toInt();
            alertToolCode = toolInfo["toolCode"].toString();
        }
        AlertService alertSvc;
        alertSvc.recordVerifyAlert(m_user["userId"].toInt(), alertToolId, alertToolCode,
            QStringLiteral("工具核对异常：%1位置工具未正确放入柜位，用户忽略告警或倒计时超时").arg(firstPosition));

        MessageDialog::showWarning(nullptr, QStringLiteral("告警已记录"),
            QStringLiteral("工具核对异常告警已记录到系统告警，页面已重置。"));
        m_checkedRecordIds.clear();
        QMetaObject::invokeMethod(this, "refresh", Qt::QueuedConnection);
    } else if (result == QDialog::Accepted) {
        executeReturn();  // 进入步骤4：执行实际归还
    }
}

void ToolReturnPage::executeReturn() {
    if (m_user.isEmpty() || m_checkedRecordIds.isEmpty()) return;

    ReturnService svc;
    QList<int> recordIds;
    for (int id : m_checkedRecordIds) {
        recordIds.append(id);
    }

    ReturnService::Result result = svc.returnTools(recordIds, m_user["userId"].toInt());

    if (result.success) {
        showReturnSuccessDialog(result);
        loadRecords();
    } else {
        MessageDialog::showError(this, QStringLiteral("归还失败"), result.message);
    }
}

void ToolReturnPage::showReturnSuccessDialog(const ReturnService::Result& result) {
    ResultDialog dlg(QStringLiteral("归还成功"), QStringLiteral("✅"),
        QStringLiteral("归还成功！"), QStringLiteral("#43a047"), 16, this);
    dlg.setFixedSize(440, 300);

    // 去掉绿色背景，透明，与借用成功对话框风格统一
    auto* detailFrame = new QFrame();
    detailFrame->setStyleSheet("background:transparent;border:none;");
    auto* detailLayout = new QVBoxLayout(detailFrame);
    detailLayout->setSpacing(6);
    detailLayout->setContentsMargins(20, 16, 20, 16);

    auto* countLabel = new QLabel(QStringLiteral("已成功归还 %1 件工具").arg(result.count));
    countLabel->setAlignment(Qt::AlignCenter);
    countLabel->setStyleSheet("font-size:16px;font-weight:600;color:#333;");
    detailLayout->addWidget(countLabel);
    dlg.bodyLayout()->addWidget(detailFrame);

    dlg.addFinishButtonRow(QStringLiteral("确定"), StyleHelper::buttonPrimary(), 0);
    dlg.exec();
}
