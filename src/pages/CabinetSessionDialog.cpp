/**
 * @file CabinetSessionDialog.cpp
 * @brief 普通用户智能柜会话对话框实现 - 真实硬件闭环（开柜→关柜→识别→比对→写库）
 * @author 袁燕
 * @说明 复用出入库侧既有能力：EdgeService（开箱/识别/在线/摄像头）、
 *       ToolService（全柜比对）、BorrowService/ReturnService（写库）、
 *       RecordDAO（操作日志）、ToolDAO（开柜基线）、DrawerOpeningDialog（等待框）。
 *       不新增任何通信协议帧或业务接口。
 */
#include "CabinetSessionDialog.h"
#include "utils/StyleHelper.h"
#include "utils/LayoutHelper.h"
#include "db/TaskTypeDAO.h"
#include "db/ToolDAO.h"
#include "db/RecordDAO.h"
#include "common/AppConfig.h"
#include "common/PositionFormatter.h"
#include "common/Constants.h"
#include "common/StatusDisplay.h"
#include "services/BorrowService.h"
#include "services/ReturnService.h"
#include "services/ToolService.h"
#include "services/AlertService.h"       // 位置反馈异常落告警（alert 域统一入口）
#include "components/TopBar.h"          // 会话顶栏与管理页面共用同一组件
#include "components/DrawerOpeningDialog.h"
#include "components/MessageDialog.h"
#include "HelpManualDialog.h"            // 顶栏"帮助"按钮打开内置操作手册
#include "utils/ScreenFit.h"             // 全屏对话框按屏幕矩形铺满（麒麟窗口管理器兼容）
#include "model/BorrowRecord.h"
#include "comm/EdgeService.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>
#include <QFrame>
#include <QApplication>
#include <QDateTime>
#include <QTimer>

CabinetSessionDialog::CabinetSessionDialog(const QJsonObject& user, QWidget* parent)
    : QDialog(parent), m_user(user)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setModal(true);
    setStyleSheet(StyleHelper::dialogBackground());

    m_userId = m_user["userId"].toInt();
    m_machineGroupId = AppConfig::instance().localMachineGroupId();

    m_stack = new QStackedWidget(this);
    QVBoxLayout* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_stack);

    // 会话顶栏：与管理员子页面共用 TopBar 组件（页面标题/时钟/用户信息/主页/注销）
    TopBar* topBar = new TopBar(this);
    topBar->setFixedHeight(68);
    topBar->setPageTitle(QStringLiteral("借用归还"));
    topBar->setUserAreaVisible(true);
    topBar->setHomeButtonVisible(true);
    QString topName = m_user["realName"].toString();
    if (topName.isEmpty()) topName = m_user["name"].toString();
    topBar->setUserName(topName);
    topBar->setDepartment(m_user["department"].toString());
    topBar->setVersionText(AppConfig::instance().appVersion());
    outer->insertWidget(0, topBar);
    // 主页=放弃本次会话回功能主页；注销=结束会话并通知主窗口注销回登录页
    connect(topBar, &TopBar::homeClicked, this, &QDialog::reject);
    connect(topBar, &TopBar::logoutClicked, this, [this] {
        emit logoutRequested();
        reject();
    });
    connect(topBar, &TopBar::exitSystemClicked, qApp, &QApplication::quit);
    connect(topBar, &TopBar::helpClicked, this, [this] {
        HelpManualDialog manual(this);
        UiFit::showFullScreenFit(&manual, this);
        manual.exec();
    });

    loadMyTaskTools();  // 先读本机组任务类型在库工具（开柜页面板数据）
    buildOpenPage();
    buildResultPage();
}

/**
 * @brief 全屏启动会话并阻塞，直到用户确认关闭智能柜
 * @note 窗口几何直接铺满屏幕，不依赖窗口管理器的全屏能力：
 *       麒麟等窗口管理器可能忽略无边框对话框的全屏请求，窗口会退回内容尺寸并居中，
 *       导致顶栏被挤出屏幕、内容显示不全。
 *       进入即自动自检（边缘机在线 + 8 路摄像头）：不通过仅弹明原因，全屏会话窗口不出现；
 *       通过才显示全屏会话并直接下发开锁指令进入取放环节——开柜全程零手动确认。
 */
void CabinetSessionDialog::startSession()
{
    // 自检前置：失败只在主界面上弹原因，不闪现全屏会话
    if (!passEntryGate()) {
        reject();
        return;
    }
    UiFit::showFullScreenFit(this, parentWidget());
    onOpenCabinetClicked();   // 自检已通过：直接开柜（重试态由等待框的"重新开柜"承接）
    QEventLoop loop;
    connect(this, &QDialog::finished, &loop, &QEventLoop::quit);
    loop.exec();
}

/**
 * @brief 进入会话前自动自检：边缘机服务、链路在线、8 路摄像头逐项检查
 * @return true=全部通过；false=有不合格项（已弹窗说明原因，弹窗挂在主界面上）
 */
