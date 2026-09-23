// 作者：袁燕  智能柜Qt Widget 2.0  字母/符号软键盘实现
// 日期：2026-06-21  1:1复刻Web端SoftKeyboard.vue
// [2026-06-26v5] 清理冗余：移除所有数字/密码模式逻辑(NumKeypad独立接管)
// [2026-06-26v8] 彻底重写rebuildKeys()：5行清晰布局，去掉_/⎵等歧义按钮
//   新布局：行0数字 / 行1 Q-P / 行2 A-L / 行3 Shift Z-M 退格 / 行4 符号+空格
//   每个按钮统一56px高度触屏优化，字体24px醒目清晰，无"线条状"按钮
#include "SoftKeyboard.h"
#include <QHBoxLayout>
#include <QGridLayout>
#include <QApplication>
#include <QScreen>
#include <QRect>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QPropertyAnimation>
#include <QDebug>

SoftKeyboard::SoftKeyboard(QWidget* parent) : QWidget(parent), m_target(nullptr), m_panel(nullptr) {
}

SoftKeyboard::~SoftKeyboard() {
    // [v4.4新增] 清理独立顶层窗口m_panel，防止窗口泄漏
    if (m_overlay) {
        m_overlay->hide();
        m_overlay->close();
        delete m_overlay;
        m_overlay = nullptr;
    }
    if (m_panel) {
        m_panel->hide();
        m_panel->close();
        delete m_panel;
        m_panel = nullptr;
    }
}

void SoftKeyboard::ensurePanel() {
    if (m_panel) return;
    // [v4.4致命修复] 独立顶层窗口(无父widget)，彻底杜绝鼠标事件穿透
    m_panel = new QWidget(nullptr);  // 无父窗口 = 独立顶层窗口
    m_panel->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    // [v6.6] 三重保险确保完全不透明
    m_panel->setAttribute(Qt::WA_TranslucentBackground, false);
    m_panel->setAutoFillBackground(true);
    m_panel->setStyleSheet("QWidget { background-color:#ffffff; border:none; }");
    // [v5.1修复] 动态宽度：根据屏幕可用宽度自动适配
    int screenW = 460;
    if (QApplication::primaryScreen()) {
        int availW = QApplication::primaryScreen()->availableGeometry().width();
        screenW = qMin(460, (int)(availW * 0.90));
        screenW = qMax(320, screenW);
    }
    m_panel->setFixedWidth(screenW);
    setupUI();
    if (m_pendingRebuild) {
        m_pendingRebuild = false;
        rebuildKeys();
    }
    m_panel->updateGeometry();
    m_panel->hide();

    // [v6.6致命修复] 创建全屏半透明遮罩窗口，彻底杜绝键盘穿透
    if (!m_overlay) {
        m_overlay = new QWidget(nullptr);
        m_overlay->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        m_overlay->setAttribute(Qt::WA_TranslucentBackground, true);
        m_overlay->setAttribute(Qt::WA_ShowWithoutActivating, false);
        m_overlay->setAutoFillBackground(false);
        m_overlay->setStyleSheet("background:rgba(0,0,0,0.65);");  // [2026-06-27] 0.45→0.65 彻底杜绝底层按钮穿透
        m_overlay->hide();
        m_overlay->installEventFilter(this);
    }
}

