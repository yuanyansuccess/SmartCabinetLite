/**
 * @file RoleSelectDialog.cpp
 * @brief 登录身份选择对话框实现
 * @author 袁燕
 */
#include "RoleSelectDialog.h"
#include "utils/StyleHelper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QKeyEvent>
#include <QDebug>

/**
 * @brief 构造函数，初始化 RoleSelectDialog 对象并连接信号槽
 * @param realName 用户真实姓名
 * @param parent 父对象指针，作为 Qt 对象树父节点接管所有权
 */
RoleSelectDialog::RoleSelectDialog(const QString& realName, QWidget* parent)
    : BaseDialog(parent, 520)
    , m_realName(realName) {
    setDialogTitle(QStringLiteral("选择登录身份"));
    setMinimumHeight(460);
    m_bornTimer.start();  // 出生冷却：拦截上一界面残留的连点
    setupContent();  // 基类不自动调用，需子类显式构建内容
}

RoleSelectDialog::~RoleSelectDialog() = default;

/**
 * @brief 拦截 Esc 键，防止误触关闭弹窗
 * @param event 键盘事件
 * 本弹窗决定本次登录进入管理侧还是用户侧，Esc 关闭会被上层判定为
 * "取消登录"并自动注销，操作员看到的就是弹窗一闪而过回到登录页。
 * 因此这里吞掉 Esc，离开弹窗必须显式点击三个按钮之一。
 */
void RoleSelectDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        return;  // 吞掉 Esc，不传给基类的 reject 逻辑
    }
    BaseDialog::keyPressEvent(event);
}

/**
 * @brief RoleSelectDialog::setupContent 的业务实现
 */
void RoleSelectDialog::setupContent() {
    auto* cl = contentLayout();
    cl->setSpacing(16);

    auto* hint = new QLabel(QStringLiteral("%1，请选择本次登录身份")
                             .arg(m_realName.isEmpty() ? QStringLiteral("您好") : m_realName));
    hint->setWordWrap(true);
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontLabel,
                                               StyleHelper::textBody(), 600));
    cl->addWidget(hint);

    // 两个身份入口按钮：大尺寸触屏目标，图标+标题+说明三行结构
    auto makeChoiceBtn = [&](const QString& icon, const QString& title,
                             const QString& desc, Choice choice) -> QPushButton* {
        auto* btn = new QPushButton();
        btn->setCursor(Qt::PointingHandCursor);
        btn->setMinimumHeight(96);
        // 水平方向可伸缩：按钮宽度跟随卡片，长说明文字换行显示而不被裁切
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        // 视觉降噪：身份入口用浅色块区分层级，不加描边，
        // emoji 图标周围不再出现边框线条，悬停时以背景色变化给出反馈
        btn->setStyleSheet(
            "QPushButton{background:#f8f9fc;border:none;border-radius:14px;"
            "text-align:left;padding:14px 18px;}"
            "QPushButton:hover{background:#f0f7ff;}");
        auto* vbox = new QVBoxLayout(btn);
        vbox->setContentsMargins(0, 0, 0, 0);
        vbox->setSpacing(4);

        auto* titleLbl = new QLabel(QStringLiteral("%1  %2").arg(icon, title));
        titleLbl->setStyleSheet("font-size:19px;font-weight:700;color:#1a1a2e;background:transparent;");
        auto* descLbl = new QLabel(desc);
        descLbl->setStyleSheet("" + StyleHelper::fontSize(StyleHelper::Token::FontBody) + ";color:#888;background:transparent;");
        descLbl->setWordWrap(true);   // 宽度不足时换行，保证说明完整可读

        vbox->addWidget(titleLbl);
        vbox->addWidget(descLbl);

        connect(btn, &QPushButton::clicked, this, [this, choice]() {
            // 冷却期内忽略点击：登录页双击"确认"时第二击的释放事件会恰好
            // 落在刚弹出的本弹窗按钮上（实测弹出后185~217ms），误选身份进错首页
            if (!m_bornTimer.hasExpired(600)) {
                qDebug() << "[RoleSelectDialog] 冷却期内忽略残留点击";
                return;
            }
            m_choice = choice;
            accept();
        });
        return btn;
    };

    cl->addWidget(makeChoiceBtn(
        QStringLiteral("🛠️"), QStringLiteral("以管理员身份登录"),
        QStringLiteral("人员、工具、台账、告警等管理功能"),
        Choice::Admin));
    cl->addWidget(makeChoiceBtn(
        QStringLiteral("👤"), QStringLiteral("以普通用户身份登录"),
        QStringLiteral("借用、归还、明细查询"),
        Choice::User));

    // 底部取消按钮：放弃本次登录回到登录页
    auto* bl = buttonLayout();
    while (bl->count() > 0) {
        QLayoutItem* item = bl->takeAt(0);
        delete item;
    }
    bl->addStretch();

    auto* cancelBtn = new QPushButton(QStringLiteral("退出登录"));
    cancelBtn->setMinimumHeight(StyleHelper::Token::ControlHeightTouch);
    cancelBtn->setMinimumWidth(120);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setStyleSheet(
        "QPushButton{background:#fff;color:#666;border:2px solid #ddd;border-radius:12px;"
        "" + StyleHelper::fontSize(StyleHelper::Token::FontLabel) + ";font-weight:600;}"
        "QPushButton:hover{background:#f5f5f5;}");
    connect(cancelBtn, &QPushButton::clicked, this, [this]() {
        // 与身份按钮同冷却：防止残留连点误触"退出登录"造成弹窗一闪而过
        if (!m_bornTimer.hasExpired(600)) {
            qDebug() << "[RoleSelectDialog] 冷却期内忽略残留点击(退出登录)";
            return;
        }
        reject();
    });
    bl->addWidget(cancelBtn);
}