bool CabinetSessionDialog::passEntryGate()
{
    if (!m_edge) {
        MessageDialog::showWarning(parentWidget(), QStringLiteral("无法进入会话"),
            QStringLiteral("未接入边缘计算机服务，暂时无法借用归还。请联系管理员。"));
        return false;
    }
    if (!m_edge->isOnline()) {
        MessageDialog::showWarning(parentWidget(), QStringLiteral("无法进入会话"),
            QStringLiteral("边缘计算机连接已断开，暂时无法借用归还。请检查边缘计算机连接后重试。"));
        return false;
    }
    if (!m_edge->allCamerasOk()) {
        MessageDialog::showWarning(parentWidget(), QStringLiteral("无法进入会话"),
            QStringLiteral("摄像头自检异常：") + m_edge->cameraAbnormalText()
                + QStringLiteral("，暂时无法借用归还。请联系管理员处理。"));
        return false;
    }
    return true;
}

/**
 * @brief 柜体打开后启动关柜等待计时（超时提示确认柜门已关，零写库）
 */
void CabinetSessionDialog::startCloseWaitTimer()
{
    if (!m_closeWaitTimer) {
        m_closeWaitTimer = new QTimer(this);
        m_closeWaitTimer->setSingleShot(true);
        connect(m_closeWaitTimer, &QTimer::timeout, this, &CabinetSessionDialog::onCloseWaitTimeout);
    }
    m_closeWaitTimer->start(SC::CABINET_CLOSE_WAIT_MS);
}

/**
 * @brief 关柜等待超时：提示用户确认柜门是否已关好（会话保留，可继续操作）
 */
void CabinetSessionDialog::onCloseWaitTimeout()
{
    if (!m_opened) return;   // 已不在"柜体打开"状态，无需提醒
    MessageDialog::showWarning(this, QStringLiteral("等待关柜超时"),
        QStringLiteral("已较长时间未关闭智能柜，请确认柜门是否已关好。")
            + QStringLiteral("取放完成后请点击「关闭智能柜」完成核对。"));
}

void CabinetSessionDialog::setEdgeService(EdgeService* svc)
{
    m_edge = svc;
}

// ─────────────────────────── 页面0：开柜提示页 ───────────────────────────

void CabinetSessionDialog::buildOpenPage()
{
    QWidget* page = new QWidget();
    QVBoxLayout* lay = new QVBoxLayout(page);
    lay->setContentsMargins(40, 22, 40, 24);
    lay->setSpacing(8);

    // 顶部：图标 + 标题
    QHBoxLayout* headRow = new QHBoxLayout();
    headRow->setSpacing(14);
    m_iconLabel = new QLabel(QStringLiteral("🔓"));
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setStyleSheet("font-size: 46px; background: transparent;");
    m_titleLabel = new QLabel(QStringLiteral("智能柜"));
    m_titleLabel->setStyleSheet(StyleHelper::largeTitleText());
    headRow->addStretch();
    headRow->addWidget(m_iconLabel);
    headRow->addWidget(m_titleLabel);
    headRow->addStretch();
    lay->addLayout(headRow);
    lay->addSpacing(10);

    // ── 取放区（开柜成功后展示）──
    m_openReady = new QWidget();
    QVBoxLayout* readyLay = new QVBoxLayout(m_openReady);
    readyLay->setContentsMargins(0, 0, 0, 0);
    readyLay->setSpacing(8);

    QString realName = m_user["realName"].toString();
    if (realName.isEmpty()) realName = m_user["name"].toString();
    QLabel* welcomeLabel = new QLabel(
        QStringLiteral("您好，%1 · 您可以自行借用或归还工具").arg(realName));
    welcomeLabel->setAlignment(Qt::AlignCenter);
    welcomeLabel->setStyleSheet(QString("font-size: 17px; color: %1; background: transparent;")
                                    .arg(StyleHelper::textSecondary()));
    readyLay->addWidget(welcomeLabel);
    readyLay->addSpacing(6);

    // "我的任务工具"面板标题行
    QHBoxLayout* ptRow = new QHBoxLayout();
    ptRow->setSpacing(10);
    QWidget* bar = new QWidget();
    bar->setFixedSize(5, 22);
    bar->setStyleSheet("background:#4da3ff; border-radius:3px;");
    QLabel* ptLabel = new QLabel(QStringLiteral("我的任务工具"));
    ptLabel->setStyleSheet(QString("" + StyleHelper::fontSize(StyleHelper::Token::FontHeading) + " font-weight: 700; color: %1; background: transparent;")
                               .arg(StyleHelper::textColor()));
    QLabel* ptSub = new QLabel(QStringLiteral("按任务类型查看 · 仅供拿取时参考"));
    ptSub->setStyleSheet("" + StyleHelper::fontSize(StyleHelper::Token::FontBody) + " color: #8a94a6; background: transparent;");
    m_stockCountLabel = new QLabel(QString());
    m_stockCountLabel->setStyleSheet(
        "" + StyleHelper::fontSize(StyleHelper::Token::FontBody) + " color: #52c41a; font-weight: 600; background: #f6ffed;"
        "border: 1px solid #b7eb8f; padding: 3px 12px; border-radius: 999px;");
    ptRow->addWidget(bar);
    ptRow->addWidget(ptLabel);
    ptRow->addWidget(ptSub);
    ptRow->addStretch();
    ptRow->addWidget(m_stockCountLabel);
    readyLay->addLayout(ptRow);

    // 任务类型页签行（横排胶囊，超出宽度可横向拖动）
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
    readyLay->addWidget(tabScroll);

    // 工具卡片滚动区（三列网格，垂直滚动，触屏拖动）
    m_toolsScroll = new QScrollArea();
    m_toolsScroll->setWidgetResizable(true);
    m_toolsScroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");
    QScroller::grabGesture(m_toolsScroll->viewport(), QScroller::LeftMouseButtonGesture);
    readyLay->addWidget(m_toolsScroll, 1);
    rebuildToolCards();

    // 底部：关闭智能柜按钮
    m_closeBtn = new QPushButton(QStringLiteral("关 闭 智 能 柜"));
    m_closeBtn->setStyleSheet(QString(
        "QPushButton {"
        "  background: %1; color: white; border: none; border-radius: 14px;"
        "  font-size: 21px; font-weight: 700; min-height: 68px; min-width: 420px;"
        "}"
        "QPushButton:hover { background: %2; }"
        "QPushButton:pressed { background: #b71c1c; }"
    ).arg(StyleHelper::dangerColor(), StyleHelper::dangerHover()));
    // 待办（协议未定）：IO 板下位机「已关柜」信号协议定稿后，改由该硬件信号触发 onCloseCabinetClicked()，
    //           替代当前"关闭智能柜"按钮手动触发；协议帧与 EdgeService 接线待定。
    connect(m_closeBtn, &QPushButton::clicked, this, &CabinetSessionDialog::onCloseCabinetClicked);
    readyLay->addWidget(m_closeBtn, 0, Qt::AlignCenter);

    lay->addWidget(m_openReady);
    m_openReady->hide();

    m_stack->addWidget(page);
}

