/**
 * @file MessageDialog.cpp
 * @brief 自定义消息对话框组件实现
 * @author 袁燕
 * @date 2026-06-25
 */
#include "MessageDialog.h"
#include "BaseDialog.h"  // 复用统一居中逻辑 centerAndKeep（兼容麒麟窗口管理器）
#include "utils/StyleHelper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QApplication>
#include <QScreen>
#include <QScrollArea>
#include <QTextLayout>
#include <QTextOption>
#include <QFontMetrics>
#include <QtMath>

namespace {

// 弹窗尺寸约束（px）：短消息紧凑显示，导出路径等长文本自动加宽换行
const int kDialogMinWidth   = 440;  // 弹窗最小宽度，保证触屏阅读舒适
const int kDialogMaxWidth   = 760;  // 弹窗最大宽度：放宽以容纳麒麟柜机屏上的长导出路径，少折行
const int kBodyHMargin      = 28;   // 内容区左右内边距
const int kBodyVMargin      = 18;   // 内容区上下内边距
const int kButtonAreaHeight = 72;   // 底部按钮区高度（含底部留白）
const int kTextSpareHeight  = 12;   // 文本高度余量：吸收麒麟文泉驿/Noto等字体比Windows更大的行距，防止末行被裁
const int kScreenUseRatio   = 95;   // 弹窗宽高最大占屏幕可用区的百分比：放宽以适配柜机小屏
const int kMinTextHeight    = 40;   // 文本区最小高度，内容超高滚动时仍保证可读

/**
 * @brief 弹窗尺寸计算结果
 */
struct DialogMetrics {
    int width = kDialogMinWidth;   // 弹窗宽度
    int textHeight = 0;            // 文本完整排版高度
    int visibleTextHeight = 0;     // 文本区实际高度（超高时小于排版高度，内部滚动）
    int height = 0;                // 弹窗总高度
    bool scrollable = false;       // 文本是否超出屏幕允许高度、需要滚动
};

/**
 * @brief 取父窗口所在屏幕的可用宽度
 * @param parent 父窗口，为空时取主屏
 * @return 屏幕可用宽度（px）
 */
int availableScreenWidth(QWidget* parent)
{
    const QScreen* screen = parent ? parent->screen() : QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry().width() : kDialogMaxWidth;
}

/**
 * @brief 取父窗口所在屏幕的可用高度
 * @param parent 父窗口，为空时取主屏
 * @return 屏幕可用高度（px）
 */
int availableScreenHeight(QWidget* parent)
{
    const QScreen* screen = parent ? parent->screen() : QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry().height() : kDialogMaxWidth * 2;
}

/**
 * @brief 计算一段文本按指定宽度换行后的排版高度
 * @param font 文本实际使用的字体（字号已确定）
 * @param width 可用排版宽度（px）
 * @param text 文本内容，支持 \n 手动换行
 * @return 换行排版后的总高度（px）；长路径这类无空格文本同样在字符间断行
 */
int wrappedTextHeight(const QFont& font, int width, const QString& text)
{
    if (text.isEmpty() || width <= 0) {
        return 0;
    }

    const QFontMetrics fm(font);
    int totalHeight = 0;
    const QStringList paragraphs = text.split(QLatin1Char('\n'));
    for (const QString& paragraph : paragraphs) {
        if (paragraph.isEmpty()) {
            totalHeight += fm.lineSpacing();  // 空行按一整行高度计
            continue;
        }
        QTextLayout layout(paragraph, font);
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);
        layout.beginLayout();
        while (true) {
            QTextLine line = layout.createLine();
            if (!line.isValid()) {
                break;
            }
            line.setLineWidth(width);
            totalHeight += qCeil(line.height());
        }
        layout.endLayout();
    }
    return totalHeight;
}

/**
 * @brief 取文本中最长一行的自然宽度
 * @param font 文本使用的字体
 * @param text 文本内容
 * @return 最长一行不换行时的宽度（px）
 */
int naturalTextWidth(const QFont& font, const QString& text)
{
    const QFontMetrics fm(font);
    int width = 0;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        width = qMax(width, fm.horizontalAdvance(line));
    }
    return width;
}

