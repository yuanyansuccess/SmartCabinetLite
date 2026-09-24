/**
 * @file CabinetSessionDialog.cpp
 * @brief 普通用户智能柜会话对话框实现 - 开柜提示(含任务工具只读面板) + 关柜差异清单（演示版）
 * @author 袁燕
 * @说明 开柜页"我的任务工具"面板读库展示本机组任务类型在库工具（只读）；
 *   关柜差异清单仍为内存模拟数据，产品化时差异来源改为柜体传感器上报。
 */
#include "CabinetSessionDialog.h"
#include "utils/StyleHelper.h"
#include "db/TaskTypeDAO.h"
#include "common/AppConfig.h"
#include "common/PositionFormatter.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>
#include <QFrame>
#include <QEvent>
#include <QMouseEvent>
#include <QEventLoop>

CabinetSessionDialog::CabinetSessionDialog(const QJsonObject& user, QWidget* parent)
    : QDialog(parent), m_user(user)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setModal(true);
    setStyleSheet("QDialog { background: " + StyleHelper::bgColor() + "; }");

    // 模拟数据：8件工具，末一件开柜前已被该用户借走（用于演示"归还"场景）
    //m_tools = {
    // { QStringLiteral("扭力扳手"), QStringLiteral("TL-S03"), QStringLiteral("A-01-02"), true },
    // { QStringLiteral("套筒组"), QStringLiteral("SK-17P"), QStringLiteral("A-01-05"), true },
    // { QStringLiteral("内六角组"), QStringLiteral("HX-09S"), QStringLiteral("A-02-01"), true },
    // { QStringLiteral("游标卡尺"), QStringLiteral("CL-150D"), QStringLiteral("A-02-04"), true },
    // { QStringLiteral("剥线钳"), QStringLiteral("WP-06"), QStringLiteral("A-03-03"), true },
    // { QStringLiteral("热风枪"), QStringLiteral("HG-880"), QStringLiteral("B-01-01"), true },
    // { QStringLiteral("防静电镊"), QStringLiteral("TS-12"), QStringLiteral("B-01-06"), true },
    // { QStringLiteral("万用表"), QStringLiteral("DM-3055"), QStringLiteral("B-02-04"), false },
    //};

    m_tools = {
      { QStringLiteral("扭力扳手"),  QStringLiteral("TL-S03"),   QStringLiteral("A"), true  },
      { QStringLiteral("套筒组"),    QStringLiteral("SK-17P"),   QStringLiteral("A"), true  },
      { QStringLiteral("内六角组"),  QStringLiteral("HX-09S"),   QStringLiteral("A"), true  },
      { QStringLiteral("游标卡尺"),  QStringLiteral("CL-150D"),  QStringLiteral("A"), true  },
      { QStringLiteral("剥线钳"),    QStringLiteral("WP-06"),    QStringLiteral("A"), true  },
      { QStringLiteral("热风枪"),    QStringLiteral("HG-880"),   QStringLiteral("B"), true  },
      { QStringLiteral("防静电镊"),  QStringLiteral("TS-12"),    QStringLiteral("B"), true  },
      { QStringLiteral("万用表"),    QStringLiteral("DM-3055"),  QStringLiteral("B"), false },
    };
    m_openSnapshot.clear();
    for (const MockTool& tool : m_tools)
        m_openSnapshot.append(tool.inCabinet);

    m_stack = new QStackedWidget(this);
    QVBoxLayout* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_stack);

    loadMyTaskTools();  // [2026-09-23] 先读本机组任务类型在库工具（开柜页面板数据）
    buildOpenPage();
    buildResultPage();
}

/**
 * @brief 全屏启动会话并阻塞，直到用户确认关闭智能柜
 */
void CabinetSessionDialog::startSession()
{
    // 全屏显示（8寸屏尺寸小，普通用户会话必须占满整个屏幕）
    showFullScreen();
    raise();
    activateWindow();
    QEventLoop loop;
    connect(this, &QDialog::finished, &loop, &QEventLoop::quit);
    loop.exec();
}

/**
 * @brief 页面0：开柜提示页
 *   [2026-09-23] 改版：新增"我的任务工具"只读面板（方案B：任务类型横排页签+工具卡片三列）
 *   用户可按本机组任务类型查看在库工具及其位置（仅供拿取参考，不可操作），适配8寸屏
 */
