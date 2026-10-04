/**
 * @file ToolBorrowPageFlow.cpp
 * @brief 工具借用-借用执行流程（确认/清单弹窗/开柜引导/人脸核验/落库执行）
 * @author 袁燕
 *
 * 本文件实现上述功能，成员函数声明见 ToolBorrowPage.h。
 */

#include "ToolBorrowPage.h"
#include "ui_ToolBorrowPage.h"
#include <QTableWidget>  // 工具/借用记录表格
#include "components/PaginationBar.h"
#include "utils/StyleHelper.h"
#include "components/DrawerOpeningDialog.h"
#include "components/VerifyAlertDialog.h"
#include "components/ResultDialog.h"
#include "services/BorrowService.h"
#include "common/AppConfig.h"       // 获取本机机组ID
#include "common/Constants.h"       // 分页常量
#include "services/AlertService.h"    // 告警闭环写入
#include "model/AlertLog.h"         // AlertLog实体
#include "db/ToolDAO.h"            // 查询机组名称和位置映射
#include "components/SoftKeyboard.h"
// 通过BorrowService访问数据
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include "components/MessageDialog.h"
#include <QDateTime>
#include <QScrollArea>
#include <QFrame>
#include <QDebug>
#include <QTabWidget>
#include <QCheckBox>
#include <QDateTimeEdit>
#include <QSet>  // 任务类型默认勾选
#include <QSpinBox>  // 数量选择
#include <QStyle>  // style()->unpolish/polish 刷新QSS属性
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QTimer>  // 勾选跳页延迟重建，避免信号回调中销毁widget
#include <QApplication>  // 退出系统
#include <QCoreApplication>  // QCoreApplication::quit

void ToolBorrowPage::onBorrowConfirm() {

    if (m_user.isEmpty()) {

        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("请先登录"));

        return;

    }

    // 必须先选择任务类型才能借用（引导用户）

    if (m_selectedTypeIds.isEmpty()) {

        MessageDialog::showWarning(this, QStringLiteral("请选择任务类型"),

            QStringLiteral("请先点击「任务类型」下拉框选择对应的任务类型，\n系统将根据任务类型智能推荐工具。"));

        return;

    }

    if (m_selectedTools.isEmpty()) {

        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("请先选择要借用的工具"));

        return;

    }



    // 从本机组在库工具列表补全选中工具的 toolCode 和 position

    // 集中在此处补全，避免改动3处分散的lambda捕获列表

    // 使用refreshAllToolTable缓存的数据，不重复查库



    {

        QJsonArray allList = m_allToolsCache.isEmpty() ? [&]() {

            BorrowService svc;

            int groupId = AppConfig::instance().localMachineGroupId();

            return svc.getAllInStockTools(1, SC::PAGE_SIZE_UNLIMITED, groupId)["list"].toArray();

        }() : m_allToolsCache;

        QHash<int, QJsonObject> toolMap;

        for (const auto& t : allList) {

            QJsonObject obj = t.toObject();

            toolMap.insert(obj["toolId"].toInt(), obj);

        }

        for (auto& sel : m_selectedTools) {

            auto it = toolMap.find(sel.toolId);

            if (it != toolMap.end()) {

                sel.toolCode = it.value()["toolCode"].toString();

                sel.position = it.value()["position"].toString();

            }

        }

    }



    // 每件工具默认借1件，m_pendingQuantity用于兼容旧逻辑

    m_pendingQuantity = 1;

    // 从系统参数自动计算归还时间（不从用户编辑的DateTimeEdit读取）

    int defaultPeriodHours = AppConfig::instance().borrowDefaultPeriod();

    if (defaultPeriodHours <= 0) defaultPeriodHours = SC::BORROW_DEFAULT_PERIOD;

    m_pendingReturnTime = QDateTime::currentDateTime().addSecs(defaultPeriodHours * 3600)

                              .toString("yyyy-MM-dd HH:mm:ss");



    // 校验借用总数量不超过系统设置的单次最大借出数量

    int maxBorrow = AppConfig::instance().borrowMaxCount();

    if (maxBorrow <= 0) maxBorrow = 5;  // 兜底默认值

    int totalQty = 0;

    for (const auto& tool : m_selectedTools) totalQty += tool.quantity;

    if (totalQty > maxBorrow) {

        MessageDialog::showWarning(this, QStringLiteral("超出借用限制"),

            QStringLiteral("系统设置单次最大借出数量为 %1 件，\n"

                           "当前已选 %2 件，超出限制 %3 件。\n\n"

                           "请减少借用数量后再试。").arg(maxBorrow).arg(totalQty).arg(totalQty - maxBorrow));

        return;

    }



    // 构建按位置展开的借用清单

    // 设计理念：一个位置(机组-柜-层-位号)=一个工具，quantity>1时需找到同名同规格的多个位置

    //   展开后每条记录对应一个具体位置，quantity恒为1。作者：袁燕

    // 从映射表自动分配in_stock位置（不从缓存匹配）

    // 每个选中工具按quantity从映射表找对应数量的in_stock位置

    m_expandedBorrowList.clear();

    {

        db::ToolDAO toolDao;

        for (const auto& sel : m_selectedTools) {

            // 从映射表查该工具的in_stock位置，取前quantity个

            QJsonArray positions = toolDao.findInStockPositions(sel.toolId, sel.quantity);

            int assigned = 0;

            for (const auto& posVal : positions) {

                QJsonObject pos = posVal.toObject();

                if (assigned >= sel.quantity) break;

                SelectedToolInfo expanded = sel;

                expanded.quantity = 1;  // 每条1件1位置

                expanded.mappingId = pos["mappingId"].toInt();

                // 记录分配的具体位置（显示给用户引导取用）

                QString posDisplay = StyleHelper::formatPosition(

                    pos["cabinetName"].toString(), pos["layer"].toString(), pos["position"].toString());

                expanded.position = posDisplay;

                m_expandedBorrowList.append(expanded);

                ++assigned;

            }

            if (assigned < sel.quantity) {

                MessageDialog::showWarning(this, QStringLiteral("可用位置不足"),

                    QStringLiteral("工具「%1」需要借用 %2 件，\n"

                                   "但映射表中在库可用位置仅有 %3 个。\n\n"

                                   "请减少借用数量后再试。")

                        .arg(sel.toolName).arg(sel.quantity).arg(assigned));

                return;

            }

        }

    }



    showBorrowConfirmDialog();

}



