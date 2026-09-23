/**
 * @file LedgerStatsPage.cpp
 * @brief 台账统计页面实现
 * @author 袁燕
 * @修改说明 V7.2 2026-06-24 新增台账导出和USB导出功能
 */
#include "LedgerStatsPage.h"
#include "utils/StyleHelper.h"
#include "services/SettingService.h"
#include "components/SingleSelectFilter.h"  // [V6.9] 通用单选筛选组件
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFrame>
#include <QTimer>
#include <QVariantMap>
#include "components/MessageDialog.h"
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QDateTime>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QDebug>

LedgerStatsPage::LedgerStatsPage(QWidget* parent) : QWidget(parent) {
    setupUI();
}

void LedgerStatsPage::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    // [V7.2 2026-06-24] 标题栏：标题在左，导出按钮在右（对齐AlertLogsPage模式）
    auto* titleBar = new QHBoxLayout();
    titleBar->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("台账统计"));
    title->setStyleSheet("font-size:20px;font-weight:700;color:#1a1a2e;");
    titleBar->addWidget(title);
    titleBar->addStretch();

    // 台账导出按钮 [2026-06-26] 尺寸对齐人员管理"新增人员"：padding:10px 22px;font-size:14px
    m_exportLedgerBtn = new QPushButton(QStringLiteral("📊 台账导出"));
    m_exportLedgerBtn->setStyleSheet(
        "QPushButton{background:#fff;color:#4da3ff;border:2px solid #4da3ff;border-radius:10px;"
        "padding:10px 22px;font-size:14px;font-weight:700;}"
        "QPushButton:hover{background:#f0f7ff;}"
        "QPushButton:pressed{transform:scale(0.96);}"
    );
    m_exportLedgerBtn->setCursor(Qt::PointingHandCursor);
    connect(m_exportLedgerBtn, &QPushButton::clicked, this, &LedgerStatsPage::onExportLedger);
    titleBar->addWidget(m_exportLedgerBtn);

    // USB导出按钮 [2026-06-26] 尺寸对齐人员管理"新增人员"
    //m_exportUSBBtn = new QPushButton(QStringLiteral("💾 USB导出"));
    //m_exportUSBBtn->setStyleSheet(
    //    "QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:10px;"
    //    "padding:10px 22px;font-size:14px;font-weight:700;}"
    //    "QPushButton:hover{background:#3d8ae0;}"
    //    "QPushButton:pressed{transform:scale(0.96);}"
    //);
    //m_exportUSBBtn->setCursor(Qt::PointingHandCursor);
    //connect(m_exportUSBBtn, &QPushButton::clicked, this, &LedgerStatsPage::onExportToUSB);
    //titleBar->addWidget(m_exportUSBBtn);

    mainLayout->addLayout(titleBar);

    // 统计卡片行
    auto* cardsRow = new QHBoxLayout();
    cardsRow->setSpacing(16);

    auto createCard = [&](const QString& labelText, QLabel*& valLabel, const QString& color) -> QWidget* {
        auto* card = new QWidget();
        card->setStyleSheet(QString("background:white;border-radius:14px;border:none;").arg(StyleHelper::borderColor()));
        card->setMinimumHeight(100);
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(20, 14, 20, 14);
        cl->setSpacing(6);
        auto* ll = new QLabel(labelText);
        ll->setStyleSheet(QString("font-size:14px;color:%1;background:transparent;").arg(StyleHelper::textSecondary()));
        valLabel = new QLabel("--");
        valLabel->setStyleSheet(QString("font-size:28px;font-weight:bold;color:%1;background:transparent;").arg(color));
        cl->addWidget(ll);
        cl->addWidget(valLabel);
        // [2026-06-24v2] 初始化数字滚动动画属性
        QVariantMap animData;
        animData["targetValue"] = 0;
        animData["currentValue"] = 0;
        animData["steps"] = 0;
        animData["maxSteps"] = 20;
        card->setProperty("animData", animData);
        return card;
    };

    cardsRow->addWidget(createCard(QStringLiteral("总借用次数"), m_totalBorrowLabel, StyleHelper::primaryColor()));
    cardsRow->addWidget(createCard(QStringLiteral("总归还次数"), m_totalReturnLabel, StyleHelper::successColor()));
    cardsRow->addWidget(createCard(QStringLiteral("当前借用中"), m_currentBorrowedLabel, StyleHelper::warningColor()));
    cardsRow->addWidget(createCard(QStringLiteral("逾期未还"), m_overdueLabel, StyleHelper::dangerColor()));
    mainLayout->addLayout(cardsRow);

    // 时间筛选 [V6.9 2026-06-24] 改为CheckBox样式单选组件
    auto* filterRow = new QHBoxLayout();
    filterRow->setSpacing(12);
    auto* filterLabel = new QLabel(QStringLiteral("统计周期:"));
    filterLabel->setStyleSheet(QString("font-size:16px;font-weight:bold;color:%1;").arg(StyleHelper::textColor()));
    m_periodFilter = new SingleSelectFilter(QStringLiteral("本月"), this);
    m_periodFilter->setOptions({QStringLiteral("本月"), QStringLiteral("本季度"), QStringLiteral("本年度"), QStringLiteral("全部")});
    connect(m_periodFilter, &SingleSelectFilter::selectionChanged, this, [this](const QString&) { onFilterChanged(); });
    filterRow->addWidget(filterLabel);
    filterRow->addWidget(m_periodFilter);
    filterRow->addStretch();
    mainLayout->addLayout(filterRow);

    // 分类统计表
    auto* catTitle = new QLabel(QStringLiteral("工具分类统计"));
    catTitle->setStyleSheet(QString("font-size:16px;font-weight:bold;color:%1;margin-top:4px;").arg(StyleHelper::textColor()));
    mainLayout->addWidget(catTitle);

    m_categoryTable = new QTableWidget();
    m_categoryTable->setColumnCount(4);
    m_categoryTable->setHorizontalHeaderLabels({QStringLiteral("分类"), QStringLiteral("总数量"), QStringLiteral("借出中"), QStringLiteral("可用")});
    m_categoryTable->horizontalHeader()->setStretchLastSection(true);
    m_categoryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_categoryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_categoryTable->verticalHeader()->setVisible(false);
    m_categoryTable->setAlternatingRowColors(true);
    m_categoryTable->setMaximumHeight(250);
    // [V2.02 2026-06-28] 移除内联表格QSS，使用全局QSS统一表格样式
    mainLayout->addWidget(m_categoryTable);

    // 部门统计表
    auto* deptTitle = new QLabel(QStringLiteral("部门借用统计"));
    deptTitle->setStyleSheet(QString("font-size:16px;font-weight:bold;color:%1;margin-top:4px;").arg(StyleHelper::textColor()));
    mainLayout->addWidget(deptTitle);

    m_deptTable = new QTableWidget();
    m_deptTable->setColumnCount(4);
    m_deptTable->setHorizontalHeaderLabels({QStringLiteral("部门"), QStringLiteral("借用次数"), QStringLiteral("逾期次数"), QStringLiteral("逾期率")});
    m_deptTable->horizontalHeader()->setStretchLastSection(true);
    m_deptTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_deptTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_deptTable->verticalHeader()->setVisible(false);
    m_deptTable->setAlternatingRowColors(false);  // [1:1复刻]
    m_deptTable->setMaximumHeight(250);
    // [V2.02 2026-06-28] 移除内联表格QSS，使用全局QSS统一表格样式
    mainLayout->addWidget(m_deptTable);

    mainLayout->addStretch();
}

