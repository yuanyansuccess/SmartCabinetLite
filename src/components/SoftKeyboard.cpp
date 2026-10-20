/**
 * @file SoftKeyboard.cpp
 * @brief 触屏软键盘实现：字母/数字/符号输入，将输入结果回填到指定输入框
 * @author 袁燕
 */
#include "SoftKeyboard.h"
#include "utils/StyleHelper.h"
#include "utils/PinyinDict.h"
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

/**
 * @brief 构造函数，初始化 SoftKeyboard 对象并连接信号槽
 * @param parent 父对象指针，作为 Qt 对象树父节点接管所有权
 */
SoftKeyboard::SoftKeyboard(QWidget* parent) : QWidget(parent), m_target(nullptr), m_panel(nullptr) {
}

SoftKeyboard::~SoftKeyboard() {
    // 清理独立顶层窗口m_panel，防止窗口泄漏
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

/**
 * @brief 确保软键盘面板已创建并按目标输入框刷新布局
 * @return 软键盘面板控件
 */
void SoftKeyboard::ensurePanel() {
    if (m_panel) return;
    // 独立顶层窗口(无父widget)，彻底杜绝鼠标事件穿透
    m_panel = new QWidget(nullptr);  // 无父窗口 = 独立顶层窗口
    m_panel->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    // 三重保险确保完全不透明
    m_panel->setAttribute(Qt::WA_TranslucentBackground, false);
    m_panel->setAutoFillBackground(true);
    m_panel->setStyleSheet("QWidget { background-color:#ffffff; border:none; }");
    // 动态宽度：根据屏幕可用宽度自动适配
    int screenW = 460;
    if (QApplication::primaryScreen()) {
        int availW = QApplication::primaryScreen()->availableGeometry().width();
        screenW = qMin(460, (int)(availW * 0.90));
        screenW = qMax(320, screenW);
    }
    m_panel->setFixedWidth(screenW);
    setupUI();               // 内部已按当前模式构建按键区，无需再次重建
    m_panel->updateGeometry();
    m_panel->hide();

    // 创建全屏半透明遮罩窗口，彻底杜绝键盘穿透
    if (!m_overlay) {
        m_overlay = new QWidget(nullptr);
        m_overlay->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        m_overlay->setAttribute(Qt::WA_TranslucentBackground, true);
        m_overlay->setAttribute(Qt::WA_ShowWithoutActivating, false);
        m_overlay->setAutoFillBackground(false);
        m_overlay->setStyleSheet("background:rgba(0,0,0,0.65);");  // 0.65 彻底杜绝底层按钮穿透
        m_overlay->hide();
        m_overlay->installEventFilter(this);
    }
}

/**
 * @brief 构建软键盘界面：字母区、数字区与功能键
 */
void SoftKeyboard::setupUI() {
    m_mainLayout = new QVBoxLayout(m_panel);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // ═══════ 顶部工具栏 ═══════
    QWidget* toolbar = new QWidget();
    toolbar->setAutoFillBackground(true);
    toolbar->setFixedHeight(StyleHelper::Token::ControlHeight);
    toolbar->setStyleSheet(
        "background:#f8f9fb; "
        "border-bottom:1px solid #e8e8e8; "
        "border-radius:16px 16px 0 0;");
    QHBoxLayout* tbLayout = new QHBoxLayout(toolbar);
    tbLayout->setContentsMargins(16, 0, 12, 0);

    QLabel* dragIcon = new QLabel(QStringLiteral("⠿"));
    dragIcon->setStyleSheet(StyleHelper::fontSize(StyleHelper::Token::FontInput) + " color:#bbbbbb; background:transparent;");
    tbLayout->addWidget(dragIcon);

    QLabel* title = new QLabel(QStringLiteral("安全软键盘"));
    title->setStyleSheet(StyleHelper::fontSize(StyleHelper::Token::FontSmall) + " font-weight:600; color:#888888; background:transparent;");
    tbLayout->addWidget(title);
    tbLayout->addStretch();

    // 关闭按钮
    QPushButton* closeBtn = new QPushButton(QStringLiteral("✕"));
    closeBtn->setFixedSize(34, 34);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QPushButton { border:none; border-radius:8px; " + StyleHelper::fontSize(StyleHelper::Token::FontTitle) + " font-weight:700; "
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
        StyleHelper::fontSize(StyleHelper::Token::FontTitle) + " font-weight:600; letter-spacing:2px; "
        "color:#333333; background:transparent;");
    dispLayout->addWidget(m_displayLabel, 1);

    m_displayCount = new QLabel();
    m_displayCount->setStyleSheet(
        StyleHelper::fontSize(StyleHelper::Token::FontSmall) + " color:#999999; font-weight:500; background:transparent;");
    m_displayCount->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    dispLayout->addWidget(m_displayCount);
    m_mainLayout->addWidget(m_displayArea);

    // ═══════ 中文候选条（仅中文模式显示，同款灰底圆角对齐NumKeypad） ═══════
    m_candBar = new QWidget();
    m_candBar->setAutoFillBackground(true);
    m_candBar->setFixedHeight(44);
    m_candBar->setStyleSheet("background:#f5f6f8; border-bottom:1px solid #e8e8e8;");
    QHBoxLayout* candLayout = new QHBoxLayout(m_candBar);
    candLayout->setContentsMargins(12, 4, 12, 4);
    candLayout->setSpacing(6);
    m_pinyinLabel = new QLabel();
    m_pinyinLabel->setStyleSheet(StyleHelper::fontSize(StyleHelper::Token::FontBody)
        + " color:#888888; background:transparent;");
    candLayout->addWidget(m_pinyinLabel);
    candLayout->addStretch();

    // 翻页按钮：候选超过一屏时翻页（上一页 / 下一页）
    m_prevPageBtn = new QPushButton(QStringLiteral("上页"));
    m_nextPageBtn = new QPushButton(QStringLiteral("下页"));
    QString pageBtnStyle =
        "QPushButton { background:#ffffff; color:#4da3ff; border:1px solid #d0d5dd; "
        "min-height:34px; max-height:34px; "  // 覆盖全局按钮最小高度，保证与候选条同高
        "border-radius:10px; " + StyleHelper::fontSize(StyleHelper::Token::FontBody) +
        " font-weight:600; font-family:\"Microsoft YaHei\"; }"
        "QPushButton:hover { background:#f0f4ff; border-color:#4da3ff; }"
        "QPushButton:pressed { background:#dce8ff; }"
        "QPushButton:disabled { background:#f0f0f0; color:#bbbbbb; border-color:#e0e0e0; }";
    m_prevPageBtn->setFixedSize(40, 36);
    m_nextPageBtn->setFixedSize(40, 36);
    m_prevPageBtn->setCursor(Qt::PointingHandCursor);
    m_nextPageBtn->setCursor(Qt::PointingHandCursor);
    m_prevPageBtn->setStyleSheet(pageBtnStyle);
    m_nextPageBtn->setStyleSheet(pageBtnStyle);
    m_prevPageBtn->setVisible(false);
    m_nextPageBtn->setVisible(false);
    connect(m_prevPageBtn, &QPushButton::clicked, this, &SoftKeyboard::onPrevPage);
    connect(m_nextPageBtn, &QPushButton::clicked, this, &SoftKeyboard::onNextPage);

    for (int i = 0; i < 9; ++i) {
        QPushButton* b = new QPushButton();
        b->setVisible(false);
        b->setMinimumSize(28, 36);
        b->setFixedHeight(36);      // 与候选条内容高度一致，避免全局按钮最小高度把候选撑出条外
        b->setMaximumWidth(32);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(
            "QPushButton { background:#ffffff; color:#1a1a2e; border:1px solid #d0d5dd; "
            "min-height:34px; max-height:34px; "  // 覆盖全局按钮最小高度，保证与候选条同高
            "border-radius:10px; " + StyleHelper::fontSize(StyleHelper::Token::FontHeading) +
            " font-weight:600; font-family:\"Microsoft YaHei\"; }"
            "QPushButton:hover { background:#f0f4ff; border-color:#4da3ff; }"
            "QPushButton:pressed { background:#dce8ff; }");
        connect(b, &QPushButton::clicked, this, [this, i]() { commitCandidate(i); });
        m_candBtns.append(b);
        candLayout->addWidget(b);
    }
    candLayout->addStretch();
    candLayout->addWidget(m_prevPageBtn);
    candLayout->addWidget(m_nextPageBtn);
    m_mainLayout->addWidget(m_candBar);

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
    m_confirmBtn->setMinimumHeight(StyleHelper::Token::ControlHeightTouch);
    m_confirmBtn->setCursor(Qt::PointingHandCursor);
    m_confirmBtn->setStyleSheet(
        "QPushButton { background:#4da3ff; color:#ffffff; border:none; "
        "border-radius:12px; font-size:17px; font-weight:700; "
        "letter-spacing:4px; }"
        "QPushButton:hover { background:#3d8ae0; }"
        "QPushButton:pressed { background:#2d7ad0;  }"
        "QPushButton:disabled { background:#c0c0c0; color:#e0e0e0; }");
    connect(m_confirmBtn, &QPushButton::clicked, this, [this]() {
        // 拼音还没选字就点确认时，先按首候选上屏，避免"输了拼音、搜索关键字仍为空"
        if (m_keyMode == ModeCn && m_cn.totalCount() > 0) {
            commitCandidate(0);
        }
        emit confirmed();
        emit enterPressed();
    });
    footerLayout->addWidget(m_confirmBtn);
    m_mainLayout->addWidget(m_footerArea);
    // 默认隐藏底部确认区，仅setConfirmText()调用后显示
    m_footerArea->setVisible(false);

    rebuildKeys();
}

/**
 * @brief 递归清空布局及其嵌套子布局中的控件
 * @param lay 待清理的布局
 * @return 无
 */
static void clearLayoutDeep(QLayout* lay) {
    while (QLayoutItem* item = lay->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            delete w;
        } else if (QLayout* child = item->layout()) {
            clearLayoutDeep(child);  // 行布局内还有按键按钮，必须深入清理
        }
        delete item;
    }
}

