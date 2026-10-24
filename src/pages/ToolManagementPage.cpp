/**
 * @file ToolManagementPage.cpp
 * @brief 工具管理页面实现 - 统计卡片导航 + 工具清单查询对话框
 * @author 袁燕
 *   1. 页面中部四张统计卡片：工具总数/在库工具/已借出/已出库
 *   2. 点击"在库工具""已借出"卡片弹出工具清单对话框（ToolListDialog）
 *   3. 统计按工具件数计算，工具总数=在库+已借出（不含出库/待入库）
 */
#include "ToolManagementPage.h"
#include "ui_ToolManagementPage.h"
#include "ToolListDialog.h"
#include "controller/ToolController.h"
#include "common/AppConfig.h"
#include "common/Constants.h"
#include <QLabel>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>

/**
 * @brief 构造函数，初始化 ToolManagementPage 对象并连接信号槽
 * @param parent 父对象指针，作为 Qt 对象树父节点接管所有权
 */
ToolManagementPage::ToolManagementPage(QWidget* parent) : QWidget(parent), ui(new Ui::ToolManagementPage),
    m_toolDialog(nullptr), m_editToolId(0) {
    // 静态布局来自ToolManagementPage.ui（Qt Designer可视化维护）
    ui->setupUi(this);
    setupUI();
    loadStats();  // 加载统计
}

ToolManagementPage::~ToolManagementPage() {
    delete ui;
}

// 创建单个统计卡片
static QFrame* createStatCard(const QString& title, const QString& icon, const QString& color, QLabel*& countLabel) {
    auto* card = new QFrame();
    card->setFixedHeight(SC::STAT_CARD_HEIGHT);
    card->setStyleSheet(QString(
        "QFrame{background:#fff;border-radius:%1px;}"
    ).arg(SC::STAT_CARD_ICON_RADIUS));
    auto* layout = new QHBoxLayout(card);
    layout->setContentsMargins(24, SC::STAT_CARD_SPACING, 24, SC::STAT_CARD_SPACING);
    layout->setSpacing(SC::STAT_CARD_SPACING);

    // 图标
    auto* iconLabel = new QLabel(icon);
    iconLabel->setFixedSize(SC::STAT_CARD_ICON_SIZE, SC::STAT_CARD_ICON_SIZE);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet(QString(
        "QLabel{background:%1;border-radius:%2px;font-size:%3px;color:#fff;}"
    ).arg(color).arg(SC::STAT_CARD_ICON_RADIUS).arg(SC::STAT_CARD_ICON_FONT));

    // 文字区域
    auto* textLayout = new QVBoxLayout();
    textLayout->setSpacing(6);
    auto* titleLabel = new QLabel(title);
    titleLabel->setStyleSheet(QString("font-size:%1px;color:#999;background:transparent;")
                              .arg(SC::FONT_SIZE_SMALL));
    countLabel = new QLabel("0");
    countLabel->setStyleSheet(QString("font-size:%1px;font-weight:700;color:%2;background:transparent;")
                              .arg(SC::STAT_CARD_VALUE_FONT).arg(color));
    textLayout->addWidget(titleLabel);
    textLayout->addWidget(countLabel);

    layout->addWidget(iconLabel);
    layout->addLayout(textLayout);
    layout->addStretch();
    return card;
}

/**
 * @brief 初始化统计卡片：工具总数/在库工具/已借出/已出库
 */
void ToolManagementPage::setupStatsCards() {
    // 统计按"种类+位置"唯一标识排列
    // 工具总数=在库+已借出（不含出库/待入库）
    m_statsCardAll        = createStatCard(QStringLiteral("工具总数"), QStringLiteral("📦"), "#4da3ff", m_labelTotalTools);
    m_statsCardInStock    = createStatCard(QStringLiteral("在库工具"), QStringLiteral("✅"), "#43a047", m_labelInStock);
    m_statsCardBorrowed   = createStatCard(QStringLiteral("已借出"),   QStringLiteral("📤"), "#f57c00", m_labelBorrowed);
    m_statsCardCheckedOut = createStatCard(QStringLiteral("已出库"),   QStringLiteral("📤"), "#7c3aed", m_labelCheckedOut);
}

/**
 * @brief 构建页面界面：读取 .ui 静态布局并补充动态控件
 */
