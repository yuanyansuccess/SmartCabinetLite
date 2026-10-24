/**
 * @file NumKeypad.cpp
 * @brief 独立数字软键盘控件实现 - QPainter自绘 + 顶层Popup弹窗
 * @author 袁燕
 * @date 2026-06-26
 *
 * 从SoftKeyboard中独立出的纯数字键盘，采用QPainter自绘 + 顶层Popup弹窗方案（参考SoftKeyboard）：
 *   show()时将NumKeypad加入m_panel布局，hide()时移回页面所属，
 *   彻底解决父布局空间不足导致的裁剪问题。
 */
#include "NumKeypad.h"
#include "utils/StyleHelper.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QApplication>
#include <QScreen>
#include <QVBoxLayout>
#include <QRandomGenerator>
#include <QTimer>
#include <algorithm>

// ═══════════ 构造/析构 ═══════════

/**
 * @brief 构造函数，初始化 NumKeypad 对象并连接信号槽
 * @param parent 父对象指针，作为 Qt 对象树父节点接管所有权
 */
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

    // 按键共11个：前三行9个数字，第四行「0」与退格键（不提供小数点）
    m_keys = {"7","8","9","4","5","6","1","2","3","0","⌫"};
}

NumKeypad::~NumKeypad()
{
    // 安全解绑：移除从面板布局，防止双重删除
    if (m_panel && m_panel->layout()) {
        m_panel->layout()->removeWidget(this);
    }
    if (m_overlay) { m_overlay->hide(); m_overlay->close(); delete m_overlay; m_overlay = nullptr; }
    if (m_panel)   { m_panel->hide();   m_panel->close();   delete m_panel;   m_panel = nullptr; }
}

// ═══════════ 顶层面板确保（参考SoftKeyboard） ═══════════

/**
 * @brief NumKeypad::ensurePanel 的业务实现
 */
void NumKeypad::ensurePanel()
{
    if (m_panel) return;

    // ── 全屏半透明遮罩 ──
    // WA_TranslucentBackground必须为true+setAutoFillBackground必须为false
    // 否则rgba半透明不生效，显示为纯黑→造成黑屏闪烁。
    // 与 SoftKeyboard 的遮罩做法保持一致。
    m_overlay = new QWidget(nullptr);
    m_overlay->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    m_overlay->setAttribute(Qt::WA_TranslucentBackground, true);
    m_overlay->setAutoFillBackground(false);
    m_overlay->setStyleSheet("background:rgba(0,0,0,0.65);");  // 0.65 杜绝底层按钮穿透

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
    // 监视面板的隐藏/关闭：Qt::Popup 在点击面板外部区域时会自动关闭最上层弹窗
    // （即本面板），必须据此联动收起遮罩，否则遮罩残留盖住整页（详见 eventFilter）
    m_panel->installEventFilter(this);
    m_panel->hide();
}

// ═══════════ 显示/隐藏 ═══════════

/**
 * @brief NumKeypad::show 的业务实现
 */
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
        m_overlay->setWindowOpacity(1.0);  // 确保遮罩完全不透明渲染
        m_overlay->show();
        m_overlay->raise();
    }

    // ── 显示面板 ──
    m_panel->setWindowOpacity(1.0);  // 确保面板完全不透明
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

/**
 * @brief 隐藏数字键盘面板
 */
