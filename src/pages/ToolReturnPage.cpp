/**
 * @file ToolReturnPage.cpp
 * @brief 工具归还页面 — 多选归还、异常告警、归还清单、分页
 * @author 袁燕
 */
#include "ToolReturnPage.h"
#include "utils/StyleHelper.h"
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
#include <QAbstractItemView>  // [2026-06-27] scrollToItem需要
#include <QTimer>             // [2026-06-27] 抽屉打开动画
#include <QTableWidget>       // [2026-06-27] 归还清单表格
#include "db/AlertDAO.h"      // [V2.03k] 写入告警
#include "model/AlertLog.h"   // [V2.03k] AlertLog实体

ToolReturnPage::ToolReturnPage(QWidget* parent) : QWidget(parent) {
    setupUI();
}

ToolReturnPage::~ToolReturnPage() = default;

void ToolReturnPage::setUser(const QJsonObject& user) {
    m_user = user;
    refresh();
}

void ToolReturnPage::refresh() {
    if (!m_user.isEmpty()) {
        m_userNameLabel->setText(m_user["realName"].toString("--"));
        m_workNoLabel->setText(m_user["workNo"].toString("--"));
        // [2026-06-27] 所属机组显示本机机组名（与借用页一致），而非用户部门
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
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    // 标题
    auto* title = new QLabel(QStringLiteral("工具归还"));
    title->setStyleSheet("font-size:20px;font-weight:700;color:#1a1a2e;");
    mainLayout->addWidget(title);

    // ==================== 顶部信息栏（横向布局，对齐ToolBorrowPage）====================
    auto* infoBar = new QFrame();
    infoBar->setStyleSheet(QString(
        "QFrame { background:white; border-bottom:1px solid #e8ecf0; padding:10px 18px; }"
    ));
    auto* infoBarLayout = new QHBoxLayout(infoBar);
    infoBarLayout->setSpacing(16);
    infoBarLayout->setContentsMargins(0, 0, 0, 0);

    // 归还人
    auto* info1 = new QWidget();
    auto* info1Layout = new QHBoxLayout(info1);
    info1Layout->setContentsMargins(0, 0, 0, 0);
    info1Layout->setSpacing(6);
    auto* label1 = new QLabel(QStringLiteral("归还人"));
    label1->setStyleSheet("font-size:15px;color:#999;");
    m_userNameLabel = new QLabel("--");
    m_userNameLabel->setWordWrap(true);
    m_userNameLabel->setStyleSheet("font-size:15px;color:#1a1a2e;font-weight:600;");
    info1Layout->addWidget(label1);
    info1Layout->addWidget(m_userNameLabel);
    infoBarLayout->addWidget(info1);

    // 工号
    auto* info2 = new QWidget();
    auto* info2Layout = new QHBoxLayout(info2);
    info2Layout->setContentsMargins(0, 0, 0, 0);
    info2Layout->setSpacing(6);
    auto* label2 = new QLabel(QStringLiteral("工号"));
    label2->setStyleSheet("font-size:15px;color:#999;");
    m_workNoLabel = new QLabel("--");
    m_workNoLabel->setWordWrap(true);
    m_workNoLabel->setStyleSheet("font-size:15px;color:#1a1a2e;font-weight:600;");
    info2Layout->addWidget(label2);
    info2Layout->addWidget(m_workNoLabel);
    infoBarLayout->addWidget(info2);

    // 所属机组
    auto* info3 = new QWidget();
    auto* info3Layout = new QHBoxLayout(info3);
    info3Layout->setContentsMargins(0, 0, 0, 0);
    info3Layout->setSpacing(6);
    auto* label3 = new QLabel(QStringLiteral("所属机组"));
    label3->setStyleSheet("font-size:15px;color:#999;");
    m_deptLabel = new QLabel("--");
    m_deptLabel->setWordWrap(true);
    m_deptLabel->setStyleSheet("font-size:15px;color:#1a1a2e;font-weight:600;");
    info3Layout->addWidget(label3);
    info3Layout->addWidget(m_deptLabel);
    infoBarLayout->addWidget(info3);

    // 日期
    auto* info4 = new QWidget();
    auto* info4Layout = new QHBoxLayout(info4);
    info4Layout->setContentsMargins(0, 0, 0, 0);
    info4Layout->setSpacing(6);
    auto* label4 = new QLabel(QStringLiteral("日期"));
    label4->setStyleSheet("font-size:15px;color:#999;");
    m_dateLabel = new QLabel(QDate::currentDate().toString("yyyy-MM-dd"));
    m_dateLabel->setStyleSheet("font-size:15px;color:#1a1a2e;font-weight:600;");
    info4Layout->addWidget(label4);
    info4Layout->addWidget(m_dateLabel);
    infoBarLayout->addWidget(info4);

    infoBarLayout->addStretch();
    mainLayout->addWidget(infoBar);

    // ==================== 蓝色提示栏 ====================
    m_warningLabel = new QLabel(QStringLiteral("提示：请勾选需要归还的工具，确认工具已放回柜位后点击\"确认归还\""));
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setStyleSheet(QString(
        "QLabel{ background:#e6f7ff; border:1px solid #91caff; border-radius:8px; "
        "font-size:14px; color:#1677ff; padding:8px 14px; }"
    ));
    mainLayout->addWidget(m_warningLabel);

    // ==================== 表格 ====================
    m_table = new QTableWidget();
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("选择"),    // 0
        QStringLiteral("借用时间"), // 1
        QStringLiteral("工具名称"), // 2
        QStringLiteral("位置"),    // 3
        QStringLiteral("借用数量"), // 4
        QStringLiteral("任务类型"), // 5 [2026-06-27] 借用原因→任务类型
        QStringLiteral("流水号"),  // 6
        QStringLiteral("状态")     // 7
    });
    m_table->horizontalHeader()->setStretchLastSection(false);
    // [2026-06-27] 选择列Fixed窄宽(60px)，对齐借用页
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->setColumnWidth(0, 60);
    for (int c = 1; c < 8; c++) {
        m_table->horizontalHeader()->setSectionResizeMode(c, QHeaderView::Stretch);
    }
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(false);
    // [V2.02 2026-06-28] 移除内联表格QSS，使用全局QSS统一表格样式（小米设计语言）
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
    mainLayout->addWidget(m_table, 1);

    // ==================== 分页栏 [2026-06-27] 对齐借用页样式 ====================
    {
        auto* toolPageRow = new QHBoxLayout();
        toolPageRow->setContentsMargins(0, 8, 0, 0);
        toolPageRow->setSpacing(6);

        m_totalLabel = new QLabel(QStringLiteral("共 0 条"));
        m_totalLabel->setStyleSheet("font-size:13px;color:#999;");

        m_prevBtn = new QPushButton(QStringLiteral("上一页"));
        m_prevBtn->setStyleSheet(
            "QPushButton{border:1px solid #ddd;border-radius:6px;padding:5px 12px;"
            "font-size:13px;font-weight:600;color:#555;background:#fff;min-height:30px;}"
            "QPushButton:hover{border-color:#4da3ff;color:#4da3ff;}"
            "QPushButton:disabled{opacity:0.35;}"
        );
        m_prevBtn->setCursor(Qt::PointingHandCursor);
        m_prevBtn->setEnabled(false);
        connect(m_prevBtn, &QPushButton::clicked, this, &ToolReturnPage::onPrevPage);

        m_nextBtn = new QPushButton(QStringLiteral("下一页"));
        m_nextBtn->setStyleSheet(
            "QPushButton{border:1px solid #ddd;border-radius:6px;padding:5px 12px;"
            "font-size:13px;font-weight:600;color:#555;background:#fff;min-height:30px;}"
            "QPushButton:hover{border-color:#4da3ff;color:#4da3ff;}"
            "QPushButton:disabled{opacity:0.35;}"
        );
        m_nextBtn->setCursor(Qt::PointingHandCursor);
        m_nextBtn->setEnabled(false);
        connect(m_nextBtn, &QPushButton::clicked, this, &ToolReturnPage::onNextPage);

        m_pageLabel = new QLabel(QStringLiteral("第 1 页"));
        m_pageLabel->setStyleSheet("font-size:13px;color:#999;padding:0 4px;");

        toolPageRow->addStretch();
        toolPageRow->addWidget(m_prevBtn);
        toolPageRow->addWidget(m_pageLabel);
        toolPageRow->addWidget(m_nextBtn);
        toolPageRow->addWidget(m_totalLabel);
        mainLayout->addLayout(toolPageRow);
    }

    // ==================== 底部操作栏 ====================
    // [2026-06-27] 去掉返回按钮，确认归还按钮参考借用页样式(蓝色实底#4da3ff)
    auto* bottomBar = new QFrame();
    bottomBar->setStyleSheet(QString(
        "QFrame { background:white; border-top:1px solid #e8ecf0; padding:12px 18px; }"
    ));
    auto* bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(16);

    bottomLayout->addStretch();

    m_selectAllBtn = new QPushButton(QStringLiteral("确认归还"));
    // [2026-06-27] 样式对齐借用页"确认借用"按钮：蓝色实底#4da3ff，padding 12px 36px，17px字体，50px最小高度
    m_selectAllBtn->setStyleSheet(QString(
        "QPushButton{ background:#4da3ff;color:white;border:none;border-radius:12px;"
        "padding:12px 36px;font-size:17px;font-weight:600;min-height:50px;}"
        "QPushButton:hover{background:#3d8ae0;}"
        "QPushButton:pressed{background:#2e7ad6;}"
        "QPushButton:disabled{background:#cccccc;color:#888888;}"
    ));
    m_selectAllBtn->setCursor(Qt::PointingHandCursor);
    m_selectAllBtn->setEnabled(false);
    connect(m_selectAllBtn, &QPushButton::clicked, this, &ToolReturnPage::onReturnSelected);
    bottomLayout->addWidget(m_selectAllBtn);

    mainLayout->addWidget(bottomBar);
}

