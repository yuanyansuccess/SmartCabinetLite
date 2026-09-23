/**
 * @file AlertLogsPage.cpp
 * @brief 告警日志页面实现 — 告警列表、筛选、确认处理、统计卡片、导出
 * @author 袁燕
 */
#include "AlertLogsPage.h"
#include "utils/StyleHelper.h"
#include "controller/AlertController.h"
#include "services/SettingService.h"       // 使用SettingService::getAllAlerts()替换getMockAlerts()
#include "components/SoftKeyboard.h"
#include "components/MultiSelectFilter.h"  // 多选筛选组件
#include "components/SingleSelectFilter.h" // 通用单选筛选组件
#include "components/BaseDialog.h"         // 统一圆角对话框
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollArea>   // 详情弹窗滚动区域
#include <QGridLayout>   // 详情弹窗网格布局
#include "components/MessageDialog.h"
#include <QDateTime>
#include <QSet>          // 多选类型全选判断
#include <QStandardPaths> // 导出日志桌面路径
#include <QDir>           // 导出日志创建目录
#include <QFile>          // 导出日志写文件
#include <QTextStream>    // 导出日志CSV流
#include <QDebug>

AlertLogsPage::AlertLogsPage(QWidget* parent) : QWidget(parent) {
    setupUI();
}