/**
 * @brief 按标题与正文的实际排版尺寸计算弹窗宽高
 * @param parent 父窗口，用于取所在屏幕的可用范围
 * @param titleFont 标题字体
 * @param messageFont 正文字体
 * @param title 标题文字
 * @param message 正文内容，支持 \n 换行
 * @return 弹窗宽度、文本排版高度与总高度
 */
DialogMetrics measureDialog(QWidget* parent, const QFont& titleFont, const QFont& messageFont,
                            const QString& title, const QString& message)
{
    DialogMetrics metrics;

    // 宽度：按最长一行自然宽度加宽，限制在最小宽度、最大宽度与屏幕可用宽度之内
    const int naturalWidth = qMax(naturalTextWidth(titleFont, title),
                                  naturalTextWidth(messageFont, message)) + kBodyHMargin * 2 + 12;
    const int screenWidthLimit = qMax(kDialogMinWidth, availableScreenWidth(parent) * kScreenUseRatio / 100);
    metrics.width = qMin(qMin(qMax(naturalWidth, kDialogMinWidth), kDialogMaxWidth), screenWidthLimit);

    // 高度：按换行后的真实行数计算，避免长文本只按一行高度导致下半截被裁
    const int textWidth = metrics.width - kBodyHMargin * 2;
    metrics.textHeight = message.isEmpty()
        ? 0
        : wrappedTextHeight(messageFont, textWidth, message) + kTextSpareHeight;

    const int headerHeight = StyleHelper::Token::ControlHeightLarge;
    const int screenHeightLimit = qMax(240, availableScreenHeight(parent) * kScreenUseRatio / 100);
    const int maxTextHeight = qMax(kMinTextHeight,
                                   screenHeightLimit - headerHeight - kButtonAreaHeight - kBodyVMargin * 2);
    metrics.scrollable = metrics.textHeight > maxTextHeight;
    metrics.visibleTextHeight = metrics.scrollable ? maxTextHeight : metrics.textHeight;
    metrics.height = headerHeight + kBodyVMargin * 2 + metrics.visibleTextHeight + kButtonAreaHeight;
    return metrics;
}

/**
 * @brief 构建消息内容区
 * @param message 消息内容，支持 \n 换行
 * @param font 正文字体
 * @param metrics 弹窗尺寸计算结果
 * @return 内容区容器控件；文本超长时内部可滚动，保证任何长度都完整可见
 */
QWidget* buildMessageBody(const QString& message, const QFont& font, const DialogMetrics& metrics)
{
    auto* bodyWidget = new QWidget();
    bodyWidget->setStyleSheet("background:transparent;");
    auto* bodyLayout = new QVBoxLayout(bodyWidget);
    bodyLayout->setContentsMargins(kBodyHMargin, kBodyVMargin, kBodyHMargin, kBodyVMargin);

    if (!message.isEmpty()) {
        auto* msgLabel = new QLabel(message);
        msgLabel->setFont(font);
        msgLabel->setWordWrap(true);
        msgLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);  // 文件路径可直接选中复制
        msgLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        msgLabel->setStyleSheet("QLabel{color:#555;background:transparent;}");
        // 按换行后的真实行数占位：QLabel 的 sizeHint 只按一行计算，不指定高度会被裁掉
        msgLabel->setMinimumHeight(metrics.textHeight);

        if (metrics.scrollable) {
            auto* scroll = new QScrollArea(bodyWidget);
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            scroll->setStyleSheet("QScrollArea{background:transparent;border:none;}");
            scroll->viewport()->setAutoFillBackground(false);
            scroll->setFixedHeight(metrics.visibleTextHeight);
            scroll->setWidget(msgLabel);
            bodyLayout->addWidget(scroll);
        } else {
            bodyLayout->addWidget(msgLabel);
        }
    }
    return bodyWidget;
}

}  // namespace

MessageDialog::MessageDialog(DialogType type, QWidget* parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
}

/**
 * @brief 构建消息对话框界面
         * @param type 对话框类型，决定配色与按钮
         * @param title 标题文字
         * @param message 消息内容
         */