void ToolReturnPage::loadRecords() {
    // 显示所有位置的待归还记录（不限当前用户）
    // 设计理念：管理员需要看到全部借用情况，不只看自己的
    ReturnService svc;
    QJsonObject result = svc.getAllBorrowingRecords(m_currentPage, m_pageSize);

    m_records = result["list"].toArray();
    m_totalRecords = result["total"].toInt(0);

    m_totalLabel->setText(QStringLiteral("共 %1 条").arg(m_totalRecords));
    int totalPages = qMax(1, (m_totalRecords + m_pageSize - 1) / m_pageSize);
    m_pageLabel->setText(QStringLiteral("第 %1/%2 页").arg(m_currentPage).arg(totalPages));
    m_prevBtn->setEnabled(m_currentPage > 1);
    m_nextBtn->setEnabled(m_currentPage < totalPages);

    m_checkedRecordIds.clear();
    updateReturnBtn();

    m_table->setRowCount(m_records.size());
    for (int i = 0; i < m_records.size(); ++i) {
        QJsonObject r = m_records[i].toObject();
        int recordId = r["recordId"].toInt();

        // [2026-06-27] checkbox选中整体填充蓝色（非边框），与借用页样式统一
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

        // [2026-06-27] 任务类型：字段名对齐DAO返回的borrowReason（原误用reason导致永远显示--）
        // 空值显示"--"（不再兜底"任务借用"，让数据真实性可见）
        QString borrowReason = r["borrowReason"].toString();
        if (borrowReason.isEmpty()) borrowReason = QStringLiteral("--");
        m_table->setItem(i, 5, new QTableWidgetItem(borrowReason));

        // [2026-06-27] 流水号：空值显示--
        QString flowNo = r["flowNo"].toString();
        m_table->setItem(i, 6, new QTableWidgetItem(flowNo.isEmpty() ? QStringLiteral("--") : flowNo));

        // [2026-06-27] 状态英文转中文
        QString status = r["status"].toString();
        QString statusText;
        QColor statusColor;
        if (status == "borrowing" || status == QStringLiteral("借用中")) {
            statusText = QStringLiteral("借用中");
            statusColor = QColor("#fa8c16");
        } else if (status == "overdue" || status == QStringLiteral("已逾期")) {
            statusText = QStringLiteral("已逾期");
            statusColor = QColor("#f5222d");
        } else if (status == "returned" || status == QStringLiteral("已归还")) {
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

    // [2026-06-27] 如果是从借用记录跳转过来的，自动选中对应记录
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

// [2026-06-27] 从借用记录跳转时，设置待归还记录ID
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
    // [2026-06-27] 改造为四步流程：清单确认 → 抽屉打开 → 异常校验 → 归还成功
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

// ═══════════ [2026-06-27] 步骤1：归还清单确认对话框 ═══════════
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
    titleLabel->setStyleSheet("font-size:22px;font-weight:bold;color:#1a1a2e;background:transparent;");
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
    table->setStyleSheet(QString(
        "QTableWidget { border:1px solid #f0f0f0; background:#fff; border-radius:10px; "
        "  font-family:\"Microsoft YaHei\",sans-serif; font-size:14px; }"
        "QTableWidget::item { padding:10px 12px; color:#333; border:none; "
        "  border-bottom:1px solid #f3f3f3; }"
        "QHeaderView::section { background:#f8f9fb; color:#666; font-weight:600; "
        "  font-size:13px; padding:10px 12px; border:none; border-bottom:1px solid #f0f0f0; }"
    ));
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
    userInfo->setStyleSheet("font-size:15px;color:#1a1a2e;font-weight:600;background:transparent;");
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
    cancelBtn->setMinimumHeight(44);
    cancelBtn->setMinimumWidth(110);
    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);

    auto* nextBtn = new QPushButton(QStringLiteral("下一步 →"));
    nextBtn->setStyleSheet(StyleHelper::buttonPrimary());
    nextBtn->setCursor(Qt::PointingHandCursor);
    nextBtn->setMinimumHeight(44);
    nextBtn->setMinimumWidth(140);
    connect(nextBtn, &QPushButton::clicked, this, [this, dlg]() {
        dlg->accept();
        showReturnDrawerOpeningDialog();  // [2026-06-27] 进入步骤2：抽屉打开中
    });
    btnLayout->addWidget(nextBtn);

    mainLayout->addLayout(btnLayout);
    dlg->exec();
    dlg->deleteLater();
}

// ═══════════ [2026-06-27] 步骤2：抽屉打开中（三点跳动动画） ═══════════
void ToolReturnPage::showReturnDrawerOpeningDialog() {
    QDialog* dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("抽屉打开中"));
    // [2026-06-27] 宽度加到560，高度根据归还数量动态调整
    QList<QJsonObject> previewRecords;
    for (int i = 0; i < m_records.size(); ++i) {
        QJsonObject r = m_records[i].toObject();
        if (m_checkedRecordIds.contains(r["recordId"].toInt())) {
            previewRecords.append(r);
        }
    }
    dlg->setFixedSize(560, qMax(460, 320 + previewRecords.size() * 36));
    dlg->setStyleSheet(StyleHelper::dialogStyle());
    dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);

    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(32, 32, 32, 24);
    layout->setSpacing(14);

    // 标题
    auto* titleLabel = new QLabel(QStringLiteral("抽屉已打开"));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size:22px;font-weight:bold;color:#1a1a2e;background:transparent;");
    layout->addWidget(titleLabel);

    // 动态加载指示器 — 三点跳动动画（与借用页完全一致）
    auto* spinnerContainer = new QWidget();
    spinnerContainer->setFixedHeight(60);
    auto* spinnerLayout = new QHBoxLayout(spinnerContainer);
    spinnerLayout->setSpacing(16);
    spinnerLayout->setAlignment(Qt::AlignCenter);
    QList<QLabel*> dots;
    for (int i = 0; i < 3; ++i) {
        auto* dot = new QLabel();
        dot->setFixedSize(16, 16);
        dot->setStyleSheet(QString(
            "QLabel{background:%1;border-radius:8px;}"
        ).arg(StyleHelper::primaryColor()));
        spinnerLayout->addWidget(dot);
        dots.append(dot);
    }
    layout->addWidget(spinnerContainer);

    // 三点跳动动画
    QTimer* dotTimer = new QTimer(dlg);
    dotTimer->setInterval(300);
    int dotIndex = 0;
    QObject::connect(dotTimer, &QTimer::timeout, dlg, [dots, &dotIndex]() {
        for (int i = 0; i < dots.size(); ++i) {
            if (i == dotIndex) {
                dots[i]->setStyleSheet("background:#4da3ff;border-radius:10px;");
                dots[i]->setFixedSize(20, 20);
            } else {
                dots[i]->setStyleSheet("background:#c8d6e5;border-radius:8px;");
                dots[i]->setFixedSize(16, 16);
            }
        }
        dotIndex = (dotIndex + 1) % dots.size();
    });
    dotTimer->start();

    // 提示信息
    auto* descLabel = new QLabel(QStringLiteral(
        "对应工具抽屉已自动打开，<br/>"
        "请按清单将工具放回指定位置手动关闭抽屉后再点击下一步。"
    ));
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setTextFormat(Qt::RichText);
    descLabel->setStyleSheet("font-size:15px;color:#555;line-height:1.6;background:transparent;");
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    // 归还清单（精简版表格）
    // [2026-06-27] 列从2列扩展为4列：工具名称/归还位置/归还数量/借用人，便于用户按清单归还
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
        // [2026-06-27] 归还数量：显示借用的数量
        int qty = r["borrowQty"].toInt();
        auto* qtyItem = new QTableWidgetItem(QStringLiteral("%1 件").arg(qty > 0 ? qty : 1));
        qtyItem->setTextAlignment(Qt::AlignCenter);
        table->setItem(i, 2, qtyItem);
        // [2026-06-27] 借用人：显示借用人姓名
        QString userName = r["userName"].toString();
        table->setItem(i, 3, new QTableWidgetItem(userName.isEmpty() ? QStringLiteral("--") : userName));
        table->setRowHeight(i, 36);
    }
    table->setFixedHeight(qMax(120, selectedRecords.size() * 36 + 36));
    layout->addWidget(table);

    layout->addStretch();

    // 底部按钮
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    cancelBtn->setStyleSheet(StyleHelper::buttonDefault());
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setMinimumHeight(44);
    cancelBtn->setMinimumWidth(110);
    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);

    auto* nextBtn = new QPushButton(QStringLiteral("下一步 →"));
    nextBtn->setStyleSheet(StyleHelper::buttonPrimary());
    nextBtn->setCursor(Qt::PointingHandCursor);
    nextBtn->setMinimumHeight(44);
    nextBtn->setMinimumWidth(140);
    connect(nextBtn, &QPushButton::clicked, this, [this, dlg]() {
        dlg->accept();
        showReturnErrorDialog();  // [2026-06-27] 进入步骤3：工具核对异常
    });
    btnRow->addWidget(cancelBtn);
    btnRow->addSpacing(12);
    btnRow->addWidget(nextBtn);
    layout->addLayout(btnRow);

    dlg->exec();
    dlg->deleteLater();
}