/// 彻底重写键盘布局
/// 反馈：底部_下划线按钮看起来像"——"分隔线，用户无法识别为可点击按钮
/// 根治方案：完全去掉_和⎵等视觉有歧义的符号按钮，重新设计5行清晰布局
/// 新布局：
/// 行0: 1 2 3 4 5 6 7 8 9 0 (数字行)
/// 行1: Q W E R T Y U I O P (字母行)
/// 行2: A S D F G H J K L (字母行)
/// 行3: Shift Z X C V B N M 退格 (功能+字母)
/// 行4: , 空格 . / @ - (符号行，所有按钮统一大字体清晰可见)
/// 设计原则：每个按钮56px高、24px字体、统一圆角、颜色醒目、触屏友好
void SoftKeyboard::rebuildKeys() {
    if (!m_keysArea) return;

    // 移除旧按键：按键放在行布局中，只删外层布局会让按钮残留在按键区并叠加显示
    if (QLayout* oldLayout = m_keysArea->layout()) {
        clearLayoutDeep(oldLayout);
        delete oldLayout;
    }

    QVBoxLayout* keysLayout = new QVBoxLayout(m_keysArea);
    keysLayout->setContentsMargins(10, 10, 10, 6);
    keysLayout->setSpacing(8);

    // 新键盘布局定义
    // 每个键: {显示文本, 输入字符, 是否功能键, 列拉伸倍数}
    struct KeyDef { QString label; QString input; bool isFn; int span; };
    struct RowDef { QList<KeyDef> keys; };

    // 行4 两种模式定义（符号行 / 中文空格+符号行），须在 rows[] 之前声明
    RowDef symbolRowEn = {{{",",","},{".",".",false,2},{"/","/",false,2},{"@","@",false,2},{"-","-",false,2}}};
    RowDef symbolRowCn = {{{"空格"," ",true,0},{",",","},{".",".",false,2},{"/","/",false,2},{"@","@",false,2},{"-","-",false,2}}};
    RowDef rows[] = {
        // 行0: 数字
        {{{"1","1"},{"2","2"},{"3","3"},{"4","4"},{"5","5"},{"6","6"},{"7","7"},{"8","8"},{"9","9"},{"0","0"}}},
        // 行1: Q-P
        {{{"Q","Q"},{"W","W"},{"E","E"},{"R","R"},{"T","T"},{"Y","Y"},{"U","U"},{"I","I"},{"O","O"},{"P","P"}}},
        // 行2: A-L
        {{{"A","A"},{"S","S"},{"D","D"},{"F","F"},{"G","G"},{"H","H"},{"J","J"},{"K","K"},{"L","L"}}},
        // 行3: Shift + Z-M + 退格
        {{{"Shift","",true,0},{"Z","Z"},{"X","X"},{"C","C"},{"V","V"},{"B","B"},{"N","N"},{"M","M"},{"退格","",true,0}}},
        // 行4: 英文模式为符号行；中文模式首键改为"空格"(选首候选)，其余符号保留
        (m_keyMode == ModeCn ? symbolRowCn : symbolRowEn),
    };

    // ── 创建单个按键按钮 ──
    auto createKeyBtn = [this](const KeyDef& kd, int span) -> QPushButton* {
        QPushButton* btn = new QPushButton(kd.label);
        btn->setFixedHeight(StyleHelper::Token::ControlHeightLarge);  // 统一56px触屏友好高度
        btn->setMinimumWidth(36);
        btn->setCursor(Qt::PointingHandCursor);

        // 字体统一：全部20px + Microsoft YaHei，消除视觉不和谐
        QString style;
        if (kd.isFn) {
            // 功能键：灰底深灰字
            style = QString(
                "QPushButton { "
                "  min-height:56px; border:1px solid #d0d5dd; border-radius:12px; "
                "  background:#e8ecf0; " + StyleHelper::fontSize(StyleHelper::Token::FontHeading) + " font-weight:600; "
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
                "  background:#ffffff; " + StyleHelper::fontSize(StyleHelper::Token::FontHeading) + " font-weight:600; "
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
                if (m_keyMode == ModeCn) backspaceCn(); else doBackspace();
                emit backspacePressed();
            } else if (kd.label == "Shift") {
                // 暂不切换大小写
            } else if (kd.label == "空格") {
                commitCandidate(0);  // 选首候选上屏
            } else if (m_keyMode == ModeCn) {
                QChar c = kd.input.isEmpty() ? QChar() : kd.input.at(0);
                if (c.isDigit()) {
                    int digit = kd.input.toInt();
                    if (m_cn.totalCount() > 0) {
                        // 有拼音候选时：0 键翻页，1-9 键选当前页对应候选
                        if (digit == 0) onNextPage();
                        else commitCandidate(digit - 1);
                    } else {
                        // 无拼音候选时：数字直接上屏，保证同一键盘能输工号/数量
                        appendChar(kd.input);
                        emit keyPressed(kd.input);
                    }
                } else if (c.isLetter()) {
                    appendPinyin(kd.input);    // 字母 -> 拼拼音
                    emit keyPressed(kd.input);
                } else {
                    appendChar(kd.input);      // 符号直接上屏
                    emit keyPressed(kd.input);
                }
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
    updateCandidates();
}

/**
 * @brief 追加字符
 * @param ch 本次输入的单个字符
 */
void SoftKeyboard::appendChar(const QString& ch) {
    if (!m_target) return;
    QString current = m_target->text();
    m_target->setText(current + ch);
    updateDisplay();
}

/// 退格
void SoftKeyboard::doBackspace() {
    if (!m_target) return;
    QString current = m_target->text();
    if (!current.isEmpty()) {
        m_target->setText(current.left(current.length() - 1));
        updateDisplay();
    }
}

// ═══════════ 中文拼音输入 ═══════════

/**
 * @brief 追加一个拼音字母（交给引擎维护缓冲与页码）
 * @param ch 本次输入的单个字符
 */
void SoftKeyboard::appendPinyin(const QString& ch) {
    m_cn.inputLetter(ch.isEmpty() ? QChar() : ch.at(0));
    updateCandidates();
}

/// 刷新候选条：按模式显隐；走引擎取当前页候选（含单字+词组联想），多页时显示翻页按钮
void SoftKeyboard::updateCandidates() {
    if (!m_candBar || m_candBtns.isEmpty()) return;
    bool cn = (m_keyMode == ModeCn);
    m_candBar->setVisible(cn);
    if (!cn) return;

    QString py = m_cn.pinyin();
    int pages = m_cn.pageCount(9);
    if (py.isEmpty()) {
        m_pinyinLabel->setText(QStringLiteral("拼音输入 · 字母拼写，数字直接输入"));
    } else {
        // 多页时仅保留简短提示，翻页由上下页按钮承担，避免标签挤压候选按钮
        QString hint = (pages > 1) ? QStringLiteral(" · 1-9选字")
                                   : QStringLiteral(" · 1-9/空格选字");
        m_pinyinLabel->setText(py + hint);
    }

    QStringList cands = m_cn.candidatesForPage(9);
    for (int i = 0; i < m_candBtns.size(); ++i) {
        QPushButton* b = m_candBtns[i];
        if (i < cands.size()) {
            b->setVisible(true);
            b->setText(cands[i]);
        } else {
            b->setVisible(false);
        }
    }

    // 仅当候选超过一屏（多页）时才显示翻页按钮，并按边界禁用
    bool multi = (pages > 1);
    m_prevPageBtn->setVisible(multi);
    m_nextPageBtn->setVisible(multi);
    m_prevPageBtn->setEnabled(m_cn.currentPage() > 0);
    m_nextPageBtn->setEnabled(m_cn.currentPage() < pages - 1);
}

/**
 * @brief 上屏当前页第 idx 个候选（0基，可为单字或词组），成功后清空拼音缓冲
 * @param idx 索引（从 0 开始）
 */
void SoftKeyboard::commitCandidate(int idx) {
    if (!m_target) return;
    QString text = m_cn.commit(idx, 9);
    if (text.isEmpty()) return;
    m_target->setText(m_target->text() + text);
    m_cn.clear();
    updateCandidates();
    updateDisplay();
}

/// 中文退格：先删拼音缓冲中的字母（引擎内），缓冲空时再删已上屏的最后一个字
void SoftKeyboard::backspaceCn() {
    if (!m_target) return;
    if (m_cn.backspace()) {
        updateCandidates();
    } else {
        QString t = m_target->text();
        if (!t.isEmpty()) m_target->setText(t.left(t.length() - 1));
        updateDisplay();
    }
}

/// 翻到上一页候选（多页时有效）
void SoftKeyboard::onPrevPage() {
    if (m_cn.pageUp()) updateCandidates();
}

/// 翻到下一页候选（多页时有效）
void SoftKeyboard::onNextPage() {
    if (m_cn.pageDown()) updateCandidates();
}

/**
 * @brief 姓名输入模式：开启后把常见姓氏候选前置
 * @param on 是否启用，true 开启 false 关闭
 */
void SoftKeyboard::setSurnameMode(bool on) {
    m_cn.setSurnameMode(on);
}

/// 更新输入显示区（纯文本模式，无密码圆点逻辑）
void SoftKeyboard::updateDisplay() {
    if (!m_target || !m_displayLabel || !m_displayCount) return;
    QString text = m_target->text();

    m_displayLabel->setText(text.isEmpty() ? "" : text);
    m_displayLabel->setStyleSheet(
        StyleHelper::fontSize(StyleHelper::Token::FontTitle) + " font-weight:600; letter-spacing:2px; "
        "color:#333333; background:transparent;");
    m_displayCount->setText(QString("%1").arg(text.length()));
}

// ═══════════ 公开API ═══════════

/**
 * @brief SoftKeyboard::attach 的业务实现
 * @param target 要绑定的目标对象（如输入控件）
 */
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

/**
 * @brief
 * @param target 要绑定的目标对象（如输入控件）
 * @param pos 位置
 */
void SoftKeyboard::show(QLineEdit* target, const QPoint& pos) {
    ensurePanel();
    m_panel->setAttribute(Qt::WA_TranslucentBackground, false);
    m_panel->setAutoFillBackground(true);
    this->setVisible(false);
    m_target = target;
    m_cn.clear();
    updateDisplay();
    updateCandidates();

    if (m_overlay) {
        QScreen* screen = QApplication::primaryScreen();
        if (screen) {
            QRect geo = screen->geometry();
            m_overlay->setGeometry(geo);
        }
        m_overlay->setWindowOpacity(1.0);  // 确保遮罩完全不透明渲染
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
    m_panel->setWindowOpacity(1.0);  // 确保面板完全不透明
    m_panel->show();
    m_panel->raise();
    m_panel->activateWindow();
    m_panel->adjustSize();
    m_panel->setFixedWidth(screenW);
    // 边界检查：中文模式多了候选条、面板更高，防止超出屏幕底部/右侧被裁切
    QScreen* scr = QApplication::primaryScreen();
    QPoint p = pos;
    if (scr) {
        QRect sg = scr->availableGeometry();
        if (p.y() + m_panel->height() > sg.bottom())
            p.setY(qMax(sg.top(), sg.bottom() - m_panel->height()));
        if (p.x() + m_panel->width() > sg.right())
            p.setX(qMax(sg.left(), sg.right() - m_panel->width()));
        if (p.x() < sg.left()) p.setX(sg.left());
    }
    m_panel->move(p);
    m_panel->raise();
}

/**
 * @brief 显示软键盘面板
 */
void SoftKeyboard::show() {
    if (!m_target) return;
    ensurePanel();
    m_panel->setAttribute(Qt::WA_TranslucentBackground, false);
    m_panel->setAutoFillBackground(true);
    this->setVisible(false);
    m_cn.clear();
    updateDisplay();
    updateCandidates();

    if (m_overlay) {
        QScreen* screen = QApplication::primaryScreen();
        if (screen) {
            QRect geo = screen->geometry();
            m_overlay->setGeometry(geo);
        }
        m_overlay->setWindowOpacity(1.0);  // 确保遮罩完全不透明渲染
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
    m_panel->setWindowOpacity(1.0);  // 确保面板完全不透明
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

/**
 * @brief 隐藏软键盘面板
 */
void SoftKeyboard::hide() {
    // 遮罩与面板必须销毁释放窗口句柄：仅 close() 会让旧面板留在内存中，
    // 下次 show() 重建时会再追加一套候选按钮，导致刷新落在已关闭的旧按钮上、新面板候选始终空白
    if (m_overlay) {
        m_overlay->hide();
        m_overlay->deleteLater();  // 延迟销毁：避免在按钮点击槽内直接销毁窗口对象
        m_overlay = nullptr;
    }
    if (m_panel) {
        m_panel->hide();
        m_panel->deleteLater();
        m_panel = nullptr;
    }

    // 子控件随面板销毁，成员指针同步置空，防止后续刷新访问已销毁对象
    m_mainLayout = nullptr;
    m_displayArea = nullptr;
    m_displayLabel = nullptr;
    m_displayCount = nullptr;
    m_keysArea = nullptr;
    m_footerArea = nullptr;
    m_confirmBtn = nullptr;
    m_candBar = nullptr;
    m_pinyinLabel = nullptr;
    m_candBtns.clear();
    m_prevPageBtn = nullptr;
    m_nextPageBtn = nullptr;
}

/**
 * @brief
 * @param mode 模式标识
 */
void SoftKeyboard::setMode(KeyMode mode) {
    m_keyMode = mode;
    m_cn.clear();  // 切换模式时重置拼音缓冲
    if (m_panel) {
        rebuildKeys();
    }
}

/**
 * @brief
 * @param text 文本字符串
 */
void SoftKeyboard::setConfirmText(const QString& text) {
    m_confirmText = text;
    if (m_confirmBtn) {
        m_confirmBtn->setText(text);
        // 设置确认文字时自动显示底部确认区
        if (m_footerArea) m_footerArea->setVisible(true);
    }
}

/**
         * @brief 读取当前输入内容
         * @return 输入文本
         */
QString SoftKeyboard::currentText() const {
    return m_target ? m_target->text() : QString();
}

/**
 * @brief 遮罩点击事件拦截 — 点击遮罩区域关闭键盘
 * @param obj 被监视的对象
 * @param event 事件对象，由 Qt 事件系统传入
 * @return 操作是否成功（true=成功，false=失败）
 */
bool SoftKeyboard::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_overlay && event->type() == QEvent::MouseButtonPress) {
        emit cancelled();
        emit inputCancelled();
        hide();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}