void LedgerStatsPage::refresh() { loadStats(); }

void LedgerStatsPage::onFilterChanged() { loadStats(); }

void LedgerStatsPage::loadStats() {
    SettingService svc;
    QJsonObject stats = svc.getLedgerStats();

    // [2026-06-24v2] 数字滚动动画，与DashboardPage风格统一
    setStatValue(m_totalBorrowLabel, stats["totalBorrows"].toInt());
    setStatValue(m_totalReturnLabel, stats["totalReturns"].toInt());
    setStatValue(m_currentBorrowedLabel, stats["currentBorrowed"].toInt());
    setStatValue(m_overdueLabel, stats["overdueCount"].toInt());

    // 分类统计
    QJsonArray categoryStats = stats["categoryStats"].toArray();
    m_categoryTable->setRowCount(categoryStats.size());
    for (int i = 0; i < categoryStats.size(); ++i) {
        QJsonObject cs = categoryStats[i].toObject();
        m_categoryTable->setItem(i, 0, new QTableWidgetItem(cs["category"].toString()));
        m_categoryTable->setItem(i, 1, new QTableWidgetItem(QString::number(cs["totalCount"].toInt())));
        m_categoryTable->setItem(i, 2, new QTableWidgetItem(QString::number(cs["borrowedCount"].toInt())));
        m_categoryTable->setItem(i, 3, new QTableWidgetItem(QString::number(cs["availableCount"].toInt())));
    }

    // 部门统计
    QJsonArray deptStats = stats["departmentStats"].toArray();
    m_deptTable->setRowCount(deptStats.size());
    for (int i = 0; i < deptStats.size(); ++i) {
        QJsonObject ds = deptStats[i].toObject();
        m_deptTable->setItem(i, 0, new QTableWidgetItem(ds["department"].toString()));
        m_deptTable->setItem(i, 1, new QTableWidgetItem(QString::number(ds["borrowCount"].toInt())));
        m_deptTable->setItem(i, 2, new QTableWidgetItem(QString::number(ds["overdueCount"].toInt())));
        double overdueRate = ds["overdueRate"].toDouble();
        auto* rateItem = new QTableWidgetItem(QString::number(overdueRate * 100, 'f', 1) + "%");
        rateItem->setForeground(overdueRate > 0.3 ? QColor(StyleHelper::dangerColor()) :
                                overdueRate > 0.1 ? QColor(StyleHelper::warningColor()) : QColor(StyleHelper::successColor()));
        m_deptTable->setItem(i, 3, rateItem);
    }
}