void NumKeypad::hide()
{
    // 恢复父控件归属，防止页面销毁时双重删除
    if (m_panel && m_panel->layout()) {
        m_panel->layout()->removeWidget(this);
    }
    if (m_ownerWidget && parentWidget() != m_ownerWidget) {
        setParent(m_ownerWidget);
    }

    // 键盘必须真正调用 QWidget::hide()：仅拆面板时本控件作为 LoginPage 子控件仍可见，
    // 表现为"密码登录后软键盘还挂在登录页上"
    QWidget::hide();

    QWidget* overlay = m_overlay;
    QWidget* panel   = m_panel;
    m_overlay = nullptr;
    m_panel   = nullptr;
    if (!overlay && !panel) return;

    // 崩溃修复（0xC0000005 读取 0xFFFFFFFFFFFFFFFF @ Qt6Widgets）：
    // 本函数是在"本面板(顶层Popup窗口)自己的 mouseReleaseEvent"里被调用的
    // （LoginPage::confirmed 回调第一句就是 m_numKeypad->hide()）。
    // 此时同步 hide()/close()/delete 面板，等于在事件派发尚未返回时销毁正在派发事件的窗口，
    // Qt 内部随后访问已释放的 QWidget/QWindow → 访问冲突；随后整条登录链
    // （onPasswordLogin → loginSuccess → MainWindow::onLoginSuccess）继续在已损坏的
    // 窗口状态上跑，最终在 onLoginSuccess 第一处 Qt 调用处崩溃。
    // 注意：面板/遮罩的关闭与销毁延迟到本轮事件派发结束（singleShot(0)）执行，
    // 且必须 deleteLater 释放——仅 close()+置空指针会每次登录泄漏一个全屏半透明顶层窗口。
    QObject* ctx = m_ownerWidget ? static_cast<QObject*>(m_ownerWidget)
                                 : static_cast<QObject*>(this);
    QTimer::singleShot(0, ctx, [overlay, panel]() {
        if (overlay) { overlay->hide(); overlay->close(); overlay->deleteLater(); }
        if (panel)   { panel->hide();   panel->close();   panel->deleteLater(); }
    });
}

/**
 * @brief 面板事件拦截：点击面板外部区域时，Qt::Popup 原生机制只自动关闭最上层弹窗
 *        （即本键盘面板），而全屏遮罩会顺势变成新的最顶层弹窗继续显示，
 *        把整个页面盖住导致点哪都没反应——表现为"键盘消失了但页面假卡死"。
 *        此处捕获面板被隐藏/关闭，联动收起遮罩，恢复页面可操作性。
 * @param obj 被监视的对象（本函数只关心键盘面板）
 * @param event 事件对象
 * @return 交还 Qt 默认处理的结果
 */
bool NumKeypad::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_panel
        && (event->type() == QEvent::Hide || event->type() == QEvent::Close)) {
        // 面板已被 Qt 关闭：走完整 hide() 收起遮罩并恢复父控件归属。
        // hide() 会先把本控件从面板布局摘出再延迟销毁面板，故本控件不会随面板一起被销毁；
        // 同时置空 m_panel/m_overlay，下次 show() 由 ensurePanel() 完整重建（含遮罩）。
        hide();
    }
    return QWidget::eventFilter(obj, event);
}

// ═══════════ 公开API ═══════════

/**
 * @brief NumKeypad::attach 的业务实现
 * @param target 要绑定的目标对象（如输入控件）
 */
void NumKeypad::attach(QLineEdit* target)
{
    m_target = target;
}

/**
 * @brief 设置数字键是否打乱顺序
 * @param on true=每次显示随机排列，便于触屏防误触
 */
void NumKeypad::setShuffle(bool enable)
{
    if (m_shuffle == enable) return;
    m_shuffle = enable;
    if (m_shuffle) shuffleKeys();
    else           m_keys = {"7","8","9","4","5","6","1","2","3","0","⌫"};
    recalcKeyRects();
    update();
}

/**
 * @brief 设置是否以密码模式显示输入内容
 * @param on true=显示为掩码
 */
void NumKeypad::setShowPassword(bool show)
{
    m_showPassword = show;
    update();
}

/**
 * @brief 读取当前输入内容
 * @return 输入文本
 */
QString NumKeypad::currentText() const
{
    return m_target ? m_target->text() : QString();
}

/**
 * @brief 清空当前输入内容
 */
void NumKeypad::clear()
{
    if (m_target) m_target->clear();
    update();
}

// ═══════════ Fisher-Yates 随机打乱 ═══════════

/**
 * @brief NumKeypad::shuffleKeys 的业务实现
 */
void NumKeypad::shuffleKeys()
{
    QStringList digits = {"1","2","3","4","5","6","7","8","9","0"};
    for (int i = digits.size() - 1; i > 0; --i) {
        int j = QRandomGenerator::global()->bounded(i + 1);
        digits.swapItemsAt(i, j);
    }
    m_keys.clear();
    for (int i = 0; i < 9; ++i) m_keys << digits[i];
    m_keys << digits[9] << "⌫";
}

// ═══════════ 按键文本 ═══════════

/**
 * @brief NumKeypad::keyText 的业务实现
 * @param row 表格行号（从 0 开始）
 * @param col 表格列号（从 0 开始）
 * @return 返回生成的字符串（失败时可能为空串）
 */