/**

 * 第一步：确认借用信息对话框（改造版）

 *   展示借用详情（工具名、位置、数量、借用人、归还时间），用户确认后进入"抽屉打开中"

 */

void ToolBorrowPage::showBorrowConfirmDialog() {

    QDialog* dlg = new QDialog(this);

    dlg->setWindowTitle(QStringLiteral("确认借用"));

    dlg->setFixedSize(560, qMax(460, 340 + m_expandedBorrowList.size() * 40));

    dlg->setStyleSheet(StyleHelper::dialogStyle());

    dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);



    auto* mainLayout = new QVBoxLayout(dlg);

    mainLayout->setContentsMargins(32, 28, 32, 24);

    mainLayout->setSpacing(14);



    // 标题栏

    auto* titleBar = new QHBoxLayout();

    auto* iconLabel = new QLabel(QStringLiteral("📋"));

    iconLabel->setStyleSheet("font-size:28px;background:transparent;");

    auto* titleLabel = new QLabel(QStringLiteral("待借用工具清单"));

    titleLabel->setStyleSheet(StyleHelper::dialogTitleText());

    titleBar->addWidget(iconLabel);

    titleBar->addWidget(titleLabel);

    titleBar->addStretch();

    mainLayout->addLayout(titleBar);



    // 借用详情表格用展开清单：工具名/位置/数量(每行1件1位置)

    auto* table = new QTableWidget();

    table->setColumnCount(3);

    table->setHorizontalHeaderLabels({

        QStringLiteral("工具名称"), QStringLiteral("存放位置"), QStringLiteral("借用数量")

    });

    table->setRowCount(m_expandedBorrowList.size());

    table->verticalHeader()->setVisible(false);

    table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    table->setSelectionBehavior(QAbstractItemView::SelectRows);

    table->setStyleSheet(StyleHelper::listTableStyle());

    for (int col = 0; col < 3; ++col) {

        table->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Stretch);

    }

    for (int i = 0; i < m_expandedBorrowList.size(); ++i) {

        const auto& tool = m_expandedBorrowList[i];

        table->setItem(i, 0, new QTableWidgetItem(tool.toolName));

        table->setItem(i, 1, new QTableWidgetItem(tool.position.isEmpty() ? QStringLiteral("--") : tool.position));

        table->setItem(i, 2, new QTableWidgetItem(QStringLiteral("%1 件").arg(tool.quantity)));

        table->setRowHeight(i, 38);

    }

    table->setFixedHeight(qMax(160, m_expandedBorrowList.size() * 38 + 38));

    mainLayout->addWidget(table);



    // 借用人信息 + 归还时间

    auto* infoRow = new QHBoxLayout();

    infoRow->setSpacing(24);

    auto* userInfo = new QLabel(QStringLiteral("借用人：%1（%2）")

        .arg(m_user["realName"].toString(), m_user["workNo"].toString()));

    userInfo->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontLabel, StyleHelper::textColor(), 600));

    auto* retInfo = new QLabel(QStringLiteral("预计归还：%1").arg(m_pendingReturnTime));

    retInfo->setStyleSheet("font-size:15px;color:#f57c00;font-weight:600;background:transparent;");

    infoRow->addWidget(userInfo);

    infoRow->addStretch();

    infoRow->addWidget(retInfo);

    mainLayout->addLayout(infoRow);



    // 流水号

    auto* flowInfo = new QLabel(QStringLiteral("流水单号：%1").arg(m_flowNo));

    flowInfo->setStyleSheet(StyleHelper::textStyle(StyleHelper::Token::FontSmall, StyleHelper::textMuted()));

    mainLayout->addWidget(flowInfo);



    mainLayout->addStretch();



    // 底部按钮栏

    auto* btnLayout = new QHBoxLayout();

    btnLayout->setSpacing(12);

    btnLayout->addStretch();



    auto* cancelBtn = new QPushButton(QStringLiteral("取消"));

    cancelBtn->setStyleSheet(StyleHelper::buttonDefault());

    cancelBtn->setCursor(Qt::PointingHandCursor);

    cancelBtn->setMinimumHeight(StyleHelper::Token::ControlHeight);

    cancelBtn->setMinimumWidth(110);

    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);

    btnLayout->addWidget(cancelBtn);



    auto* nextBtn = new QPushButton(QStringLiteral("下一步 →"));

    nextBtn->setStyleSheet(StyleHelper::buttonPrimary());

    nextBtn->setCursor(Qt::PointingHandCursor);

    nextBtn->setMinimumHeight(StyleHelper::Token::ControlHeight);

    nextBtn->setMinimumWidth(140);

    connect(nextBtn, &QPushButton::clicked, this, [this, dlg]() {

        dlg->accept();

        showBorrowDrawerOpeningDialog();  // 进入步骤2：抽屉打开中

    });

    btnLayout->addWidget(nextBtn);



    mainLayout->addLayout(btnLayout);

    dlg->exec();

    dlg->deleteLater();

}