void CabinetSessionDialog::buildOpenPage()
{
    QWidget* page = new QWidget();
    QVBoxLayout* lay = new QVBoxLayout(page);
    lay->setContentsMargins(40, 22, 40, 24);
    lay->setSpacing(8);

    // ── 顶部：图标+标题同行（图标连点3次 = 演示面板开关入口，保留）──
    QHBoxLayout* headRow = new QHBoxLayout();
    headRow->setSpacing(14);
    m_iconLabel = new QLabel(QStringLiteral("🔓"));
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setStyleSheet("font-size: 46px; background: transparent;");
    m_iconLabel->installEventFilter(this);
    QLabel* titleLabel = new QLabel(QStringLiteral("智能柜已开启"));
    titleLabel->setStyleSheet(QString("font-size: 30px; font-weight: 700; color: %1; background: transparent;")
                                  .arg(StyleHelper::textColor()));
    headRow->addStretch();
    headRow->addWidget(m_iconLabel);
    headRow->addWidget(titleLabel);
    headRow->addStretch();
    lay->addLayout(headRow);

    QString realName = m_user["realName"].toString();
    if (realName.isEmpty()) realName = m_user["name"].toString();
    QLabel* welcomeLabel = new QLabel(
        QStringLiteral("您好，%1 · 您可以自行借用或归还工具").arg(realName));
    welcomeLabel->setAlignment(Qt::AlignCenter);
    welcomeLabel->setStyleSheet(QString("font-size: 17px; color: %1; background: transparent;")
                                    .arg(StyleHelper::textSecondary()));
    lay->addWidget(welcomeLabel);
    lay->addSpacing(6);

    // ── "我的任务工具"面板标题行 ──
    QHBoxLayout* ptRow = new QHBoxLayout();
    ptRow->setSpacing(10);
    QWidget* bar = new QWidget();
    bar->setFixedSize(5, 22);
    bar->setStyleSheet("background:#4da3ff; border-radius:3px;");
    QLabel* ptLabel = new QLabel(QStringLiteral("我的任务工具"));
    ptLabel->setStyleSheet(QString("font-size: 20px; font-weight: 700; color: %1; background: transparent;")
                               .arg(StyleHelper::textColor()));
    QLabel* ptSub = new QLabel(QStringLiteral("按任务类型查看 · 仅供拿取时参考"));
    ptSub->setStyleSheet("font-size: 14px; color: #8a94a6; background: transparent;");
    m_stockCountLabel = new QLabel(QString());
    m_stockCountLabel->setStyleSheet(
        "font-size: 14px; color: #52c41a; font-weight: 600; background: #f6ffed;"
        "border: 1px solid #b7eb8f; padding: 3px 12px; border-radius: 999px;");
    ptRow->addWidget(bar);
    ptRow->addWidget(ptLabel);
    ptRow->addWidget(ptSub);
    ptRow->addStretch();
    ptRow->addWidget(m_stockCountLabel);
    lay->addLayout(ptRow);

    // ── 任务类型页签行（横排胶囊，超出宽度可横向拖动）──
    QScrollArea* tabScroll = new QScrollArea();
    tabScroll->setFixedHeight(64);
    tabScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tabScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tabScroll->setWidgetResizable(true);
    tabScroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");
    QWidget* tabHost = new QWidget();
    tabHost->setStyleSheet("background: transparent;");
    QHBoxLayout* tabLay = new QHBoxLayout(tabHost);
    tabLay->setContentsMargins(0, 0, 0, 0);
    tabLay->setSpacing(10);
    for (int i = 0; i < m_myTypes.size(); ++i) {
        QPushButton* tb = new QPushButton(
            QStringLiteral("%1  %2").arg(m_myTypes.at(i).typeName).arg(m_myTypes.at(i).tools.size()));
        tb->setCheckable(true);
        tb->setChecked(i == 0);
        tb->setCursor(Qt::PointingHandCursor);
        tb->setStyleSheet(
            "QPushButton { background: #ffffff; border: 2px solid #e6e9f0; border-radius: 999px;"
            "  min-height: 50px; padding: 0 24px; font-size: 17px; color: #4a5568; }"
            "QPushButton:checked { background: #4da3ff; border-color: #4da3ff;"
            "  color: white; font-weight: 700; }");
        connect(tb, &QPushButton::clicked, this, [this, i]() { selectTypeTab(i); });
        tabLay->addWidget(tb);
        m_typeTabBtns.append(tb);
    }
    tabLay->addStretch();
    tabScroll->setWidget(tabHost);
    QScroller::grabGesture(tabScroll->viewport(), QScroller::LeftMouseButtonGesture);
    lay->addWidget(tabScroll);

    // ── 工具卡片滚动区（三列网格，垂直滚动，触屏拖动）──
    m_toolsScroll = new QScrollArea();
    m_toolsScroll->setWidgetResizable(true);
    m_toolsScroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");
    QScroller::grabGesture(m_toolsScroll->viewport(), QScroller::LeftMouseButtonGesture);
    lay->addWidget(m_toolsScroll, 1);
    rebuildToolCards();

    // ── 底部：关闭智能柜按钮（原有流程不变）──
    QPushButton* closeBtn = new QPushButton(QStringLiteral("关 闭 智 能 柜"));
    closeBtn->setStyleSheet(QString(
        "QPushButton {"
        "  background: %1; color: white; border: none; border-radius: 14px;"
        "  font-size: 21px; font-weight: 700; min-height: 68px; min-width: 420px;"
        "}"
        "QPushButton:hover { background: %2; }"
        "QPushButton:pressed { background: #b71c1c; }"
    ).arg(StyleHelper::dangerColor(), StyleHelper::dangerHover()));
    connect(closeBtn, &QPushButton::clicked, this, &CabinetSessionDialog::onCloseCabinetClicked);
    lay->addWidget(closeBtn, 0, Qt::AlignCenter);

    m_stack->addWidget(page);
}

