/**
 * @file SingleSelectFilter.cpp
 * @brief 通用单选筛选组件实现
 * @author 袁燕
 * @说明 2026-06-24 用于替换筛选栏QComboBox
 *   - CheckBox弹出面板外观（与MultiSelectFilter一致）
 *   - 单选互斥行为：点击任意项→取消其他项→关闭面板→发射信号
 *   - 无"清空"和"确定"按钮，用户体验更简洁
 */
#include "SingleSelectFilter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QMouseEvent>

SingleSelectFilter::SingleSelectFilter(const QString& placeholder, QWidget* parent)
    : QWidget(parent), m_placeholder(placeholder)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // [2026-06-25] 统一边框画在容器QWidget上(与搜索框QFrame模式一致)
    // 容器固定48px，border在容器外侧，内部QPushButton无边框避免Qt原生样式margin
    this->setFixedHeight(48);
    this->setAttribute(Qt::WA_StyledBackground, true);
    this->setStyleSheet(
        "SingleSelectFilter{border:2px solid #e0e0e0;border-radius:12px;background:#fff;}"
    );
    m_filterBtn = new QPushButton(placeholder);
    m_filterBtn->setStyleSheet(
        "QPushButton{padding:0 16px;border:none;border-radius:10px;"
        "font-size:16px;background:transparent;color:#333;text-align:left;min-width:110px;}"
        "QPushButton:hover{background:#f5f7fa;}"
    );
    m_filterBtn->setCursor(Qt::PointingHandCursor);
    connect(m_filterBtn, &QPushButton::clicked, this, &SingleSelectFilter::onFilterBtnClicked);
    layout->addWidget(m_filterBtn);

    // 弹出面板 [V7.9] 不设parent避免Qt自动删除与析构手动delete冲突导致双重删除
    m_popup = new QDialog(nullptr);
    m_popup->setWindowFlags(Qt::FramelessWindowHint | Qt::Popup);
    m_popup->setModal(false);
    m_popup->setFixedWidth(240);
    m_popup->setStyleSheet(
        "QDialog{background:white;border:2px solid #e0e0e0;border-radius:10px;}"
        "QCheckBox{font-size:14px;padding:8px 20px;spacing:10px;}"
        "QCheckBox::indicator{width:16px;height:16px;}"
    );
    m_popup->hide();
}

SingleSelectFilter::~SingleSelectFilter() {
    if (m_popup) {
        delete m_popup;
        m_popup = nullptr;
    }
}

void SingleSelectFilter::setOptions(const QStringList& options) {
    m_options = options;
    m_selectedIndex = 0;  // 默认选中第一项
    m_filterBtn->setText(m_placeholder);
    rebuildPopup();
}

QString SingleSelectFilter::selectedText() const {
    if (m_selectedIndex >= 0 && m_selectedIndex < m_options.size()) {
        return m_options[m_selectedIndex];
    }
    return QString();
}

int SingleSelectFilter::selectedIndex() const {
    return m_selectedIndex;
}

void SingleSelectFilter::setPlaceholderText(const QString& text) {
    m_placeholder = text;
    // 仅在选中第一项"全部"时显示占位文本
    if (m_selectedIndex <= 0) {
        m_filterBtn->setText(text);
    }
}

void SingleSelectFilter::reset() {
    m_selectedIndex = 0;
    m_filterBtn->setText(m_placeholder);
}

void SingleSelectFilter::selectText(const QString& text) {
    for (int i = 0; i < m_options.size(); ++i) {
        if (m_options[i] == text) {
            m_selectedIndex = i;
            updateButtonText();
            return;
        }
    }
    // 未找到则重置
    m_selectedIndex = 0;
    m_filterBtn->setText(m_placeholder);
}

void SingleSelectFilter::selectIndex(int index) {
    if (index >= 0 && index < m_options.size()) {
        m_selectedIndex = index;
        updateButtonText();
    }
}

void SingleSelectFilter::onFilterBtnClicked() {
    if (m_popup->isVisible()) {
        m_popup->hide();
        m_popupVisible = false;
    } else {
        // 确保popup内容是最新的
        if (m_popup->layout() == nullptr || m_options.isEmpty()) {
            rebuildPopup();
        }
        QPoint pos = m_filterBtn->mapToGlobal(QPoint(0, m_filterBtn->height() + 4));
        m_popup->move(pos);
        m_popup->show();
        m_popupVisible = true;
    }
}

void SingleSelectFilter::rebuildPopup() {
    // 清除旧布局
    QLayout* oldLayout = m_popup->layout();
    if (oldLayout) {
        QLayoutItem* item;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            if (item->widget()) {
                item->widget()->setParent(nullptr);
            }
            delete item;
        }
        delete oldLayout;
    }

    if (m_options.isEmpty()) return;

    auto* layout = new QVBoxLayout(m_popup);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 选项>6个时启用滚动
    int itemCount = m_options.size();
    bool needScroll = itemCount > 6;
    int scrollHeight = qMin(itemCount, 8) * 40 + 8;

    // CheckBox容器
    QWidget* checkContainer = new QWidget();
    auto* checkLayout = new QVBoxLayout(checkContainer);
    checkLayout->setContentsMargins(12, 8, 12, 8);
    checkLayout->setSpacing(2);

    for (int i = 0; i < m_options.size(); ++i) {
        auto* cb = new QCheckBox(m_options[i]);
        cb->setChecked(i == m_selectedIndex);
        // 点击即选中（单选行为：关闭面板+发射信号）
        int idx = i;
        connect(cb, &QCheckBox::clicked, this, [this, idx](bool checked) {
            if (!checked) {
                // 不允许取消选中（至少保持一项选中）
                // 重新勾上
                QCheckBox* senderCb = qobject_cast<QCheckBox*>(sender());
                if (senderCb) senderCb->setChecked(true);
                return;
            }
            // 单选：更新选中索引
            m_selectedIndex = idx;
            // 更新所有checkbox状态
            QLayout* popupLayout = m_popup->layout();
            if (popupLayout) {
                QWidget* container = popupLayout->itemAt(0) ? popupLayout->itemAt(0)->widget() : nullptr;
                if (container) {
                    QList<QCheckBox*> allCbs = container->findChildren<QCheckBox*>();
                    for (int j = 0; j < allCbs.size(); ++j) {
                        allCbs[j]->setChecked(j == idx);
                    }
                }
            }
            updateButtonText();
            m_popup->hide();
            m_popupVisible = false;
            emit selectionChanged(m_options[idx]);
        });
        checkLayout->addWidget(cb);
    }

    if (needScroll) {
        auto* scrollArea = new QScrollArea();
        scrollArea->setWidgetResizable(true);
        scrollArea->setWidget(checkContainer);
        scrollArea->setFixedHeight(scrollHeight);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scrollArea->setStyleSheet(
            "QScrollArea{border:none;background:transparent;}"
            "QScrollBar:vertical{width:6px;background:transparent;}"
            "QScrollBar::handle:vertical{background:#c0c4cc;border-radius:3px;min-height:20px;}"
            "QScrollBar::add-line:vertical{height:0;}"
            "QScrollBar::sub-line:vertical{height:0;}"
        );
        layout->addWidget(scrollArea);
    } else {
        layout->addWidget(checkContainer);
    }
}

void SingleSelectFilter::updateButtonText() {
    if (m_selectedIndex <= 0 || m_selectedIndex >= m_options.size()) {
        // 选中第一项"全部"或无效索引，显示占位文本
        m_filterBtn->setText(m_placeholder);
    } else {
        m_filterBtn->setText(m_options[m_selectedIndex]);
    }
}