void ToolManagementPage::setupUI() {
    // 口径说明行：统计数字与借用/出入库页走同一口径（本机组），写清楚范围避免被当成"数据对不上"
    m_statsScopeHint = new QLabel();
    m_statsScopeHint->setWordWrap(true);
    m_statsScopeHint->setAlignment(Qt::AlignCenter);
    m_statsScopeHint->setStyleSheet(QString(
        "QLabel{font-size:%1px;color:#888;background:transparent;padding:6px 12px;}"
    ).arg(SC::FONT_SIZE_SMALL));
    ui->mainLayout->insertWidget(0, m_statsScopeHint);

    // 统计卡片装入.ui容器（2x2田字居中布局，间隔24px，由.ui弹性spacer垂直居中）
    setupStatsCards();
    ui->statsLayout->addWidget(m_statsCardAll, 0, 0);
    ui->statsLayout->addWidget(m_statsCardInStock, 0, 1);
    ui->statsLayout->addWidget(m_statsCardBorrowed, 1, 0);
    ui->statsLayout->addWidget(m_statsCardCheckedOut, 1, 1);

    // 在库/已借出卡片可点击，打开对应状态的清单对话框
    m_statsCardInStock->setCursor(Qt::PointingHandCursor);
    m_statsCardInStock->setToolTip(QStringLiteral("点击查看在库工具清单"));
    m_statsCardInStock->installEventFilter(this);
    m_statsCardBorrowed->setCursor(Qt::PointingHandCursor);
    m_statsCardBorrowed->setToolTip(QStringLiteral("点击查看已借出工具清单"));
    m_statsCardBorrowed->installEventFilter(this);
}

/**
 * @brief 事件过滤器：点击"在库工具""已借出"卡片打开清单对话框
 * @param watched 被监控对象
 * @param event 事件
 * @return 基类结果（本页不拦截事件）
 */
bool ToolManagementPage::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonRelease) {
        if (watched == m_statsCardInStock) {
            openToolListDialog(SC::TOOL_IN_STOCK, QStringLiteral("在库工具"));
        } else if (watched == m_statsCardBorrowed) {
            openToolListDialog(SC::TOOL_BORROWED, QStringLiteral("已借出工具"));
        }
    }
    return QWidget::eventFilter(watched, event);
}

/**
 * @brief 打开指定状态的工具清单对话框，关闭后刷新统计
 * @param status 过滤的工具状态（SC::TOOL_*）
 * @param title 对话框标题
 */
void ToolManagementPage::openToolListDialog(const QString& status, const QString& title) {
    ToolListDialog dlg(status, title, this);
    connect(&dlg, &ToolListDialog::detailRequested, this, &ToolManagementPage::onDetailTool);
    dlg.exec();
    loadStats();  // 详情内操作可能影响库存统计，关闭后刷新
}

/**
 * @brief 刷新页面统计显示
 */
void ToolManagementPage::refresh() {
    loadStats();
}

/**
 * @brief 按工具件数统计（total_qty/current_qty/borrowed_qty），不按种类数 加载统计数据
 */
void ToolManagementPage::loadStats() {
    // 与借用/出入库页同一口径：只统计本机组（本机柜里的工具），
    // 未配置本机机组时才退化为全库统计，并在说明行写明
    const int groupId = AppConfig::instance().localMachineGroupId();
    ToolController ctrl;
    QJsonObject stats = ctrl.getToolStats(groupId);

    if (m_statsScopeHint) {
        const QString scope = groupId > 0
            ? QStringLiteral("本机组（编号 %1）").arg(groupId)
            : QStringLiteral("全部机组（未配置本机机组）");
        m_statsScopeHint->setText(QStringLiteral("统计范围：%1 · 与借用/出入库页一致 · 工具总数=在库+已借出").arg(scope));
    }
    // 统计按位置维度计算：工具总数 = 在库 + 已借出
    int total = stats["inStockCount"].toInt() + stats["borrowedCount"].toInt();

    m_labelTotalTools->setText(QString::number(total));
    m_labelInStock->setText(QString::number(stats["inStockCount"].toInt()));
    m_labelBorrowed->setText(QString::number(stats["borrowedCount"].toInt()));
    m_labelCheckedOut->setText(QString::number(stats["checkedOutCount"].toInt()));
}