// [2026-09-23] 读库：本机组全部启用任务类型及各自在库工具（位置维度）
// 输入：无（机组ID取AppConfig本机机组）；输出：填充m_myTypes
void CabinetSessionDialog::loadMyTaskTools()
{
    m_myTypes.clear();
    db::TaskTypeDAO ttDao;
    const int machineGroupId = AppConfig::instance().localMachineGroupId();
    const QJsonArray types = ttDao.findAllActive();
    for (const auto& v : types) {
        const QJsonObject t = v.toObject();
        MyTypeTools mt;
        mt.typeId = t["typeId"].toInt();
        mt.typeName = t["typeName"].toString();
        const QJsonArray tools = ttDao.findInStockToolsByType(mt.typeId, machineGroupId);
        for (const auto& tv : tools) {
            const QJsonObject o = tv.toObject();
            ToolRow row;
            row.name = o["toolName"].toString();
            row.code = o["toolCode"].toString();
            row.pos = common::formatPosition(o["cabinetName"].toString(),
                                             o["layer"].toString(), o["position"].toString());
            mt.tools.append(row);
        }
        m_myTypes.append(mt);
    }
}

// [2026-09-23] 切换任务类型页签：更新选中态并重建卡片区
void CabinetSessionDialog::selectTypeTab(int idx)
{
    if (idx < 0 || idx >= m_myTypes.size()) return;
    m_curTypeIdx = idx;
    for (int i = 0; i < m_typeTabBtns.size(); ++i)
        m_typeTabBtns.at(i)->setChecked(i == idx);
    rebuildToolCards();
}

// [2026-09-23] 按当前页签重建工具卡片网格（三列；空态显示灰色提示）
void CabinetSessionDialog::rebuildToolCards()
{
    if (!m_toolsScroll) return;
    QWidget* host = new QWidget();
    host->setStyleSheet("background: transparent;");
    QGridLayout* grid = new QGridLayout(host);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(12);

    if (m_myTypes.isEmpty()) {
        QLabel* hint = new QLabel(QStringLiteral("暂无任务类型工具"));
        hint->setAlignment(Qt::AlignCenter);
        hint->setStyleSheet("font-size: 18px; color: #b0b8c6; background: transparent; padding: 40px;");
        grid->addWidget(hint, 0, 0, 1, 3);
        m_stockCountLabel->setText(QStringLiteral("在库 0 件"));
    } else {
        const MyTypeTools cur = m_myTypes.at(m_curTypeIdx);
        m_stockCountLabel->setText(QStringLiteral("在库 %1 件").arg(cur.tools.size()));
        if (cur.tools.isEmpty()) {
            QLabel* hint = new QLabel(QStringLiteral("该任务类型下暂无在库工具"));
            hint->setAlignment(Qt::AlignCenter);
            hint->setStyleSheet("font-size: 18px; color: #b0b8c6; background: transparent; padding: 40px;");
            grid->addWidget(hint, 0, 0, 1, 3);
        } else {
            for (int i = 0; i < cur.tools.size(); ++i)
                grid->addWidget(makeToolCard(cur.tools.at(i)), i / 3, i % 3);
        }
    }
    grid->setRowStretch(grid->rowCount(), 1);
    m_toolsScroll->setWidget(host);   // QScrollArea自动销毁旧内容
}