void ToolReturnPage::showReturnErrorDialog() {
    // [V2.03k 2026-06-29] 步骤3：工具核对异常 — 右上角倒计时+左下角忽略+告警入库+退出系统
    QDialog* dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("归还校验"));
    dlg->setFixedSize(520, 420);
    dlg->setStyleSheet(StyleHelper::dialogStyle());
    dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);

    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(32, 32, 32, 24);
    layout->setSpacing(18);

    // [V2.03k] 顶部行：图标居中 + 右上角倒计时
    auto* topRow = new QHBoxLayout();
    auto* iconLabel = new QLabel(QStringLiteral("⚠️"));
    iconLabel->setStyleSheet("font-size:56px;background:transparent;");
    // 倒计时标签（右上角）
    int bufferMinutes = AppConfig::instance().borrowReturnBuffer();
    if (bufferMinutes <= 0) bufferMinutes = 30;  // 兜底默认30分钟
    auto* countdownLabel = new QLabel(QStringLiteral("⏱ %1:00").arg(bufferMinutes));
    countdownLabel->setStyleSheet("font-size:18px;font-weight:bold;color:#e74c3c;background:#fdecea;border:1px solid #f5c6cb;border-radius:8px;padding:6px 12px;");
    topRow->addStretch();
    topRow->addWidget(iconLabel);
    topRow->addStretch();
    topRow->addWidget(countdownLabel);
    layout->addLayout(topRow);

    // 警告标题
    auto* titleLabel = new QLabel(QStringLiteral("工具核对异常"));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size:22px;font-weight:bold;color:#fa8c16;background:transparent;");
    layout->addWidget(titleLabel);

    // 警告详情（假异常：取出第一个选中工具的位置）
    QString firstPosition = QStringLiteral("A-01");
    for (int i = 0; i < m_records.size(); ++i) {
        QJsonObject r = m_records[i].toObject();
        if (m_checkedRecordIds.contains(r["recordId"].toInt())) {
            QString pos = r["position"].toString();
            if (!pos.isEmpty()) { firstPosition = pos; break; }
        }
    }
    auto* descLabel = new QLabel(QStringLiteral(
        "检测到 <b>%1</b> 位置的工具未正确放入柜位，<br/>"
        "可能原因：工具放置位置错误、视觉识别未通过或柜门未完全关闭。<br/><br/>"
        "请重新核对工具后点击确认，系统将完成归还登记。"
    ).arg(firstPosition));
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setTextFormat(Qt::RichText);
    descLabel->setStyleSheet("font-size:14px;color:#555;line-height:1.7;background:transparent;");
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    layout->addStretch();

    // [V2.03k] 倒计时定时器：每秒更新，倒计时结束自动忽略+告警+退出
    int* remainSeconds = new int(bufferMinutes * 60);
    QTimer* countdownTimer = new QTimer(dlg);
    countdownTimer->setInterval(1000);
    QObject::connect(countdownTimer, &QTimer::timeout, dlg, [countdownLabel, remainSeconds, dlg, countdownTimer]() {
        (*remainSeconds)--;
        if (*remainSeconds <= 0) {
            countdownTimer->stop();
            dlg->done(2);  // 倒计时结束→走忽略流程
        } else {
            int mins = *remainSeconds / 60;
            int secs = *remainSeconds % 60;
            countdownLabel->setText(QStringLiteral("⏱ %1:%2")
                .arg(mins, 2, 10, QChar('0'))
                .arg(secs, 2, 10, QChar('0')));
        }
    });
    countdownTimer->start();

    // [V2.03k] 忽略动作：写入告警 + 关闭对话框 + 退出系统
    // 忽略按钮回调：关闭对话框走done(2)流程，告警在exec后统一写入
    auto doIgnore = [this, dlg, countdownTimer, remainSeconds]() mutable {
        countdownTimer->stop();
        delete remainSeconds;
        dlg->done(2);
    };

    // [V2.03k] 按钮区：左下角忽略 + 右下角取消归还 + 确认完成核对
    auto* btnRow = new QHBoxLayout();
    // 左下角：忽略按钮
    auto* ignoreBtn = new QPushButton(QStringLiteral("忽略"));
    ignoreBtn->setStyleSheet("QPushButton{background:#e74c3c;color:#fff;border:none;border-radius:10px;padding:10px 24px;font-size:14px;font-weight:700;min-height:44px;}QPushButton:hover{background:#c0392b;}");
    ignoreBtn->setCursor(Qt::PointingHandCursor);
    connect(ignoreBtn, &QPushButton::clicked, this, doIgnore);
    btnRow->addWidget(ignoreBtn);
    btnRow->addStretch();

    auto* cancelBtn = new QPushButton(QStringLiteral("取消归还"));
    cancelBtn->setStyleSheet(StyleHelper::buttonDefault());
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setMinimumHeight(44);
    cancelBtn->setMinimumWidth(120);
    connect(cancelBtn, &QPushButton::clicked, dlg, [countdownTimer, remainSeconds]() {
        countdownTimer->stop();
        delete remainSeconds;
        static_cast<QDialog*>(static_cast<QWidget*>(countdownTimer->parent()))->reject();
    });

    auto* confirmBtn = new QPushButton(QStringLiteral("✓ 确认完成核对"));
    confirmBtn->setStyleSheet(StyleHelper::buttonPrimary());
    confirmBtn->setCursor(Qt::PointingHandCursor);
    confirmBtn->setMinimumHeight(44);
    confirmBtn->setMinimumWidth(160);
    connect(confirmBtn, &QPushButton::clicked, this, [this, dlg, countdownTimer, remainSeconds]() {
        countdownTimer->stop();
        delete remainSeconds;
        dlg->accept();
        executeReturn();  // 进入步骤4：执行实际归还
    });
    btnRow->addWidget(cancelBtn);
    btnRow->addSpacing(12);
    btnRow->addWidget(confirmBtn);
    layout->addLayout(btnRow);

    int result = dlg->exec();
    countdownTimer->stop();

    if (result == 2) {
        // 忽略或倒计时结束：写告警日志（闭环）
        db::AlertDAO alertDao;
        AlertLog alert;
        alert.typeId = 2;
        alert.userId = m_user["userId"].toInt();
        if (!m_checkedRecordIds.isEmpty()) {
            db::RecordDAO recDao;
            QJsonObject toolInfo = recDao.findToolInfoByRecordId(*m_checkedRecordIds.constBegin());
            alert.toolId = toolInfo["toolId"].toInt();
            alert.toolCode = toolInfo["toolCode"].toString();
        }
        alert.message = QStringLiteral("工具核对异常：%1位置工具未正确放入柜位，用户忽略告警或倒计时超时").arg(firstPosition);
        alert.status = "unhandled";
        alertDao.insertAlert(alert);

        MessageDialog::showWarning(nullptr, QStringLiteral("告警已记录"),
            QStringLiteral("工具核对异常告警已记录到系统告警，页面已重置。"));
        m_checkedRecordIds.clear();
        QMetaObject::invokeMethod(this, "refresh", Qt::QueuedConnection);
    }
    dlg->deleteLater();
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
    QDialog* dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("归还成功"));
    dlg->setFixedSize(440, 300);
    dlg->setStyleSheet(StyleHelper::dialogStyle());
    dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);

    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(32, 32, 32, 24);
    layout->setSpacing(16);

    // [2026-06-27] 图标和标题加background:transparent去除灰色底色
    auto* iconLabel = new QLabel(QStringLiteral("✅"));
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet("font-size:56px;background:transparent;");
    layout->addWidget(iconLabel);

    auto* titleLabel = new QLabel(QStringLiteral("归还成功！"));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size:24px;font-weight:bold;color:#43a047;background:transparent;");
    layout->addWidget(titleLabel);

    // [2026-06-27] 去掉绿色背景，改为透明，与借用成功对话框风格统一
    auto* detailFrame = new QFrame();
    detailFrame->setStyleSheet("background:transparent;border:none;");
    auto* detailLayout = new QVBoxLayout(detailFrame);
    detailLayout->setSpacing(6);
    detailLayout->setContentsMargins(20, 16, 20, 16);

    auto* countLabel = new QLabel(QStringLiteral("已成功归还 %1 件工具").arg(result.count));
    countLabel->setAlignment(Qt::AlignCenter);
    countLabel->setStyleSheet("font-size:16px;font-weight:600;color:#333;");
    detailLayout->addWidget(countLabel);

    layout->addWidget(detailFrame);

    layout->addStretch();

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto* okBtn = new QPushButton(QStringLiteral("确定"));
    okBtn->setStyleSheet(StyleHelper::buttonPrimary());
    okBtn->setCursor(Qt::PointingHandCursor);
    connect(okBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    btnLayout->addWidget(okBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    dlg->exec();
    dlg->deleteLater();
}
