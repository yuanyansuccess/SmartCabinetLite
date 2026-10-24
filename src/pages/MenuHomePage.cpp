/**
 * @file MenuHomePage.cpp
 * @brief 功能导航主页实现 - 欢迎语+大按钮卡片网格
 * @author 袁燕
 * @说明 触屏标准：卡片尺寸按页面可用宽自适应（保持240:225比例、约束[150,240]），字号26px；
 *   卡片样式与管理页统计卡片同语言（白底圆角+主色hover），保证管理员/普通用户风格统一。
 */
#include "MenuHomePage.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>

#include "utils/StyleHelper.h"
#include "utils/LayoutHelper.h"

/**
 * @brief 构造函数，初始化 MenuHomePage 对象并连接信号槽
 * @param parent 父对象指针，作为 Qt 对象树父节点接管所有权
 */
MenuHomePage::MenuHomePage(QWidget* parent)
    : QWidget(parent)
{
    buildBaseUI();
}

/**
 * @brief 构建页面骨架：欢迎语、副标题、卡片网格容器与告警提示行
 */
void MenuHomePage::buildBaseUI()
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    lay->addStretch(1);

    // 欢迎语（用户姓名运行时填充）
    m_welcomeLabel = new QLabel(QStringLiteral("您好"));
    m_welcomeLabel->setAlignment(Qt::AlignCenter);
    m_welcomeLabel->setStyleSheet(QString(
        "font-size:34px; font-weight:700; color:%1; background:transparent;")
        .arg(StyleHelper::textColor()));
    lay->addWidget(m_welcomeLabel);

    // 副标题
    m_hintLabel = new QLabel(QStringLiteral("请选择您要进行的操作"));
    m_hintLabel->setAlignment(Qt::AlignCenter);
    m_hintLabel->setStyleSheet(QString(
        "" + StyleHelper::fontSize(StyleHelper::Token::FontTitle) + "; color:%1; background:transparent; margin-top:10px;")
        .arg(StyleHelper::textSecondary()));
    lay->addWidget(m_hintLabel);

    lay->addSpacing(32);

    // 卡片网格容器：不参与垂直stretch分配（保持卡片自然高度不被压缩），
    // 两侧stretch包裹使卡片数量少于4张时整组水平居中
    m_cardArea = new QWidget();
    m_cardArea->setStyleSheet("background:transparent;");
    auto* cardWrap = new QHBoxLayout(m_cardArea);
    cardWrap->setContentsMargins(60, 0, 60, 0);
    cardWrap->addStretch(1);
    m_cardGrid = new QGridLayout();
    m_cardGrid->setSpacing(24);
    cardWrap->addLayout(m_cardGrid, 0);
    cardWrap->addStretch(1);
    lay->addWidget(m_cardArea);

    lay->addStretch(1);
}

/**
 * @brief 设置欢迎语中的用户姓名
 * @param name 用户姓名；为空时只显示"您好"
 */
void MenuHomePage::setUserName(const QString& name)
{
    if (name.isEmpty()) {
        m_welcomeLabel->setText(QStringLiteral("您好"));
    } else {
        m_welcomeLabel->setText(QStringLiteral("您好，%1").arg(name));
    }
}

/**
 * @brief 暂存卡片数据并重建网格
 * @param cards 卡片数据列表（由 MainWindow 按登录角色组装）
 */
void MenuHomePage::setCards(const QList<MenuCard>& cards)
{
    m_cards = cards;
    rebuildCards();
}

/**
 * @brief 按 m_cards 数据重建卡片按钮网格：4列布局，超出一行自动换行
 */
void MenuHomePage::rebuildCards()
{
    // 清空旧卡片
    LayoutHelper::clearItemsAndDeleteWidgets(m_cardGrid);

    const int columns = 4;
    const QSize cs = computeCardSize();
    m_cardSize = cs;
    for (int i = 0; i < m_cards.size(); ++i) {
        const MenuCard& card = m_cards.at(i);
        QPushButton* btn = new QPushButton(card.icon + QStringLiteral("\n") + card.label, this);
        btn->setObjectName(card.key);
        btn->setCursor(Qt::PointingHandCursor);
        // 先设样式再固定尺寸：QSS生效时会重算并覆盖先设置的尺寸约束
        btn->setStyleSheet(cardStyle(cs.width(), cs.height()));
        // 卡片尺寸按页面可用宽自适应（保持240:225比例），兼容1024x768与1280x800
        btn->setFixedSize(cs);
        connect(btn, &QPushButton::clicked, this, [this, key = card.key] {
            emit cardClicked(key);
        });
        m_cardGrid->addWidget(btn, i / columns, i % columns);
    }
}

/**
 * @brief
 * @return 计算得到的尺寸
 */
QSize MenuHomePage::computeCardSize() const
{
    constexpr int columns = 4;
    constexpr int sideMargin = 60;
    constexpr int spacing = 24;
    const int available = qMax(0, this->width() - sideMargin * 2 - spacing * (columns - 1));
    const int w = qBound(150, available / columns, 240);
    const int h = qRound(w * 225.0 / 240.0);
    return QSize(w, h);
}

/**
 * @brief
 * @param event 事件对象，由 Qt 事件系统传入
 */
void MenuHomePage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_cards.isEmpty()) return;
    if (computeCardSize() != m_cardSize) {
        rebuildCards();
    }
}

/**
 * @brief 卡片按钮统一样式：白底大圆角，悬停主色描边
 * @param w 卡片宽（来自 computeCardSize，写入QSS尺寸约束）
 * @param h 卡片高
 * @return QSS样式串
 */
QString MenuHomePage::cardStyle(int w, int h)
{
    // 卡片尺寸同时写入QSS约束与setFixedSize：QSS生效时会重置代码设置的
    // minimumSize（实测被覆盖为内容高度导致卡片压扁），样式内约束不受影响
    return QString(
        "QPushButton { background:%1; border:1px solid #e8ecf1; border-radius:%2px;"
        "  font-size:26px; font-weight:700; color:%3;"
        "  min-width:%4px; max-width:%4px; min-height:%5px; max-height:%5px; }"
        "QPushButton:hover { border-color:%6; background:#f8fbff; }"
        "QPushButton:pressed { background:#e8f2ff; }"
    ).arg(StyleHelper::whiteColor())
     .arg(StyleHelper::Token::RadiusLarge)
     .arg(StyleHelper::textColor())
     .arg(w)
     .arg(h)
     .arg(StyleHelper::primaryColor());
}
