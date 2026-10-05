/**
 * @file UserEntryDialog.cpp
 * @brief 普通用户功能选择对话框实现 - UI与逻辑分离：主界面静态布局在UserEntryDialog.ui
 * @author 袁燕
 * @说明 样式与CabinetSessionDialog开柜页保持一致：全屏、浅灰背景、大圆角按钮。
 *   触屏优化：按钮高110px、字号26px，符合8寸屏触控标准。
 *   借用明细/告警日志对话框为运行时动态构建（含数据填充），保留在本文件。
 */
#include "UserEntryDialog.h"
#include "ui_UserEntryDialog.h"
#include "utils/StyleHelper.h"
#include "db/RecordDAO.h"
#include "db/AlertDAO.h"
#include "services/AlertService.h"
#include "common/Constants.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QFont>
#include <QColor>
#include <QJsonArray>

UserEntryDialog::UserEntryDialog(const QJsonObject& user, QWidget* parent)
    : QDialog(parent), m_user(user), ui(new Ui::UserEntryDialog)
{
    ui->setupUi(this);  // 静态布局来自UserEntryDialog.ui（Qt Designer可视化维护）
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setModal(true);

    // 左上角"退出登录"
    connect(ui->logoutBtn, &QPushButton::clicked, this, &QDialog::reject);

    // 欢迎语（用户姓名运行时填充）
    ui->welcomeLabel->setText(QStringLiteral("您好，%1").arg(m_user["realName"].toString()));

    // 大按钮1：借用/归还
    connect(ui->borrowBtn, &QPushButton::clicked, this, [this]() {
        m_choice = Choice::BorrowReturn;
        accept();
    });

    // 大按钮2：查询 → 弹出本用户借用明细对话框
    connect(ui->queryBtn, &QPushButton::clicked, this, [this]() {
        showBorrowDetail();
    });

    // 大按钮3：告警日志
    connect(ui->alertBtn, &QPushButton::clicked, this, [this]() {
        showAlertDialog();
    });

    // 醒目提示：存在未处理告警时，在按钮下方以醒目颜色显示
    m_alertHintLabel = ui->alertHintLabel;
    m_alertHintLabel->setVisible(unhandledAlertCount() > 0);
}

UserEntryDialog::~UserEntryDialog()
{
    delete ui;
}

/**
 * @brief 打开功能选择对话框并等待用户选择
 * @return 用户选择的功能项；取消时返回无效值
 */
UserEntryDialog::Choice UserEntryDialog::execChoice()
{
    m_choice = Choice::Logout;  // 默认：直接关闭视为退出登录
    showFullScreen();
    raise();
    activateWindow();
    exec();  // 阻塞至用户选择/退出
    return m_choice;
}