void SoftKeyboard::setupUI() {
    m_mainLayout = new QVBoxLayout(m_panel);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // ═══════ 顶部工具栏 ═══════
    QWidget* toolbar = new QWidget();
    toolbar->setAutoFillBackground(true);
    toolbar->setFixedHeight(44);
    toolbar->setStyleSheet(
        "background:#f8f9fb; "
        "border-bottom:1px solid #e8e8e8; "
        "border-radius:16px 16px 0 0;");
    QHBoxLayout* tbLayout = new QHBoxLayout(toolbar);
    tbLayout->setContentsMargins(16, 0, 12, 0);

    QLabel* dragIcon = new QLabel(QStringLiteral("⠿"));
    dragIcon->setStyleSheet("font-size:16px; color:#bbbbbb; background:transparent;");
    tbLayout->addWidget(dragIcon);

    QLabel* title = new QLabel(QStringLiteral("安全软键盘"));
    title->setStyleSheet("font-size:13px; font-weight:600; color:#888888; background:transparent;");
    tbLayout->addWidget(title);
    tbLayout->addStretch();

    // [v5] 关闭按钮
    QPushButton* closeBtn = new QPushButton(QStringLiteral("✕"));
    closeBtn->setFixedSize(34, 34);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QPushButton { border:none; border-radius:8px; font-size:18px; font-weight:700; "
        "color:#999999; background:transparent; }"
        "QPushButton:hover { background:#ffebee; color:#e74c3c; }"
        "QPushButton:pressed { transform:scale(0.9); }");
    connect(closeBtn, &QPushButton::clicked, this, [this]() {
        emit cancelled();
        emit inputCancelled();
        hide();
    });
    tbLayout->addWidget(closeBtn);
    m_mainLayout->addWidget(toolbar);

    // ═══════ 输入显示区 ═══════
    m_displayArea = new QWidget();
    m_displayArea->setAutoFillBackground(true);
    m_displayArea->setFixedHeight(52);
    m_displayArea->setStyleSheet("background:#f5f6f8; border-bottom:1px solid #e8e8e8;");
    QHBoxLayout* dispLayout = new QHBoxLayout(m_displayArea);
    dispLayout->setContentsMargins(20, 0, 20, 0);

    m_displayLabel = new QLabel();
    m_displayLabel->setStyleSheet(
        "font-size:18px; font-weight:600; letter-spacing:2px; "
        "color:#333333; background:transparent;");
    dispLayout->addWidget(m_displayLabel, 1);

    m_displayCount = new QLabel();
    m_displayCount->setStyleSheet(
        "font-size:13px; color:#999999; font-weight:500; background:transparent;");
    m_displayCount->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    dispLayout->addWidget(m_displayCount);
    m_mainLayout->addWidget(m_displayArea);

    // ═══════ 按键区域容器 ═══════
    m_keysArea = new QWidget();
    m_keysArea->setAutoFillBackground(true);
    m_keysArea->setStyleSheet("background:#ffffff;");
    m_mainLayout->addWidget(m_keysArea);

    // ═══════ 底部确认区 ═══════
    m_footerArea = new QWidget();
    m_footerArea->setAutoFillBackground(true);
    m_footerArea->setStyleSheet("background:#ffffff; border-top:1px solid #e8e8e8;");
    QHBoxLayout* footerLayout = new QHBoxLayout(m_footerArea);
    footerLayout->setContentsMargins(16, 8, 16, 14);

    m_confirmBtn = new QPushButton(m_confirmText.isEmpty() ? QStringLiteral("确 认") : m_confirmText);
    m_confirmBtn->setMinimumHeight(48);
    m_confirmBtn->setCursor(Qt::PointingHandCursor);
    m_confirmBtn->setStyleSheet(
        "QPushButton { background:#4da3ff; color:#ffffff; border:none; "
        "border-radius:12px; font-size:17px; font-weight:700; "
        "letter-spacing:4px; }"
        "QPushButton:hover { background:#3d8ae0; }"
        "QPushButton:pressed { background:#2d7ad0; transform:scale(0.96); }"
        "QPushButton:disabled { background:#c0c0c0; color:#e0e0e0; }");
    connect(m_confirmBtn, &QPushButton::clicked, this, [this]() {
        emit confirmed();
        emit enterPressed();
    });
    footerLayout->addWidget(m_confirmBtn);
    m_mainLayout->addWidget(m_footerArea);
    // [2026-06-26v9] 默认隐藏底部确认区，仅setConfirmText()调用后显示
    m_footerArea->setVisible(false);

    rebuildKeys();
}

