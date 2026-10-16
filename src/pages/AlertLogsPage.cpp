/**
 * @file AlertLogsPage.cpp
 * @brief 告警日志页面实现 - 系统告警与操作告警共用：静态布局在AlertLogsPage.ui，本文件只含动态构建与业务逻辑
 * @author 袁燕
 */
#include "AlertLogsPage.h"
#include "ui_AlertLogsPage.h"
#include <QTableWidget>  // 告警日志表格
#include "common/Constants.h"
#include "utils/StyleHelper.h"
#include "services/SettingService.h"  // 分页列表数据源
#include "services/AlertService.h"    // 告警类型列表
#include "components/SoftKeyboard.h"
#include "components/MultiSelectFilter.h"  // 多选筛选组件
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSet>          // 多选类型全选判断
#include <QDebug>

AlertLogsPage::AlertLogsPage(QWidget* parent) : QWidget(parent), ui(new Ui::AlertLogsPage) {
    ui->setupUi(this);  // 静态布局来自AlertLogsPage.ui（Qt Designer可视化维护）
    setupUI();          // 动态部分：筛选组件、信号槽连接
}

AlertLogsPage::~AlertLogsPage() {
    delete ui;
}

/**
 * @brief 构建页面界面：读取 .ui 静态布局并补充动态控件
 */
void AlertLogsPage::setupUI() {
    // 桥接.ui控件（业务逻辑沿用m_成员，零改动）
    m_keywordEdit = ui->searchBar->lineEdit();
    m_keywordEdit->setPlaceholderText(QStringLiteral("搜索告警内容..."));
    ui->searchBar->setEditPadding(14);
    m_searchBtn = ui->searchBtn;
    m_resetBtn = ui->resetBtn;
    m_table = ui->table;
    m_paginationBar = ui->paginationBar;

    // 类型多选筛选（自定义组件，动态创建装入.ui槽位），选项按当前类别过滤
    m_typeFilter = new MultiSelectFilter(QStringLiteral("全部类型"), this);
    // 初始空选项，applyCategoryOptions() 中按类别填充
    connect(m_typeFilter, &MultiSelectFilter::selectionChanged, this, &AlertLogsPage::onSearch);
    ui->typeFilterSlotLayout->addWidget(m_typeFilter);

    // 级别筛选槽位不参与本页三列显示，隐藏腾出筛选栏空间
    ui->levelFilterSlot->setVisible(false);

    // 搜索框软键盘按钮
    connect(ui->searchBar->keyboardButton(), &QPushButton::clicked, this, &AlertLogsPage::onSearchFieldClicked);

    connect(m_searchBtn, &QPushButton::clicked, this, &AlertLogsPage::onSearch);
    connect(m_resetBtn, &QPushButton::clicked, this, &AlertLogsPage::onReset);

    // 表格只显示 告警时间/告警类型/告警内容 三列
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({QStringLiteral("告警时间"),
                                        QStringLiteral("告警类型"),
                                        QStringLiteral("告警内容")});
    // 列宽策略：时间/类型按内容自适应，告警内容列拉伸占满剩余宽度
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->setColumnWidth(0, 190);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setMinimumSectionSize(60);

    // 分页按钮
    connect(m_paginationBar, &PaginationBar::prevClicked, this, &AlertLogsPage::onPrevPage);
    connect(m_paginationBar, &PaginationBar::nextClicked, this, &AlertLogsPage::onNextPage);

    // 统计卡片区域不参与本页三列显示，整行隐藏
    ui->cardsContainer->setVisible(false);
}

/**
 * @brief 注入当前页面显示的告警类别
 * @param category 类别：System=系统告警，Operation=操作告警
 * @note 类别切换后标题与类型筛选选项随之更新；列表在下次 refresh() 时按新类别查询
 */
void AlertLogsPage::setCategory(Category category) {
    if (m_category == category && !m_categoryOptionsDirty) return;
    m_category = category;
    m_categoryOptionsDirty = true;
    // 页面标题随类别变化（顶部标题栏与TopBar面包屑双入口语义一致）
    ui->titleLabel->setText(category == Category::System
                            ? QStringLiteral("系统告警日志")
                            : QStringLiteral("操作告警日志"));
}

/**
 * @brief 当前类别包含的告警类型编码集合
 * @return 类别对应的 type_code 列表（单一来源为 SC::ALERT_*_TYPES 常量）
 */
QStringList AlertLogsPage::categoryTypeCodes() const {
    return (m_category == Category::System)
           ? SC::ALERT_SYSTEM_TYPES
           : SC::ALERT_OPERATION_TYPES;
}

