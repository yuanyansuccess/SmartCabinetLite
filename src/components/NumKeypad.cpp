/**
 * @file NumKeypad.cpp
 * @brief 独立数字软键盘控件实现 - QPainter自绘 + 顶层Popup弹窗
 * @author 袁燕
 * @date 2026-06-26
 *
 * [2026-06-26] 初始创建：从SoftKeyboard中独立纯数字键盘，QPainter自绘。
 * [2026-06-26v2] 改用顶层Popup窗口方案（参考SoftKeyboard）：
 *   show()时将NumKeypad加入m_panel布局，hide()时移回页面所属。
 *   彻底解决父布局空间不足导致的裁剪问题。
 */
#include "NumKeypad.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QApplication>
#include <QScreen>
#include <QVBoxLayout>
#include <QRandomGenerator>
#include <algorithm>

// ═══════════ 构造/析构 ═══════════

NumKeypad::NumKeypad(QWidget* parent)
    : QWidget(parent)
    , m_ownerWidget(parent)  // 保存原始父窗口
{
    setVisible(false);
    setMouseTracking(true);

    // 计算屏幕适配宽度
    int screenW = 460;
    if (QApplication::primaryScreen()) {
        int availW = QApplication::primaryScreen()->availableGeometry().width();
        screenW = qMin(460, (int)(availW * 0.90));
        screenW = qMax(320, screenW);
    }
    setFixedWidth(screenW);

    m_keys = {"7","8","9","4","5","6","1","2","3",".","0","⌫"};
}

NumKeypad::~NumKeypad()
{
    // [2026-06-26v2] 安全解绑：移除从面板布局，防止双重删除
    if (m_panel && m_panel->layout()) {
        m_panel->layout()->removeWidget(this);
    }
    if (m_overlay) { m_overlay->hide(); m_overlay->close(); delete m_overlay; m_overlay = nullptr; }
    if (m_panel)   { m_panel->hide();   m_panel->close();   delete m_panel;   m_panel = nullptr; }
}

// ═══════════ 顶层面板确保（参考SoftKeyboard） ═══════════

