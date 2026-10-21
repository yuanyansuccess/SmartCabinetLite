/**
 * @file BaseDialog.cpp
 * @brief 通用对话框基类实现
 * @author 袁燕
 * @date 2026-06-26
 */
#include "BaseDialog.h"
#include "utils/StyleHelper.h"  // 字号统一走 StyleHelper::Token
#include <QLabel>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QApplication>
#include <QScreen>
#include <QGraphicsDropShadowEffect>
#include <QLayout>
#include <QTimer>
#include <QWindow>

namespace {

// 窗口映射后重新居中的时间点（毫秒）：0 处理本轮事件，60/200 吸收窗口管理器的异步重排与后到的布局变化
const int kRecenterDelaysMs[] = {0, 60, 200};

} // namespace

BaseDialog::BaseDialog(QWidget* parent, int cardWidth)
    : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    setFixedWidth(cardWidth + 4);  // +4 容纳阴影边距

    buildBaseUI(cardWidth);
}

/**
 * @brief 构建对话框基础结构：标题栏、内容区与按钮区
 */
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
        "  " + StyleHelper::fontSize(StyleHelper::Token::FontTitle) + "font-weight:700;color:#1a1a2e;"
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

/**
 * @brief 设置对话框标题
 */
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

/**
 * @brief 显示或隐藏底部按钮区
 * @param visible true=显示
 */
void BaseDialog::setButtonAreaVisible(bool visible)
{
    m_buttonArea->setVisible(visible);
}

/**
 * @brief 将对话框居中到父窗口，并钳制在其所在屏幕的可用区域内
 * @param dialog 待定位的顶层对话框
 *
 * 坐标计算：取父窗口全局矩形中心减去对话框尺寸的一半作为目标左上角；
 * 钳制基准取对话框实际所在屏幕（多屏环境与主屏可能不同，避免钳到错误屏幕），
 * 对话框宽/高超出屏幕可用区域时锚定左上角，保证标题与首行控件始终可见。
 */
void BaseDialog::centerOnParent(QWidget* dialog)
{
    if (!dialog) {
        return;
    }

    QWidget* pw = dialog->parentWidget();

    // 以父窗口所在屏幕为准：父窗口显示前位置已确定，比尚未映射的对话框 windowHandle 可靠，
    // 多屏环境下可避免把弹窗钳到错误的屏幕（例如副屏父窗口被钳到主屏右缘）
    QScreen* screen = nullptr;
    if (pw) {
        screen = QApplication::screenAt(pw->mapToGlobal(pw->rect().center()));
    }
    if (!screen) {
        if (QWindow* handle = dialog->window()->windowHandle()) {
            screen = handle->screen();
        }
    }
    if (!screen) {
        screen = QApplication::screenAt(dialog->frameGeometry().center());
    }
    if (!screen) {
        screen = QApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }
    const QRect sg = screen->availableGeometry();

    // 尺寸取对话框自身的 size()：无边框窗口的 frameGeometry 在窗口映射完成前，
    // 部分平台（如麒麟窗口管理器）会返回空尺寸，此时"父窗口中心减尺寸一半"会退化成
    // 直接用父窗口中心定位，弹窗整体偏向右下、底部按钮被屏幕边缘裁掉。
    // 取不到有效尺寸时按 adjustSize、sizeHint 逐级兜底；仍无效则不强行定位。
    QSize mySize = dialog->size();
    if (mySize.width() <= 0 || mySize.height() <= 0) {
        dialog->adjustSize();
        mySize = dialog->size();
    }
    if (mySize.width() <= 0 || mySize.height() <= 0) {
        mySize = dialog->sizeHint();
    }
    if (mySize.width() <= 0 || mySize.height() <= 0) {
        return;
    }

    QPoint topLeft;
    if (pw) {
        const QRect parentRect(pw->mapToGlobal(QPoint(0, 0)), pw->size());
        topLeft = QPoint(parentRect.center().x() - mySize.width() / 2,
                         parentRect.center().y() - mySize.height() / 2);
    } else {
        topLeft = QPoint(sg.center().x() - mySize.width() / 2,
                         sg.center().y() - mySize.height() / 2);
    }

    // 宽或高超出屏幕时锚定左上角，防止钳制上下界倒挂把标题顶出屏幕
    if (mySize.width() >= sg.width()) {
        topLeft.setX(sg.left());
    }
    if (mySize.height() >= sg.height()) {
        topLeft.setY(sg.top());
    }
    topLeft.setX(qBound(sg.left(), topLeft.x(), sg.right() - mySize.width()));
    topLeft.setY(qBound(sg.top(), topLeft.y(), sg.bottom() - mySize.height()));

#ifdef Q_OS_LINUX
    // 麒麟定位诊断：记录映射后的实际几何与屏幕可用区域，便于现场核对坐标口径
    qInfo() << "[BaseDialog] reposition dialogSize" << mySize
            << "topLeft" << topLeft << "available" << sg;
#endif
    dialog->move(topLeft);
}

/**
 * @brief 显示事件：居中到父窗口，并在映射稳定后按多个时间点重新校准位置
 * @param event 显示事件对象
 */
void BaseDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    // 统一走 centerAndKeep：立即居中 + 多时间点重校准，兼容麒麟等窗口管理器
    centerAndKeep(this);
}

/**
 * @brief 显示时居中父窗口，并在窗口映射稳定后按多个时间点重新校准位置
 * @param dialog 待定位的顶层对话框
 */
void BaseDialog::centerAndKeep(QWidget* dialog)
{
    if (!dialog) {
        return;
    }

    // 首次定位：窗口映射时按当前几何居中
    centerOnParent(dialog);

    // 麒麟等平台的窗口管理器可能在映射后重排窗口，布局也可能在显示后才最终定型；
    // 按多个时间点再校准一次，保证任何窗口环境下都准确居中
    for (int delayMs : kRecenterDelaysMs) {
        QTimer::singleShot(delayMs, dialog, [dialog]() {
            if (dialog->isVisible()) {
                centerOnParent(dialog);
            }
        });
    }
}

/**
 * @brief 显示与隐藏切换：首次显示前先按布局尺寸预约居中位置
 * @param visible true=显示，false=隐藏
 */
void BaseDialog::setVisible(bool visible)
{
    if (visible && !isVisible()) {
        // 映射前定型布局并按真实尺寸算好位置：窗口管理器对首次映射时的位置更尊重，
        // 映射之后再移动则可能被级联放置规则覆盖
        ensurePolished();
        if (QLayout* lay = layout()) {
            lay->activate();
        }
        adjustSize();
        centerOnParent(this);
    }
    QDialog::setVisible(visible);
}

/**
 * @brief 处理键盘按键：回车触发默认按钮，ESC 关闭对话框
 * @param event 按键事件
 */
void BaseDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        reject();
        return;
    }
    QDialog::keyPressEvent(event);
}