/**

 * 第二步：抽屉打开中（动态旋转框）

 *   提示用户对应抽屉已打开，按清单借用工具

 * 加入待借用工具列表，便于用户按列表取用

 */

void ToolBorrowPage::showBorrowDrawerOpeningDialog() {

    // 根据借用工具数量动态调整高度，确保列表完整显示

    // 用展开清单显示，每行1件1位置

    int listCount = m_expandedBorrowList.size();



    DrawerOpeningDialog dlg(QStringLiteral("抽屉打开中"), QStringLiteral("抽屉已打开"),

        QStringLiteral("对应工具抽屉已自动打开，<br/>"

                       "请按下方清单取用工具后手动关闭抽屉再点击下一步。"),

        28, 48, this);  // [等价保留] 本页历史边距28/动画区高48

    dlg.setFixedSize(560, qMax(460, 320 + listCount * 36));



    // 待借用工具列表，便于用户按列表取用

    if (listCount > 0) {

        auto* table = new QTableWidget();

        table->setColumnCount(4);

        table->setHorizontalHeaderLabels({

            QStringLiteral("工具名称"), QStringLiteral("工具编号"),

            QStringLiteral("存放位置"), QStringLiteral("借用数量")

        });

        table->setRowCount(listCount);

        table->verticalHeader()->setVisible(false);

        table->setEditTriggers(QAbstractItemView::NoEditTriggers);

        table->setSelectionBehavior(QAbstractItemView::SelectRows);

        table->setStyleSheet(QString(

            "QTableWidget { border:1px solid #f0f0f0; background:#fff; border-radius:10px; "

            "  font-family:\"Microsoft YaHei\",sans-serif; font-size:13px; outline:none; }"

            "QTableWidget::item { padding:8px 12px; color:#333; border:none; "

            "  border-bottom:1px solid #f3f3f3; outline:none; }"

            "QTableWidget::item:selected { background:#e6f0ff; outline:none; }"

            "QHeaderView::section { background:#f8f9fb; color:#666; font-weight:600; "

            "  font-size:12px; padding:8px 12px; border:none; border-bottom:1px solid #f0f0f0; }"

        ));

        // 列宽：名称Stretch，编号Stretch，位置Stretch，数量Fixed窄列

        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

        table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);

        table->setColumnWidth(3, 80);

        table->horizontalHeader()->setStretchLastSection(false);



        for (int i = 0; i < listCount; ++i) {

            const auto& it = m_expandedBorrowList[i];

            table->setItem(i, 0, new QTableWidgetItem(it.toolName));

            table->setItem(i, 1, new QTableWidgetItem(it.toolCode.isEmpty() ? QStringLiteral("--") : it.toolCode));

            table->setItem(i, 2, new QTableWidgetItem(it.position.isEmpty() ? QStringLiteral("--") : it.position));

            table->setItem(i, 3, new QTableWidgetItem(QStringLiteral("%1 件").arg(it.quantity)));

            table->setRowHeight(i, 36);

        }

        table->setFixedHeight(qMax(120, listCount * 36 + 36));

        dlg.bodyLayout()->addWidget(table);

    }



    if (dlg.exec() == QDialog::Accepted) {

        showToolVerifyDialog();  // 进入步骤3：工具核对

    }

}