void AlertLogsPage::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    // 标题栏（匹配Web版）
    auto* titleBar = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("告警日志管理"));
    title->setStyleSheet(QString("font-size:20px;font-weight:bold;color:%1;").arg(StyleHelper::textColor()));  // 22→20对齐Vue
    titleBar->addWidget(title);
    titleBar->addStretch();

    // 告警状态指示器+切换按钮（二态：正常/告警）
    // 正常态：绿色"运行正常" + "触发告警"按钮；告警态：红色"告警中" + "解除告警"按钮
    m_alarmIndicator = new QLabel(QStringLiteral("运行正常"));
    m_alarmIndicator->setFixedHeight(40);
    m_alarmIndicator->setMinimumWidth(90);
    m_alarmIndicator->setAlignment(Qt::AlignCenter);
    m_alarmIndicator->setStyleSheet(
        "font-size:14px;font-weight:700;padding:8px 18px;border-radius:10px;"
        "background:#f6ffed;color:#389e0d;"
    );
    titleBar->addWidget(m_alarmIndicator);

    // 切换按钮：正常态显示"触发告警"，告警态显示"解除告警"
    // 普通用户禁用（只可看不可点），仅管理员可操作
    m_dismissBtn = new QPushButton(QStringLiteral("触发告警"));
    m_dismissBtn->setStyleSheet(
        "QPushButton{background:#e53935;color:#fff;border:none;border-radius:10px;"
        "padding:10px 22px;font-size:14px;font-weight:700;}"
        "QPushButton:hover{background:#c62828;}"
        "QPushButton:pressed{transform:scale(0.96);}"
        "QPushButton:disabled{background:#b0b0b0;color:#e0e0e0;}"
    );
    m_dismissBtn->setCursor(Qt::PointingHandCursor);
    connect(m_dismissBtn, &QPushButton::clicked, this, &AlertLogsPage::onDismissAlarm);
    titleBar->addWidget(m_dismissBtn);

    // 导出日志按钮 [2026-06-26] 尺寸对齐人员管理"新增人员"
    m_exportBtn = new QPushButton(QStringLiteral("📥 导出日志"));
    m_exportBtn->setStyleSheet(
        "QPushButton{background:#fff;color:#4da3ff;border:2px solid #4da3ff;border-radius:10px;"
        "padding:10px 22px;font-size:14px;font-weight:700;}"
        "QPushButton:hover{background:#f0f7ff;}"
        "QPushButton:pressed{transform:scale(0.96);}"
    );
    m_exportBtn->setCursor(Qt::PointingHandCursor);
    connect(m_exportBtn, &QPushButton::clicked, this, &AlertLogsPage::onExportLogs);
    titleBar->addWidget(m_exportBtn);

    mainLayout->addLayout(titleBar);

    // 告警统计卡片（匹配Web版）
    auto* cardLayout = new QGridLayout();
    cardLayout->setSpacing(10);
    cardLayout->setContentsMargins(0, 0, 0, 0);

    // 创建统计卡片
    m_totalCard = createStatCard(QStringLiteral("告警总数"), "0", "#333");
    m_critCard = createStatCard(QStringLiteral("严重告警"), "0", "#cf1322");  // 高级深红
    m_warnCard = createStatCard(QStringLiteral("一般告警"), "0", "#fa8c16");
    m_infoCard = createStatCard(QStringLiteral("提示告警"), "0", "#4da3ff");
    m_resolvedCard = createStatCard(QStringLiteral("已处理"), "0", "#52c41a");

    cardLayout->addWidget(m_totalCard, 0, 0);
    cardLayout->addWidget(m_critCard, 0, 1);
    cardLayout->addWidget(m_warnCard, 0, 2);
    cardLayout->addWidget(m_infoCard, 0, 3);
    cardLayout->addWidget(m_resolvedCard, 0, 4);

    mainLayout->addLayout(cardLayout);

    // 筛选栏（对齐人员管理页面风格：统一高度46px，font-size:14-15px）
    auto* filterRow = new QHBoxLayout();
    filterRow->setSpacing(12);

    // 类型多选筛选 - 从数据库动态加载，不再硬编码
    m_typeFilter = new MultiSelectFilter(QStringLiteral("全部类型"), this);
    // 初始空选项，loadAlertTypes() 中从数据库填充

    connect(m_typeFilter, &MultiSelectFilter::selectionChanged, this, &AlertLogsPage::onSearch);

    // 级别筛选 - 改为CheckBox样式单选组件
    m_levelFilter = new SingleSelectFilter(QStringLiteral("全部级别"), this);
    m_levelFilter->setOptions({QStringLiteral("全部级别"), QStringLiteral("严重"), QStringLiteral("一般"), QStringLiteral("提示")});

    connect(m_levelFilter, &SingleSelectFilter::selectionChanged, this, [this](const QString&) { m_currentPage = 1; loadAlerts(); });

    // 关键词搜索框 [V6.6] 对齐UserManagementPage：外层QFrame包裹+⌨按钮
    auto* searchInputWrap = new QFrame();
    searchInputWrap->setAttribute(Qt::WA_StyledBackground, true);
    searchInputWrap->setFixedWidth(280);
    searchInputWrap->setFixedHeight(48);  // 与筛选按钮高度统一
    searchInputWrap->setStyleSheet(
        "QFrame{border:2px solid #e0e0e0;border-radius:12px;background:#fff;}"
    );
    auto* searchInputLayout = new QHBoxLayout(searchInputWrap);
    // 右内边距1px防止按钮覆盖QFrame右下角边框
    searchInputLayout->setContentsMargins(0, 0, 1, 0);
    searchInputLayout->setSpacing(0);

    m_keywordEdit = new QLineEdit();
    m_keywordEdit->setPlaceholderText(QStringLiteral("搜索工具名称..."));
    // 对齐UserManagementPage搜索框风格：无边框透明背景+padding:4px 14px
    m_keywordEdit->setStyleSheet(
        "QLineEdit{border:none;padding:0 14px;font-size:16px;background:transparent;color:#333;min-height:42px;}"
    );
    searchInputLayout->addWidget(m_keywordEdit, 1);

    auto* kbdBtn = new QPushButton(QStringLiteral("⌨"));
    kbdBtn->setFixedSize(46, 44);  // 适配48px搜索框(内部44px=48-2-2边框)
    kbdBtn->setCursor(Qt::PointingHandCursor);
    // 按钮圆角10px对齐QFrame内边距(12px外框-2px边框=10px内径)
    kbdBtn->setStyleSheet(
        "QPushButton{border:none;border-radius:0 10px 10px 0;"
        "background:#f0f2f5;font-size:22px;color:#888;}"
        "QPushButton:hover{background:#e6f0ff;color:#4da3ff;}"
    );
    connect(kbdBtn, &QPushButton::clicked, this, &AlertLogsPage::onSearchFieldClicked);
    searchInputLayout->addWidget(kbdBtn);

    m_searchBtn = new QPushButton(QStringLiteral("查询"));
    m_searchBtn->setFixedHeight(48);
    m_searchBtn->setStyleSheet(
        "QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:12px;"
        "padding:0 24px;font-size:16px;font-weight:700;}"
        "QPushButton:hover{background:#3d8ae0;}"
        "QPushButton:pressed{transform:scale(0.96);}"
    );
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    connect(m_searchBtn, &QPushButton::clicked, this, &AlertLogsPage::onSearch);

    m_resetBtn = new QPushButton(QStringLiteral("重置"));
    m_resetBtn->setFixedHeight(48);
    m_resetBtn->setStyleSheet(
        "QPushButton{background:#fff;color:#4da3ff;border:2px solid #4da3ff;border-radius:12px;"
        "padding:0 24px;font-size:16px;font-weight:700;}"
        "QPushButton:hover{background:#f0f7ff;}"
        "QPushButton:pressed{transform:scale(0.96);}"
    );
    m_resetBtn->setCursor(Qt::PointingHandCursor);
    connect(m_resetBtn, &QPushButton::clicked, this, &AlertLogsPage::onReset);

    filterRow->addWidget(m_typeFilter);
    filterRow->addWidget(m_levelFilter);
    filterRow->addWidget(searchInputWrap);  // 含软键盘按钮的搜索框
    filterRow->addWidget(m_searchBtn);
    filterRow->addWidget(m_resetBtn);
    filterRow->addStretch();
    mainLayout->addLayout(filterRow);

    // ==================== 表格面板 [V8.2 2026-06-25] 去除外阴影，保持简洁 ====================
    auto* panel = new QFrame();
    panel->setStyleSheet("QFrame#alertTablePanel{ background:white; border-radius:12px; }");
    panel->setObjectName("alertTablePanel");
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(0);

    // 精简为7列：合并"关联工具+存放位置"为"关联信息"，减少拥挤
    //   列：时间 | 级别 | 类型 | 告警内容 | 关联信息 | 借用人 | 状态 | 操作
    m_table = new QTableWidget();
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("时间"), QStringLiteral("级别"), QStringLiteral("类型"),
        QStringLiteral("告警内容"), QStringLiteral("关联信息"), QStringLiteral("借用人"),
        QStringLiteral("状态"), QStringLiteral("操作")
    });
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->setAlternatingRowColors(false);
    // 移除内联表格QSS，使用全局QSS统一表格样式（小米设计语言）
    // 前7列保持拉伸
    for (int i = 0; i < 7; i++) {
        m_table->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
    }

    // 操作列调整为180px（原130px）
    m_table->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Fixed);
    m_table->setColumnWidth(7, 180);

    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setMinimumSectionSize(60);
    
    panelLayout->addWidget(m_table, 1);

    // 分页栏：共N条在左，页码按钮在右（参考UserManagementPage样式）
    auto* pageRow = new QHBoxLayout();
    pageRow->setContentsMargins(18, 12, 18, 12);
    pageRow->setSpacing(6);

    m_totalLabel = new QLabel(QStringLiteral("共 0 条"));
    m_totalLabel->setStyleSheet("font-size:13px;color:#999;");

    m_prevBtn = new QPushButton(QStringLiteral("上一页"));
    m_prevBtn->setStyleSheet(
        "QPushButton{border:1px solid #ddd;border-radius:6px;padding:5px 12px;"
        "font-size:13px;font-weight:600;color:#555;background:#fff;min-height:30px;}"
        "QPushButton:hover{border-color:#4da3ff;color:#4da3ff;}"
        "QPushButton:disabled{opacity:0.35;}"
    );
    m_prevBtn->setCursor(Qt::PointingHandCursor);
    connect(m_prevBtn, &QPushButton::clicked, this, &AlertLogsPage::onPrevPage);

    m_nextBtn = new QPushButton(QStringLiteral("下一页"));
    m_nextBtn->setStyleSheet(
        "QPushButton{border:1px solid #ddd;border-radius:6px;padding:5px 12px;"
        "font-size:13px;font-weight:600;color:#555;background:#fff;min-height:30px;}"
        "QPushButton:hover{border-color:#4da3ff;color:#4da3ff;}"
        "QPushButton:disabled{opacity:0.35;}"
    );
    m_nextBtn->setCursor(Qt::PointingHandCursor);
    connect(m_nextBtn, &QPushButton::clicked, this, &AlertLogsPage::onNextPage);

    m_pageLabel = new QLabel(QStringLiteral("第 1 页"));
    m_pageLabel->setStyleSheet("font-size:13px;color:#999;padding:0 4px;");

    // 布局顺序：stretch | 上一页 | 第X页 | 下一页 | 共N条
    pageRow->addStretch();
    pageRow->addWidget(m_prevBtn);
    pageRow->addWidget(m_pageLabel);
    pageRow->addWidget(m_nextBtn);
    pageRow->addWidget(m_totalLabel);

    auto* pageWidget = new QWidget();
    pageWidget->setStyleSheet("border-top:1px solid #f0f0f0; background:transparent;");
    pageWidget->setLayout(pageRow);
    panelLayout->addWidget(pageWidget);
    mainLayout->addWidget(panel, 1);
}

