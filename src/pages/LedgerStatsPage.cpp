/**
 * @file LedgerStatsPage.cpp
 * @brief 台账统计页面实现 - UI与逻辑分离：静态布局在LedgerStatsPage.ui，本文件只含动态构建与业务逻辑
 * @author 袁燕
 */
#include "LedgerStatsPage.h"
#include "ui_LedgerStatsPage.h"
#include <QTableWidget>  // 统计明细表格
#include "utils/StyleHelper.h"
#include "services/SettingService.h"
#include "components/SingleSelectFilter.h"  // 通用单选筛选组件
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

LedgerStatsPage::LedgerStatsPage(QWidget* parent) : QWidget(parent), ui(new Ui::LedgerStatsPage) {
    ui->setupUi(this);  // 静态布局来自LedgerStatsPage.ui（Qt Designer可视化维护）
    setupUI();          // 动态部分：统计卡片、筛选组件、信号槽连接
}

LedgerStatsPage::~LedgerStatsPage() {
    delete ui;
}

void LedgerStatsPage::setupUI() {
    // 桥接.ui控件（业务逻辑沿用m_成员，零改动）
    m_exportLedgerBtn = ui->exportLedgerBtn;
    m_categoryTable = ui->categoryTable;
    m_deptTable = ui->deptTable;
    connect(m_exportLedgerBtn, &QPushButton::clicked, this, &LedgerStatsPage::onExportLedger);

    // 统计卡片行（卡片挂载数字滚动动画属性，运行时构建）
    auto createCard = [&](const QString& labelText, QLabel*& valLabel, const QString& color) -> QWidget* {
        auto* card = new QWidget();
        card->setStyleSheet("background:white;border-radius:14px;border:none;");
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
        // 初始化数字滚动动画属性
        QVariantMap animData;
        animData["targetValue"] = 0;
        animData["currentValue"] = 0;
        animData["steps"] = 0;
        animData["maxSteps"] = 20;
        card->setProperty("animData", animData);
        return card;
    };

    ui->cardsLayout->addWidget(createCard(QStringLiteral("总借用次数"), m_totalBorrowLabel, StyleHelper::primaryColor()));
    ui->cardsLayout->addWidget(createCard(QStringLiteral("总归还次数"), m_totalReturnLabel, StyleHelper::successColor()));
    ui->cardsLayout->addWidget(createCard(QStringLiteral("当前借用中"), m_currentBorrowedLabel, StyleHelper::warningColor()));
    ui->cardsLayout->addWidget(createCard(QStringLiteral("逾期未还"), m_overdueLabel, StyleHelper::dangerColor()));

    // 时间筛选 自定义单选组件，动态创建后装入.ui预留槽位
    m_periodFilter = new SingleSelectFilter(QStringLiteral("本月"), this);
    m_periodFilter->setOptions({QStringLiteral("本月"), QStringLiteral("本季度"), QStringLiteral("本年度"), QStringLiteral("全部")});
    connect(m_periodFilter, &SingleSelectFilter::selectionChanged, this, [this](const QString&) { onFilterChanged(); });
    ui->filterSlotLayout->addWidget(m_periodFilter);
}

void LedgerStatsPage::refresh() { loadStats(); }

void LedgerStatsPage::onFilterChanged() { loadStats(); }

void LedgerStatsPage::loadStats() {
    SettingService svc;
    QJsonObject stats = svc.getLedgerStats();

    // 数字滚动动画，与DashboardPage风格统一
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

// 台账导出：生成CSV文件并保存到桌面
// 彻底修复编码问题：QTextStream在Windows中文环境默认GBK编码，
// 导致BOM(UTF-8)与内容(GBK)编码不一致，Excel打开乱码。
// 直接用QFile::write()写入UTF-8字节流，保证BOM和内容编码统一。
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

    // 直接用二进制方式写入UTF-8字节流：BOM + UTF-8内容
    file.write("\xEF\xBB\xBF");              // UTF-8 BOM (3字节)
    file.write(csv.toUtf8());                // CSV内容转UTF-8字节流
    file.close();

    showExportSuccess(filePath);
}

// 生成台账CSV内容
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

// 替换为MessageDialog
void LedgerStatsPage::showExportSuccess(const QString& path) {
    MessageDialog::showSuccess(this, QStringLiteral("导出成功"),
        QStringLiteral("台账数据已成功导出！\n\n文件位置：\n%1").arg(path));
}

// 数字滚动动画：每次从1开始跳转到目标值（与DashboardPage统一风格）
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