void NumKeypad::ensurePanel()
{
    if (m_panel) return;

    // ── 全屏半透明遮罩 ──
    // [2026-06-26v3致命修复] WA_TranslucentBackground必须为true+setAutoFillBackground必须为false
    //   否则rgba半透明不生效，显示为纯黑→造成黑屏闪烁。
    //   与SoftKeyboard遮罩方案完全对齐。
    m_overlay = new QWidget(nullptr);
    m_overlay->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    m_overlay->setAttribute(Qt::WA_TranslucentBackground, true);
    m_overlay->setAutoFillBackground(false);
    m_overlay->setStyleSheet("background:rgba(0,0,0,0.65);");  // [2026-06-27] 0.45→0.65 杜绝底层按钮穿透

    // ── 键盘面板（独立顶层窗口） ──
    m_panel = new QWidget(nullptr);
    m_panel->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    m_panel->setAttribute(Qt::WA_TranslucentBackground, false);
    m_panel->setAutoFillBackground(true);
    m_panel->setStyleSheet("QWidget{background:#ffffff;border:none;}");
    m_panel->setFixedWidth(width());

    // 面板布局：NumKeypad作为子控件填充
    auto* panelLayout = new QVBoxLayout(m_panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(0);

    // 计算总高度
    qreal totalH = m_padTop + m_toolbarH + m_displayH
                   + 4 * m_keyH + 3 * m_keySpacing
                   + m_footerH + m_padBottom;
    m_panel->setFixedHeight((int)totalH);
    m_panel->hide();
}

// ═══════════ 显示/隐藏 ═══════════

void NumKeypad::show()
{
    ensurePanel();
    if (!m_panel) return;

    // 将NumKeypad移入面板布局中
    // 先确保不在任何布局中（从panelLayout移除后重新加入）
    auto* panelLayout = qobject_cast<QVBoxLayout*>(m_panel->layout());
    if (panelLayout) {
        int idx = panelLayout->indexOf(this);
        if (idx < 0) {
            panelLayout->addWidget(this);  // reparent NumKeypad → m_panel
        }
    }

    // 设置NumKeypad尺寸填满面板
    resize(m_panel->size());
    recalcKeyRects();
    QWidget::show();  // 显示NumKeypad（现在是面板的子控件）
    m_panel->update();

    // ── 显示遮罩（先于面板） ──
    QScreen* screen = QApplication::primaryScreen();
    if (screen && m_overlay) {
        m_overlay->setGeometry(screen->geometry());
        m_overlay->setWindowOpacity(1.0);  // [2026-06-27] 确保遮罩完全不透明渲染
        m_overlay->show();
        m_overlay->raise();
    }

    // ── 显示面板 ──
    m_panel->setWindowOpacity(1.0);  // [2026-06-27] 确保面板完全不透明
    m_panel->show();
    m_panel->raise();
    m_panel->activateWindow();

    // 定位到屏幕底部居中
    if (screen) {
        QRect scr = screen->availableGeometry();
        int x = scr.center().x() - m_panel->width() / 2;
        int y = scr.bottom() - m_panel->height() - 8;
        m_panel->move(x, y);
    }
    m_panel->raise();
}

void NumKeypad::hide()
{
    // [2026-06-26v2] 恢复父控件归属，防止页面销毁时双重删除
    if (m_panel && m_panel->layout()) {
        m_panel->layout()->removeWidget(this);
    }
    if (m_ownerWidget) {
        setParent(m_ownerWidget);
    }

    // [2026-06-26v6致命修复] 遮罩层必须close()释放窗口句柄，仅hide()会残留拦截鼠标事件
    //   导致弹窗内"取消""确认"等按钮无法点击（模态对话框被遮罩层阻断）
    //   close()后置nullptr，确保下次show()时ensurePanel()会重建
    if (m_overlay) { m_overlay->hide(); m_overlay->close(); m_overlay = nullptr; }
    if (m_panel)   { m_panel->hide();   m_panel->close();   m_panel = nullptr; }
}

// ═══════════ 公开API ═══════════

void NumKeypad::attach(QLineEdit* target)
{
    m_target = target;
}

void NumKeypad::setShuffle(bool enable)
{
    if (m_shuffle == enable) return;
    m_shuffle = enable;
    if (m_shuffle) shuffleKeys();
    else           m_keys = {"7","8","9","4","5","6","1","2","3",".","0","⌫"};
    recalcKeyRects();
    update();
}

void NumKeypad::setShowPassword(bool show)
{
    m_showPassword = show;
    update();
}

QString NumKeypad::currentText() const
{
    return m_target ? m_target->text() : QString();
}

void NumKeypad::clear()
{
    if (m_target) m_target->clear();
    update();
}

// ═══════════ Fisher-Yates 随机打乱 ═══════════

void NumKeypad::shuffleKeys()
{
    QStringList digits = {"1","2","3","4","5","6","7","8","9","0"};
    for (int i = digits.size() - 1; i > 0; --i) {
        int j = QRandomGenerator::global()->bounded(i + 1);
        digits.swapItemsAt(i, j);
    }
    m_keys.clear();
    for (int i = 0; i < 9; ++i) m_keys << digits[i];
    m_keys << digits[9] << "." << "⌫";
}

// ═══════════ 按键文本 ═══════════

QString NumKeypad::keyText(int row, int col) const
{
    static const int layout[4][3] = { {0,1,2}, {3,4,5}, {6,7,8}, {9,10,11} };
    if (row < 0 || row > 3 || col < 0 || col > 2) return QString();
    int idx = layout[row][col];
    return (idx < m_keys.size()) ? m_keys[idx] : QString();
}

// ═══════════ 布局计算 ═══════════

void NumKeypad::recalcKeyRects()
{
    m_keyRects.clear();
    qreal w = (qreal)width();
    if (w < 100) w = 320;

    qreal y = m_padTop + m_toolbarH + m_displayH;
    qreal availW = w - m_padLeft - m_padRight;
    qreal colW = (availW - 2 * m_keySpacing) / 3.0;

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 3; ++col) {
            KeyRect kr;
            kr.row = row; kr.col = col;
            kr.text = keyText(row, col);
            kr.rect = QRectF(
                m_padLeft + col * (colW + m_keySpacing),
                y + row * (m_keyH + m_keySpacing),
                colW, m_keyH);
            m_keyRects.append(kr);
        }
    }
}

void NumKeypad::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    recalcKeyRects();
    qreal totalH = m_padTop + m_toolbarH + m_displayH
                   + 4 * m_keyH + 3 * m_keySpacing
                   + m_footerH + m_padBottom;
    setFixedHeight((int)totalH);
}

// ═══════════ 鼠标事件 ═══════════

