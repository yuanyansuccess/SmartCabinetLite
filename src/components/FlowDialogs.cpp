/**
 * @file FlowDialogs.cpp
 * @brief 借用/归还/入库/出库四类页面共用的对话框骨架实现
 * @author 袁燕
 */
#include "FlowDialogs.h"
#include "utils/StyleHelper.h"

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QAbstractItemView>

namespace {
const int kTitleIconFontSize = 28;   // 标题栏图标字号(px)
const int kTipIconFontSize   = 20;   // 提示条图标字号(px)
const int kListRowHeight     = 38;   // 清单表格行高(px)
const int kTableHeaderHeight = 38;   // 清单表格表头高度(px)
}

/**
 * @brief 创建对话框
 */
QDialog* FlowDialogs::createDialog(QWidget* parent, const Setup& setup, QVBoxLayout** body) {
    QDialog* dlg = new QDialog(parent);
    dlg->setWindowTitle(setup.windowTitle);
    dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dlg->setStyleSheet(StyleHelper::dialogStyle());
    dlg->setFixedSize(setup.width, setup.height);

    QVBoxLayout* mainLayout = new QVBoxLayout(dlg);
    mainLayout->setContentsMargins(setup.margin, 28, setup.margin, 24);
    mainLayout->setSpacing(setup.spacing);

    if (body) {
        *body = mainLayout;
    }
    return dlg;
}

/**
 * @brief 添加标题
 */
void FlowDialogs::addTitle(QVBoxLayout* body, const QString& icon, const QString& title) {
    QHBoxLayout* titleBar = new QHBoxLayout();
    auto* iconLabel = new QLabel(icon);
    iconLabel->setStyleSheet(QStringLiteral("font-size:%1px;background:transparent;").arg(kTitleIconFontSize));
    auto* titleLabel = new QLabel(title);
    titleLabel->setStyleSheet(StyleHelper::dialogTitleText());
    titleBar->addWidget(iconLabel);
    titleBar->addWidget(titleLabel);
    titleBar->addStretch();
    body->addLayout(titleBar);
}

/**
 * @brief 添加提示栏
 */
void FlowDialogs::addTipBar(QVBoxLayout* body, const QString& icon, const QString& text, int padding) {
    auto* tipFrame = new QFrame();
    tipFrame->setObjectName("tipFrame");
    tipFrame->setStyleSheet(
        "QFrame#tipFrame{background:#e6f7ff;border:1px solid #91d5ff;"
        "border-radius:10px;padding:10px 14px;}"
    );
    QHBoxLayout* tipLayout = new QHBoxLayout(tipFrame);
    tipLayout->setContentsMargins(12, padding, 12, padding);
    auto* tipIcon = new QLabel(icon);
    tipIcon->setStyleSheet(QStringLiteral("font-size:%1px;background:transparent;").arg(kTipIconFontSize));
    auto* tipText = new QLabel(text);
    tipText->setStyleSheet(QStringLiteral("font-size:15px;color:#1890ff;font-weight:600;background:transparent;"));
    tipLayout->addWidget(tipIcon);
    tipLayout->addWidget(tipText, 1);
    body->addWidget(tipFrame);
}

/**
 * @brief 创建列表表格
 */
QTableWidget* FlowDialogs::createListTable(const QStringList& headers) {
    auto* table = new QTableWidget();
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setStyleSheet(StyleHelper::listTableStyle());
    for (int col = 0; col < headers.size(); ++col) {
        table->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Stretch);
    }
    return table;
}

/**
 * @brief 用清单数据填充确认表格
 * @param rows 清单行数据
 * @param headers 表头列名
 */
void FlowDialogs::fillTable(QTableWidget* table, const QList<QStringList>& rows,
                            const QString& placeholder) {
    table->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        const QStringList& cells = rows[row];
        for (int col = 0; col < cells.size() && col < table->columnCount(); ++col) {
            const QString& text = cells[col];
            table->setItem(row, col, new QTableWidgetItem(
                text.isEmpty() ? placeholder : text));
        }
        table->setRowHeight(row, kListRowHeight);
    }
    table->setFixedHeight(qMax(kTableHeaderHeight + kListRowHeight,
                                rows.size() * kListRowHeight + kTableHeaderHeight));
}

/**
 * @brief 在表格下方追加合计或提示信息行
 * @param text 提示文本
 */
void FlowDialogs::addFooter(QDialog* dlg, QVBoxLayout* body, const Setup& setup,
                            const QString& actionText,
                            const std::function<void()>& onAccepted,
                            const QString& cancelText) {
    const int buttonHeight = setup.buttonHeight > 0
        ? setup.buttonHeight
        : StyleHelper::Token::ControlHeight;

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(setup.footerSpacing);
    btnLayout->addStretch();

    auto* cancelBtn = new QPushButton(cancelText);
    cancelBtn->setStyleSheet(StyleHelper::buttonDefault());
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setMinimumHeight(buttonHeight);
    cancelBtn->setMinimumWidth(setup.cancelWidth);
    QObject::connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);

    auto* actionBtn = new QPushButton(actionText);
    actionBtn->setStyleSheet(StyleHelper::buttonPrimary());
    actionBtn->setCursor(Qt::PointingHandCursor);
    actionBtn->setMinimumHeight(buttonHeight);
    actionBtn->setMinimumWidth(setup.actionWidth);
    QObject::connect(actionBtn, &QPushButton::clicked, dlg, [dlg, onAccepted]() {
        dlg->accept();
        if (onAccepted) {
            onAccepted();
        }
    });

    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(actionBtn);
    body->addLayout(btnLayout);
}