// [V7.2 2026-06-24] 台账导出：生成CSV文件并保存到桌面
// [V7.3 2026-06-26] 彻底修复编码问题：QTextStream在Windows中文环境默认GBK编码，
//   导致BOM(UTF-8)与内容(GBK)编码不一致，Excel打开乱码。
//   改为直接用QFile::write()写入UTF-8字节流，保证BOM和内容编码统一。
void LedgerStatsPage::onExportLedger() {
    QString csv = generateCSV();
    if (csv.isEmpty()) {
        MessageDialog::showError(this, QStringLiteral("导出失败"), QStringLiteral("没有可导出的台账数据。"));
        return;
    }

    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    QString fileName = QStringLiteral("台账统计_%1.csv")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString filePath = QDir(desktopPath).filePath(fileName);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        MessageDialog::showError(this, QStringLiteral("导出失败"),
            QStringLiteral("无法写入文件：\n%1").arg(filePath));
        return;
    }

    // [V7.3] 直接用二进制方式写入UTF-8字节流：BOM + UTF-8内容
    file.write("\xEF\xBB\xBF");              // UTF-8 BOM (3字节)
    file.write(csv.toUtf8());                // CSV内容转UTF-8字节流
    file.close();

    showExportSuccess(filePath);
}

// [V7.2 2026-06-24] USB导出：检测USB设备并复制CSV文件到USB
// [V7.3 2026-06-26] 同onExportLedger修复编码问题
void LedgerStatsPage::onExportToUSB() {
    QString usbPath = detectUSBDrive();
    if (usbPath.isEmpty()) {
        MessageDialog::showError(this, QStringLiteral("未检测到USB设备"),
            QStringLiteral("请插入U盘后重试。\n\n"
            "提示：确保U盘已正确插入并被系统识别。"));
        return;
    }

    QString csv = generateCSV();
    if (csv.isEmpty()) {
        MessageDialog::showError(this, QStringLiteral("导出失败"), QStringLiteral("没有可导出的台账数据。"));
        return;
    }

    QString fileName = QStringLiteral("台账统计_%1.csv")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString filePath = QDir(usbPath).filePath(fileName);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        MessageDialog::showError(this, QStringLiteral("USB导出失败"),
            QStringLiteral("无法写入USB设备：\n%1\n\n请检查U盘是否被写保护。").arg(filePath));
        return;
    }

    // [V7.3] 直接用二进制方式写入UTF-8字节流
    file.write("\xEF\xBB\xBF");
    file.write(csv.toUtf8());
    file.close();

    showExportSuccess(filePath);
}

