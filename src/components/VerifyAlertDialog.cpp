/**
 * @file VerifyAlertDialog.cpp
 * @brief 校验异常对话框实现（样式与倒计时逻辑与原各页面内联实现逐字一致）
 * @author 袁燕
 */
#include "VerifyAlertDialog.h"

#include "utils/StyleHelper.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

VerifyAlertDialog::VerifyAlertDialog(const QString& windowTitle, const QString& title,
                                     const QString& descHtml, const QString& descStyle,
                                     const QString& cancelText, const QString& confirmText,
                                     int confirmMinWidth, int bufferMinutes,
                                     QWidget* parent)
    : QDialog(parent), m_bufferMinutes(bufferMinutes) {
    setWindowTitle(windowTitle);
    setStyleSheet(StyleHelper::dialogStyle());
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 32, 32, 24);
    layout->setSpacing(18);

    // 顶部行：图标居中 + 右上角倒计时
    auto* topRow = new QHBoxLayout();
    auto* iconLabel = new QLabel(QStringLiteral("⚠️"));
    iconLabel->setStyleSheet("font-size:56px;background:transparent;");
    topRow->addStretch();
    topRow->addWidget(iconLabel);
    topRow->addStretch();
    buildCountdown(topRow);
    layout->addLayout(topRow);

    // 警告标题
    auto* titleLabel = new QLabel(title);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet(StyleHelper::fontSize(StyleHelper::Token::FontDisplay) + "font-weight:bold;color:#fa8c16;background:transparent;");
    layout->addWidget(titleLabel);

    // 警告详情
    auto* descLabel = new QLabel(descHtml);
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setTextFormat(Qt::RichText);
    descLabel->setStyleSheet(descStyle);
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    layout->addStretch();

    // 按钮区：左下角忽略 + 右下角取消 + 确认
    auto* btnRow = new QHBoxLayout();
    auto* ignoreBtn = new QPushButton(QStringLiteral("忽略"));
    ignoreBtn->setStyleSheet("QPushButton{background:#e74c3c;color:#fff;border:none;border-radius:10px;padding:10px 24px;" + StyleHelper::fontSize(StyleHelper::Token::FontBody) + "font-weight:700;" + StyleHelper::minHeight(StyleHelper::Token::ControlHeight) + "}QPushButton:hover{background:#c0392b;}");
    ignoreBtn->setCursor(Qt::PointingHandCursor);
    connect(ignoreBtn, &QPushButton::clicked, this, [this]() {
        m_timer->stop();
        done(2);  // 忽略→走告警流程
    });
    btnRow->addWidget(ignoreBtn);
    btnRow->addStretch();

    auto* cancelBtn = new QPushButton(cancelText);
    cancelBtn->setStyleSheet(StyleHelper::buttonDefault());
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setMinimumHeight(StyleHelper::Token::ControlHeight);
    cancelBtn->setMinimumWidth(120);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto* confirmBtn = new QPushButton(confirmText);
    confirmBtn->setStyleSheet(StyleHelper::buttonPrimary());
    confirmBtn->setCursor(Qt::PointingHandCursor);
    confirmBtn->setMinimumHeight(StyleHelper::Token::ControlHeight);
    confirmBtn->setMinimumWidth(confirmMinWidth);
    connect(confirmBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(cancelBtn);
    btnRow->addSpacing(12);
    btnRow->addWidget(confirmBtn);
    layout->addLayout(btnRow);
}

void VerifyAlertDialog::buildCountdown(QHBoxLayout* topRow) {
    int bufferMinutes = m_bufferMinutes;
    if (bufferMinutes <= 0) bufferMinutes = 30;  // 兜底默认30分钟
    m_remainSeconds = bufferMinutes * 60;

    m_countdownLabel = new QLabel(QStringLiteral("⏱ %1:00").arg(bufferMinutes));
    m_countdownLabel->setStyleSheet(StyleHelper::fontSize(StyleHelper::Token::FontTitle) + "font-weight:bold;color:#e74c3c;background:#fdecea;border:1px solid #f5c6cb;border-radius:8px;padding:6px 12px;");
    topRow->addWidget(m_countdownLabel);

    // 倒计时定时器：每秒更新，到0自动走忽略流程（done(2)）
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        m_remainSeconds--;
        if (m_remainSeconds <= 0) {
            m_timer->stop();
            done(2);  // 倒计时结束→走告警流程
        } else {
            int mins = m_remainSeconds / 60;
            int secs = m_remainSeconds % 60;
            m_countdownLabel->setText(QStringLiteral("⏱ %1:%2")
                .arg(mins, 2, 10, QChar('0'))
                .arg(secs, 2, 10, QChar('0')));
        }
    });
    m_timer->start();
}
