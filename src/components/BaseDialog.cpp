/**
 * @file BaseDialog.cpp
 * @brief 通用对话框基类实现
 * @author 袁燕
 * @date 2026-06-26
 */
#include "BaseDialog.h"
#include <QLabel>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QApplication>
#include <QScreen>
#include <QGraphicsDropShadowEffect>

BaseDialog::BaseDialog(QWidget* parent, int cardWidth)
    : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    setFixedWidth(cardWidth + 4);  // +4 容纳阴影边距

    buildBaseUI(cardWidth);
}

void BaseDialog::buildBaseUI(int cardWidth)
{
    // ── 卡片容器（圆角白色背景 + 阴影效果） ──
    m_card = new QFrame(this);
    m_card->setObjectName(QStringLiteral("baseDlgCard"));
    m_card->setStyleSheet(
        "#baseDlgCard{"
        "  background:#fff;"
        "  border-radius:16px;"
        "  border:1px solid #e0e4e8;"
        "}"
    );
    // 通过 QGraphicsDropShadowEffect 添加阴影
    auto* shadow = new QGraphicsDropShadowEffect(m_card);
    shadow->setBlurRadius(24);
    shadow->setColor(QColor(0, 0, 0, 40));
    shadow->setOffset(0, 4);
    m_card->setGraphicsEffect(shadow);

    auto* cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    // ── 标题栏（可选） ──
    m_titleLabel = new QLabel();
    m_titleLabel->setFixedHeight(52);
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_titleLabel->setStyleSheet(
        "QLabel{"
        "  font-size:18px;font-weight:700;color:#1a1a2e;"
        "  background:transparent;"
        "  padding-left:24px;"
        "  border-bottom:1px solid #eef0f3;"
        "}"
    );
    m_titleLabel->setVisible(false);  // 默认隐藏，调用setDialogTitle后显示
    cardLayout->addWidget(m_titleLabel);

    // ── 内容区域（子类通过 contentLayout() 添加控件） ──
    auto* contentArea = new QWidget();
    contentArea->setStyleSheet("background:transparent;");
    m_contentLayout = new QVBoxLayout(contentArea);
    m_contentLayout->setContentsMargins(24, 20, 24, 16);
    m_contentLayout->setSpacing(16);
    cardLayout->addWidget(contentArea, 1);

    // ── 底部按钮区域 ──
    m_buttonArea = new QWidget();
    m_buttonArea->setFixedHeight(64);
    m_buttonArea->setStyleSheet("background:transparent;border-top:1px solid #eef0f3;");
    m_buttonLayout = new QHBoxLayout(m_buttonArea);
    m_buttonLayout->setContentsMargins(20, 0, 20, 8);
    m_buttonLayout->setSpacing(12);
    m_buttonLayout->addStretch();
    cardLayout->addWidget(m_buttonArea);

    // ── 外层布局（透明背景，卡片居中） ──
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(2, 2, 2, 2);
    outer->addWidget(m_card);
}

void BaseDialog::setDialogTitle(const QString& title)
{
    if (title.isEmpty()) {
        m_titleLabel->setVisible(false);
        m_titleLabel->setFixedHeight(0);
        return;
    }
    m_titleLabel->setText(title);
    m_titleLabel->setVisible(true);
    m_titleLabel->setFixedHeight(52);
}

void BaseDialog::setButtonAreaVisible(bool visible)
{
    m_buttonArea->setVisible(visible);
}

void BaseDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);

    // 自动居中到父窗口
    QWidget* pw = parentWidget();
    if (!pw) {
        QScreen* screen = QApplication::primaryScreen();
        if (screen) {
            QRect sg = screen->availableGeometry();
            move(sg.center() - rect().center());
        }
        return;
    }

    QPoint parentGlobal = pw->mapToGlobal(QPoint(0, 0));
    QRect parentRect(parentGlobal, pw->size());
    QSize mySize = frameGeometry().size();

    int x = parentRect.center().x() - mySize.width() / 2;
    int y = parentRect.center().y() - mySize.height() / 2;

    QScreen* screen = QApplication::primaryScreen();
    if (screen) {
        QRect sg = screen->availableGeometry();
        if (x < sg.left()) x = sg.left();
        if (y < sg.top()) y = sg.top();
        if (x + mySize.width() > sg.right()) x = sg.right() - mySize.width();
        if (y + mySize.height() > sg.bottom()) y = sg.bottom() - mySize.height();
    }

    move(x, y);
}

void BaseDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        reject();
        return;
    }
    QDialog::keyPressEvent(event);
}