// [2026-09-23] 构建单个只读工具卡片（工具名/编号/位置徽章，左侧绿色竖条标识在库）
QWidget* CabinetSessionDialog::makeToolCard(const ToolRow& row)
{
    QWidget* card = new QWidget();
    card->setStyleSheet(
        "QWidget { background: #ffffff; border: 1px solid #eceff4; border-radius: 14px; }");
    card->setMinimumHeight(96);
    QVBoxLayout* v = new QVBoxLayout(card);
    v->setContentsMargins(16, 12, 16, 12);
    v->setSpacing(6);
    QLabel* name = new QLabel(row.name);
    name->setStyleSheet(QString("font-size: 18px; font-weight: 700; color: %1;"
                                "background: transparent; border: none;")
                            .arg(StyleHelper::textColor()));
    QLabel* code = new QLabel(QStringLiteral("编号 %1").arg(row.code));
    code->setStyleSheet("font-size: 14px; color: #8a94a6; background: transparent; border: none;");
    QLabel* pos = new QLabel(row.pos);
    pos->setAlignment(Qt::AlignCenter);
    pos->setFixedWidth(110);
    pos->setStyleSheet("font-size: 15px; font-weight: 700; color: #2e7bd6; background: #e8f3ff;"
                       "border: none; border-radius: 6px; padding: 2px 0;");
    QHBoxLayout* r2 = new QHBoxLayout();
    r2->setSpacing(8);
    r2->addWidget(code);
    r2->addStretch();
    r2->addWidget(pos);
    v->addWidget(name);
    v->addLayout(r2);
    return card;
}

/**
 * @brief 页面1：关柜清单页（借用/归还差异 + 确认关闭）
 */