// 查询：弹出当前用户借用明细对话框（全屏模态，触屏风格）
// 展示该用户全部借用记录（当前借用在前），关闭后停留在功能选择页
// 数据源：RecordDAO::findBorrowsByUser（DAO层现成接口，零新增SQL）
// 输入：无（使用m_user.userId）；输出：无（仅展示）
void UserEntryDialog::showBorrowDetail()
{
    db::RecordDAO dao;
    const QList<BorrowRecord> records = dao.findBorrowsByUser(m_user["userId"].toInt(), 200);

    // 统计：当前借用件数（borrowing/overdue）+ 累计借用次数
    int activeCount = 0;
    for (const auto& r : records) {
        if (r.status == SC::RECORD_BORROWING || r.status == SC::RECORD_OVERDUE) activeCount++;
    }

    QDialog dlg(this);
    dlg.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dlg.setModal(true);
    dlg.setStyleSheet(StyleHelper::dialogBackground());

    auto* lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(40, 28, 40, 36);
    lay->setSpacing(14);

    // 标题
    auto* title = new QLabel(QStringLiteral("借 用 明 细"));
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(StyleHelper::largeTitleText());
    lay->addWidget(title);

    // 统计行
    auto* statLabel = new QLabel(QStringLiteral("%1 · 当前借用 %2 件 · 累计借用 %3 次")
                                     .arg(m_user["realName"].toString())
                                     .arg(activeCount).arg(records.size()));
    statLabel->setAlignment(Qt::AlignCenter);
    statLabel->setStyleSheet(QString("font-size: 17px; color: %1; background: transparent;")
                                 .arg(StyleHelper::textSecondary()));
    lay->addWidget(statLabel);

    // 明细表格
    auto* table = new QTableWidget();
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({QStringLiteral("借用时间"), QStringLiteral("工具名称"),
                                      QStringLiteral("工具编号"), QStringLiteral("数量"),
                                      QStringLiteral("预计归还"), QStringLiteral("状态")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(false);
    table->setStyleSheet(
        "QTableWidget { background:white; border:1px solid #e0e0e0; border-radius:10px;"
        "  font-size:16px; color:#333; gridline-color:#eee; }"
        "QHeaderView::section { background:#f5f6f8; color:#999; font-size:14px; font-weight:600;"
        "  border:none; border-bottom:1px solid #e0e0e0; padding:10px 6px; }"
        "QTableWidget::item { padding:8px 6px; }");
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(46);  // 触屏行高
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);   // 工具名称撑满
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    table->setColumnWidth(0, 170);
    table->setRowCount(records.size());
    for (int i = 0; i < records.size(); ++i) {
        const BorrowRecord& r = records.at(i);
        table->setItem(i, 0, new QTableWidgetItem(r.borrowTime.toString("yyyy-MM-dd HH:mm")));
        table->setItem(i, 1, new QTableWidgetItem(r.toolName));
        table->setItem(i, 2, new QTableWidgetItem(r.toolCode));
        table->setItem(i, 3, new QTableWidgetItem(QString::number(r.quantity)));
        table->setItem(i, 4, new QTableWidgetItem(r.expectReturnTime.toString("yyyy-MM-dd HH:mm")));

        // 状态列：中文+配色（借用中蓝/已逾期红/已归还绿）
        QString statusText;
        QString statusColor;
        if (r.status == SC::RECORD_BORROWING) { statusText = "借用中"; statusColor = "#4da3ff"; }
        else if (r.status == SC::RECORD_OVERDUE)  { statusText = "已逾期"; statusColor = "#ff4d4f"; }
        else if (r.status == SC::RECORD_RETURNED) { statusText = "已归还"; statusColor = "#43a047"; }
        else                              { statusText = r.status; statusColor = "#666666"; }
        auto* statusItem = new QTableWidgetItem(statusText);
        statusItem->setForeground(QColor(statusColor));
        statusItem->setFont(QFont(QString(), -1, QFont::Bold));
        table->setItem(i, 5, statusItem);
    }
    if (records.isEmpty()) {
        auto* emptyLabel = new QLabel(QStringLiteral("暂无借用记录"));
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setStyleSheet(QString("font-size: 18px; color: %1; background: transparent; padding: 24px;")
                                      .arg(StyleHelper::textSecondary()));
        lay->addWidget(emptyLabel);
    }
    lay->addWidget(table, 1);

    // 关闭按钮（大，触屏友好）
    auto* closeBtn = new QPushButton(QStringLiteral("关  闭"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(QString(
        "QPushButton { background: %1; color: white; border: none; border-radius: 12px;"
        "  font-size: 20px; font-weight: 700; min-height: 56px; min-width: 260px; }"
        "QPushButton:hover { background: %2; }"
    ).arg(StyleHelper::primaryColor(), StyleHelper::primaryHover()));
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    lay->addWidget(closeBtn, 0, Qt::AlignCenter);

    dlg.showFullScreen();
    dlg.exec();
}

// 未处理告警数（提示显隐依据）
// 输入：无；返回：sys_alert中status=unhandled的记录数（查询失败返回0）
int UserEntryDialog::unhandledAlertCount()
{
    AlertService svc;
    return svc.unhandledCount();
}

// 告警日志：弹出系统告警列表对话框（全屏模态，触屏风格）
// 展示告警内容/类型/借用人/时间/状态，待处理排前；关闭后停留在功能选择页
// 数据源：AlertDAO::findAll（含JOIN类型字典/借用人/工具，DAO零改动）
// 输入：无；输出：无（仅展示）
void UserEntryDialog::showAlertDialog()
{
    db::AlertDAO dao;
    const QJsonObject result = dao.findAll(QString(), QString(), QString(),
                                           QString(), QString(), 1, 200);
    const QJsonArray list = result["list"].toArray();

    int unhandled = 0;
    for (const auto& v : list) {
        if (v.toObject()["status"].toString() == SC::ALERT_UNHANDLED) unhandled++;
    }

    QDialog dlg(this);
    dlg.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dlg.setModal(true);
    dlg.setStyleSheet(StyleHelper::dialogBackground());

    auto* lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(40, 28, 40, 36);
    lay->setSpacing(14);

    // 标题
    auto* title = new QLabel(QStringLiteral("告 警 日 志"));
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(StyleHelper::largeTitleText());
    lay->addWidget(title);

    // 统计行
    auto* statLabel = new QLabel(QStringLiteral("共 %1 条告警 · 待处理 %2 条")
                                     .arg(list.size()).arg(unhandled));
    statLabel->setAlignment(Qt::AlignCenter);
    statLabel->setStyleSheet(QString("font-size: 17px; color: %1; background: transparent;")
                                 .arg(unhandled > 0 ? "#ff4d4f" : StyleHelper::textSecondary()));
    lay->addWidget(statLabel);

    // 告警表格：时间/类型/告警内容/借用人/状态
    auto* table = new QTableWidget();
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("告警类型"),
                                      QStringLiteral("告警内容"), QStringLiteral("借用人"),
                                      QStringLiteral("状态")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setStyleSheet(
        "QTableWidget { background:white; border:1px solid #e0e0e0; border-radius:10px;"
        "  font-size:16px; color:#333; gridline-color:#eee; }"
        "QHeaderView::section { background:#f5f6f8; color:#999; font-size:14px; font-weight:600;"
        "  border:none; border-bottom:1px solid #e0e0e0; padding:10px 6px; }"
        "QTableWidget::item { padding:8px 6px; }");
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(46);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);   // 告警内容撑满
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table->setColumnWidth(0, 170);
    table->setRowCount(list.size());
    for (int i = 0; i < list.size(); ++i) {
        const QJsonObject a = list.at(i).toObject();
        table->setItem(i, 0, new QTableWidgetItem(a["createdAt"].toString()));
        table->setItem(i, 1, new QTableWidgetItem(a["alertType"].toString()));
        table->setItem(i, 2, new QTableWidgetItem(a["content"].toString()));
        // 借用人：硬件类告警无借用人显示"系统"
        const QString borrower = a["userName"].toString().trimmed();
        table->setItem(i, 3, new QTableWidgetItem(borrower.isEmpty() ? QStringLiteral("系统") : borrower));

        // 状态列：待处理红/已处理绿
        const bool isUnhandled = (a["status"].toString() == SC::ALERT_UNHANDLED);
        auto* statusItem = new QTableWidgetItem(isUnhandled ? QStringLiteral("待处理") : QStringLiteral("已处理"));
        statusItem->setForeground(QColor(isUnhandled ? "#ff4d4f" : "#43a047"));
        statusItem->setFont(QFont(QString(), -1, QFont::Bold));
        table->setItem(i, 4, statusItem);
    }
    if (list.isEmpty()) {
        auto* emptyLabel = new QLabel(QStringLiteral("当前无告警日志"));
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setStyleSheet(QString("font-size: 18px; color: %1; background: transparent; padding: 24px;")
                                      .arg(StyleHelper::textSecondary()));
        lay->addWidget(emptyLabel);
    }
    lay->addWidget(table, 1);

    // 关闭按钮
    auto* closeBtn = new QPushButton(QStringLiteral("关  闭"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(QString(
        "QPushButton { background: %1; color: white; border: none; border-radius: 12px;"
        "  font-size: 20px; font-weight: 700; min-height: 56px; min-width: 260px; }"
        "QPushButton:hover { background: %2; }"
    ).arg(StyleHelper::primaryColor(), StyleHelper::primaryHover()));
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    lay->addWidget(closeBtn, 0, Qt::AlignCenter);

    dlg.showFullScreen();
    dlg.exec();
}
