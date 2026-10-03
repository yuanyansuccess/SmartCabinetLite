/**
 * @file ResultDialog.cpp
 * @brief 流程结果对话框骨架实现（样式与原各页面内联实现逐字一致）
 * @author 袁燕
 */
#include "ResultDialog.h"

#include "utils/StyleHelper.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ResultDialog::ResultDialog(const QString& windowTitle, const QString& icon,
                           const QString& title, const QString& titleColor,
                           int spacing, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(windowTitle);
    setStyleSheet(StyleHelper::dialogStyle());
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(32, 32, 32, 24);
    m_layout->setSpacing(spacing);

    // 结果图标 — background:transparent防止灰色底
    auto* iconLabel = new QLabel(icon);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet("font-size:56px;background:transparent;");
    m_layout->addWidget(iconLabel);

    // 大标题
    auto* titleLabel = new QLabel(title);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet(QString(
        "font-size:24px;font-weight:bold;color:%1;background:transparent;").arg(titleColor));
    m_layout->addWidget(titleLabel);

    // 详情槽位
    m_body = new QVBoxLayout();
    m_body->setContentsMargins(0, 0, 0, 0);
    m_body->setSpacing(0);
    m_layout->addLayout(m_body);
}

void ResultDialog::setHint(const QString& text) {
    auto* hintLabel = new QLabel(text);
    hintLabel->setWordWrap(true);
    hintLabel->setAlignment(Qt::AlignCenter);
    hintLabel->setStyleSheet("font-size:14px;color:#555;background:transparent;");
    m_layout->addWidget(hintLabel);
}

void ResultDialog::addFinishButtonCentered(const QString& text, const QString& style) {
    auto* okBtn = new QPushButton(text);
    okBtn->setStyleSheet(style);
    okBtn->setCursor(Qt::PointingHandCursor);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    m_layout->addWidget(okBtn, 0, Qt::AlignCenter);
}

void ResultDialog::addFinishButtonRow(const QString& text, const QString& style, int minWidth) {
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* finishBtn = new QPushButton(text);
    finishBtn->setStyleSheet(style);
    finishBtn->setCursor(Qt::PointingHandCursor);
    finishBtn->setMinimumHeight(StyleHelper::Token::ControlHeight);
    if (minWidth > 0) finishBtn->setMinimumWidth(minWidth);
    connect(finishBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(finishBtn);
    btnRow->addStretch();
    m_layout->addLayout(btnRow);
}
