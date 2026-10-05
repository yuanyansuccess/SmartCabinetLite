/**
 * @file PaginationBar.cpp
 * @brief 通用分页栏组件实现
 * @author 袁燕
 */
#include "PaginationBar.h"
#include "utils/StyleHelper.h"  // 字号统一走 StyleHelper::Token

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

PaginationBar::PaginationBar(QWidget* parent) : QWidget(parent) {
    setupUI();
}

/**
 * @brief 构建分页栏：上一页/下一页按钮与页码信息
 */
void PaginationBar::setupUI() {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    layout->addStretch();

    m_prevBtn = new QPushButton(QStringLiteral("上一页"));
    m_prevBtn->setStyleSheet(
        "QPushButton{border:1px solid #ddd;border-radius:6px;padding:5px 12px;"
        + StyleHelper::fontSize(StyleHelper::Token::FontSmall) + "font-weight:600;color:#555;background:#fff;min-height:30px;}"
        "QPushButton:hover{border-color:#4da3ff;color:#4da3ff;}"
        "QPushButton:disabled{opacity:0.35;}"
    );
    m_prevBtn->setCursor(Qt::PointingHandCursor);
    connect(m_prevBtn, &QPushButton::clicked, this, &PaginationBar::prevClicked);
    layout->addWidget(m_prevBtn);

    m_pageLabel = new QLabel(QStringLiteral("第 1 页"));
    m_pageLabel->setStyleSheet(StyleHelper::fontSize(StyleHelper::Token::FontSmall) + "color:#999;padding:0 4px;");
    layout->addWidget(m_pageLabel);

    m_nextBtn = new QPushButton(QStringLiteral("下一页"));
    m_nextBtn->setStyleSheet(
        "QPushButton{border:1px solid #ddd;border-radius:6px;padding:5px 12px;"
        + StyleHelper::fontSize(StyleHelper::Token::FontSmall) + "font-weight:600;color:#555;background:#fff;min-height:30px;}"
        "QPushButton:hover{border-color:#4da3ff;color:#4da3ff;}"
        "QPushButton:disabled{opacity:0.35;}"
    );
    m_nextBtn->setCursor(Qt::PointingHandCursor);
    connect(m_nextBtn, &QPushButton::clicked, this, &PaginationBar::nextClicked);
    layout->addWidget(m_nextBtn);

    m_totalLabel = new QLabel(QStringLiteral("共 0 条"));
    m_totalLabel->setStyleSheet(StyleHelper::fontSize(StyleHelper::Token::FontSmall) + "color:#999;");
    layout->addWidget(m_totalLabel);
}

/**
 * @brief 设置页面信息
 */
void PaginationBar::setPageInfo(int currentPage, int totalPages) {
    const int safeTotal = qMax(1, totalPages);
    m_pageLabel->setText(QStringLiteral("第 %1/%2 页").arg(currentPage).arg(safeTotal));
    m_prevBtn->setEnabled(currentPage > 1);
    m_nextBtn->setEnabled(currentPage < safeTotal);
}

/**
 * @brief 设置总数记录
 */
void PaginationBar::setTotalRecords(int totalRecords) {
    m_totalLabel->setText(QStringLiteral("共 %1 条").arg(totalRecords));
}

/**
 * @brief 读取当前页码显示文本
 * @return 形如"第 3 / 10 页"的文本
 */
QString PaginationBar::pageText() const { return m_pageLabel->text(); }
/**
 * @brief 读取总条数显示文本
 * @return 形如"共 128 条"的文本
 */
QString PaginationBar::totalText() const { return m_totalLabel->text(); }
/**
 * @brief 判断上一页按钮是否可用
 * @return true=可回到上一页
 */
bool PaginationBar::prevEnabled() const { return m_prevBtn->isEnabled(); }
/**
 * @brief 判断下一页按钮是否可用
 * @return true=可进入下一页
 */
bool PaginationBar::nextEnabled() const { return m_nextBtn->isEnabled(); }