void MessageDialog::setupUI(DialogType type, const QString& title, const QString& message)
{
    // 先确定实际字体，再按文本排版尺寸定宽高：长路径换行后不会被裁掉
    QFont messageFont = QApplication::font();
    messageFont.setPixelSize(StyleHelper::Token::FontInput);
    QFont titleFont = QApplication::font();
    titleFont.setPixelSize(StyleHelper::Token::FontTitle);
    titleFont.setBold(true);

    const DialogMetrics metrics = measureDialog(parentWidget(), titleFont, messageFont, title, message);
    setFixedWidth(metrics.width);
    setMinimumHeight(metrics.height);
    resize(metrics.width, metrics.height);

    // 主容器（圆角白色卡片 + 阴影效果）
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("msgCard"));
    card->setStyleSheet(
        "#msgCard{background:#fff;border-radius:16px;border:1px solid #e8ecf1;}"
    );

    auto* mainLayout = new QVBoxLayout(card);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ── 图标 + 标题区域 ──
    auto* headerWidget = new QWidget();
    headerWidget->setFixedHeight(StyleHelper::Token::ControlHeightLarge);
    QString headerBg;
    QString iconChar;
    switch (type) {
    case Success: headerBg = "#f0fff4"; iconChar = QStringLiteral("✓"); break;
    case Error:   headerBg = "#fff5f5"; iconChar = QStringLiteral("✕"); break;
    case Warning: headerBg = "#fffbf0"; iconChar = QStringLiteral("⚠"); break;
    case Question:headerBg = "#f0f7ff"; iconChar = QStringLiteral("?"); break;
    default:      headerBg = "#f5f5f5"; iconChar = QStringLiteral("i"); break;
    }
    headerWidget->setStyleSheet(QString("background:%1;border-radius:16px 16px 0 0;").arg(headerBg));

    auto* headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(24, 0, 24, 0);
    headerLayout->setSpacing(14);

    // 图标圆圈
    auto* iconCircle = new QLabel(iconChar);
    iconCircle->setFixedSize(40, 40);
    iconCircle->setAlignment(Qt::AlignCenter);
    QString iconBg, iconColor;
    switch (type) {
    case Success: iconBg = "#52c41a"; iconColor = "#fff"; break;
    case Error:   iconBg = "#ff4d4f"; iconColor = "#fff"; break;
    case Warning: iconBg = "#faad14"; iconColor = "#fff"; break;
    case Question:iconBg = "#4da3ff"; iconColor = "#fff"; break;
    default:      iconBg = "#999";     iconColor = "#fff"; break;
    }
    iconCircle->setStyleSheet(
        QString("QLabel{background:%1;color:%2;border-radius:20px;"
                + StyleHelper::fontSize(StyleHelper::Token::FontDisplay) + "font-weight:bold;}")
            .arg(iconBg, iconColor)
    );

    // 标题
    auto* titleLabel = new QLabel(title);
    titleLabel->setFont(titleFont);
    titleLabel->setStyleSheet("QLabel{color:#1a1a2e;background:transparent;}");

    headerLayout->addWidget(iconCircle);
    headerLayout->addWidget(titleLabel, 1);

    mainLayout->addWidget(headerWidget);

    // ── 消息内容区域（按真实换行行数占位）──
    mainLayout->addWidget(buildMessageBody(message, messageFont, metrics));

    // ── 按钮区域 ──
    auto* btnWidget = new QWidget();
    btnWidget->setFixedHeight(kButtonAreaHeight);
    btnWidget->setStyleSheet("background:transparent;");

    auto* btnLayout = new QHBoxLayout(btnWidget);
    btnLayout->setContentsMargins(24, 0, 24, 16);
    btnLayout->setSpacing(16);

    // 取消按钮（仅Question类型显示）
    if (type == Question) {
        auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
        cancelBtn->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
        cancelBtn->setMinimumWidth(120);
        cancelBtn->setCursor(Qt::PointingHandCursor);
        cancelBtn->setStyleSheet(
            "QPushButton{background:#fff;color:#666;border:2px solid #ddd;border-radius:12px;"
            + StyleHelper::fontSize(StyleHelper::Token::FontInput) + "font-weight:500;}"
            "QPushButton:hover{background:#f5f5f5;border-color:#bbb;}"
            "QPushButton:pressed{}"
        );
        connect(cancelBtn, &QPushButton::clicked, this, [this]() {
            m_confirmed = false;
            reject();
        });
        btnLayout->addWidget(cancelBtn);
    }

    // 确定按钮
    auto* okBtn = new QPushButton(type == Question ? QStringLiteral("确定") : QStringLiteral("知道了"));
    okBtn->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
    okBtn->setMinimumWidth(120);
    okBtn->setCursor(Qt::PointingHandCursor);
    QString okBg, okHover;
    switch (type) {
    case Success: okBg = "#52c41a"; okHover = "#45a515"; break;
    case Error:   okBg = "#ff4d4f"; okHover = "#e04345"; break;
    case Warning: okBg = "#faad14"; okHover = "#e09d12"; break;
    case Question:okBg = "#4da3ff"; okHover = "#3d8ae0"; break;
    default:      okBg = "#4da3ff"; okHover = "#3d8ae0"; break;
    }
    okBtn->setStyleSheet(
        QString("QPushButton{background:%1;color:#fff;border:none;border-radius:12px;"
                + StyleHelper::fontSize(StyleHelper::Token::FontInput) + "font-weight:600;}")
            .arg(okBg)
        + QString("QPushButton:hover{background:%1;}")
            .arg(okHover)
        + QString("QPushButton:pressed{}")
    );
    connect(okBtn, &QPushButton::clicked, this, [this]() {
        m_confirmed = true;
        accept();
    });
    btnLayout->addWidget(okBtn);

    mainLayout->addWidget(btnWidget);

    // 卡片作为主布局
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(card);
}