/// [2026-06-26v8] 彻底重写键盘布局
///   反馈：底部_下划线按钮看起来像"——"分隔线，用户无法识别为可点击按钮
///   根治方案：完全去掉_和⎵等视觉有歧义的符号按钮，重新设计5行清晰布局
///   新布局：
///     行0: 1 2 3 4 5 6 7 8 9 0          (数字行)
///     行1: Q W E R T Y U I O P            (字母行)
///     行2: A S D F G H J K L              (字母行)
///     行3: Shift Z X C V B N M 退格        (功能+字母)
///     行4: , 空格 . / @ -                 (符号行，所有按钮统一大字体清晰可见)
///   设计原则：每个按钮56px高、24px字体、统一圆角、颜色醒目、触屏友好
void SoftKeyboard::rebuildKeys() {
    if (!m_keysArea) return;

    // 移除旧按键
    QLayoutItem* item;
    QLayout* oldLayout = m_keysArea->layout();
    if (oldLayout) {
        while ((item = oldLayout->takeAt(0))) {
            if (item->widget()) delete item->widget();
            delete item;
        }
        delete oldLayout;
    }

    QVBoxLayout* keysLayout = new QVBoxLayout(m_keysArea);
    keysLayout->setContentsMargins(10, 10, 10, 6);
    keysLayout->setSpacing(8);

    // [v8] 新键盘布局定义
    //   每个键: {显示文本, 输入字符, 是否功能键, 列拉伸倍数}
    struct KeyDef { QString label; QString input; bool isFn; int span; };
    struct RowDef { QList<KeyDef> keys; };

    RowDef rows[] = {
        // 行0: 数字
        {{{"1","1"},{"2","2"},{"3","3"},{"4","4"},{"5","5"},{"6","6"},{"7","7"},{"8","8"},{"9","9"},{"0","0"}}},
        // 行1: Q-P
        {{{"Q","Q"},{"W","W"},{"E","E"},{"R","R"},{"T","T"},{"Y","Y"},{"U","U"},{"I","I"},{"O","O"},{"P","P"}}},
        // 行2: A-L
        {{{"A","A"},{"S","S"},{"D","D"},{"F","F"},{"G","G"},{"H","H"},{"J","J"},{"K","K"},{"L","L"}}},
        // 行3: Shift + Z-M + 退格
        {{{"Shift","",true,0},{"Z","Z"},{"X","X"},{"C","C"},{"V","V"},{"B","B"},{"N","N"},{"M","M"},{"退格","",true,0}}},
        // 行4: 符号行 — [2026-06-26v9] 去掉空格按钮，符号均分填充
        {{{",",","},{".",".",false,2},{"/","/",false,2},{"@","@",false,2},{"-","-",false,2}}},
    };

    // ── 创建单个按键按钮 ──
    auto createKeyBtn = [this](const KeyDef& kd, int span) -> QPushButton* {
        QPushButton* btn = new QPushButton(kd.label);
        btn->setFixedHeight(56);             // [v8] 统一56px触屏友好高度
        btn->setMinimumWidth(36);
        btn->setCursor(Qt::PointingHandCursor);

        // [2026-06-27] 字体统一：全部20px + Microsoft YaHei，消除视觉不和谐
        QString style;
        if (kd.isFn) {
            // 功能键：灰底深灰字
            style = QString(
                "QPushButton { "
                "  min-height:56px; border:1px solid #d0d5dd; border-radius:12px; "
                "  background:#e8ecf0; font-size:20px; font-weight:600; "
                "  font-family:\"Microsoft YaHei\"; "
                "  color:#5a6270; "
                "}"
                "QPushButton:hover { background:#d0d8e0; }"
                "QPushButton:pressed { background:#bcc4d0; }");
        } else {
            // 普通键：白底深色大字
            style = QString(
                "QPushButton { "
                "  min-height:56px; border:1px solid #c8cdd5; border-radius:12px; "
                "  background:#ffffff; font-size:20px; font-weight:600; "
                "  font-family:\"Microsoft YaHei\"; "
                "  color:#1a1a2e; "
                "}"
                "QPushButton:hover { background:#f0f4ff; border-color:#4da3ff; }"
                "QPushButton:pressed { background:#dce8ff; border-color:#3d8ae0; }");
        }
        btn->setStyleSheet(style);

        // 点击逻辑
        connect(btn, &QPushButton::clicked, this, [this, kd]() {
            if (kd.label == "退格") {
                doBackspace();
                emit backspacePressed();
            } else if (kd.label == "Shift") {
                // [v8] Shift暂保持大写模式（后续可扩展大小写切换）
            } else {
                appendChar(kd.input);
                emit keyPressed(kd.input);
            }
        });
        return btn;
    };

    // ── 构建键盘行 ──
    for (const auto& row : rows) {
        QHBoxLayout* rowLayout = new QHBoxLayout();
        rowLayout->setSpacing(8);
        for (const auto& kd : row.keys) {
            int span = (kd.span > 0) ? kd.span : 1;
            QPushButton* btn = createKeyBtn(kd, span);
            rowLayout->addWidget(btn, span);
        }
        keysLayout->addLayout(rowLayout);
    }
    updateDisplay();
}