/**

 * 第三步：工具核对对话框（假异常提示）

 *   提示用户工具未正确放置/数量不符，确认后执行实际借用

 */

void ToolBorrowPage::showToolVerifyDialog() {

    // 右上角倒计时+左下角忽略+告警入库+退出系统

    // 展开清单取首个位置展示

    QString firstPosition = m_expandedBorrowList.isEmpty() ? QStringLiteral("A-01")

        : (m_expandedBorrowList.first().position.isEmpty() ? QStringLiteral("A-01") : m_expandedBorrowList.first().position);



    VerifyAlertDialog dlg(QStringLiteral("工具核对"), QStringLiteral("工具核对异常"),

        QStringLiteral(

        "检测到 <b>%1</b> 位置的工具视觉识别与清单不符，<br/>"

        "可能原因：误取相邻柜位工具、标签损坏或柜位错位。<br/><br/>"

        "请重新核对工具后点击确认，系统将完成借用登记。").arg(firstPosition),

        "font-size:14px;color:#555;line-height:1.7;background:transparent;",

        QStringLiteral("取消借用"), QStringLiteral("✓ 确认完成核对"), 160,

        AppConfig::instance().borrowReturnBuffer(), this);

    dlg.setFixedSize(520, 420);



    int result = dlg.exec();



    if (result == 2) {

        // 忽略或倒计时结束：写告警日志（闭环）

        int alertToolId = 0;

        QString alertToolCode;

        if (!m_selectedTools.isEmpty()) {

            alertToolId = m_selectedTools.first().toolId;

            alertToolCode = m_selectedTools.first().toolCode;

        }

        AlertService alertSvc;

        alertSvc.recordVerifyAlert(m_user["userId"].toInt(), alertToolId, alertToolCode,

            QStringLiteral("工具核对异常：%1位置工具视觉识别与清单不符，用户忽略告警或倒计时超时").arg(firstPosition));



        MessageDialog::showWarning(nullptr, QStringLiteral("告警已记录"),

            QStringLiteral("工具核对异常告警已记录到系统告警，页面已重置。"));

        QMetaObject::invokeMethod(this, "refresh", Qt::QueuedConnection);

        m_selectedToolIds.clear();

        m_selectedTools.clear();

    } else if (result == QDialog::Accepted) {

        executeBorrow();  // 进入步骤4：执行借用

    }

}



/**

 * 第三步：执行借用（DAO层写入数据库）

 * 支持多工具批量借用

 * 通过BorrowService执行实际借用操作，写入tool_borrow_record表

 * 成功后弹出美观的成功提示

 */

