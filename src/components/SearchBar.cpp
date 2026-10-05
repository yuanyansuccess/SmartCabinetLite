/**
 * @file SearchBar.cpp
 * @brief 通用搜索栏组件实现（样式与原各页面内联样式逐字一致）
 * @author 袁燕
 */
#include "SearchBar.h"
#include "utils/StyleHelper.h"  // 字号统一走 StyleHelper::Token

#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>

SearchBar::SearchBar(QWidget* parent) : QFrame(parent) {
    setupUI();
}

/**
 * @brief 构建搜索栏
 */
void SearchBar::setupUI() {
    setStyleSheet(
        "QFrame{border:2px solid #e0e0e0;border-radius:12px;background:#fff;}"
    );

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 1, 0);  // 右内边距1px防止按钮覆盖边框圆角
    layout->setSpacing(0);

    m_edit = new QLineEdit(this);
    m_edit->setStyleSheet(
        "QLineEdit{border:none;padding:0 16px;" + StyleHelper::fontSize(StyleHelper::Token::FontInput) + "background:transparent;color:#333;min-height:42px;}"
    );
    layout->addWidget(m_edit);

    m_kbdBtn = new QPushButton(QStringLiteral("⌨"), this);
    m_kbdBtn->setFixedSize(46, 44);
    m_kbdBtn->setCursor(Qt::PointingHandCursor);
    m_kbdBtn->setStyleSheet(
        "QPushButton{border:none;border-radius:0 10px 10px 0;"
        "background:#f0f2f5;" + StyleHelper::fontSize(StyleHelper::Token::FontDisplay) + "color:#888;}"
        "QPushButton:hover{background:#e6f0ff;color:#4da3ff;}"
    );
    connect(m_kbdBtn, &QPushButton::clicked, this, &SearchBar::keyboardRequested);
    layout->addWidget(m_kbdBtn);
}

/**
 * @brief 设置输入框左右内边距，为前置图标留出空间
         * @param padding 内边距像素
         */
void SearchBar::setEditPadding(int leftRightPx) {
    m_edit->setStyleSheet(QString(
        "QLineEdit{border:none;padding:0 %1px;" + StyleHelper::fontSize(StyleHelper::Token::FontInput) + "background:transparent;color:#333;min-height:42px;}"
    ).arg(leftRightPx));
}