// [V7.2 2026-06-24] 生成台账CSV内容
QString LedgerStatsPage::generateCSV() {
    SettingService svc;
    QJsonObject stats = svc.getLedgerStats();

    QString csv;
    QTextStream ts(&csv);

    // 概览统计
    ts << QStringLiteral("=== 台账概览统计 ===\n");
    ts << QStringLiteral("总借用次数,%1\n").arg(stats["totalBorrows"].toInt());
    ts << QStringLiteral("总归还次数,%1\n").arg(stats["totalReturns"].toInt());
    ts << QStringLiteral("当前借用中,%1\n").arg(stats["currentBorrowed"].toInt());
    ts << QStringLiteral("逾期未还,%1\n\n").arg(stats["overdueCount"].toInt());

    // 分类统计
    ts << QStringLiteral("=== 工具分类统计 ===\n");
    ts << QStringLiteral("分类,总数量,借出中,可用\n");
    QJsonArray categoryStats = stats["categoryStats"].toArray();
    for (int i = 0; i < categoryStats.size(); ++i) {
        QJsonObject cs = categoryStats[i].toObject();
        ts << QStringLiteral("%1,%2,%3,%4\n")
            .arg(cs["category"].toString())
            .arg(cs["totalCount"].toInt())
            .arg(cs["borrowedCount"].toInt())
            .arg(cs["availableCount"].toInt());
    }

    // 部门统计
    ts << QStringLiteral("\n=== 部门借用统计 ===\n");
    ts << QStringLiteral("部门,借用次数,逾期次数,逾期率\n");
    QJsonArray deptStats = stats["departmentStats"].toArray();
    for (int i = 0; i < deptStats.size(); ++i) {
        QJsonObject ds = deptStats[i].toObject();
        double overdueRate = ds["overdueRate"].toDouble();
        ts << QStringLiteral("%1,%2,%3,%4%\n")
            .arg(ds["department"].toString())
            .arg(ds["borrowCount"].toInt())
            .arg(ds["overdueCount"].toInt())
            .arg(QString::number(overdueRate * 100, 'f', 1));
    }

    // 导出信息
    ts << QStringLiteral("\n=== 导出信息 ===\n");
    ts << QStringLiteral("导出时间,%1\n").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    ts << QStringLiteral("统计周期,%1\n").arg(m_periodFilter ? m_periodFilter->selectedText() : QStringLiteral("全部"));

    return csv;
}

// [V7.2 2026-06-24] 检测USB设备挂载路径（Windows/Linux）
QString LedgerStatsPage::detectUSBDrive() {
#ifdef Q_OS_WIN
    // Windows：遍历D:到Z:盘符，查找可移动磁盘
    foreach (const QFileInfo& info, QDir::drives()) {
        QString drivePath = info.absolutePath();
        UINT driveType = GetDriveTypeW(reinterpret_cast<const wchar_t*>(drivePath.utf16()));
        if (driveType == DRIVE_REMOVABLE) {
            return drivePath;
        }
    }
#else
    // Linux：检查 /media/ 和 /mnt/ 下的挂载点
    QStringList searchPaths = {"/media", "/mnt"};
    for (const QString& basePath : searchPaths) {
        QDir dir(basePath);
        if (!dir.exists()) continue;
        QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& entry : entries) {
            QString mountPath = entry.absoluteFilePath();
            // 检查是否为可写挂载点
            QFileInfo mountInfo(mountPath);
            if (mountInfo.isWritable() && mountInfo.isReadable()) {
                return mountPath;
            }
        }
    }
#endif
    return QString();
}

// [2026-06-25] 替换为MessageDialog
void LedgerStatsPage::showExportSuccess(const QString& path) {
    MessageDialog::showSuccess(this, QStringLiteral("导出成功"),
        QStringLiteral("台账数据已成功导出！\n\n文件位置：\n%1").arg(path));
}

// [2026-06-24v2] 数字滚动动画：每次从1开始跳转到目标值（与DashboardPage统一风格）
void LedgerStatsPage::setStatValue(QLabel* label, int targetValue) {
    if (!label) return;

    QWidget* card = label->parentWidget();
    if (!card) {
        label->setText(QString::number(targetValue));
        return;
    }

    QVariantMap animData = card->property("animData").toMap();
    animData["targetValue"] = targetValue;
    animData["currentValue"] = 1;  // 始终从1开始滚动
    animData["steps"] = 0;
    animData["maxSteps"] = 20;
    card->setProperty("animData", animData);

    QTimer* animTimer = card->findChild<QTimer*>();
    if (!animTimer) {
        animTimer = new QTimer(card);
        animTimer->setInterval(30);
        QObject::connect(animTimer, &QTimer::timeout, card, [card, label, animTimer]() {
            QVariantMap data = card->property("animData").toMap();
            if (data.isEmpty()) { animTimer->stop(); return; }
            int current = data["currentValue"].toInt();
            int target = data["targetValue"].toInt();
            int steps = data["steps"].toInt();
            int maxSteps = data["maxSteps"].toInt();
            if (steps >= maxSteps) {
                label->setText(QString::number(target));
                card->setProperty("animData", QVariantMap());
                animTimer->stop();
                return;
            }
            int newValue = current + (target - current) * (steps + 1) / maxSteps;
            label->setText(QString::number(newValue));
            data["currentValue"] = newValue;
            data["steps"] = steps + 1;
            card->setProperty("animData", data);
        });
    }
    animTimer->start();
}