// 每次弹出自动居中到父窗口（showEvent保证实际尺寸已确定）
void MessageDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);

    // 兜底：不同平台字体行距存在差异，按当前宽度重算一次所需高度，不足时补足
    if (layout()) {
        const int neededHeight = layout()->hasHeightForWidth()
            ? layout()->totalHeightForWidth(width())
            : layout()->totalSizeHint().height();
        const int maxHeight = availableScreenHeight(this) * kScreenUseRatio / 100;
        if (neededHeight > height() && neededHeight <= maxHeight) {
            resize(width(), neededHeight);
        }
    }

    // 统一居中到父窗口（无父窗口时居中到屏幕），并在映射稳定后重复校准，
    // 兼容麒麟窗口管理器首次映射后的重排，避免弹窗越偏越远
    BaseDialog::centerAndKeep(this);
}

/**
         * @brief 显示单按钮消息对话框
         * @param type 对话框类型
         * @param parent 父窗口
         * @param title 标题文字
         * @param message 消息内容
         */
void MessageDialog::showSingle(DialogType type, QWidget* parent,
                                const QString& title, const QString& message)
{
    MessageDialog dlg(type, parent);
    dlg.setupUI(type, title, message);
    dlg.exec();
}

/**
         * @brief 显示成功提示（单按钮）
         * @param parent 父窗口
         * @param title 标题文字
         * @param message 消息内容
         */
void MessageDialog::showSuccess(QWidget* parent, const QString& title, const QString& message)
{
    showSingle(Success, parent, title, message);
}

/**
 * @brief 显示异常
 */
void MessageDialog::showError(QWidget* parent, const QString& title, const QString& message)
{
    showSingle(Error, parent, title, message);
}

/**
         * @brief 显示警告提示（单按钮）
         * @param parent 父窗口
         * @param title 标题文字
         * @param message 消息内容
         */
void MessageDialog::showWarning(QWidget* parent, const QString& title, const QString& message)
{
    showSingle(Warning, parent, title, message);
}

/**
         * @brief 显示确认询问（确定与取消两个按钮）
         * @param parent 父窗口
         * @param title 标题文字
         * @param message 消息内容
         * @return true=用户点击确定
         */
bool MessageDialog::showQuestion(QWidget* parent, const QString& title, const QString& message)
{
    MessageDialog dlg(Question, parent);
    dlg.setupUI(Question, title, message);
    dlg.exec();
    return dlg.m_confirmed;
}