void AlertLogsPage::refresh() {
    // 首次进入时从数据库加载告警类型列表
    if (m_typeMap.isEmpty()) loadAlertTypes();
    loadAlerts();
}

void AlertLogsPage::onSearch() {
    m_currentPage = 1;  // 搜索重置到第1页
    loadAlerts();
}

void AlertLogsPage::onReset() {
    m_typeFilter->selectAll();  // 重置为全选
    m_levelFilter->reset();     // SingleSelectFilter::reset()
    m_keywordEdit->clear();
    m_currentPage = 1;  // 重置到第1页
    loadAlerts();
}

void AlertLogsPage::loadAlerts() {
    // 从数据库sys_alert表JOIN加载真实告警数据，替换硬编码getMockAlerts()
    // 类型筛选改为type_code匹配（从数据库缓存m_typeMap获取）
    QStringList typeNames = m_typeFilter->selectedOptions();
    QStringList dbTypes;
    for (const QString& cn : typeNames) {
        // 反向查找：typeName → typeCode
        QString code = m_typeMap.key(cn, QString());
        if (!code.isEmpty()) dbTypes.append(code);
    }
    // 如果全选了则传空串（不限制类型），否则逗号拼接typeCode
    QString dbType = dbTypes.join(",");
    QStringList allTypeNames = m_typeMap.values();
    QSet<QString> selSet(typeNames.begin(), typeNames.end());
    QSet<QString> allSet(allTypeNames.begin(), allTypeNames.end());
    if (selSet == allSet || selSet.isEmpty()) dbType = "";

    // SingleSelectFilter::selectedIndex() + selectedText() 替换 QComboBox::currentIndex() + currentText()
    QString levelText = m_levelFilter->selectedIndex() > 0 ? m_levelFilter->selectedText() : "";
    QString keyword = m_keywordEdit->text().trimmed();

    SettingService svc;
    // 改为服务端分页，使用m_currentPage/m_pageSize
    QJsonObject pageResult = svc.getAllAlerts(m_currentPage, m_pageSize, dbType, levelText, keyword);
    QJsonArray list = pageResult["list"].toArray();
    m_totalRecords = pageResult["total"].toInt();

    qInfo() << "[AlertLogsPage] loadAlerts: total=" << m_totalRecords << ", listSize=" << list.size()
            << ", dbType=" << dbType << ", levelText=" << levelText << ", keyword=" << keyword;

    // 更新分页信息
    int totalPages = (m_totalRecords + m_pageSize - 1) / m_pageSize;
    m_pageLabel->setText(QStringLiteral("第 %1/%2 页").arg(m_currentPage).arg(qMax(1, totalPages)));
    m_totalLabel->setText(QStringLiteral("共 %1 条").arg(m_totalRecords));
    m_prevBtn->setEnabled(m_currentPage > 1);
    m_nextBtn->setEnabled(m_currentPage < totalPages);
    
    // 更新统计卡片（全局统计，不受翻页影响）
    updateStatCards();

    // 填充表格（7列：时间/级别/类型/告警内容/关联信息/借用人/操作）
    m_table->setRowCount(list.size());
    for (int i = 0; i < list.size(); ++i) {
        QJsonObject alert = list[i].toObject();
        
        // 列0：时间
        QString time = alert["createdAt"].toString();
        if (!time.isEmpty() && time.length() > 19) time = time.left(19);
        m_table->setItem(i, 0, new QTableWidgetItem(time));

        // 列1：级别
        QString level = alert["alertLevel"].toString();
        QString levelTextDisplay = (level == "error" || level == "crit") ? "严重" : (level == "warn") ? "一般" : "提示";
        auto* levelItem = new QTableWidgetItem(levelTextDisplay);
        levelItem->setTextAlignment(Qt::AlignCenter);
        if (level == "error" || level == "crit") {
            levelItem->setForeground(QColor("#cf1322"));
            levelItem->setBackground(QColor("#fff1f0"));
            QFont levelFont = levelItem->font();
            levelFont.setBold(true);
            levelItem->setFont(levelFont);
        } else if (level == "warn") {
            levelItem->setForeground(QColor("#d46b08"));
            levelItem->setBackground(QColor("#fff7e6"));
        } else {
            levelItem->setForeground(QColor("#096dd9"));
            levelItem->setBackground(QColor("#e6f0ff"));
        }
        m_table->setItem(i, 1, levelItem);

        // 列2：类型 [2026-06-25] 数据库已返回中文类型名，直接显示
        QString type = alert["alertType"].toString();
        m_table->setItem(i, 2, new QTableWidgetItem(type.isEmpty() ? "--" : type));

        // 列3：告警内容
        m_table->setItem(i, 3, new QTableWidgetItem(alert["content"].toString()));

        // 列4：关联信息（合并关联工具+存放位置）
        QString toolName = alert["toolName"].toString();
        if (toolName.isEmpty()) toolName = alert["toolCode"].toString();
        QString cabinet = alert["cabinetName"].toString();
        QString position = alert["position"].toString();
        if (!position.isEmpty() && position != "--") cabinet += "-" + position;
        QString relatedInfo;
        if (!toolName.isEmpty() && toolName != "--") relatedInfo = toolName;
        if (!cabinet.isEmpty() && cabinet != "--") {
            if (!relatedInfo.isEmpty()) relatedInfo += " | ";
            relatedInfo += cabinet;
        }
        if (relatedInfo.isEmpty()) relatedInfo = "--";
        m_table->setItem(i, 4, new QTableWidgetItem(relatedInfo));

        // 列5：借用人
        QString borrower = alert["borrowerName"].toString();
        if (borrower.isEmpty() || borrower == "--") borrower = "--";
        auto* borrowerItem = new QTableWidgetItem(borrower);
        borrowerItem->setForeground(QColor("#1890ff"));
        QFont borrowerFont = borrowerItem->font();
        borrowerFont.setBold(true);
        borrowerItem->setFont(borrowerFont);
        m_table->setItem(i, 5, borrowerItem);

        // 列6：状态 [2026-06-27] 新增状态列，放在借用人旁边
        QString status = alert["status"].toString();
        QString statusText, statusBg, statusColor;
        if (status == "unhandled") {
            statusText = QStringLiteral("未处理"); statusBg = "#fff1f0"; statusColor = "#cf1322";
        } else if (status == "handled") {
            statusText = QStringLiteral("已处理"); statusBg = "#f6ffed"; statusColor = "#389e0d";
        } else if (status == "ignored") {
            statusText = QStringLiteral("已忽略"); statusBg = "#f5f5f5"; statusColor = "#8c8c8c";
        } else {
            statusText = status.isEmpty() ? QStringLiteral("--") : status; statusBg = "#f5f5f5"; statusColor = "#8c8c8c";
        }
        auto* statusItem = new QTableWidgetItem(statusText);
        statusItem->setTextAlignment(Qt::AlignCenter);
        QFont statusFont = statusItem->font();
        statusFont.setBold(true);
        statusItem->setFont(statusFont);
        statusItem->setForeground(QColor(statusColor));
        statusItem->setBackground(QColor(statusBg));
        m_table->setItem(i, 6, statusItem);

        // 列7：操作（[V7.9 2026-06-24] 只保留忽略和详情，去掉确认按钮）
        int alertId = alert["alertId"].toInt();
        auto* opWidget = new QWidget();
        opWidget->setStyleSheet("background:transparent;");
        auto* opLayout = new QHBoxLayout(opWidget);
        opLayout->setContentsMargins(4, 4, 4, 4);
        opLayout->setSpacing(6);

        if (status == "unhandled") {
            // 未处理告警：显示忽略按钮+详情按钮
            // 忽略按钮仅管理员可见可操作，普通用户隐藏
            if (isAdmin()) {
                auto* ignoreBtn = new QPushButton(QStringLiteral("忽略"));
                ignoreBtn->setFixedSize(60, 34);
                ignoreBtn->setStyleSheet("QPushButton{background:#e74c3c;color:#fff;border:none;border-radius:8px;font-size:14px;font-weight:700;}QPushButton:hover{background:#c0392b;}QPushButton:pressed{background:#a93226;}");
                ignoreBtn->setCursor(Qt::PointingHandCursor);
                connect(ignoreBtn, &QPushButton::clicked, this, [this, alertId, i]
                {
                    m_table->selectRow(i);
                    onIgnore(alertId);
                });
                opLayout->addWidget(ignoreBtn);
            }
        }
        // 所有状态都显示详情按钮（含已忽略/已处理）
        {
            auto* detailBtn = new QPushButton(QStringLiteral("详情"));
            detailBtn->setFixedSize(60, 34);
            detailBtn->setStyleSheet("QPushButton{background:#8c8c8c;color:#fff;border:none;border-radius:8px;font-size:14px;font-weight:700;}QPushButton:hover{background:#666666;}QPushButton:pressed{background:#4d4d4d;}");
            detailBtn->setCursor(Qt::PointingHandCursor);
            connect(detailBtn, &QPushButton::clicked, this, [this, alertId, i] { m_table->selectRow(i); onDetail(alertId); });
            opLayout->addWidget(detailBtn);
        }
        opLayout->addStretch();
        m_table->setCellWidget(i, 7, opWidget);
        m_table->setRowHeight(i, 60);
    }
}

