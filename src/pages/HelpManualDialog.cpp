/**
 * @file HelpManualDialog.cpp
 * @brief 操作手册对话框实现 — 全屏窗口 + QTextBrowser 加载qrc内置HTML手册
 * @author 袁燕
 *
 * 手册HTML随程序编译进资源文件（:/resources/manual.html），
 * 无需随安装包额外携带文件，Windows与麒麟环境均可直接打开。
 */
#include "HelpManualDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QFile>

HelpManualDialog::HelpManualDialog(QWidget* parent) : QDialog(parent) {
    setupUI();
}

/**
 * @brief 构建全屏手册窗口：顶部标题栏与HTML内容浏览区
 */
void HelpManualDialog::setupUI() {
    // 无边框全屏窗口，贴合柜机触屏使用场景
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setModal(true);
    setStyleSheet("QDialog { background:#ffffff; }");

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // ── 顶部标题栏：标题 + 关闭按钮 ──
    auto* topBar = new QWidget();
    topBar->setFixedHeight(72);
    topBar->setStyleSheet("background:#1a1a2e;");
    auto* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(28, 0, 20, 0);

    auto* titleLabel = new QLabel(QStringLiteral("智能工具柜管理系统 · 操作手册"));
    titleLabel->setStyleSheet("color:#ffffff; font-size:22px; font-weight:bold; background:transparent;");
    topLayout->addWidget(titleLabel);
    topLayout->addStretch();

    // 关闭按钮：触屏友好尺寸，浅色描边
    m_closeBtn = new QPushButton(QStringLiteral("关 闭"));
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setFixedSize(120, 48);
    m_closeBtn->setStyleSheet(
        "QPushButton { background:transparent; color:#ffffff; border:2px solid #4a4a6e;"
        "  border-radius:12px; font-size:18px; font-weight:600; }"
        "QPushButton:hover { background:#4da3ff; border-color:#4da3ff; }"
        "QPushButton:pressed { background:#3d8ae0; }");
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    topLayout->addWidget(m_closeBtn);

    lay->addWidget(topBar);

    // ── 内容区：QTextBrowser 加载内置手册 ──
    m_browser = new QTextBrowser();
    m_browser->setFrameShape(QFrame::NoFrame);
    m_browser->setStyleSheet(
        "QTextBrowser { background:#ffffff; border:none; padding:8px 16px; }"
        "QScrollBar:vertical { width:10px; background:transparent; }"
        "QScrollBar::handle:vertical { background:#d0d0d0; border-radius:5px; min-height:40px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }");
    QFile manualFile(":/resources/manual.html");
    if (manualFile.open(QIODevice::ReadOnly)) {
        m_browser->setHtml(QString::fromUtf8(manualFile.readAll()));
        manualFile.close();
    } else {
        // 资源加载失败的兜底提示（正常打包下不会出现）
        m_browser->setHtml(QStringLiteral("<h2>手册资源加载失败，请联系维护人员。</h2>"));
    }
    lay->addWidget(m_browser, 1);
}
