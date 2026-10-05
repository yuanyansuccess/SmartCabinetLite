/**
 * @file DrawerOpeningDialog.cpp
 * @brief 抽屉/柜体打开中对话框实现（样式与原各页面内联实现逐字一致）
 * @author 袁燕
 */
#include "DrawerOpeningDialog.h"

#include "utils/StyleHelper.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

DrawerOpeningDialog::DrawerOpeningDialog(const QString& windowTitle, const QString& title,
                                         const QString& descHtml,
                                         int contentTopMargin, int spinnerHeight,
                                         QWidget* parent)
    : QDialog(parent), m_spinnerHeight(spinnerHeight) {
    setWindowTitle(windowTitle);
    setStyleSheet(StyleHelper::dialogStyle());
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(32, contentTopMargin, 32, 24);
    m_layout->setSpacing(14);

    // 标题
    auto* titleLabel = new QLabel(title);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet(StyleHelper::dialogTitleText());
    m_layout->addWidget(titleLabel);

    // 三点跳动动画
    buildSpinner();

    // 提示信息
    auto* descLabel = new QLabel(descHtml);
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setTextFormat(Qt::RichText);
    descLabel->setStyleSheet(StyleHelper::fontSize(StyleHelper::Token::FontLabel) + "color:#555;line-height:1.6;background:transparent;");
    descLabel->setWordWrap(true);
    m_layout->addWidget(descLabel);

    // 页面内容槽位（清单表格等）
    m_body = new QVBoxLayout();
    m_body->setContentsMargins(0, 0, 0, 0);
    m_body->setSpacing(0);
    m_layout->addLayout(m_body);

    m_layout->addStretch();

    // 底部按钮：取消 / 下一步 →
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
    cancelBtn->setStyleSheet(StyleHelper::buttonDefault());
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setMinimumHeight(StyleHelper::Token::ControlHeight);
    cancelBtn->setMinimumWidth(110);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto* nextBtn = new QPushButton(QStringLiteral("下一步 →"));
    nextBtn->setStyleSheet(StyleHelper::buttonPrimary());
    nextBtn->setCursor(Qt::PointingHandCursor);
    nextBtn->setMinimumHeight(StyleHelper::Token::ControlHeight);
    nextBtn->setMinimumWidth(140);
    connect(nextBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(cancelBtn);
    btnRow->addSpacing(12);
    btnRow->addWidget(nextBtn);
    m_layout->addLayout(btnRow);
}

/**
 * @brief 构建开柜等待动画：旋转指示器与提示文字
 * @return 动画区域控件
 */
void DrawerOpeningDialog::buildSpinner() {
    auto* spinnerContainer = new QWidget();
    spinnerContainer->setFixedHeight(m_spinnerHeight);
    auto* spinnerLayout = new QHBoxLayout(spinnerContainer);
    spinnerLayout->setSpacing(16);
    spinnerLayout->setAlignment(Qt::AlignCenter);
    QList<QLabel*> dots;
    for (int i = 0; i < 3; ++i) {
        auto* dot = new QLabel();
        dot->setFixedSize(16, 16);
        dot->setStyleSheet(QString(
            "QLabel{background:%1;border-radius:8px;}"
        ).arg(StyleHelper::primaryColor()));
        spinnerLayout->addWidget(dot);
        dots.append(dot);
    }
    m_layout->addWidget(spinnerContainer);

    // 三点依次跳动：定时器控制颜色和大小变化
    QTimer* dotTimer = new QTimer(this);
    dotTimer->setInterval(300);
    connect(dotTimer, &QTimer::timeout, this, [this, dots]() {
        for (int i = 0; i < dots.size(); ++i) {
            if (i == m_dotIndex) {
                dots[i]->setStyleSheet("background:#4da3ff;border-radius:10px;");
                dots[i]->setFixedSize(20, 20);
            } else {
                dots[i]->setStyleSheet("background:#c8d6e5;border-radius:8px;");
                dots[i]->setFixedSize(16, 16);
            }
        }
        m_dotIndex = (m_dotIndex + 1) % dots.size();
    });
    dotTimer->start();
}