/**
 * 更新统计卡片（从数据库查询全局统计数据，不受分页影响）
 * 统计受当前筛选条件（类型/级别/关键词）影响，但不限页码
 */
void AlertLogsPage::updateStatCards() {
    // 构建与loadAlerts相同的筛选条件
    QStringList typeNames = m_typeFilter->selectedOptions();
    QStringList dbTypes;
    for (const QString& cn : typeNames) {
        QString code = m_typeMap.key(cn, QString());
        if (!code.isEmpty()) dbTypes.append(code);
    }
    QString dbType = dbTypes.join(",");
    QStringList allTypeNames = m_typeMap.values();
    QSet<QString> selSet(typeNames.begin(), typeNames.end());
    QSet<QString> allSet(allTypeNames.begin(), allTypeNames.end());
    if (selSet == allSet || selSet.isEmpty()) dbType = "";

    QString levelText = m_levelFilter->selectedIndex() > 0 ? m_levelFilter->selectedText() : "";
    QString keyword = m_keywordEdit->text().trimmed();

    SettingService svc;
    QJsonObject stats = svc.getAlertStats(dbType, levelText, keyword);

    auto updateCardValue = [](QFrame* card, const QString& value) {
        if (!card) return;
        QLabel* vl = card->findChild<QLabel*>();
        if (vl) vl->setText(value);
    };
    updateCardValue(m_totalCard, QString::number(stats["total"].toInt()));
    updateCardValue(m_critCard,  QString::number(stats["crit"].toInt()));
    updateCardValue(m_warnCard,  QString::number(stats["warn"].toInt()));
    updateCardValue(m_infoCard,  QString::number(stats["info"].toInt()));
    updateCardValue(m_resolvedCard, QString::number(stats["resolved"].toInt()));
}

