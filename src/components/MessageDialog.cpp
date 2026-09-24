/**
 * @file MessageDialog.cpp
 * @brief 自定义消息对话框组件实现
 * @author 袁燕
 * @date 2026-06-25
 */
#include "MessageDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QApplication>
#include <QScreen>

MessageDialog::MessageDialog(DialogType type, QWidget* parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
}

void MessageDialog::setupUI(DialogType type, const QString& title, const QString& message)
{
    // 弹窗尺寸：宽380px，高自适应
    setFixedWidth(400);

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
    headerWidget->setFixedHeight(56);
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
        QString("QLabel{background:%1;color:%2;border-radius:20px;font-size:22px;font-weight:bold;}")
            .arg(iconBg, iconColor)
    );

    // 标题
    auto* titleLabel = new QLabel(title);
    titleLabel->setStyleSheet(
        "QLabel{font-size:18px;font-weight:700;color:#1a1a2e;background:transparent;}"
    );

    headerLayout->addWidget(iconCircle);
    headerLayout->addWidget(titleLabel, 1);

    mainLayout->addWidget(headerWidget);

    // ── 消息内容区域 ──
    auto* bodyWidget = new QWidget();
    bodyWidget->setStyleSheet("background:transparent;");
    auto* bodyLayout = new QVBoxLayout(bodyWidget);
    bodyLayout->setContentsMargins(32, 20, 32, 20);

    if (!message.isEmpty()) {
        auto* msgLabel = new QLabel(message);
        msgLabel->setWordWrap(true);
        msgLabel->setStyleSheet(
            "QLabel{font-size:16px;color:#555;line-height:1.6;background:transparent;}"
        );
        bodyLayout->addWidget(msgLabel);
    }

    mainLayout->addWidget(bodyWidget);

    // ── 按钮区域 ──
    auto* btnWidget = new QWidget();
    btnWidget->setFixedHeight(72);
    btnWidget->setStyleSheet("background:transparent;");

    auto* btnLayout = new QHBoxLayout(btnWidget);
    btnLayout->setContentsMargins(24, 0, 24, 16);
    btnLayout->setSpacing(16);

    // 取消按钮（仅Question类型显示）
    if (type == Question) {
        auto* cancelBtn = new QPushButton(QStringLiteral("取消"));
        cancelBtn->setFixedHeight(48);
        cancelBtn->setMinimumWidth(120);
        cancelBtn->setCursor(Qt::PointingHandCursor);
        cancelBtn->setStyleSheet(
            "QPushButton{background:#fff;color:#666;border:2px solid #ddd;border-radius:12px;"
            "font-size:16px;font-weight:500;}"
            "QPushButton:hover{background:#f5f5f5;border-color:#bbb;}"
            "QPushButton:pressed{transform:scale(0.96);}"
        );
        connect(cancelBtn, &QPushButton::clicked, this, [this]() {
            m_confirmed = false;
            reject();
        });
        btnLayout->addWidget(cancelBtn);
    }

    // 确定按钮
    auto* okBtn = new QPushButton(type == Question ? QStringLiteral("确定") : QStringLiteral("知道了"));
    okBtn->setFixedHeight(48);
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
                "font-size:16px;font-weight:600;}")
            .arg(okBg)
        + QString("QPushButton:hover{background:%1;}")
            .arg(okHover)
        + QString("QPushButton:pressed{transform:scale(0.96);}")
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

// [2026-06-25] 每次弹出自动居中到父窗口（showEvent保证实际尺寸已确定）
void MessageDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);

    QWidget* pw = parentWidget();
    if (!pw) {
        // 无父窗口则居中到屏幕
        QScreen* screen = QApplication::primaryScreen();
        if (screen) {
            QRect sg = screen->availableGeometry();
            move(sg.center() - rect().center());
        }
        return;
    }

    // 获取父窗口在屏幕上的全局矩形
    QPoint parentGlobal = pw->mapToGlobal(QPoint(0, 0));
    QRect parentRect(parentGlobal, pw->size());

    // 当前对话框实际尺寸（showEvent时已确定）
    QSize mySize = frameGeometry().size();

    int x = parentRect.center().x() - mySize.width() / 2;
    int y = parentRect.center().y() - mySize.height() / 2;

    // 确保不出屏幕边界
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

void MessageDialog::showSingle(DialogType type, QWidget* parent,
                                const QString& title, const QString& message)
{
    MessageDialog dlg(type, parent);
    dlg.setupUI(type, title, message);
    dlg.exec();
}