// 读库：本机组全部启用任务类型及各自在库工具（位置维度）
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

void CabinetSessionDialog::selectTypeTab(int idx)
{
    if (idx < 0 || idx >= m_myTypes.size()) return;
    m_curTypeIdx = idx;
    for (int i = 0; i < m_typeTabBtns.size(); ++i)
        m_typeTabBtns.at(i)->setChecked(i == idx);
    rebuildToolCards();
}

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
        hint->setStyleSheet("" + StyleHelper::fontSize(StyleHelper::Token::FontTitle) + " color: #b0b8c6; background: transparent; padding: 40px;");
        grid->addWidget(hint, 0, 0, 1, 3);
        m_stockCountLabel->setText(QStringLiteral("在库 0 件"));
    } else {
        const MyTypeTools cur = m_myTypes.at(m_curTypeIdx);
        m_stockCountLabel->setText(QStringLiteral("在库 %1 件").arg(cur.tools.size()));
        if (cur.tools.isEmpty()) {
            QLabel* hint = new QLabel(QStringLiteral("该任务类型下暂无在库工具"));
            hint->setAlignment(Qt::AlignCenter);
            hint->setStyleSheet("" + StyleHelper::fontSize(StyleHelper::Token::FontTitle) + " color: #b0b8c6; background: transparent; padding: 40px;");
            grid->addWidget(hint, 0, 0, 1, 3);
        } else {
            for (int i = 0; i < cur.tools.size(); ++i)
                grid->addWidget(makeToolCard(cur.tools.at(i)), i / 3, i % 3);
        }
    }
    grid->setRowStretch(grid->rowCount(), 1);
    m_toolsScroll->setWidget(host);   // QScrollArea自动销毁旧内容
}

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
    name->setStyleSheet(QString("" + StyleHelper::fontSize(StyleHelper::Token::FontTitle) + " font-weight: 700; color: %1;"
                                "background: transparent; border: none;")
                            .arg(StyleHelper::textColor()));
    QLabel* code = new QLabel(QStringLiteral("编号 %1").arg(row.code));
    code->setStyleSheet("" + StyleHelper::fontSize(StyleHelper::Token::FontBody) + " color: #8a94a6; background: transparent; border: none;");
    QLabel* pos = new QLabel(row.pos);
    pos->setAlignment(Qt::AlignCenter);
    pos->setFixedWidth(110);
    pos->setStyleSheet("" + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + " font-weight: 700; color: #2e7bd6; background: #e8f3ff;"
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

// ─────────────────────────── 页面1：关柜清单页 ───────────────────────────

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

    // 借用清单区块
    m_borrowBlock = new QWidget();
    QVBoxLayout* borrowLay = new QVBoxLayout(m_borrowBlock);
    borrowLay->setContentsMargins(0, 0, 0, 0);
    borrowLay->setSpacing(10);
    QLabel* borrowTitle = new QLabel();
    borrowTitle->setAlignment(Qt::AlignCenter);
    borrowTitle->setStyleSheet(QString("" + StyleHelper::fontSize(StyleHelper::Token::FontHeading) + " font-weight: 700; color: %1;")
                                   .arg(StyleHelper::warningColor()));
    borrowLay->addWidget(borrowTitle);
    m_borrowListLayout = new QVBoxLayout();
    m_borrowListLayout->setContentsMargins(0, 0, 0, 0);
    m_borrowListLayout->setSpacing(8);
    borrowLay->addLayout(m_borrowListLayout);
    lay->addWidget(m_borrowBlock);

    // 归还清单区块
    m_returnBlock = new QWidget();
    QVBoxLayout* returnLay = new QVBoxLayout(m_returnBlock);
    returnLay->setContentsMargins(0, 0, 0, 0);
    returnLay->setSpacing(10);
    QLabel* returnTitle = new QLabel();
    returnTitle->setAlignment(Qt::AlignCenter);
    returnTitle->setStyleSheet(QString("" + StyleHelper::fontSize(StyleHelper::Token::FontHeading) + " font-weight: 700; color: %1;")
                                   .arg(StyleHelper::successColor()));
    returnLay->addWidget(returnTitle);
    m_returnListLayout = new QVBoxLayout();
    m_returnListLayout->setContentsMargins(0, 0, 0, 0);
    m_returnListLayout->setSpacing(8);
    returnLay->addLayout(m_returnListLayout);
    lay->addWidget(m_returnBlock);

    // 告警区块（错放/未登记工具，红色醒目提示）
    m_alertBlock = new QWidget();
    QVBoxLayout* alertLay = new QVBoxLayout(m_alertBlock);
    alertLay->setContentsMargins(0, 0, 0, 0);
    alertLay->setSpacing(10);
    QLabel* alertTitle = new QLabel();
    alertTitle->setAlignment(Qt::AlignCenter);
    alertTitle->setStyleSheet(QString("" + StyleHelper::fontSize(StyleHelper::Token::FontHeading) + " font-weight: 700; color: %1;")
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
    m_noChangeLabel->setStyleSheet(QString("" + StyleHelper::fontSize(StyleHelper::Token::FontDisplay) + " color: %1;")
                                       .arg(StyleHelper::textSecondary()));
    m_noChangeLabel->hide();
    lay->addWidget(m_noChangeLabel);

    lay->addStretch(1);

    // 重新开柜整理（错放≤3轮时出现）
    m_reopenBtn = new QPushButton(QStringLiteral("重 新 开 柜 整 理"));
    m_reopenBtn->setStyleSheet(QString(
        "QPushButton {"
        "  background: %1; color: white; border: none; border-radius: 14px;"
        "  font-size: 19px; font-weight: 700; min-height: 60px; min-width: 360px;"
        "}"
        "QPushButton:hover { background: %2; }"
        "QPushButton:pressed { background: #1565c0; }"
    ).arg(StyleHelper::primaryColor(), StyleHelper::primaryHover()));
    connect(m_reopenBtn, &QPushButton::clicked, this, &CabinetSessionDialog::onReopenClicked);
    m_reopenBtn->hide();

    // 确认关闭按钮
    QPushButton* confirmBtn = new QPushButton(QStringLiteral("确 认 关 闭"));
    confirmBtn->setStyleSheet(QString(
        "QPushButton {"
        "  background: %1; color: white; border: none; border-radius: 14px;"
        "  " + StyleHelper::fontSize(StyleHelper::Token::FontHeading) + " font-weight: 700; min-height: 64px; min-width: 320px;"
        "}"
        "QPushButton:hover { background: %2; }"
        "QPushButton:pressed { background: #2e7d32; }"
    ).arg(StyleHelper::successColor(), StyleHelper::successHover()));
    connect(confirmBtn, &QPushButton::clicked, this, &CabinetSessionDialog::onConfirmCloseClicked);

    // 两个按钮左右排布一行；重新开柜整理隐藏时确认关闭仍居中（隐藏控件不占布局空间）
    QHBoxLayout* btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    btnRow->addWidget(m_reopenBtn);
    btnRow->addSpacing(24);
    btnRow->addWidget(confirmBtn);
    btnRow->addStretch(1);
    lay->addLayout(btnRow);

    // 填充区块标题文字（借用/归还件数在进入本页时更新）
    m_borrowBlock->findChild<QLabel*>()->setText(QStringLiteral("本次借用了"));
    m_returnBlock->findChild<QLabel*>()->setText(QStringLiteral("本次归还了"));

    m_stack->addWidget(page);
}

// ─────────────────────────── 等待框 ───────────────────────────

void CabinetSessionDialog::showWait(const QString& windowTitle, const QString& title, const QString& desc)
{
    closeWait();
    auto* dlg = new DrawerOpeningDialog(windowTitle, title, desc, 32, 48, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setNextButtonText(QStringLiteral("处理中…"));
    dlg->setNextEnabled(false);
    dlg->setFixedSize(520, 380);
    m_waitDlg = dlg;
    dlg->show();
}

void CabinetSessionDialog::closeWait()
{
    if (m_waitDlg) {
        m_waitDlg->accept();
        m_waitDlg = nullptr;
    }
}

// ─────────────────────────── 开柜 ───────────────────────────

void CabinetSessionDialog::onOpenCabinetClicked()
{
    if (!m_edge) {
        MessageDialog::showWarning(this, QStringLiteral("无法开柜"), QStringLiteral("未接入边缘机服务"));
        return;
    }
    if (!m_edge->isOnline()) {
        MessageDialog::showWarning(this, QStringLiteral("无法开柜"), QStringLiteral("边缘机离线，无法开柜"));
        return;
    }
    if (!m_edge->allCamerasOk()) {
        MessageDialog::showWarning(this, QStringLiteral("无法开柜"),
            QStringLiteral("摄像头自检异常：") + m_edge->cameraAbnormalText());
        return;
    }
    m_openRequestId = m_edge->requestOpen();
    onOpenStarted(m_openRequestId);
}

void CabinetSessionDialog::onOpenStarted(const QString& requestId)
{
    if (requestId.isEmpty()) return;   // requestOpen 离线拒发时已先发 openFailed，原因随后提示
    showWait(QStringLiteral("开柜中"), QStringLiteral("正在打开智能柜"),
             QStringLiteral("系统正在打开智能柜，请稍候…"));
}

void CabinetSessionDialog::onOpenFinished(const QString& requestId, bool success, const QString& message)
{
    if (requestId != m_openRequestId) return;
    if (success) {
        closeWait();
        // 采集开柜基线：本机组当前全部在库货位（与关柜识别结果比对）
        m_baselineStock.clear();
        db::ToolDAO tdao;
        const QJsonArray arr = tdao.findAllInStockPositions(m_machineGroupId);
        for (const auto& v : arr) {
            const QJsonObject o = v.toObject();
            StockItem s;
            s.mappingId = o["mappingId"].toInt();
            s.toolId    = o["toolId"].toInt();
            s.toolCode  = o["toolCode"].toString();
            s.toolName  = o["toolName"].toString();
            s.position  = common::formatPosition(o["cabinetName"].toString(),
                                                 o["layer"].toString(), o["position"].toString());
            m_baselineStock.append(s);
        }
        m_opened = true;
        m_openReady->show();
        m_titleLabel->setText(QStringLiteral("智能柜已开启"));
        m_stack->setCurrentIndex(0);
        startCloseWaitTimer();   // 柜体已打开：启动关柜等待计时（含重新开柜整理后的再次进入）
    } else if (m_waitDlg) {
        m_waitDlg->showFailureState(QStringLiteral("开柜失败"), message, QStringLiteral("重新开柜"));
        connect(m_waitDlg, &DrawerOpeningDialog::retryRequested, this, &CabinetSessionDialog::onOpenCabinetClicked);
    }
}

void CabinetSessionDialog::onOpenFailed(const QString& requestId, const QString& reason)
{
    if (requestId.isEmpty()) {   // 离线拒发：requestOpen 内部已先发本信号，reason 即真实原因
        MessageDialog::showWarning(this, QStringLiteral("无法开柜"), reason);
        return;
    }
    if (requestId != m_openRequestId) return;
    if (m_waitDlg) {
        m_waitDlg->showFailureState(QStringLiteral("开柜失败"), reason, QStringLiteral("重新开柜"));
        connect(m_waitDlg, &DrawerOpeningDialog::retryRequested, this, &CabinetSessionDialog::onOpenCabinetClicked);
    }
}

// ─────────────────────────── 关柜 + 识别 ───────────────────────────

void CabinetSessionDialog::onCloseCabinetClicked()
{
    // 待办（协议未定）：IO 板下位机「已关柜」信号协议定稿后，由该硬件信号触发本函数，
    //           替代"关闭智能柜"按钮手动触发。
    if (!m_opened) { onOpenCabinetClicked(); return; }   // 防护：未开柜先开柜
    if (m_closeWaitTimer) m_closeWaitTimer->stop();   // 用户已主动关柜，结束等待计时
    if (!m_edge) {
        MessageDialog::showWarning(this, QStringLiteral("无法核对"), QStringLiteral("未接入边缘机服务"));
        return;
    }
    m_recognizeRequestId = m_edge->requestRecognize();
    onRecognitionStarted(m_recognizeRequestId);
}

void CabinetSessionDialog::onRecognitionStarted(const QString& requestId)
{
    if (requestId.isEmpty()) return;   // requestRecognize 离线拒发时已先发 recognizeFailed
    showWait(QStringLiteral("核对中"), QStringLiteral("正在核对柜内工具"),
             QStringLiteral("系统正在与边缘计算机核对柜内工具，请稍候…"));
}

void CabinetSessionDialog::onRecognitionResult(const QString& requestId, const QList<ToolService::RecogSpot>& spots)
{
    if (requestId != m_recognizeRequestId) return;   // 编号对不上说明这份结果来晚了，丢弃
    m_spots = spots;
    m_recognizeRequestId.clear();
    closeWait();
    // 比对写库逻辑排到事件循环下一轮：避免在通信回调栈上弹出模态结果页
    QTimer::singleShot(0, this, &CabinetSessionDialog::applyResult);
}

void CabinetSessionDialog::onRecognitionFailed(const QString& requestId, const QString& reason)
{
    if (requestId.isEmpty()) {
        MessageDialog::showWarning(this, QStringLiteral("无法核对"), reason);
        return;
    }
    if (requestId != m_recognizeRequestId) return;
    if (m_waitDlg) {
        m_waitDlg->showFailureState(QStringLiteral("核对失败"), reason, QStringLiteral("重新核对"));
        connect(m_waitDlg, &DrawerOpeningDialog::retryRequested, this, &CabinetSessionDialog::onCloseCabinetClicked);
    }
}

void CabinetSessionDialog::onEdgeOnlineChanged(bool online)
{
    if (!online && m_waitDlg) {
        closeWait();
        MessageDialog::showWarning(this, QStringLiteral("链路中断"),
            QStringLiteral("边缘机链路已断开，请检查边缘计算机连接后重试。"));
    }
}

// ─────────────────────────── 比对 + 渲染 ───────────────────────────

void CabinetSessionDialog::applyResult()
{
    // 反馈完整性校验：边缘机回报的货位数量必须与开柜快照（库内映射表）长度一致。
    // 两者长度不一致即视为位置反馈不全（漏报/多报），按袁总要求本次不予识别并反馈错误
    if (m_spots.size() != m_baselineStock.size()) {
        // 位置反馈异常属识别故障：落系统告警便于运维排查边缘机识别漏报/多报
        const QString detail = QStringLiteral(
            "边缘机反馈的货位数量（%1）与库内映射表（%2）不一致，疑似位置反馈不全，本次不予识别。")
            .arg(m_spots.size()).arg(m_baselineStock.size());
        AlertService().recordRecogFaultAlert(m_userId,
            QStringLiteral("借用归还核对失败：") + detail);
        MessageDialog::showWarning(this, QStringLiteral("核对失败"), detail);
        reject();   // 关闭警告即退出会话返回首页，本次不予识别、不写库
        return;
    }

    // 基线 → ToolService 比对入参（页面中立结构，不碰通信层类型）
    QList<ToolService::RecogStockItem> stocks;
    for (const StockItem& s : m_baselineStock) {
        ToolService::RecogStockItem it;
        it.mappingId = s.mappingId;
        it.toolId    = s.toolId;
        it.toolCode  = s.toolCode;
        it.toolName  = s.toolName;
        it.position  = s.position;
        stocks.append(it);
    }
    // 全柜核对（复用出入库侧既有比对纯函数）
    ToolService::RecogStockOutcome outcome =
        ToolService::compareStockWithRecognition(stocks, m_spots, {});

    QHash<QString, StockItem> stockByPos;
    QHash<QString, QString> codeToPos;     // toolCode → 登记货位（错放检测用）
    QSet<QString> baselineCodes;
    for (const StockItem& s : m_baselineStock) {
        stockByPos[s.position] = s;
        codeToPos[s.toolCode]  = s.position;
        baselineCodes.insert(s.toolCode);
    }
    // 借出工具的登记货位一并入映射：归还兜底取位置 + 归还错放检测（见 Extra 分支），
    // 保证每条归还日志与归还明细都带位置
    const QJsonArray borrowedPos = db::ToolDAO().findBorrowedOutPositions(m_machineGroupId);
    for (const auto& v : borrowedPos) {
        const QJsonObject o = v.toObject();
        const QString code = o["toolCode"].toString();
        if (!codeToPos.contains(code))
            codeToPos[code] = common::formatPosition(o["cabinetName"].toString(),
                                                     o["layer"].toString(), o["position"].toString());
    }

    // 当前用户有效借用记录（用于识别"归还"）：库内已无在库记录，柜内复现即归还。
    // 整条记录按编号留存：写库需要 recordId，明细展示需要工具名（识别回包不带工具名）
    QList<BorrowRecord> borrows = db::RecordDAO().findBorrowsByUser(m_userId, 200);
    QHash<QString, BorrowRecord> borrowingByCode;
    for (const BorrowRecord& b : borrows)
        if (b.status == SC::RECORD_BORROWING || b.status == SC::RECORD_OVERDUE)
            borrowingByCode[b.toolCode] = b;

    m_borrowItems.clear();
    m_returnItems.clear();
    m_alertItems.clear();

    for (const ToolService::RecogStockDiff& d : outcome.diffs) {
        switch (d.type) {
        case ToolService::RecogStockDiffType::Missing:
        case ToolService::RecogStockDiffType::Mismatch: {
            // 在库→缺失/被换：用户借出
            const StockItem st = stockByPos.value(d.position);
            BorrowItem bi;
            bi.toolId    = st.toolId;
            bi.mappingId = st.mappingId;
            bi.toolCode  = st.toolCode;
            bi.toolName  = st.toolName;
            bi.position  = d.position;
            m_borrowItems.append(bi);
            break;
        }
        case ToolService::RecogStockDiffType::Extra:
            if (borrowingByCode.contains(d.toolCode)) {
                // 用户此前借出、现柜内复现：归还；工具名以借用记录（数据库）为准，
                // 识别回包只有编号与货位、没有工具名，明细行展示不能用回包名
                const BorrowRecord rec = borrowingByCode.value(d.toolCode);
                ReturnItem ri;
                ri.recordId = rec.borrowId;
                ri.toolCode = d.toolCode;
                ri.toolName = rec.toolName;
                // 识别货位缺失时用登记货位兜底，归还记录不允许没有位置
                ri.position = d.position.isEmpty() ? codeToPos.value(d.toolCode) : d.position;
                m_returnItems.append(ri);
            } else if (!baselineCodes.contains(d.toolCode)) {
                // 库内/借用记录均无此工具：未登记工具出现在柜内
                m_alertItems.append(
                    QStringLiteral("柜内识别到未登记工具 %1（位置 %2）").arg(d.toolCode, d.position));
            }
            // 注：baselineCodes 命中的 Extra（同一在库工具被移到别处）不在此报，
            //     交由下方"错放"检测统一提示，避免与借用重复计数
            break;
        case ToolService::RecogStockDiffType::NotCovered:
        default:
            // 摄像头未覆盖该货位，无法判定，不误报借用
            break;
        }
    }

    // 归还错放（借出工具回归非登记货位）：与在库工具错放同一判定口径，
    // 由静态纯函数统一计算，便于单元测试覆盖
    m_alertItems.append(ToolService::detectReturnMisplaced(outcome.diffs, codeToPos));

    // 错放检测：识别到某在库工具但不在其登记货位
    for (const ToolService::RecogSpot& s : m_spots) {
        if (s.toolCode.isEmpty()) continue;
        if (baselineCodes.contains(s.toolCode)) {
            const QString exp = codeToPos.value(s.toolCode);
            if (!exp.isEmpty() && s.position != exp)
                m_alertItems.append(
                    QStringLiteral("工具 %1 应位于 %2，实际位于 %3（错放）").arg(s.toolCode, exp, s.position));
        }
    }

    fillResultUI();
}

void CabinetSessionDialog::fillResultUI()
{
    LayoutHelper::clearItemsAndDeleteWidgets(m_borrowListLayout);
    LayoutHelper::clearItemsAndDeleteWidgets(m_returnListLayout);
    LayoutHelper::clearItemsAndDeleteWidgets(m_alertListLayout);

    auto addRows = [](QVBoxLayout* lay, const QStringList& rows) {
        for (const QString& row : rows) {
            QLabel* label = new QLabel(QStringLiteral("· %1").arg(row));
            label->setAlignment(Qt::AlignCenter);
            label->setStyleSheet(QString("" + StyleHelper::fontSize(StyleHelper::Token::FontTitle) + " color: %1;").arg(StyleHelper::textColor()));
            lay->addWidget(label);
        }
    };

    QStringList borrowed, returned, alerts;
    for (const BorrowItem& it : m_borrowItems)
        borrowed.append(QStringLiteral("%1（%2）").arg(it.toolName, it.position));
    for (const ReturnItem& it : m_returnItems)
        returned.append(it.position.isEmpty()
                            ? it.toolName
                            : QStringLiteral("%1（%2）").arg(it.toolName, it.position));
    alerts = m_alertItems;

    bool hasChange = !borrowed.isEmpty() || !returned.isEmpty() || !alerts.isEmpty();
    m_borrowBlock->setVisible(!borrowed.isEmpty());
    m_returnBlock->setVisible(!returned.isEmpty());
    m_alertBlock->setVisible(!alerts.isEmpty());
    m_noChangeLabel->setVisible(!hasChange);
    addRows(m_borrowListLayout, borrowed);
    addRows(m_returnListLayout, returned);
    addRows(m_alertListLayout, alerts);

    QLabel* borrowTitle = m_borrowBlock->findChild<QLabel*>();
    if (borrowTitle) borrowTitle->setText(QStringLiteral("本次借用了 %1 件工具").arg(borrowed.size()));
    QLabel* returnTitle = m_returnBlock->findChild<QLabel*>();
    if (returnTitle) returnTitle->setText(QStringLiteral("本次归还了 %1 件工具").arg(returned.size()));
    QLabel* alertTitle = m_alertBlock->findChild<QLabel*>();
    if (alertTitle) alertTitle->setText(QStringLiteral("⚠ 告警：%1 处异常（错放/未登记）").arg(alerts.size()));

    // 错放≤3轮：有差异且未到上限，给出"重新开柜整理"
    m_reopenBtn->setVisible(hasChange && m_round < kMaxRounds);
    m_stack->setCurrentIndex(1);
}

// ─────────────────────────── 结果页动作 ───────────────────────────

void CabinetSessionDialog::onReopenClicked()
{
    if (m_round >= kMaxRounds) return;   // 已达上限，不再重开
    ++m_round;
    onOpenCabinetClicked();   // 重新下发开箱；开柜成功后回到开柜页，用户整理后再次关柜触发识别
}

void CabinetSessionDialog::onConfirmCloseClicked()
{
    // 借：调用既有 BorrowService（单事务写库 + 状态更新）
    if (!m_borrowItems.isEmpty()) {
        BorrowService bs;
        const QString flowNo = bs.generateFlowNo(QStringLiteral("自助借用"));
        // 预计归还时间与工具借用页同一口径：按系统参数默认借用期限（小时）自动计算，
        // 服务层拒绝空时间，自助会话无用户选时间入口，统一取默认期限
        int defaultPeriodHours = AppConfig::instance().borrowDefaultPeriod();
        if (defaultPeriodHours <= 0) defaultPeriodHours = SC::BORROW_DEFAULT_PERIOD;
        const QString returnTime = QDateTime::currentDateTime()
                                       .addSecs(defaultPeriodHours * 3600)
                                       .toString(SC::TIMESTAMP_FORMAT);
        QStringList failB;
        // flow_no 有唯一索引，整批共用一个流水号时第 2 件起必然冲突，
        // 每件工具在流水号后追加序号（-01、-02…），既保持批次关联又满足唯一约束
        const int totalBorrow = m_borrowItems.size();
        int borrowSeq = 0;
        for (const BorrowItem& it : m_borrowItems) {
            QString itemFlowNo = flowNo;
            if (totalBorrow > 1)
                itemFlowNo = QStringLiteral("%1-%2")
                                 .arg(flowNo)
                                 .arg(borrowSeq + 1, 2, 10, QChar('0'));
            ++borrowSeq;
            BorrowService::Result r = bs.borrowTool(
                m_userId, it.toolId, it.mappingId, 1,
                QStringLiteral("自助借用"), returnTime, itemFlowNo, m_machineGroupId);
            if (r.success) {
                db::RecordDAO().insertOperationLog(
                    m_userId, common::operationTypeToString(common::OperationType::Borrow), "tool", it.toolCode,
                    QStringLiteral("自助借用工具「%1」编号[%2] 位置%3").arg(it.toolName, it.toolCode, it.position));
            } else {
                qWarning() << "[CabinetSessionDialog] 借用写库失败: tool=" << it.toolCode
                           << "reason=" << r.message;
                failB.append(QStringLiteral("%1（%2）").arg(it.toolName, r.message));
            }
        }
        if (!failB.isEmpty())
            MessageDialog::showWarning(this, QStringLiteral("借用写库失败"),
                QStringLiteral("以下工具借用未成功写入，请稍后核对：\n%1").arg(failB.join("、")));
    }

    // 还：调用既有 ReturnService（整批单事务写库）
    if (!m_returnItems.isEmpty()) {
        QList<int> recIds;
        for (const ReturnItem& it : m_returnItems) recIds.append(it.recordId);
        QJsonObject info;
        info["condition"] = QStringLiteral("正常");
        ReturnService rs;
        ReturnService::Result r = rs.returnTools(recIds, m_userId, info);
        if (r.success) {
            for (const ReturnItem& it : m_returnItems) {
                // 归还日志与借用日志同口径：识别货位缺失时省略位置段，避免出现空的"位置"字样
                const QString content = it.position.isEmpty()
                    ? QStringLiteral("自助归还工具「%1」编号[%2]").arg(it.toolName, it.toolCode)
                    : QStringLiteral("自助归还工具「%1」编号[%2] 位置%3").arg(it.toolName, it.toolCode, it.position);
                db::RecordDAO().insertOperationLog(m_userId, common::operationTypeToString(common::OperationType::Return), "tool", it.toolCode, content);
            }
        } else {
            MessageDialog::showWarning(this, QStringLiteral("归还写库失败"), r.message);
        }
    }
    // 无借无还：零写库，直接回主页

    accept();
}