// 从数据库加载告警类型列表，填充筛选下拉
void AlertLogsPage::loadAlertTypes() {
    SettingService svc;
    QJsonArray types = svc.getAlertTypes();
    qInfo() << "[AlertLogsPage] loadAlertTypes: got" << types.size() << "types from DB";
    if (types.isEmpty()) {
        qWarning() << "[AlertLogsPage] 告警类型列表为空，数据库可能未初始化，尝试强制刷新";
        // 可能是首次运行表还未创建，再试一次（seedBusinessData会创建表）
        return;
    }
    m_typeMap.clear();
    m_typeLevelMap.clear();
    QStringList typeNames;
    for (int i = 0; i < types.size(); ++i) {
        QJsonObject t = types[i].toObject();
        QString code = t["typeCode"].toString();
        QString name = t["typeName"].toString();
        QString level = t["alertLevel"].toString();
        m_typeMap[code] = name;
        m_typeLevelMap[code] = level;
        typeNames.append(name);
    }
    m_typeFilter->setOptions(typeNames);
    m_typeFilter->selectAll();  // 默认全选
    qInfo() << "[AlertLogsPage] loadAlertTypes done: typeNames=" << typeNames;
}

void AlertLogsPage::onAcknowledge(int alertId) {
    // [V1.00.9.1 架构修复] 通过AlertController替代直接调用db/AlertDAO —— 作者：袁燕
    AlertController ctrl;
    if (ctrl.markHandled(alertId, "admin")) {
        loadAlerts();
    } else {
        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("确认失败"));
    }
}

void AlertLogsPage::onResolve(int alertId) {
    // [V1.00.9.1 架构修复] 通过AlertController替代直接调用db/AlertDAO —— 作者：袁燕
    AlertController ctrl;
    if (ctrl.markHandled(alertId, "admin")) {
        MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("告警已处理"));
        loadAlerts();
    } else {
        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("处理失败"));
    }
}

// 忽略告警：增加确认对话框，忽略后不可恢复
void AlertLogsPage::onIgnore(int alertId) {
    qWarning() << "[AlertLogsPage] onIgnore START: alertId=" << alertId;
    if (alertId <= 0) {
        qWarning() << "[AlertLogsPage] onIgnore: invalid alertId=" << alertId;
        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("无效的告警ID"));
        return;
    }
    bool confirmed = MessageDialog::showQuestion(this,
        QStringLiteral("确认忽略"),
        QStringLiteral("确定要忽略此告警吗？\n忽略后告警状态将变为「已忽略」，记录保留不删除。"));
    if (!confirmed) {
        qWarning() << "[AlertLogsPage] onIgnore: user cancelled";
        return;
    }

    AlertController ctrl;
    bool ok = ctrl.markIgnored(alertId, "admin");
    qWarning() << "[AlertLogsPage] onIgnore RESULT: ok=" << ok << "alertId=" << alertId;
    if (ok) {
        MessageDialog::showSuccess(this, QStringLiteral("操作成功"), QStringLiteral("告警已忽略，状态已更新为「已忽略」"));
        loadAlerts();  // 重新加载列表，已忽略告警仍显示但状态变为"已忽略"
    } else {
        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("操作失败，请重试"));
    }
}