void MessageDialog::showSuccess(QWidget* parent, const QString& title, const QString& message)
{
    showSingle(Success, parent, title, message);
}

void MessageDialog::showError(QWidget* parent, const QString& title, const QString& message)
{
    showSingle(Error, parent, title, message);
}

void MessageDialog::showWarning(QWidget* parent, const QString& title, const QString& message)
{
    showSingle(Warning, parent, title, message);
}

bool MessageDialog::showQuestion(QWidget* parent, const QString& title, const QString& message)
{
    MessageDialog dlg(Question, parent);
    dlg.setupUI(Question, title, message);
    dlg.exec();
    return dlg.m_confirmed;
}

// [2026-06-26] 脏数据保存确认对话框（保存/不保存，无取消按钮）
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
    dlg.setFixedWidth(400);

    // 主容器（圆角白色卡片）
    auto* card = new QFrame(&dlg);
    card->setObjectName(QStringLiteral("dirtyCard"));
    card->setStyleSheet("#dirtyCard{background:#fff;border-radius:16px;border:1px solid #e8ecf1;}");

    auto* mainLayout = new QVBoxLayout(card);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ── 图标 + 标题区域 ──
    auto* headerWidget = new QWidget();
    headerWidget->setFixedHeight(56);
    headerWidget->setStyleSheet("background:#f0f7ff;border-radius:16px 16px 0 0;");

    auto* headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(24, 0, 24, 0);
    headerLayout->setSpacing(14);

    auto* iconCircle = new QLabel(QStringLiteral("?"));
    iconCircle->setFixedSize(40, 40);
    iconCircle->setAlignment(Qt::AlignCenter);
    iconCircle->setStyleSheet(
        "QLabel{background:#4da3ff;color:#fff;border-radius:20px;font-size:22px;font-weight:bold;}");

    auto* titleLabel = new QLabel(title);
    titleLabel->setStyleSheet(
        "QLabel{font-size:18px;font-weight:700;color:#1a1a2e;background:transparent;}");

    headerLayout->addWidget(iconCircle);
    headerLayout->addWidget(titleLabel, 1);
    mainLayout->addWidget(headerWidget);

    // ── 消息内容区域 ──
    auto* bodyWidget = new QWidget();
    bodyWidget->setStyleSheet("background:transparent;");
    auto* bodyLayout = new QVBoxLayout(bodyWidget);
    bodyLayout->setContentsMargins(32, 20, 32, 20);

    if (!message.isEmpty()) {
        auto* msgLabel = new QLabel(message);
        msgLabel->setWordWrap(true);
        msgLabel->setStyleSheet(
            "QLabel{font-size:16px;color:#555;line-height:1.6;background:transparent;}");
        bodyLayout->addWidget(msgLabel);
    }
    mainLayout->addWidget(bodyWidget);

    // ── 按钮区域（保存 + 不保存，无取消按钮）──
    auto* btnWidget = new QWidget();
    btnWidget->setFixedHeight(72);
    btnWidget->setStyleSheet("background:transparent;");

    auto* btnLayout = new QHBoxLayout(btnWidget);
    btnLayout->setContentsMargins(24, 0, 24, 16);
    btnLayout->setSpacing(16);

    // 不保存按钮（次要按钮，灰色边框）
    auto* discardBtn = new QPushButton(dText);
    discardBtn->setFixedHeight(48);
    discardBtn->setMinimumWidth(130);
    discardBtn->setCursor(Qt::PointingHandCursor);
    discardBtn->setStyleSheet(
        "QPushButton{background:#fff;color:#666;border:2px solid #ddd;border-radius:12px;"
        "font-size:16px;font-weight:500;}"
        "QPushButton:hover{background:#f5f5f5;border-color:#bbb;}"
        "QPushButton:pressed{transform:scale(0.96);}");
    btnLayout->addWidget(discardBtn);

    // 保存按钮（主要按钮，蓝色实心）
    auto* saveBtn = new QPushButton(sText);
    saveBtn->setFixedHeight(48);
    saveBtn->setMinimumWidth(130);
    saveBtn->setCursor(Qt::PointingHandCursor);
    saveBtn->setStyleSheet(
        "QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:12px;"
        "font-size:16px;font-weight:600;}"
        "QPushButton:hover{background:#3d8ae0;}"
        "QPushButton:pressed{transform:scale(0.96);}");
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