QString NumKeypad::keyText(int row, int col) const
{
    // 前三行每行三个数字键，第四行为「0」与退格键（第三格空缺）
    static const int layout[4][3] = { {0,1,2}, {3,4,5}, {6,7,8}, {9,10,-1} };
    if (row < 0 || row > 3 || col < 0 || col > 2) return QString();
    int idx = layout[row][col];
    if (idx < 0 || idx >= m_keys.size()) return QString();
    return m_keys[idx];
}

// ═══════════ 布局计算 ═══════════

/**
 * @brief NumKeypad::recalcKeyRects 的业务实现
 */
void NumKeypad::recalcKeyRects()
{
    m_keyRects.clear();
    qreal w = (qreal)width();
    if (w < 100) w = 320;

    qreal y = m_padTop + m_toolbarH + m_displayH;
    qreal availW = w - m_padLeft - m_padRight;
    qreal colW = (availW - 2 * m_keySpacing) / 3.0;

    // 前三行：每行三个数字键
    for (int row = 0; row < 3; ++row) {
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

    // 第四行：只有「0」与退格键，两键各占半行宽度
    qreal lastW = (availW - m_keySpacing) / 2.0;
    for (int col = 0; col < 2; ++col) {
        KeyRect kr;
        kr.row = 3; kr.col = col;
        kr.text = keyText(3, col);
        kr.rect = QRectF(
            m_padLeft + col * (lastW + m_keySpacing),
            y + 3 * (m_keyH + m_keySpacing),
            lastW, m_keyH);
        m_keyRects.append(kr);
    }
}

/**
 * @brief 尺寸变化事件处理：按新尺寸重排按键
 * @param event 尺寸变化事件
 */
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

/**
 * @brief NumKeypad::mouseMoveEvent 的业务实现
 * @param event 事件对象，由 Qt 事件系统传入
 */
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

/**
 * @brief 鼠标移出事件处理：隐藏按键高亮
 * @param event 离开事件
 */
void NumKeypad::leaveEvent(QEvent*)
{
    m_hoveredRow = -1; m_hoveredCol = -1;
    update();
}

/**
 * @brief 按键按下事件处理：记录按下位置
 * @param event 鼠标事件
 */
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

/**
 * @brief 按键释放事件处理：命中按键时输出对应字符
 * @param event 鼠标事件
 */
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

/**
 * @brief NumKeypad::appendChar 的业务实现
 * @param ch 本次输入的单个字符
 */
void NumKeypad::appendChar(const QString& ch)
{
    if (!m_target) return;
    m_target->setText(m_target->text() + ch);
    update();
}

/**
 * @brief 删除输入的最后一个字符
 */
void NumKeypad::doBackspace()
{
    if (!m_target) return;
    QString t = m_target->text();
    if (!t.isEmpty()) m_target->setText(t.left(t.length() - 1));
    update();
}

// ═══════════ QPainter 自绘 ═══════════

/**
 * @brief NumKeypad::paintEvent 的业务实现
 * @param QPaintEvent 参数 QPaintEvent（类型与取值约束见函数签名）
 */
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
    titleFont.setPixelSize(StyleHelper::Token::FontSmall);
    titleFont.setWeight(QFont::DemiBold);
    p.setFont(titleFont);
    QRectF titleRect(m_padLeft + 28, m_padTop, w - 80, m_toolbarH);
    p.drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("安全软键盘"));

    p.setPen(QColor("#bbbbbb"));
    QFont iconFont;
    iconFont.setPixelSize(StyleHelper::Token::FontInput);
    p.setFont(iconFont);
    p.drawText(QRectF(m_padLeft, m_padTop, 28, m_toolbarH), Qt::AlignCenter, QStringLiteral("⠿"));

    qreal closeX = w - m_padRight - 36;
    QRectF closeBtn(closeX, m_padTop, 36, m_toolbarH);
    p.setPen((m_pressedRow == -3) ? QColor("#e74c3c") : QColor("#999999"));
    QFont closeFont;
    closeFont.setPixelSize(StyleHelper::Token::FontTitle);
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
        dispFont.setPixelSize(StyleHelper::Token::FontHeading);
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
    countFont.setPixelSize(StyleHelper::Token::FontSmall);
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
        keyFont.setFamily(QStringLiteral("Microsoft YaHei"));  // 统一中文系统字体
        keyFont.setPixelSize(StyleHelper::Token::FontHeading);  // 统一字号，消除功能键/数字键视觉不和谐
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