void CabinetSessionDialog::buildResultPage()
{
    QWidget* page = new QWidget();
    QVBoxLayout* lay = new QVBoxLayout(page);
    lay->setContentsMargins(48, 40, 48, 40);
    lay->setSpacing(20);

    QLabel* titleLabel = new QLabel(QStringLiteral("智能柜已关闭"));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet(QString("font-size: 30px; font-weight: 700; color: %1;")
                                  .arg(StyleHelper::textColor()));
    lay->addWidget(titleLabel);

    // ── 借用清单区块 ──
    m_borrowBlock = new QWidget();
    QVBoxLayout* borrowLay = new QVBoxLayout(m_borrowBlock);
    borrowLay->setContentsMargins(0, 0, 0, 0);
    borrowLay->setSpacing(10);
    QLabel* borrowTitle = new QLabel();
    borrowTitle->setAlignment(Qt::AlignCenter);
    borrowTitle->setStyleSheet(QString("font-size: 20px; font-weight: 700; color: %1;")
                                   .arg(StyleHelper::warningColor()));
    borrowLay->addWidget(borrowTitle);
    m_borrowListLayout = new QVBoxLayout();
    m_borrowListLayout->setContentsMargins(0, 0, 0, 0);
    m_borrowListLayout->setSpacing(8);
    borrowLay->addLayout(m_borrowListLayout);
    lay->addWidget(m_borrowBlock);

    // ── 归还清单区块 ──
    m_returnBlock = new QWidget();
    QVBoxLayout* returnLay = new QVBoxLayout(m_returnBlock);
    returnLay->setContentsMargins(0, 0, 0, 0);
    returnLay->setSpacing(10);
    QLabel* returnTitle = new QLabel();
    returnTitle->setAlignment(Qt::AlignCenter);
    returnTitle->setStyleSheet(QString("font-size: 20px; font-weight: 700; color: %1;")
                                   .arg(StyleHelper::successColor()));
    returnLay->addWidget(returnTitle);
    m_returnListLayout = new QVBoxLayout();
    m_returnListLayout->setContentsMargins(0, 0, 0, 0);
    m_returnListLayout->setSpacing(8);
    returnLay->addLayout(m_returnListLayout);
    lay->addWidget(m_returnBlock);

    // ── 告警区块（工具放错位置，红色醒目提示）──
    m_alertBlock = new QWidget();
    QVBoxLayout* alertLay = new QVBoxLayout(m_alertBlock);
    alertLay->setContentsMargins(0, 0, 0, 0);
    alertLay->setSpacing(10);
    QLabel* alertTitle = new QLabel();
    alertTitle->setAlignment(Qt::AlignCenter);
    alertTitle->setStyleSheet(QString("font-size: 20px; font-weight: 700; color: %1;")
                                  .arg(StyleHelper::dangerColor()));
    alertLay->addWidget(alertTitle);
    m_alertListLayout = new QVBoxLayout();
    m_alertListLayout->setContentsMargins(0, 0, 0, 0);
    m_alertListLayout->setSpacing(8);
    alertLay->addLayout(m_alertListLayout);
    m_alertBlock->hide();
    lay->addWidget(m_alertBlock);

    // 无差异提示（默认隐藏）
    m_noChangeLabel = new QLabel(QStringLiteral("本次无借用、归还记录"));
    m_noChangeLabel->setAlignment(Qt::AlignCenter);
    m_noChangeLabel->setStyleSheet(QString("font-size: 22px; color: %1;")
                                       .arg(StyleHelper::textSecondary()));
    m_noChangeLabel->hide();
    lay->addWidget(m_noChangeLabel);

    lay->addStretch(1);

    // 确认关闭按钮
    QPushButton* confirmBtn = new QPushButton(QStringLiteral("确 认 关 闭"));
    confirmBtn->setStyleSheet(QString(
        "QPushButton {"
        "  background: %1; color: white; border: none; border-radius: 14px;"
        "  font-size: 20px; font-weight: 700; min-height: 64px; min-width: 320px;"
        "}"
        "QPushButton:hover { background: %2; }"
        "QPushButton:pressed { background: #2e7d32; }"
    ).arg(StyleHelper::successColor(), StyleHelper::successHover()));
    connect(confirmBtn, &QPushButton::clicked, this, &CabinetSessionDialog::onConfirmCloseClicked);
    lay->addWidget(confirmBtn, 0, Qt::AlignCenter);

    // 填充区块标题文字（借用/归还件数在进入本页时更新）
    m_borrowBlock->findChild<QLabel*>()->setText(QStringLiteral("本次借用了"));
    m_returnBlock->findChild<QLabel*>()->setText(QStringLiteral("本次归还了"));

    m_stack->addWidget(page);
}

/**
 * @brief 演示专用面板：工具列表，点击行按钮切换在柜/已取走（模拟拿走/放回）
 */
void CabinetSessionDialog::buildDemoPanel()
{
    m_demoPanel = new QDialog(this);
    m_demoPanel->setWindowTitle(QStringLiteral("演示专用 · 模拟柜内拿取/放回"));
    m_demoPanel->setStyleSheet("QDialog { background: white; }");
    m_demoPanel->setFixedSize(560, 560);

    QVBoxLayout* lay = new QVBoxLayout(m_demoPanel);
    lay->setContentsMargins(20, 16, 20, 16);
    lay->setSpacing(8);

    QLabel* tip = new QLabel(QStringLiteral("演示专用：点击按钮模拟工具拿走/放回，产品版由传感器自动感知"));
    tip->setWordWrap(true);
    tip->setStyleSheet(QString("font-size: 14px; color: %1;").arg(StyleHelper::textSecondary()));
    lay->addWidget(tip);

    for (int i = 0; i < m_tools.size(); ++i) {
        QWidget* row = new QWidget();
        QHBoxLayout* rowLay = new QHBoxLayout(row);
        rowLay->setContentsMargins(8, 0, 8, 0);
        const MockTool& tool = m_tools[i];
        QLabel* info = new QLabel(QStringLiteral("%1  %2  %3")
                                      .arg(tool.name, tool.code, tool.position));
        info->setStyleSheet(QString("font-size: 16px; color: %1;").arg(StyleHelper::textColor()));
        QPushButton* statusBtn = new QPushButton();
        statusBtn->setStyleSheet(StyleHelper::buttonOutline());
        statusBtn->setFixedWidth(100);
        connect(statusBtn, &QPushButton::clicked, this, &CabinetSessionDialog::onDemoToolToggled);
        // 错放按钮：模拟工具放回错误位置（触发关柜告警演示）
        QPushButton* wrongBtn = new QPushButton();
        wrongBtn->setFixedWidth(100);
        connect(wrongBtn, &QPushButton::clicked, this, &CabinetSessionDialog::onDemoWrongPlaceToggled);
        rowLay->addWidget(info, 1);
        rowLay->addWidget(statusBtn);
        rowLay->addWidget(wrongBtn);
        lay->addWidget(row);
        m_demoStatusBtns.append(statusBtn);
        m_demoWrongBtns.append(wrongBtn);
    }
    updateDemoRows();
}