// 脏数据保存确认对话框（保存/不保存，无取消按钮）
// 返回: 0=不保存, 1=保存
// 风格统一：圆角卡片+图标+触屏按钮，不使用QMessageBox原生样式
int MessageDialog::showDirtyConfirm(QWidget* parent, const QString& title, const QString& message,
                                     const QString& saveText, const QString& discardText)
{
    MessageDialog dlg(Question, parent);

    const QString sText = saveText.isEmpty() ? QStringLiteral("保存") : saveText;
    const QString dText = discardText.isEmpty() ? QStringLiteral("不保存") : discardText;

    dlg.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dlg.setAttribute(Qt::WA_TranslucentBackground);
    dlg.setModal(true);

    QFont messageFont = QApplication::font();
    messageFont.setPixelSize(StyleHelper::Token::FontInput);
    QFont titleFont = QApplication::font();
    titleFont.setPixelSize(StyleHelper::Token::FontTitle);
    titleFont.setBold(true);

    const DialogMetrics metrics = measureDialog(&dlg, titleFont, messageFont, title, message);
    dlg.setFixedWidth(metrics.width);
    dlg.setMinimumHeight(metrics.height);
    dlg.resize(metrics.width, metrics.height);

    // 主容器（圆角白色卡片）
    auto* card = new QFrame(&dlg);
    card->setObjectName(QStringLiteral("dirtyCard"));
    card->setStyleSheet("#dirtyCard{background:#fff;border-radius:16px;border:1px solid #e8ecf1;}");

    auto* mainLayout = new QVBoxLayout(card);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ── 图标 + 标题区域 ──
    auto* headerWidget = new QWidget();
    headerWidget->setFixedHeight(StyleHelper::Token::ControlHeightLarge);
    headerWidget->setStyleSheet("background:#f0f7ff;border-radius:16px 16px 0 0;");

    auto* headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(24, 0, 24, 0);
    headerLayout->setSpacing(14);

    auto* iconCircle = new QLabel(QStringLiteral("?"));
    iconCircle->setFixedSize(40, 40);
    iconCircle->setAlignment(Qt::AlignCenter);
    iconCircle->setStyleSheet(
        "QLabel{background:#4da3ff;color:#fff;border-radius:20px;"
        + StyleHelper::fontSize(StyleHelper::Token::FontDisplay) + "font-weight:bold;}");

    auto* titleLabel = new QLabel(title);
    titleLabel->setFont(titleFont);
    titleLabel->setStyleSheet("QLabel{color:#1a1a2e;background:transparent;}");

    headerLayout->addWidget(iconCircle);
    headerLayout->addWidget(titleLabel, 1);
    mainLayout->addWidget(headerWidget);

    // ── 消息内容区域（按真实换行行数占位）──
    mainLayout->addWidget(buildMessageBody(message, messageFont, metrics));

    // ── 按钮区域（保存 + 不保存，无取消按钮）──
    auto* btnWidget = new QWidget();
    btnWidget->setFixedHeight(kButtonAreaHeight);
    btnWidget->setStyleSheet("background:transparent;");

    auto* btnLayout = new QHBoxLayout(btnWidget);
    btnLayout->setContentsMargins(24, 0, 24, 16);
    btnLayout->setSpacing(16);

    // 不保存按钮（次要按钮，灰色边框）
    auto* discardBtn = new QPushButton(dText);
    discardBtn->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
    discardBtn->setMinimumWidth(130);
    discardBtn->setCursor(Qt::PointingHandCursor);
    discardBtn->setStyleSheet(
        "QPushButton{background:#fff;color:#666;border:2px solid #ddd;border-radius:12px;"
        + StyleHelper::fontSize(StyleHelper::Token::FontInput) + "font-weight:500;}"
        "QPushButton:hover{background:#f5f5f5;border-color:#bbb;}"
        "QPushButton:pressed{}");
    btnLayout->addWidget(discardBtn);

    // 保存按钮（主要按钮，蓝色实心）
    auto* saveBtn = new QPushButton(sText);
    saveBtn->setFixedHeight(StyleHelper::Token::ControlHeightTouch);
    saveBtn->setMinimumWidth(130);
    saveBtn->setCursor(Qt::PointingHandCursor);
    saveBtn->setStyleSheet(
        "QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:12px;"
        + StyleHelper::fontSize(StyleHelper::Token::FontInput) + "font-weight:600;}"
        "QPushButton:hover{background:#3d8ae0;}"
        "QPushButton:pressed{}");
    saveBtn->setDefault(true);
    btnLayout->addWidget(saveBtn);

    mainLayout->addWidget(btnWidget);

    // 卡片放入对话框
    auto* outerLayout = new QVBoxLayout(&dlg);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(card);

    // 连接信号
    int result = 0;
    connect(discardBtn, &QPushButton::clicked, &dlg, [&]() { result = 0; dlg.reject(); });
    connect(saveBtn, &QPushButton::clicked, &dlg, [&]() { result = 1; dlg.accept(); });

    dlg.exec();
    return result;
}
