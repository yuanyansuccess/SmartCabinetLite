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

RoleSelectDialog::RoleSelectDialog(const QString& realName, QWidget* parent)
    : BaseDialog(parent, 520)
    , m_realName(realName) {
    setDialogTitle(QStringLiteral("选择登录身份"));
    setMinimumHeight(460);
    setupContent();  // 基类不自动调用，需子类显式构建内容
}

RoleSelectDialog::~RoleSelectDialog() = default;

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
        btn->setStyleSheet(
            "QPushButton{background:#f8f9fc;border:2px solid #eef0f4;border-radius:14px;"
            "text-align:left;padding:14px 18px;}"
            "QPushButton:hover{border-color:#4da3ff;background:#f0f7ff;}");
        auto* vbox = new QVBoxLayout(btn);
        vbox->setContentsMargins(0, 0, 0, 0);
        vbox->setSpacing(4);

        auto* titleLbl = new QLabel(QStringLiteral("%1  %2").arg(icon, title));
        titleLbl->setStyleSheet("font-size:19px;font-weight:700;color:#1a1a2e;background:transparent;");
        auto* descLbl = new QLabel(desc);
        descLbl->setStyleSheet("font-size:14px;color:#888;background:transparent;");
        descLbl->setWordWrap(true);   // 宽度不足时换行，保证说明完整可读

        vbox->addWidget(titleLbl);
        vbox->addWidget(descLbl);

        connect(btn, &QPushButton::clicked, this, [this, choice]() {
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
        "font-size:15px;font-weight:600;}"
        "QPushButton:hover{background:#f5f5f5;}");
    connect(cancelBtn, &QPushButton::clicked, this, &RoleSelectDialog::reject);
    bl->addWidget(cancelBtn);
}