/**
 * @brief 位号+1生成"错放位置"（A-01-02 → A-01-03），仅演示用
 */
QString CabinetSessionDialog::bumpedPosition(const QString& position)
{
    QStringList parts = position.split('-');
    if (parts.size() == 3) {
        int pos = parts[2].toInt() + 1;
        parts[2] = QString("%1").arg(pos, 2, 10, QChar('0'));
        return parts.join('-');
    }
    return position;
}

/**
 * @brief 刷新演示面板各行按钮：状态（在柜/已取走）+ 错放标记
 */
void CabinetSessionDialog::updateDemoRows()
{
    for (int i = 0; i < m_demoStatusBtns.size(); ++i) {
        QPushButton* statusBtn = m_demoStatusBtns[i];
        QPushButton* wrongBtn = m_demoWrongBtns[i];
        if (m_tools[i].inCabinet) {
            statusBtn->setText(QStringLiteral("在柜"));
            statusBtn->setStyleSheet(StyleHelper::buttonOutline());
        } else {
            statusBtn->setText(QStringLiteral("已取走"));
            statusBtn->setStyleSheet(StyleHelper::buttonDanger());
        }
        // 错放按钮仅在"在柜"时可用，再点一次恢复正确位置
        wrongBtn->setEnabled(m_tools[i].inCabinet);
        if (!m_tools[i].inCabinet) {
            wrongBtn->setText(QStringLiteral("错放"));
            wrongBtn->setStyleSheet(StyleHelper::buttonDefault());
        } else if (m_tools[i].misplaced) {
            wrongBtn->setText(QStringLiteral("已错放"));
            wrongBtn->setStyleSheet(StyleHelper::buttonDanger());
        } else {
            wrongBtn->setText(QStringLiteral("错放"));
            wrongBtn->setStyleSheet(StyleHelper::buttonDefault());
        }
    }
}

/**
 * @brief 显示/隐藏演示面板（首次调用时构建）
 */
void CabinetSessionDialog::toggleDemoPanel()
{
    if (!m_demoPanel) buildDemoPanel();
    if (m_demoPanel->isVisible()) {
        m_demoPanel->hide();
    } else {
        m_demoPanel->move(QDialog::x() + (QDialog::width() - m_demoPanel->width()) / 2,
                          QDialog::y() + (QDialog::height() - m_demoPanel->height()) / 2);
        m_demoPanel->show();
    }
}

/**
 * @brief 事件过滤：开柜图标在1.5秒内连点3次 → 切换演示面板
 */
bool CabinetSessionDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_iconLabel && event->type() == QEvent::MouseButtonPress) {
        if (!m_iconClickTimer.isValid() || m_iconClickTimer.elapsed() > 1500)
            m_iconClickCount = 0;
        m_iconClickTimer.start();
        if (++m_iconClickCount >= 3) {
            m_iconClickCount = 0;
            toggleDemoPanel();
        }
        return true;
    }
    return QDialog::eventFilter(watched, event);
}

/**
 * @brief 关闭智能柜：对比开柜快照与当前状态，得出借用/归还差异
 */