void NumKeypad::mouseMoveEvent(QMouseEvent* event)
{
    QPointF pos = event->position();
    int oldRow = m_hoveredRow, oldCol = m_hoveredCol;
    m_hoveredRow = -1; m_hoveredCol = -1;

    for (const auto& kr : m_keyRects) {
        if (kr.rect.contains(pos)) { m_hoveredRow = kr.row; m_hoveredCol = kr.col; break; }
    }
    if (oldRow != m_hoveredRow || oldCol != m_hoveredCol) update();
}

void NumKeypad::leaveEvent(QEvent*)
{
    m_hoveredRow = -1; m_hoveredCol = -1;
    update();
}

void NumKeypad::mousePressEvent(QMouseEvent* event)
{
    QPointF pos = event->position();
    qreal w = (qreal)width();

    // 确认按钮
    qreal footerY = m_padTop + m_toolbarH + m_displayH + 4 * m_keyH + 3 * m_keySpacing;
    QRectF confirmRect(m_padLeft, footerY, w - m_padLeft - m_padRight, m_footerH - 8);
    if (confirmRect.contains(pos)) { m_pressedRow = -2; update(); return; }

    // 关闭按钮
    qreal closeX = w - m_padRight - 36;
    QRectF closeRect(closeX, m_padTop, 36, m_toolbarH);
    if (closeRect.contains(pos)) { m_pressedRow = -3; update(); return; }

    // 按键
    for (const auto& kr : m_keyRects) {
        if (kr.rect.contains(pos)) { m_pressedRow = kr.row; m_pressedCol = kr.col; update(); return; }
    }
}

void NumKeypad::mouseReleaseEvent(QMouseEvent* event)
{
    QPointF pos = event->position();
    qreal w = (qreal)width();

    // 确认按钮
    if (m_pressedRow == -2) {
        m_pressedRow = -1;
        qreal footerY = m_padTop + m_toolbarH + m_displayH + 4 * m_keyH + 3 * m_keySpacing;
        QRectF confirmRect(m_padLeft, footerY, w - m_padLeft - m_padRight, m_footerH - 8);
        if (confirmRect.contains(pos)) emit confirmed();
        update(); return;
    }

    // 关闭按钮
    if (m_pressedRow == -3) {
        m_pressedRow = -1;
        qreal closeX = w - m_padRight - 36;
        QRectF closeRect(closeX, m_padTop, 36, m_toolbarH);
        if (closeRect.contains(pos)) emit cancelled();
        update(); return;
    }

    // 按键
    int pr = m_pressedRow, pc = m_pressedCol;
    m_pressedRow = -1; m_pressedCol = -1;
    for (const auto& kr : m_keyRects) {
        if (kr.rect.contains(pos) && kr.row == pr && kr.col == pc) {
            QString key = kr.text;
            if (key == "⌫") doBackspace();
            else appendChar(key);
            emit textChanged(m_target ? m_target->text() : QString());
            break;
        }
    }
    update();
}

// ═══════════ 输入处理 ═══════════

void NumKeypad::appendChar(const QString& ch)
{
    if (!m_target) return;
    m_target->setText(m_target->text() + ch);
    update();
}

void NumKeypad::doBackspace()
{
    if (!m_target) return;
    QString t = m_target->text();
    if (!t.isEmpty()) m_target->setText(t.left(t.length() - 1));
    update();
}

// ═══════════ QPainter 自绘 ═══════════