/**
 * @brief 按当前类别重建类型筛选选项：只保留本类别包含的启用类型，默认全选
 * @note 类别切换后由 refresh() 调用；同类别重复调用会重置用户已选状态，
 *       故用 m_categoryOptionsDirty 保证一次类别切换只重建一次
 */
void AlertLogsPage::applyCategoryOptions() {
    const QStringList codes = categoryTypeCodes();
    QStringList typeNames;
    for (const QString& code : codes) {
        const QString name = m_typeMap.value(code);
        if (!name.isEmpty()) typeNames.append(name);
    }
    m_typeFilter->setOptions(typeNames);
    m_typeFilter->selectAll();
    m_categoryOptionsDirty = false;
}

/**
 * @brief 刷新页面数据与筛选选项
 */
void AlertLogsPage::refresh() {
    // 首次进入时从数据库加载告警类型列表
    if (m_typeMap.isEmpty()) loadAlertTypes();
    // 类别切换后重建类型筛选选项（只显示本类别类型）
    if (m_categoryOptionsDirty) applyCategoryOptions();
    loadAlerts();
}

/**
 * @brief 处理搜索
 */
void AlertLogsPage::onSearch() {
    m_currentPage = 1;  // 搜索重置到第1页
    loadAlerts();
}

/**
 * @brief 处理重置
 */
void AlertLogsPage::onReset() {
    m_typeFilter->selectAll();  // 重置为本类别全选
    m_keywordEdit->clear();
    m_currentPage = 1;  // 重置到第1页
    loadAlerts();
}

/**
 * @brief 构建类型筛选条件：类别约束与用户多选合并
 * @return 逗号分隔的 type_code 串（传给 DAO 的 IN 查询）；
 *         用户在本类别内全选或未选择时返回本类别全部类型编码
 */
QString AlertLogsPage::buildDbTypeFilter() const {
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
    // 全选/未选时不限定子类型，但仍保留类别约束，防止跨类别告警混入
    if (selSet == allSet || selSet.isEmpty()) dbType = categoryTypeCodes().join(",");
    return dbType;
}

/**
 * @brief 加载告警列表（三列：告警时间/告警类型/告警内容）
 */
void AlertLogsPage::loadAlerts() {
    // 从数据库sys_alert表JOIN加载真实告警数据；类型条件已含类别约束
    QString dbType = buildDbTypeFilter();
    QString keyword = m_keywordEdit->text().trimmed();

    SettingService svc;
    // 服务端分页，使用m_currentPage/m_pageSize
    QJsonObject pageResult = svc.getAllAlerts(m_currentPage, m_pageSize, dbType, QString(), keyword);
    QJsonArray list = pageResult["list"].toArray();
    m_totalRecords = pageResult["total"].toInt();

    qInfo() << "[AlertLogsPage] loadAlerts: total=" << m_totalRecords << ", listSize=" << list.size()
            << ", dbType=" << dbType << ", keyword=" << keyword;

    // 更新分页信息
    int totalPages = (m_totalRecords + m_pageSize - 1) / m_pageSize;
    m_paginationBar->setPageInfo(m_currentPage, totalPages);
    m_paginationBar->setTotalRecords(m_totalRecords);

    // 填充表格（3列：告警时间/告警类型/告警内容）
    m_table->setRowCount(list.size());
    for (int i = 0; i < list.size(); ++i) {
        QJsonObject alert = list[i].toObject();

        // 列0：告警时间
        QString time = alert["createdAt"].toString();
        if (!time.isEmpty() && time.length() > 19) time = time.left(19);
        m_table->setItem(i, 0, new QTableWidgetItem(time));

        // 列1：告警类型 数据库已返回中文类型名，直接显示
        QString type = alert["alertType"].toString();
        m_table->setItem(i, 1, new QTableWidgetItem(type.isEmpty() ? "--" : type));

        // 列2：告警内容
        m_table->setItem(i, 2, new QTableWidgetItem(alert["content"].toString()));

        m_table->setRowHeight(i, 52);
    }
}

/**
 * @brief 从数据库加载启用的告警类型，建立 类型编码→类型名 缓存
 * @note 筛选选项的类别过滤与默认全选由 applyCategoryOptions() 负责，此处只建缓存
 */
void AlertLogsPage::loadAlertTypes() {
    AlertService svc;
    QJsonArray types = svc.getAlertTypes();
    qInfo() << "[AlertLogsPage] loadAlertTypes: got" << types.size() << "types from DB";
    if (types.isEmpty()) {
        qWarning() << "[AlertLogsPage] 告警类型列表为空，数据库可能未初始化，尝试强制刷新";
        return;
    }
    m_typeMap.clear();
    for (int i = 0; i < types.size(); ++i) {
        QJsonObject t = types[i].toObject();
        m_typeMap[t["typeCode"].toString()] = t["typeName"].toString();
    }
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