void CabinetSessionDialog::onCloseCabinetClicked()
{
    if (m_demoPanel) m_demoPanel->hide();

    QStringList borrowed, returned, alerts;
    for (int i = 0; i < m_tools.size(); ++i) {
        const MockTool& tool = m_tools[i];
        QString desc = QStringLiteral("%1（%2）").arg(tool.name, tool.position);
        if (m_openSnapshot[i] && !tool.inCabinet)
            borrowed.append(desc);      // 开柜在柜 → 关柜不在：借用
        else if (!m_openSnapshot[i] && tool.inCabinet)
            returned.append(desc);      // 开柜不在 → 关柜在柜：归还
        // 错放告警：工具在柜但位置不对（借用/归还判定不受影响）
        //if (tool.inCabinet && tool.misplaced)
        // alerts.append(QStringLiteral("%1：应放 %2，实际放 %3")
        // .arg(tool.name, tool.position, tool.wrongPosition));
        if (tool.inCabinet && tool.misplaced)
            alerts.append(QStringLiteral("%1：识别错误请检查").arg(tool.name));
    }

    // 清空上次的差异行
    auto clearRows = [](QVBoxLayout* lay) {
        while (lay->count() > 0) {
            QLayoutItem* item = lay->takeAt(0);
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
    };
    clearRows(m_borrowListLayout);
    clearRows(m_returnListLayout);
    clearRows(m_alertListLayout);

    bool hasChange = !borrowed.isEmpty() || !returned.isEmpty() || !alerts.isEmpty();
    m_borrowBlock->setVisible(!borrowed.isEmpty());
    m_returnBlock->setVisible(!returned.isEmpty());
    m_alertBlock->setVisible(!alerts.isEmpty());
    m_noChangeLabel->setVisible(!hasChange);

    auto addRows = [](QVBoxLayout* lay, const QStringList& rows) {
        for (const QString& row : rows) {
            QLabel* label = new QLabel(QStringLiteral("· %1").arg(row));
            label->setAlignment(Qt::AlignCenter);
            label->setStyleSheet(QString("font-size: 18px; color: %1;").arg(StyleHelper::textColor()));
            lay->addWidget(label);
        }
    };
    addRows(m_borrowListLayout, borrowed);
    addRows(m_returnListLayout, returned);
    addRows(m_alertListLayout, alerts);

    // 区块标题带上件数（每个区块第一个子控件为标题QLabel）
    QLabel* borrowTitle = m_borrowBlock->findChild<QLabel*>();
    if (borrowTitle)
        borrowTitle->setText(QStringLiteral("本次借用了 %1 件工具").arg(borrowed.size()));
    QLabel* returnTitle = m_returnBlock->findChild<QLabel*>();
    if (returnTitle)
        returnTitle->setText(QStringLiteral("本次归还了 %1 件工具").arg(returned.size()));
    QLabel* alertTitle = m_alertBlock->findChild<QLabel*>();
    if (alertTitle)
        alertTitle->setText(QStringLiteral("⚠ 告警：检测到 %1 件工具放错位置").arg(alerts.size()));

    m_stack->setCurrentIndex(1);
}

/**
 * @brief 清单页确认关闭：结束会话，回到登录页
 */
void CabinetSessionDialog::onConfirmCloseClicked()
{
    accept();
}

/**
 * @brief 演示面板：切换被点按钮对应工具的在柜状态（取走时清除错放标记）
 */                                                                                                                                 
void CabinetSessionDialog::onDemoToolToggled()
{
    QPushButton* statusBtn = qobject_cast<QPushButton*>(sender());
    if (!statusBtn) return;
    int row = m_demoStatusBtns.indexOf(statusBtn);
    if (row < 0 || row >= m_tools.size()) return;
    m_tools[row].inCabinet = !m_tools[row].inCabinet;
    m_tools[row].misplaced = false;
    m_tools[row].wrongPosition.clear();
    updateDemoRows();
}

/**
 * @brief 演示面板：标记/取消"放错位置"（再次点击恢复正确位置）
 */
void CabinetSessionDialog::onDemoWrongPlaceToggled()
{
    QPushButton* wrongBtn = qobject_cast<QPushButton*>(sender());
    if (!wrongBtn) return;
    int row = m_demoWrongBtns.indexOf(wrongBtn);
    if (row < 0 || row >= m_tools.size()) return;
    if (!m_tools[row].inCabinet) return;   // 已取走的工具无错放概念
    m_tools[row].misplaced = !m_tools[row].misplaced;
    m_tools[row].wrongPosition = m_tools[row].misplaced
                                     ? bumpedPosition(m_tools[row].position) : QString();
    updateDemoRows();
}
