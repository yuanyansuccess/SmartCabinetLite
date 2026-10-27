/**
 * @file DrawerOpeningDialog.cpp
 * @brief 抽屉/柜体打开中对话框实现（样式与原各页面内联实现逐字一致）
 * @author 袁燕
 */
#include "DrawerOpeningDialog.h"

#include "BaseDialog.h"  // 复用统一居中逻辑 centerAndKeep
#include "utils/StyleHelper.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
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

    m_titleLabel = titleLabel; // 留指针供 setTitleText 动态切换标题
    m_descLabel = descLabel;   // 留指针供 setDescHtml 动态切换文案
    m_nextBtn = nextBtn;       // 留指针供 setNextButtonText/setNextEnabled 使用
}

/// 修改"下一步"按钮文案（页面按场景定制，不影响其它调用方默认值）
/// @param text 按钮要显示的文案
void DrawerOpeningDialog::setNextButtonText(const QString& text) {
    if (!m_nextBtn) return;
    m_nextBtn->setText(text);
    // 按钮宽度按文案自适应且只增不减：长文案（如"我已关柜，开始核对 →"）不会被裁切，
    // 同一页面反复切换文案时按钮宽度保持不变（切换瞬间不产生宽度跳动）
    const int needed = m_nextBtn->sizeHint().width();   // 已含样式内边距，是不裁切文字所需的宽度
    if (needed > m_nextBtn->minimumWidth()) {
        m_nextBtn->setMinimumWidth(needed);
    }
}

/// 动态修改提示文案（等待态切换用）
/// @param html 提示区要显示的富文本（HTML 片段）
void DrawerOpeningDialog::setDescHtml(const QString& html) {
    if (m_descLabel) m_descLabel->setText(html);
}

/// 动态修改标题文案（同一页面切换状态用）
/// @param text 标题区要显示的文案
void DrawerOpeningDialog::setTitleText(const QString& text) {
    if (m_titleLabel) m_titleLabel->setText(text);
}

/// 启用/禁用"下一步"按钮（等待态禁用，防止用户重复提交）
/// @param enabled true 启用按钮，false 禁用
void DrawerOpeningDialog::setNextEnabled(bool enabled) {
    if (m_nextBtn) m_nextBtn->setEnabled(enabled);
}

/// 切换到"失败态"：提示失败原因并把下一步按钮改为"重新核对"，点击发出重试信号
/// @param title 失败态标题
/// @param desc 失败原因文案（人读）
/// @param retryText 下一步按钮文案（如"重新核对"）
void DrawerOpeningDialog::showFailureState(const QString& title, const QString& desc,
                                            const QString& retryText) {
    if (!m_nextBtn) return;
    setTitleText(title);
    setDescHtml(desc);
    setNextButtonText(retryText);
    setNextEnabled(true);
    // 下一步按钮正常流程连到 accept（如"我已关柜"），失败态改为发出 retryRequested，
    // 由页面决定重试动作；先断开两种可能的旧连接再连，避免重复连接累积
    disconnect(m_nextBtn, &QPushButton::clicked, this, &QDialog::accept);
    disconnect(m_nextBtn, &QPushButton::clicked, this, &DrawerOpeningDialog::retryRequested);
    connect(m_nextBtn, &QPushButton::clicked, this, &DrawerOpeningDialog::retryRequested);
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

/**
 * @brief 显示时统一居中到父窗口并兼容麒麟窗口管理器的映射后重排
 * @param event 显示事件对象
 */
void DrawerOpeningDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    BaseDialog::centerAndKeep(this);
}