void AlertLogsPage::onDetail(int alertId) {
    SettingService svc;
    QJsonObject detail = svc.getAlertDetail(alertId);
    if (detail.isEmpty()) {
        MessageDialog::showError(this, QStringLiteral("错误"), QStringLiteral("无法获取告警详情，请稍后重试"));
        return;
    }
    showAlertDetail(detail);
}

// 告警详情弹窗：显示完整告警信息及处理情况，设计美观、触屏友好
// 改用BaseDialog统一圆角无边框风格
void AlertLogsPage::showAlertDetail(const QJsonObject& detail) {
    auto* dlg = new BaseDialog(this, 560);
    dlg->setDialogTitle(QStringLiteral("告警详情"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setMinimumHeight(480);

    auto* cl = dlg->contentLayout();
    cl->setSpacing(0);

    // ═══════════════════════════════════════
    // 标题栏：级别badge + 类型名
    // ═══════════════════════════════════════
    QString level = detail["alertLevel"].toString();
    QString type = detail["alertType"].toString();
    QString levelText, levelBg, levelColor;
    if (level == "error" || level == "crit") {
        levelText = QStringLiteral("严重告警"); levelBg = "#fff1f0"; levelColor = "#cf1322";
    } else if (level == "warn") {
        levelText = QStringLiteral("一般告警"); levelBg = "#fff7e6"; levelColor = "#d46b08";
    } else {
        levelText = QStringLiteral("提示告警"); levelBg = "#e6f0ff"; levelColor = "#096dd9";
    }

    auto* badgeRow = new QHBoxLayout();
    auto* badge = new QLabel(QString("  %1  ").arg(levelText));
    badge->setStyleSheet(QString(
        "QLabel{background:%1;color:%2;font-size:15px;font-weight:700;"
        "border-radius:10px;padding:6px 16px;font-family:\"Microsoft YaHei\",\"PingFang SC\",sans-serif;}"
    ).arg(levelBg, levelColor));
    badgeRow->addWidget(badge);

    auto* typeLabel = new QLabel(QString(" · %1").arg(type));
    typeLabel->setStyleSheet("font-size:17px;font-weight:600;color:#333;font-family:\"Microsoft YaHei\",\"PingFang SC\",sans-serif;");
    badgeRow->addWidget(typeLabel);
    badgeRow->addStretch();
    cl->addLayout(badgeRow);

    // 分割线
    auto* divider = new QFrame();
    divider->setStyleSheet("QFrame{background:#eee;min-height:1px;}");
    divider->setFixedHeight(1);
    cl->addWidget(divider);
    cl->addSpacing(4);

    // ═══════════════════════════════════════
    // 滚动内容区域
    // ═══════════════════════════════════════
    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea{border:none;background:transparent;}QScrollBar{width:6px;background:transparent;}QScrollBar::handle{background:#d0d0d0;border-radius:3px;}");

    auto* contentWidget = new QWidget();
    contentWidget->setStyleSheet("background:transparent;");
    auto* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(0, 8, 0, 12);
    contentLayout->setSpacing(0);

    // ── 告警基本信息区域 ──
    auto* secLabel = new QLabel(QStringLiteral("告警信息"));
    secLabel->setStyleSheet("font-size:16px;font-weight:700;color:#1a1a2e;font-family:\"Microsoft YaHei\",\"PingFang SC\",sans-serif;margin-bottom:12px;");
    contentLayout->addWidget(secLabel);
    contentLayout->addSpacing(10);

    auto* infoFrame = new QFrame();
    infoFrame->setStyleSheet("QFrame{background:#fafbfc;border-radius:12px;}");
    auto* infoGrid = new QGridLayout(infoFrame);
    infoGrid->setContentsMargins(14, 14, 14, 14);
    infoGrid->setSpacing(10);

    auto addInfoRow = [](QGridLayout* grid, int row, const QString& label, const QString& value, const QString& valueColor = "#333") {
        auto* lb = new QLabel(label);
        lb->setStyleSheet("font-size:14px;color:#999;font-family:\"Microsoft YaHei\",\"PingFang SC\",sans-serif;min-width:60px;");
        lb->setFixedWidth(70);
        grid->addWidget(lb, row, 0);
        auto* vl = new QLabel(value.isEmpty() ? "--" : value);
        vl->setWordWrap(true);
        vl->setStyleSheet(QString("font-size:15px;color:%1;font-weight:500;font-family:\"Microsoft YaHei\",\"PingFang SC\",sans-serif;").arg(valueColor));
        grid->addWidget(vl, row, 1);
    };

    QString createdAt = detail["createdAt"].toString();
    if (createdAt.length() > 19) createdAt = createdAt.left(19);
    QString toolInfo = detail["toolName"].toString();
    if (toolInfo == "--" || toolInfo.isEmpty()) toolInfo = QStringLiteral("未知工具");
    QString toolCode = detail["toolCode"].toString();
    if (!toolCode.isEmpty() && toolCode != "--")
        toolInfo += QString(" (编号:%1)").arg(toolCode);
    QString location = detail["cabinetName"].toString();
    QString pos = detail["position"].toString();
    if (!pos.isEmpty() && pos != "--")
        location += QString(" · %1").arg(pos);

    addInfoRow(infoGrid, 0, QStringLiteral("发生时间"), createdAt);
    addInfoRow(infoGrid, 1, QStringLiteral("告警内容"), detail["content"].toString());
    addInfoRow(infoGrid, 2, QStringLiteral("关联工具"), toolInfo);
    addInfoRow(infoGrid, 3, QStringLiteral("存放位置"), location);
    addInfoRow(infoGrid, 4, QStringLiteral("借用人"), detail["borrowerName"].toString(), "#1890ff");

    contentLayout->addWidget(infoFrame);

    // ── 处理情况区域（仅已处理或已忽略时显示）──
    QString status = detail["status"].toString();
    if (status == "handled" || status == "ignored") {
        contentLayout->addSpacing(20);

        auto* handleSecLabel = new QLabel(QStringLiteral("处理情况"));
        handleSecLabel->setStyleSheet("font-size:16px;font-weight:700;color:#1a1a2e;font-family:\"Microsoft YaHei\",\"PingFang SC\",sans-serif;margin-bottom:12px;");
        contentLayout->addWidget(handleSecLabel);
        contentLayout->addSpacing(10);

        auto* handleFrame = new QFrame();
        handleFrame->setStyleSheet("QFrame{background:#fafbfc;border-radius:12px;}");
        auto* handleGrid = new QGridLayout(handleFrame);
        handleGrid->setContentsMargins(14, 14, 14, 14);
        handleGrid->setSpacing(10);

        QString statusText, statusBg, statusColor;
        if (status == "handled") {
            statusText = QStringLiteral("已处理"); statusBg = "#f6ffed"; statusColor = "#389e0d";
        } else {
            statusText = QStringLiteral("已忽略"); statusBg = "#f5f5f5"; statusColor = "#8c8c8c";
        }
        auto* statusBadgeWidget = new QWidget();
        auto* statusBadgeLayout = new QHBoxLayout(statusBadgeWidget);
        statusBadgeLayout->setContentsMargins(0, 0, 0, 0);
        auto* statusBadge = new QLabel(QString(" %1 ").arg(statusText));
        statusBadge->setStyleSheet(QString(
            "QLabel{background:%1;color:%2;font-size:14px;font-weight:700;"
            "border-radius:8px;padding:4px 14px;font-family:\"Microsoft YaHei\",\"PingFang SC\",sans-serif;}"
        ).arg(statusBg, statusColor));
        statusBadgeLayout->addWidget(statusBadge);
        statusBadgeLayout->addStretch();

        {
            auto* lb0 = new QLabel(QStringLiteral("处理状态"));
            lb0->setStyleSheet("font-size:14px;color:#999;font-family:\"Microsoft YaHei\",\"PingFang SC\",sans-serif;min-width:60px;");
            lb0->setFixedWidth(70);
            handleGrid->addWidget(lb0, 0, 0);
            handleGrid->addWidget(statusBadgeWidget, 0, 1);
        }

        QString handledAt = detail["handledAt"].toString();
        if (handledAt.length() > 19) handledAt = handledAt.left(19);
        addInfoRow(handleGrid, 1, QStringLiteral("处理人"), detail["handlerName"].toString(), "#722ed1");
        addInfoRow(handleGrid, 2, QStringLiteral("处理时间"), handledAt);
        QString remark = detail["remark"].toString();
        if (!remark.isEmpty() && remark != "--")
            addInfoRow(handleGrid, 3, QStringLiteral("处理备注"), remark, "#666");
        else
            addInfoRow(handleGrid, 3, QStringLiteral("处理备注"), QStringLiteral("无"), "#999");

        contentLayout->addWidget(handleFrame);
    }

    contentLayout->addStretch();
    scrollArea->setWidget(contentWidget);
    cl->addWidget(scrollArea, 1);

    // 关闭按钮
    auto* closeBtn = new QPushButton(QStringLiteral("关闭"));
    closeBtn->setFixedSize(100, 44);
    closeBtn->setStyleSheet(
        "QPushButton{background:#4da3ff;color:#fff;border:none;border-radius:12px;"
        "font-size:16px;font-weight:700;font-family:\"Microsoft YaHei\",\"PingFang SC\",sans-serif;}"
        "QPushButton:hover{background:#3d8ae0;}"
    );
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::close);

    auto* btnLayout = dlg->buttonLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    btnLayout->addStretch();

    dlg->exec();
}

// ==================== 辅助函数 ====================

/**
 * 创建统计卡片（匹配Web版样式）
 */
QFrame* AlertLogsPage::createStatCard(const QString& label, const QString& value, const QString& color) {
    // 对齐ToolManagementPage统计卡片样式：font-size 34→32px，高度固定100px
    //   作者：袁燕 — 告警统计卡片与工具管理页面统一视觉风格
    auto* card = new QFrame();
    card->setFixedHeight(100);
    card->setStyleSheet(QString(
        "QFrame{ background:white; border-radius:14px; }"
    ));

    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 14, 0, 14);
    layout->setSpacing(4);

    auto* valueLabel = new QLabel(value);
    valueLabel->setStyleSheet(QString("font-size:32px;font-weight:700;line-height:1;color:%1;background:transparent;").arg(color));
    valueLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(valueLabel);

    auto* labelLabel = new QLabel(label);
    labelLabel->setStyleSheet("font-size:14px;color:#999;background:transparent;");
    labelLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(labelLabel);

    return card;
}

/**
 * [2026-06-26v11] 告警状态切换按钮：正常↔告警二态切换
 * 正常态→点击"触发告警"→变为告警态；告警态→点击"解除告警"→变为正常态
 */
void AlertLogsPage::onDismissAlarm() {
    if (m_isAlarming) {
        // 当前告警中 → 解除告警 → 恢复正常
        m_isAlarming = false;
        m_alarmIndicator->setText(QStringLiteral("运行正常"));
        m_alarmIndicator->setStyleSheet(
            "font-size:14px;font-weight:700;padding:8px 18px;border-radius:10px;"
            "background:#f6ffed;color:#389e0d;"
        );
        m_dismissBtn->setText(QStringLiteral("触发告警"));
        m_dismissBtn->setStyleSheet(
            "QPushButton{background:#e53935;color:#fff;border:none;border-radius:10px;"
            "padding:10px 22px;font-size:14px;font-weight:700;}"
            "QPushButton:hover{background:#c62828;}"
            "QPushButton:pressed{transform:scale(0.96);}"
            "QPushButton:disabled{background:#b0b0b0;color:#e0e0e0;}"
        );
        MessageDialog::showSuccess(this, QStringLiteral("成功"), QStringLiteral("警报已解除，系统恢复正常"));
    } else {
        // 当前正常 → 触发告警 → 进入告警态
        m_isAlarming = true;
        m_alarmIndicator->setText(QStringLiteral("告警中"));
        m_alarmIndicator->setStyleSheet(
            "font-size:14px;font-weight:700;padding:8px 18px;border-radius:10px;"
            "background:#fff1f0;color:#cf1322;"
        );
        m_dismissBtn->setText(QStringLiteral("解除告警"));
        m_dismissBtn->setStyleSheet(
            "QPushButton{background:#389e0d;color:#fff;border:none;border-radius:10px;"
            "padding:10px 22px;font-size:14px;font-weight:700;}"
            "QPushButton:hover{background:#237804;}"
            "QPushButton:pressed{transform:scale(0.96);}"
            "QPushButton:disabled{background:#b0b0b0;color:#e0e0e0;}"
        );
        MessageDialog::showWarning(this, QStringLiteral("告警"), QStringLiteral("系统已触发告警状态，请及时处理"));
    }
}

/**
 * [2026-06-27] 根据当前用户角色应用权限控制
 * 解除告警按钮(m_dismissBtn)：管理员可点击，普通用户禁用（只可看不可点）
 */
void AlertLogsPage::applyAdminPermission() {
    if (m_dismissBtn) {
        m_dismissBtn->setEnabled(isAdmin());
    }
}

/**
 * [2026-06-26] 导出告警日志到桌面log文件夹(CSV格式)
 */
void AlertLogsPage::onExportLogs() {
    // 获取桌面路径，创建log子目录
    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    QString logDir = desktopPath + "/log";
    QDir dir(logDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    int rowCount = m_table->rowCount();
    if (rowCount == 0) {
        MessageDialog::showWarning(this, QStringLiteral("提示"), QStringLiteral("当前没有告警数据可导出"));
        return;
    }

    // 生成带时间戳的文件名
    QString filePath = logDir + "/alert_logs_" +
        QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".csv";

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        MessageDialog::showError(this, QStringLiteral("导出失败"),
            QStringLiteral("无法创建文件: %1").arg(filePath));
        return;
    }

    QTextStream stream(&file);
    // Qt6默认UTF-8，写BOM头确保Excel正确识别中文
    stream << "\xEF\xBB\xBF";

    // 表头：时间,级别,告警类型,告警内容,关联信息,借用人,状态
    stream << QStringLiteral("时间,级别,告警类型,告警内容,关联信息,借用人,状态\n");

    // 逐行写数据
    for (int i = 0; i < rowCount; ++i) {
        auto cell = [&](int col) {
            QTableWidgetItem* it = m_table->item(i, col);
            QString text = it ? it->text() : "";
            // 字段内容含逗号时用引号包裹
            if (text.contains(",") || text.contains("\"")) {
                text.replace("\"", "\"\"");
                text = "\"" + text + "\"";
            }
            return text;
        };

        stream << cell(0) << ","   // 时间
               << cell(1) << ","   // 级别
               << cell(2) << ","   // 告警类型
               << cell(3) << ","   // 告警内容
               << cell(4) << ","   // 关联信息
               << cell(5) << ","   // 借用人
               << cell(6) << "\n"; // 状态
        // 列7为操作按钮，不导出
    }

    file.close();

    MessageDialog::showSuccess(this, QStringLiteral("导出成功"),
        QStringLiteral("已导出 %1 条告警记录\n文件位置: %2").arg(rowCount).arg(filePath));
}

// 搜索框点击弹出软键盘
void AlertLogsPage::onSearchFieldClicked() {
    if (!m_softKeyboard) {
        m_softKeyboard = new SoftKeyboard(this);
        m_softKeyboard->setMode(SoftKeyboard::ModeEn);
        connect(m_softKeyboard, &SoftKeyboard::confirmed, this, [this]() {
            m_softKeyboard->hide();
        });
    }
    QPoint pos = m_keywordEdit->mapToGlobal(QPoint(0, m_keywordEdit->height() + 4));
    m_softKeyboard->show(m_keywordEdit, pos);
}

// 分页：上一页
void AlertLogsPage::onPrevPage() {
    if (m_currentPage > 1) {
        m_currentPage--;
        loadAlerts();
    }
}

// 分页：下一页
void AlertLogsPage::onNextPage() {
    int totalPages = (m_totalRecords + m_pageSize - 1) / m_pageSize;
    if (m_currentPage < totalPages) {
        m_currentPage++;
        loadAlerts();
    }
}