void ToolBorrowPage::executeBorrow() {

    BorrowService svc;

    // successTools存QPair<名称,数量>，成功对话框按件数显示

    QList<QPair<QString, int>> successTools;

    QStringList failTools;

    int successCount = 0;



    // 逐个工具借用

    // MySQL flow_no有UNIQUE约束，多工具共用同一flowNo会导致只有第1条插入成功

    // 修复方案：每个工具的flowNo加序号后缀(-01,-02,...)，既保持批次关联性又满足UNIQUE约束

    // 展开清单(m_expandedBorrowList)，每条对应一个位置，quantity=1

    int totalTools = m_expandedBorrowList.size();

    int seqIndex = 0;

    for (const auto& tool : m_expandedBorrowList) {

        // 生成带序号的flowNo：原flowNo + "-01" / "-02" / ...

        QString toolFlowNo = m_flowNo;

        if (totalTools > 1) {

            toolFlowNo = QStringLiteral("%1-%2")

                .arg(m_flowNo)

                .arg(seqIndex + 1, 2, 10, QChar('0'));

        }

        ++seqIndex;

        // 使用每个工具自己的 quantity，而非统一的 m_pendingQuantity

        // reason为用户选中的任务类型名称列表，保持归还页"任务类型"列与借用时一致

        QString reason = m_selectedTypeNames.isEmpty()

            ? QStringLiteral("任务借用")

            : m_selectedTypeNames.join("、");

        // 直接用展开清单中保存的mappingId，不重新查

        // 根因：原代码查"第一个in_stock位置"可能匹配到错误位置，

        // 导致借用记录mappingId与实际借用的位置不一致→数据混乱

        // 注意：展开清单时已保存mappingId，直接用

        int borrowMappingId = tool.mappingId;

        if (borrowMappingId <= 0) {

            failTools.append(QStringLiteral("%1: 位置分配异常(mappingId=0)").arg(tool.toolName));

            continue;

        }

        auto result = svc.borrowTool(m_user["userId"].toInt(), tool.toolId, borrowMappingId,

            tool.quantity, reason, m_pendingReturnTime, toolFlowNo,

            AppConfig::instance().localMachineGroupId());



        if (result.success) {

            successCount++;

            // 展开后每条quantity=1，按工具名汇总数量显示

            bool merged = false;

            for (auto& st : successTools) {

                if (st.first == tool.toolName) { st.second += tool.quantity; merged = true; break; }

            }

            if (!merged) successTools.append(qMakePair(tool.toolName, tool.quantity));

        } else {

            failTools.append(QStringLiteral("%1: %2").arg(tool.toolName, result.message));

        }

    }



    if (successCount > 0) {

        // 借用成功 - 显示美观的成功提示

        // 借用成功 - 显示美观的成功提示

        ResultDialog successDlg(QStringLiteral("借用成功"), QStringLiteral("✅"),

            QStringLiteral("借用成功！"), QStringLiteral("#43a047"), 16, this);

        successDlg.setFixedSize(460, qMax(380, 300 + successTools.size() * 30));



        // 借用详情 — 去掉绿色背景，纯文本显示工具清单，更清爽

        auto* detailFrame = new QFrame();

        detailFrame->setStyleSheet("background:transparent;border:none;");

        auto* detailLayout = new QVBoxLayout(detailFrame);

        detailLayout->setSpacing(6);

        detailLayout->setContentsMargins(0, 8, 0, 8);



        // 显示实际借用数量，而非恒定的m_pendingQuantity

        for (const auto& tool : successTools) {

            auto* toolInfo = new QLabel(QStringLiteral("✓ %1  ×%2件").arg(tool.first).arg(tool.second));

            toolInfo->setStyleSheet("font-size:15px;font-weight:600;color:#333;");

            detailLayout->addWidget(toolInfo);

        }



        if (!failTools.isEmpty()) {

            auto* failTitle = new QLabel(QStringLiteral("以下工具借用失败："));

            failTitle->setStyleSheet("font-size:14px;color:#e53935;font-weight:600;margin-top:8px;");

            detailLayout->addWidget(failTitle);

            for (const auto& fail : failTools) {

                auto* failInfo = new QLabel(QStringLiteral("✗ %1").arg(fail));

                failInfo->setStyleSheet("font-size:13px;color:#e53935;");

                detailLayout->addWidget(failInfo);

            }

        }

        successDlg.bodyLayout()->addWidget(detailFrame);



        // 提示

        successDlg.setHint(QStringLiteral("请在预计归还时间前归还，逾期将产生记录"));



        // 确定按钮 — 缩小按钮尺寸，44px，18px字体→15px，去掉过大padding

        successDlg.addFinishButtonCentered(QStringLiteral("知道了"), QString(

            "QPushButton{ background:#43a047;color:white;border:none;border-radius:10px;"

            "padding:8px 28px;font-size:15px;font-weight:600;min-height:44px;}"

            "QPushButton:hover{background:#388e3c;}"

            "QPushButton:pressed{background:#2e7d32;}"

        ));



        successDlg.exec();



        // 刷新页面数据

        refresh();

    } else {

        // 全部借用失败

        MessageDialog::showError(this, QStringLiteral("借用失败"),

            QStringLiteral("所有工具借用均失败，请重试。"));

    }

}



/** 任务类型按钮点击（显示下拉面板） */
