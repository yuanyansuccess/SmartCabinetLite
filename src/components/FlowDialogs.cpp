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
#include <QScroller>          // 触屏拖动清单
#include <QGuiApplication>    // 取屏幕可用高度封顶弹窗
#include <QScreen>
#include <QWidget>

namespace {
const int kTitleIconFontSize = 28;   // 标题栏图标字号(px)
const int kTipIconFontSize   = 20;   // 提示条图标字号(px)
const int kListRowHeight     = 38;   // 清单表格行高(px)
const int kTableHeaderHeight = 38;   // 清单表格表头高度(px)
const int kMinDialogHeight   = 460;  // 骨架弹窗高度下限，短单据维持原有高度
const int kFixedPartsHeight  = 300;  // 骨架弹窗内除清单表外的固定高度（标题/信息/流水/按钮/边距/间距）
const int kMaxVisibleRows    = 6;    // 清单表最多铺开的行数，其余由表格自身滚动承接
const int kScreenMargin      = 80;   // 屏幕上下为任务栏与窗口装饰预留的余量

/**
 * @brief 按屏幕可用高度给弹窗高度封顶，避免清单过长把底部按钮行挤出屏幕
 * @param parent 父窗口，用于取所在屏幕；为空时取主屏
 * @param wanted 按清单行数算出的期望高度
 * @param minHeight 弹窗高度下限
 * @return 封顶后的弹窗有效高度；短单据等于期望高度，长单据不超过屏幕可用高度
 */
static int clampDialogHeight(QWidget* parent, int wanted, int minHeight) {
    // 弹窗最高只到屏幕可用高度减去上下余量，标题与底部按钮始终留在屏幕内
    QScreen* screen = parent ? parent->screen() : nullptr;
    if (!screen) screen = QGuiApplication::primaryScreen();
    const int screenLimit = screen
        ? qMax(minHeight, screen->availableGeometry().height() - kScreenMargin)
        : qMax(minHeight, wanted);  // 取不到屏幕信息时不封顶，维持原有高度
    return qBound(minHeight, wanted, screenLimit);
}

/**
 * @brief 按弹窗有效高度反算清单表高度，多余的清单行由表格自身滚动承接
 * @param wanted 按行数算出的期望表格高度（已含表格下限）
 * @param dialogHeight 封顶后的弹窗有效高度
 * @param fixedParts 弹窗内除表格外的固定高度（标题/信息/按钮/边距）
 * @param header 表头高度
 * @param row 单行高度
 * @param maxRows 表格最多铺开的行数
 * @return 表格高度，与固定部分相加不超过弹窗高度；短单据等于期望高度
 */
static int clampTableHeight(int wanted, int dialogHeight, int fixedParts,
                            int header, int row, int maxRows) {
    const int minTable = header + row;
    const int maxTable = maxRows * row + header;
    return qBound(minTable, qMin(wanted, dialogHeight - fixedParts), maxTable);
}
}

/**
 * @brief 创建对话框
 * @param parent 父窗口
 * @param setup 对话框配置（标题/尺寸/边距等，详见 Setup 结构）
 * @param body 可选输出参数，返回主布局指针供调用方添加内容
 * @param outEffectiveHeight 可选输出参数，回写屏幕封顶后的窗口有效高度供 fillTable 反算表格
 * @return 新建的对话框实例
 */
QDialog* FlowDialogs::createDialog(QWidget* parent, const Setup& setup, QVBoxLayout** body,
                                   int* outEffectiveHeight) {
    QDialog* dlg = new QDialog(parent);
    dlg->setWindowTitle(setup.windowTitle);
    dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dlg->setStyleSheet(StyleHelper::dialogStyle());
    // 骨架是弹窗高度的唯一钳制点：调用方给期望高度，此处按屏幕可用高度封顶，
    // 清单过长也不把底部按钮挤出屏幕；封顶后的有效高度回写供 fillTable 反算表格高度。
    const int effectiveHeight = clampDialogHeight(parent, setup.height, kMinDialogHeight);
    dlg->setFixedSize(setup.width, effectiveHeight);
    if (outEffectiveHeight) {
        *outEffectiveHeight = effectiveHeight;
    }

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
 * @param body 目标垂直布局
 * @param icon 标题前的图标字符
 * @param title 标题文本
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
 * @brief 创建列表表格
 * @param headers 表头文本列表
 * @return 新建的表格控件
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
    // 清单条目多时可手指拖动查看全部，不依赖细滚动条
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    QScroller::grabGesture(table->viewport(), QScroller::LeftMouseButtonGesture);
    return table;
}

/**
 * @brief 用清单数据填充确认表格并按窗口有效高度确定表格高度
 * @param rows 清单行数据
 * @param placeholder 单元格为空时显示的占位文本
 * @param maxTableHeight 骨架回写的窗口有效高度；大于 0 时按固定部分反算表格高度，
 *                       多余清单行由表格滚动承接，底部按钮行不被挤出；-1 沿用原有按行数的高度
 */
void FlowDialogs::fillTable(QTableWidget* table, const QList<QStringList>& rows,
                            const QString& placeholder, int maxTableHeight) {
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
    const int wanted = qMax(kTableHeaderHeight + kListRowHeight,
                            static_cast<int>(rows.size()) * kListRowHeight + kTableHeaderHeight);
    const int tableHeight = (maxTableHeight > 0)
        ? clampTableHeight(wanted, maxTableHeight, kFixedPartsHeight,
                           kTableHeaderHeight, kListRowHeight, kMaxVisibleRows)
        : wanted;  // 未传窗口有效高度时维持原有按行数计算的高度，两个现有调用零改动可编译
    table->setFixedHeight(tableHeight);
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
