/**
 * @file PaginationBar.cpp
 * @brief 通用分页栏组件实现
 * @author 袁燕
 */
#include "PaginationBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

PaginationBar::PaginationBar(QWidget* parent) : QWidget(parent) {
    setupUI();
}

void PaginationBar::setupUI() {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    layout->addStretch();

    m_prevBtn = new QPushButton(QStringLiteral("上一页"));
    m_prevBtn->setStyleSheet(
        "QPushButton{border:1px solid #ddd;border-radius:6px;padding:5px 12px;"
        "font-size:13px;font-weight:600;color:#555;background:#fff;min-height:30px;}"
        "QPushButton:hover{border-color:#4da3ff;color:#4da3ff;}"
        "QPushButton:disabled{opacity:0.35;}"
    );
    m_prevBtn->setCursor(Qt::PointingHandCursor);
    connect(m_prevBtn, &QPushButton::clicked, this, &PaginationBar::prevClicked);
    layout->addWidget(m_prevBtn);

    m_pageLabel = new QLabel(QStringLiteral("第 1 页"));
    m_pageLabel->setStyleSheet("font-size:13px;color:#999;padding:0 4px;");
    layout->addWidget(m_pageLabel);

    m_nextBtn = new QPushButton(QStringLiteral("下一页"));
    m_nextBtn->setStyleSheet(
        "QPushButton{border:1px solid #ddd;border-radius:6px;padding:5px 12px;"
        "font-size:13px;font-weight:600;color:#555;background:#fff;min-height:30px;}"
        "QPushButton:hover{border-color:#4da3ff;color:#4da3ff;}"
        "QPushButton:disabled{opacity:0.35;}"
    );
    m_nextBtn->setCursor(Qt::PointingHandCursor);
    connect(m_nextBtn, &QPushButton::clicked, this, &PaginationBar::nextClicked);
    layout->addWidget(m_nextBtn);

    m_totalLabel = new QLabel(QStringLiteral("共 0 条"));
    m_totalLabel->setStyleSheet("font-size:13px;color:#999;");
    layout->addWidget(m_totalLabel);
}

void PaginationBar::setPageInfo(int currentPage, int totalPages) {
    const int safeTotal = qMax(1, totalPages);
    m_pageLabel->setText(QStringLiteral("第 %1/%2 页").arg(currentPage).arg(safeTotal));
    m_prevBtn->setEnabled(currentPage > 1);
    m_nextBtn->setEnabled(currentPage < safeTotal);
}

void PaginationBar::setTotalRecords(int totalRecords) {
    m_totalLabel->setText(QStringLiteral("共 %1 条").arg(totalRecords));
}

QString PaginationBar::pageText() const { return m_pageLabel->text(); }
QString PaginationBar::totalText() const { return m_totalLabel->text(); }
bool PaginationBar::prevEnabled() const { return m_prevBtn->isEnabled(); }
bool PaginationBar::nextEnabled() const { return m_nextBtn->isEnabled(); }
