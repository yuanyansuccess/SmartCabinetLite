/**
 * @file MultiSelectFilter.cpp
 * @brief 通用多选筛选组件实现
 * @author 袁燕
 * @说明 2026-06-24v8 从UserManagementPage部门筛选逻辑提取为通用组件
 */
#include "MultiSelectFilter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QMouseEvent>

MultiSelectFilter::MultiSelectFilter(const QString& placeholder, QWidget* parent)
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
        "MultiSelectFilter{border:2px solid #e0e0e0;border-radius:12px;background:#fff;}"
    );
    m_filterBtn = new QPushButton(placeholder);
    m_filterBtn->setStyleSheet(
        "QPushButton{padding:0 16px;border:none;border-radius:10px;"
        "font-size:16px;background:transparent;color:#333;text-align:left;min-width:130px;}"
        "QPushButton:hover{background:#f5f7fa;}"
    );
    m_filterBtn->setCursor(Qt::PointingHandCursor);
    connect(m_filterBtn, &QPushButton::clicked, this, &MultiSelectFilter::onFilterBtnClicked);
    layout->addWidget(m_filterBtn);

    // 弹出面板 [V7.9] 不设parent避免Qt自动删除与析构手动delete冲突导致双重删除
    m_popup = new QDialog(nullptr);
    m_popup->setWindowFlags(Qt::FramelessWindowHint | Qt::Popup);
    m_popup->setModal(false);
    m_popup->setFixedWidth(240);
    m_popup->setStyleSheet(
        "QDialog{background:white;border:2px solid #e0e0e0;border-radius:10px;}"
        "QCheckBox{font-size:15px;padding:10px 20px;spacing:10px;}"
        "QCheckBox::indicator{width:20px;height:20px;}"
        "QPushButton{min-height:40px;font-size:15px;border-radius:8px;}"
    );
    m_popup->hide();
}

MultiSelectFilter::~MultiSelectFilter() {
    if (m_popup) {
        delete m_popup;
        m_popup = nullptr;
    }
}

void MultiSelectFilter::setOptions(const QStringList& options) {
    m_checkBoxes.clear();

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

    // 主布局
    auto* layout = new QVBoxLayout(m_popup);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // [2026-06-24v9] 选项>6个时启用滚动条，限制最大高度
    int itemCount = options.size();
    int scrollHeight = qMin(itemCount, 8) * 40 + 4;  // 每项约40px，最多显示8项
    bool needScroll = itemCount > 6;

    // 复选框容器
    QWidget* checkContainer = new QWidget();
    auto* checkLayout = new QVBoxLayout(checkContainer);
    checkLayout->setContentsMargins(12, 12, 12, 4);
    checkLayout->setSpacing(4);

    for (const QString& opt : options) {
        auto* cb = new QCheckBox(opt);
        cb->setChecked(true);
        m_checkBoxes.append(qMakePair(opt, cb));
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

    // 底部分隔线
    auto* sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("QFrame{color:#e8e8e8;max-height:1px;}");
    layout->addWidget(sep);

    // 底部按钮（始终可见）
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(12, 6, 12, 8);
    btnLayout->setSpacing(8);

    auto* clearBtn = new QPushButton(QStringLiteral("清空"));
    clearBtn->setStyleSheet(
        "QPushButton{background:#fff;color:#666;border:none;border-radius:8px;padding:8px 16px;font-size:13px;font-weight:600;}"
        "QPushButton:hover{background:#f0f2f5;}"
    );
    connect(clearBtn, &QPushButton::clicked, this, &MultiSelectFilter::onClear);

    auto* confirmBtn = new QPushButton(QStringLiteral("确定"));
    confirmBtn->setStyleSheet(
        "QPushButton{background:transparent;color:#4da3ff;border:none;border-radius:8px;padding:8px 16px;font-size:13px;font-weight:600;}"
        "QPushButton:hover{background:#f0f2f5;}"
    );
    connect(confirmBtn, &QPushButton::clicked, this, &MultiSelectFilter::onConfirm);

    btnLayout->addWidget(clearBtn);
    btnLayout->addWidget(confirmBtn);
    layout->addLayout(btnLayout);

    m_selectedCache = options;
    m_filterBtn->setText(m_placeholder);
}

QStringList MultiSelectFilter::selectedOptions() const {
    QStringList result;
    for (const auto& pair : m_checkBoxes) {
        if (pair.second && pair.second->isChecked()) {
            result.append(pair.first);
        }
    }
    return result;
}

void MultiSelectFilter::setPlaceholderText(const QString& text) {
    m_placeholder = text;
    // 仅在无选中内容时更新按钮文字
    if (selectedOptions().isEmpty() ||
        (m_selectedCache.size() == m_checkBoxes.size() && !m_checkBoxes.isEmpty())) {
        m_filterBtn->setText(text);
    }
}

void MultiSelectFilter::clear() {
    for (auto& pair : m_checkBoxes) {
        if (pair.second) {
            pair.second->setChecked(false);
        }
    }
    m_selectedCache.clear();
    m_filterBtn->setText(m_placeholder);
}

void MultiSelectFilter::selectAll() {
    for (auto& pair : m_checkBoxes) {
        if (pair.second) {
            pair.second->setChecked(true);
        }
    }
    QStringList all;
    for (const auto& pair : m_checkBoxes) {
        all.append(pair.first);
    }
    m_selectedCache = all;
    m_filterBtn->setText(m_placeholder);  // 全选时显示占位文字
}

void MultiSelectFilter::onFilterBtnClicked() {
    if (m_popup->isVisible()) {
        m_popup->hide();
        m_visible = false;
    } else {
        QPoint pos = m_filterBtn->mapToGlobal(QPoint(0, m_filterBtn->height() + 4));
        m_popup->move(pos);
        m_popup->show();
        m_visible = true;
    }
}

void MultiSelectFilter::onClear() {
    for (auto& pair : m_checkBoxes) {
        if (pair.second) {
            pair.second->setChecked(false);
        }
    }
}

void MultiSelectFilter::onConfirm() {
    QStringList selected;
    for (const auto& pair : m_checkBoxes) {
        if (pair.second && pair.second->isChecked()) {
            selected.append(pair.first);
        }
    }
    m_selectedCache = selected;
    updateButtonText();
    m_popup->hide();
    m_visible = false;
    emit selectionChanged(selected);
}

void MultiSelectFilter::updateButtonText() {
    if (m_selectedCache.isEmpty()) {
        m_filterBtn->setText(m_placeholder);
    } else if (m_selectedCache.size() <= 2) {
        m_filterBtn->setText(m_selectedCache.join(", "));
    } else {
        m_filterBtn->setText(QStringLiteral("%1项已选").arg(m_selectedCache.size()));
    }
}