void NumKeypad::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    qreal w = (qreal)width();

    // ── 1. 整体背景圆角 ──
    QPainterPath bgPath;
    bgPath.addRoundedRect(QRectF(0, 0, w, height()), m_radius, m_radius);
    p.setClipPath(bgPath);
    p.fillRect(rect(), QColor(COLOR_BG));

    // ── 2. 工具栏 ──
    QRectF toolbarRect(0, m_padTop, w, m_toolbarH);
    p.fillRect(toolbarRect, QColor(COLOR_TOOLBAR_BG));

    p.setPen(QColor("#888888"));
    QFont titleFont;
    titleFont.setPixelSize(13);
    titleFont.setWeight(QFont::DemiBold);
    p.setFont(titleFont);
    QRectF titleRect(m_padLeft + 28, m_padTop, w - 80, m_toolbarH);
    p.drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("安全软键盘"));

    p.setPen(QColor("#bbbbbb"));
    QFont iconFont;
    iconFont.setPixelSize(16);
    p.setFont(iconFont);
    p.drawText(QRectF(m_padLeft, m_padTop, 28, m_toolbarH), Qt::AlignCenter, QStringLiteral("⠿"));

    qreal closeX = w - m_padRight - 36;
    QRectF closeBtn(closeX, m_padTop, 36, m_toolbarH);
    p.setPen((m_pressedRow == -3) ? QColor("#e74c3c") : QColor("#999999"));
    QFont closeFont;
    closeFont.setPixelSize(18);
    closeFont.setWeight(QFont::Bold);
    p.setFont(closeFont);
    p.drawText(closeBtn, Qt::AlignCenter, QStringLiteral("✕"));

    // ── 3. 显示区 ──
    qreal displayY = m_padTop + m_toolbarH;
    QRectF displayRect(0, displayY, w, m_displayH);
    p.fillRect(displayRect, QColor(COLOR_DISPLAY_BG));

    QString text = m_target ? m_target->text() : QString();
    if (m_showPassword) {
        p.setPen(QColor("#333333"));
        QFont dispFont;
        dispFont.setPixelSize(20);
        dispFont.setWeight(QFont::Bold);
        p.setFont(dispFont);
        QRectF textRect(m_padLeft, displayY, w - m_padLeft - 60, m_displayH);
        p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text);
    } else {
        qreal dotX = m_padLeft;
        qreal dotY = displayY + m_displayH / 2;
        for (int i = 0; i < text.length(); ++i) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(COLOR_DOT));
            p.drawEllipse(QPointF(dotX + 10 + i * 22, dotY), 6, 6);
        }
    }
    p.setPen(QColor("#999999"));
    QFont countFont;
    countFont.setPixelSize(13);
    p.setFont(countFont);
    QRectF countRect(w - 60, displayY, 44, m_displayH);
    p.drawText(countRect, Qt::AlignVCenter | Qt::AlignRight, QString::number(text.length()));

    // ── 4. 按键 ──
    for (const auto& kr : m_keyRects) {
        QRectF r = kr.rect;
        bool isFn = (kr.text == "⌫");
        bool isHover = (kr.row == m_hoveredRow && kr.col == m_hoveredCol);
        bool isPress = (kr.row == m_pressedRow && kr.col == m_pressedCol);

        QColor bg = isPress ? QColor(COLOR_KEY_PRESS)
                  : isHover ? QColor(COLOR_KEY_HOVER)
                  : isFn    ? QColor(COLOR_KEY_FN_BG)
                  :           QColor(COLOR_KEY_BG);

        QPainterPath keyPath;
        keyPath.addRoundedRect(r, 10, 10);
        p.fillPath(keyPath, bg);

        if (!isPress && !isHover) {
            p.setPen(QPen(QColor(COLOR_KEY_BORDER), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r, 10, 10);
        } else if (isHover) {
            p.setPen(QPen(QColor(COLOR_CONFIRM_BG), 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r, 10, 10);
        }

        p.setPen(isFn ? QColor(COLOR_KEY_FN_TEXT) : QColor(COLOR_KEY_TEXT));
        QFont keyFont;
        keyFont.setFamily(QStringLiteral("Microsoft YaHei"));   // [2026-06-27] 统一中文系统字体
        keyFont.setPixelSize(20);                                // [2026-06-27] 统一20px，消除功能键/数字键视觉不和谐
        keyFont.setWeight(QFont::DemiBold);
        p.setFont(keyFont);
        p.drawText(r, Qt::AlignCenter, kr.text);
    }

    // ── 5. 确认按钮 ──
    qreal keysY = displayY + m_displayH;
    qreal footerY = keysY + 4 * m_keyH + 3 * m_keySpacing;
    QRectF confirmRect(m_padLeft, footerY, w - m_padLeft - m_padRight, m_footerH - 8);
    QPainterPath cfmPath;
    cfmPath.addRoundedRect(confirmRect, 12, 12);

    QColor cfmBg = (m_pressedRow == -2) ? QColor("#3d8ae0") : QColor(COLOR_CONFIRM_BG);
    p.fillPath(cfmPath, cfmBg);

    p.setPen(QColor(COLOR_CONFIRM_TEXT));
    QFont cfmFont;
    cfmFont.setPixelSize(17);
    cfmFont.setWeight(QFont::Bold);
    p.setFont(cfmFont);
    p.drawText(confirmRect, Qt::AlignCenter, QStringLiteral("确 认"));

    // ── 6. 分隔线 ──
    p.setPen(QPen(QColor(COLOR_BORDER), 1));
    p.drawLine(QPointF(0, displayY), QPointF(w, displayY));
}