/// [2026-06-21] 追加字符
void SoftKeyboard::appendChar(const QString& ch) {
    if (!m_target) return;
    QString current = m_target->text();
    m_target->setText(current + ch);
    updateDisplay();
}

/// [2026-06-21] 退格
void SoftKeyboard::doBackspace() {
    if (!m_target) return;
    QString current = m_target->text();
    if (!current.isEmpty()) {
        m_target->setText(current.left(current.length() - 1));
        updateDisplay();
    }
}

/// [2026-06-21] 更新输入显示区（纯文本模式，无密码圆点逻辑）
void SoftKeyboard::updateDisplay() {
    if (!m_target || !m_displayLabel || !m_displayCount) return;
    QString text = m_target->text();

    m_displayLabel->setText(text.isEmpty() ? "" : text);
    m_displayLabel->setStyleSheet(
        "font-size:18px; font-weight:600; letter-spacing:2px; "
        "color:#333333; background:transparent;");
    m_displayCount->setText(QString("%1").arg(text.length()));
}

// ═══════════ 公开API ═══════════

void SoftKeyboard::attach(QLineEdit* target) {
    if (m_target) disconnect(m_target, nullptr, this, nullptr);
    m_target = target;
    if (m_target) {
        connect(m_target, &QLineEdit::selectionChanged, this, [this]() {
            ensurePanel();
            show(m_target, m_target->mapToGlobal(QPoint(0, m_target->height()+4)));
        });
    }
}

void SoftKeyboard::show(QLineEdit* target, const QPoint& pos) {
    ensurePanel();
    m_panel->setAttribute(Qt::WA_TranslucentBackground, false);
    m_panel->setAutoFillBackground(true);
    this->setVisible(false);
    m_target = target;
    updateDisplay();

    if (m_overlay) {
        QScreen* screen = QApplication::primaryScreen();
        if (screen) {
            QRect geo = screen->geometry();
            m_overlay->setGeometry(geo);
        }
        m_overlay->setWindowOpacity(1.0);  // [2026-06-27] 确保遮罩完全不透明渲染
        m_overlay->show();
        m_overlay->raise();
    }

    int screenW = 460;
    if (QApplication::primaryScreen()) {
        int availW = QApplication::primaryScreen()->availableGeometry().width();
        screenW = qMin(460, (int)(availW * 0.90));
        screenW = qMax(320, screenW);
    }
    m_panel->setFixedWidth(screenW);
    m_panel->setWindowOpacity(1.0);  // [2026-06-27] 确保面板完全不透明
    m_panel->show();
    m_panel->raise();
    m_panel->activateWindow();
    m_panel->adjustSize();
    m_panel->setFixedWidth(screenW);
    m_panel->move(pos);
    m_panel->raise();
}

void SoftKeyboard::show() {
    if (!m_target) return;
    ensurePanel();
    m_panel->setAttribute(Qt::WA_TranslucentBackground, false);
    m_panel->setAutoFillBackground(true);
    this->setVisible(false);
    updateDisplay();

    if (m_overlay) {
        QScreen* screen = QApplication::primaryScreen();
        if (screen) {
            QRect geo = screen->geometry();
            m_overlay->setGeometry(geo);
        }
        m_overlay->setWindowOpacity(1.0);  // [2026-06-27] 确保遮罩完全不透明渲染
        m_overlay->show();
        m_overlay->raise();
    }

    int screenW = 460;
    if (QApplication::primaryScreen()) {
        int availW = QApplication::primaryScreen()->availableGeometry().width();
        screenW = qMin(460, (int)(availW * 0.90));
        screenW = qMax(320, screenW);
    }
    m_panel->setFixedWidth(screenW);
    m_panel->setWindowOpacity(1.0);  // [2026-06-27] 确保面板完全不透明
    m_panel->show();
    m_panel->raise();
    m_panel->activateWindow();
    m_panel->adjustSize();
    m_panel->setFixedWidth(screenW);
    QPoint pos = m_target->mapToGlobal(QPoint(0, m_target->height() + 4));
    QScreen* screen = QApplication::primaryScreen();
    if (screen) {
        QRect screenGeo = screen->availableGeometry();
        if (pos.y() + m_panel->height() > screenGeo.bottom())
            pos.setY(screenGeo.bottom() - m_panel->height());
        if (pos.x() + m_panel->width() > screenGeo.right())
            pos.setX(screenGeo.right() - m_panel->width());
        if (pos.x() < screenGeo.left()) pos.setX(screenGeo.left());
    }
    m_panel->move(pos);
    m_panel->raise();
}

void SoftKeyboard::hide() {
    // [2026-06-26v6致命修复] 遮罩层必须close()释放窗口句柄，仅hide()会残留拦截鼠标事件
    //   close()后置nullptr，确保下次show()时ensurePanel()会重建
    if (m_overlay) { m_overlay->hide(); m_overlay->close(); m_overlay = nullptr; }
    if (m_panel)   { m_panel->hide();   m_panel->close();   m_panel = nullptr; }
}

void SoftKeyboard::setMode(KeyMode mode) {
    m_keyMode = mode;
    if (m_panel) {
        rebuildKeys();
    } else {
        m_pendingRebuild = true;
    }
}

void SoftKeyboard::setConfirmText(const QString& text) {
    m_confirmText = text;
    if (m_confirmBtn) {
        m_confirmBtn->setText(text);
        // [2026-06-26v9] 设置确认文字时自动显示底部确认区
        if (m_footerArea) m_footerArea->setVisible(true);
    }
}

void SoftKeyboard::setConfirmVisible(bool visible) {
    if (m_footerArea) m_footerArea->setVisible(visible);
}

QString SoftKeyboard::currentText() const {
    return m_target ? m_target->text() : QString();
}

/// [v6.6] 遮罩点击事件拦截 — 点击遮罩区域关闭键盘
bool SoftKeyboard::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_overlay && event->type() == QEvent::MouseButtonPress) {
        emit cancelled();
        emit inputCancelled();
        hide();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

/// [2026-06-21] 错误抖动动画
void SoftKeyboard::triggerShake() {
    if (!m_panel) return;
    QPoint orig = m_panel->pos();
    QPropertyAnimation* anim = new QPropertyAnimation(m_panel, "pos", this);
    anim->setDuration(400);
    anim->setStartValue(orig);
    anim->setKeyValueAt(0.1, orig + QPoint(-6, 0));
    anim->setKeyValueAt(0.2, orig + QPoint(6, 0));
    anim->setKeyValueAt(0.3, orig + QPoint(-6, 0));
    anim->setKeyValueAt(0.4, orig + QPoint(6, 0));
    anim->setKeyValueAt(0.5, orig + QPoint(-6, 0));
    anim->setKeyValueAt(0.6, orig + QPoint(6, 0));
    anim->setKeyValueAt(0.7, orig + QPoint(-4, 0));
    anim->setKeyValueAt(0.8, orig + QPoint(4, 0));
    anim->setKeyValueAt(0.9, orig + QPoint(-2, 0));
    anim->setEndValue(orig);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}
