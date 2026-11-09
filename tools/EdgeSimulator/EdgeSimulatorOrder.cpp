/**
 * @file  EdgeSimulatorOrder.cpp
 * @brief 模拟器"单据模拟"页实现：清单表、本单候选、按本单生成清单
 * @author 袁燕
 */
#include "EdgeSimulator.h"

#include "../../src/common/PositionFormatter.h"
#include "../../src/common/ProtocolAlertTypes.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonDocument>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidget>
#include <QTextEdit>
#include <QVariant>
#include <QVBoxLayout>

namespace {

/// 清单表列序（与生成帧时的字段顺序一致，改一处要同步改另一处）
enum OrderColumn {
    ColLine = 0,   ///< 行号（只读）
    ColPos,        ///< 货位
    ColToolCode,   ///< 工具编号
    ColToolName,   ///< 工具名（只读）
    ColBelong,     ///< 本单归属（下拉）
    ColResult,     ///< 识别结果（下拉）
    ColConfidence, ///< 置信度（0~1，可改）
    ColAlertType,  ///< 告警类型（下拉，取值对齐上位机协议枚举）
    ColAlertLevel, ///< 告警级别（可改）
    ColReply,      ///< 是否回应（复选）
    ColCount       ///< 列数
};

/// 本单归属取值
const QString kBelongIn        = QStringLiteral("in");         ///< 本单-入库
const QString kBelongOut       = QStringLiteral("out");        ///< 本单-出库
const QString kBelongBorrowOut = QStringLiteral("borrow_out"); ///< 本单-借出转出库（免开箱）
const QString kBelongReturn    = QStringLiteral("return");     ///< 本单-归还（借出中的工具放回柜内）
const QString kBelongOther     = QStringLiteral("other");      ///< 非本单（背景行）

/// 识别结果取值
const QString kResultNormal    = QStringLiteral("normal");    ///< 正常
const QString kResultMisplaced = QStringLiteral("misplaced"); ///< 错放
const QString kResultUnknown   = QStringLiteral("unknown");   ///< 未知物品
const QString kResultMissing   = QStringLiteral("missing");   ///< 缺失
const QString kResultLowConf   = QStringLiteral("low_conf"); ///< 置信度不足
const QString kResultEmpty     = QStringLiteral("empty");     ///< 货位空（工具已取走/没放进去）

/// 表格里显示的归属文案（下拉项 data 为实际取值）
const QString kBelongLabelIn        = QStringLiteral("本单-入库");
const QString kBelongLabelOut       = QStringLiteral("本单-出库");
const QString kBelongLabelBorrowOut = QStringLiteral("本单-借出转出库");
const QString kBelongLabelReturn    = QStringLiteral("本单-归还");
const QString kBelongLabelOther     = QStringLiteral("非本单");

/// 表格里显示的识别结果文案
const QString kResultLabelNormal    = QStringLiteral("正常");
const QString kResultLabelMisplaced = QStringLiteral("错放");
const QString kResultLabelUnknown   = QStringLiteral("未知物品");
const QString kResultLabelMissing   = QStringLiteral("缺失");
const QString kResultLabelLowConf   = QStringLiteral("置信度不足");
const QString kResultLabelEmpty     = QStringLiteral("货位空");

/// 生成清单时在库行的默认置信度（0~1）
const double kGeneratedConfidence = 0.95;

/**
 * @brief 识别结果对应的协议告警类型
 * @param result 识别结果取值
 * @return 协议 alert_type；正常项与货位空返回 none
 * @note  货位空不是告警（只是这个位置没有工具），协议里用 tool_id 为空表达
 */
QString alertTypeOfResult(const QString& result)
{
    if (result == kResultMisplaced) return Proto::ALERT_TYPE_MISPLACED;
    if (result == kResultUnknown)   return Proto::ALERT_TYPE_UNKNOWN;
    if (result == kResultMissing)   return Proto::ALERT_TYPE_MISSING;
    if (result == kResultLowConf)   return Proto::ALERT_TYPE_LOW_CONFIDENCE;
    return Proto::ALERT_TYPE_NONE;
}

/**
 * @brief 给告警类型下拉填入协议枚举项
 * @param box 待填充的下拉框
 * @return 无
 * @note  显示中文说明，itemData 是协议取值；两侧共用 Proto::ALERT_TYPE_VALUES
 */
void addAlertTypeItems(QComboBox* box)
{
    for (int i = 0; i < Proto::ALERT_TYPE_VALUES.size(); ++i) {
        box->addItem(Proto::ALERT_TYPE_LABELS.at(i), Proto::ALERT_TYPE_VALUES.at(i));
    }
}

/**
 * @brief 识别结果对应的协议告警级别
 * @param result 识别结果取值
 * @return 协议 alert_level：正常项与货位空为 0，其余为 2
 */
int alertLevelOfResult(const QString& result)
{
    if (result == kResultNormal || result == kResultEmpty) {
        return 0;
    }
    return 2;
}

/**
 * @brief 识别结果对应的工具编号处理
 * @param result 识别结果取值
 * @param toolCode 原来的工具编号
 * @return 需要发回给上位机的工具编号：未知物品与货位空必须为空串
 * @note  上位机靠 tool_id 为空判断"这个货位上没有工具/认不出"，填错会判成放错工具
 */
QString toolCodeOfResult(const QString& result, const QString& toolCode)
{
    if (result == kResultUnknown || result == kResultEmpty) {
        return QString();
    }
    return toolCode;
}

}  // namespace

/**
 * @brief 用解析结果重建清单表
 * @param rows 逐行清单数据
 * @return 无。表格被整体重建，结构变化（插入/删除/生成/导入）后调用
 * @note  重建期间会屏蔽单元格改动信号，避免"填表格"被当成"用户编辑"触发联动
 */
void EdgeSimulator::fillOrderTable(const QList<CustomRow>& rows)
{
    m_frameEdited = false;
    if (!m_orderTable) {
        return;
    }
    m_tableUpdating = true;   // 屏蔽联动，避免重建时误触发
    m_orderTable->setRowCount(0);
    m_orderTable->setRowCount(rows.size());

    for (int r = 0; r < rows.size(); ++r) {
        const CustomRow& row = rows.at(r);

        QTableWidgetItem* lineItem = new QTableWidgetItem(QString::number(r + 1));
        lineItem->setFlags(lineItem->flags() & ~Qt::ItemIsEditable);
        m_orderTable->setItem(r, ColLine, lineItem);

        m_orderTable->setItem(r, ColPos, new QTableWidgetItem(row.position));
        m_orderTable->setItem(r, ColToolCode, new QTableWidgetItem(row.toolCode));
        QTableWidgetItem* nameItem = new QTableWidgetItem(row.toolName);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_orderTable->setItem(r, ColToolName, nameItem);

        // 归属下拉：本单归属决定上位机把这一行当入库位还是出库位
        auto* belongBox = new QComboBox;
        belongBox->addItem(kBelongLabelIn, kBelongIn);
        belongBox->addItem(kBelongLabelOut, kBelongOut);
        belongBox->addItem(kBelongLabelBorrowOut, kBelongBorrowOut);
        belongBox->addItem(kBelongLabelReturn, kBelongReturn);
        belongBox->addItem(kBelongLabelOther, kBelongOther);
        const int belongIndex = belongBox->findData(row.belong);
        belongBox->setCurrentIndex(belongIndex >= 0 ? belongIndex : 0);
        connect(belongBox, &QComboBox::currentIndexChanged, this, [this, r](int) {
            if (!m_tableUpdating) {
                onOrderCellChanged(r, ColBelong);
            }
        });
        m_orderTable->setCellWidget(r, ColBelong, belongBox);

        // 识别结果下拉：决定这一行回给上位机时带的告警字段与工具编号
        auto* resultBox = new QComboBox;
        resultBox->addItem(kResultLabelNormal, kResultNormal);
        resultBox->addItem(kResultLabelMisplaced, kResultMisplaced);
        resultBox->addItem(kResultLabelUnknown, kResultUnknown);
        resultBox->addItem(kResultLabelMissing, kResultMissing);
        resultBox->addItem(kResultLabelLowConf, kResultLowConf);
        resultBox->addItem(kResultLabelEmpty, kResultEmpty);
        const int resultIndex = resultBox->findData(row.result);
        resultBox->setCurrentIndex(resultIndex >= 0 ? resultIndex : 0);
        connect(resultBox, &QComboBox::currentIndexChanged, this, [this, r](int) {
            if (!m_tableUpdating) {
                onOrderCellChanged(r, ColResult);
            }
        });
        m_orderTable->setCellWidget(r, ColResult, resultBox);

        m_orderTable->setItem(r, ColConfidence, new QTableWidgetItem(QString::number(row.confidence, 'f', 2)));

        // 告警类型下拉：可手选，识别结果变化时自动带出协议默认值
        auto* alertBox = new QComboBox;
        addAlertTypeItems(alertBox);
        const QString alertValue = row.alertType.isEmpty() ? Proto::ALERT_TYPE_NONE : row.alertType;
        const int alertIndex = alertBox->findData(alertValue);
        alertBox->setCurrentIndex(alertIndex >= 0 ? alertIndex : 0);
        connect(alertBox, &QComboBox::currentIndexChanged, this, [this, r](int) {
            if (!m_tableUpdating) {
                refreshOrderStat();   // 只刷帧，不回灌联动，避免覆盖手选值
            }
        });
        m_orderTable->setCellWidget(r, ColAlertType, alertBox);

        m_orderTable->setItem(r, ColAlertLevel, new QTableWidgetItem(QString::number(row.alertLevel)));

        QTableWidgetItem* replyItem = new QTableWidgetItem();
        replyItem->setCheckState(row.reply ? Qt::Checked : Qt::Unchecked);
        replyItem->setFlags(replyItem->flags() & ~Qt::ItemIsEditable);
        m_orderTable->setItem(r, ColReply, replyItem);
    }
    m_tableUpdating = false;
    refreshOrderStat();
}

/**
 * @brief 从清单表取当前各行的数据
 * @return 逐行清单数据（与表格内容一致）
 * @note  下拉列从单元格里的控件取值，取不到时按该行其它内容兜底
 */
QList<EdgeSimulator::CustomRow> EdgeSimulator::orderRowsFromTable() const
{
    QList<CustomRow> rows;
    if (!m_orderTable) {
        return rows;
    }
    for (int r = 0; r < m_orderTable->rowCount(); ++r) {
        CustomRow row;
        row.lineNo = r + 1;
        row.position = m_orderTable->item(r, ColPos) ? m_orderTable->item(r, ColPos)->text() : QString();
        row.toolCode = m_orderTable->item(r, ColToolCode) ? m_orderTable->item(r, ColToolCode)->text() : QString();
        row.toolName = m_orderTable->item(r, ColToolName) ? m_orderTable->item(r, ColToolName)->text() : QString();

        if (auto* box = qobject_cast<QComboBox*>(m_orderTable->cellWidget(r, ColBelong))) {
            row.belong = box->currentData().toString();
        }
        if (auto* box = qobject_cast<QComboBox*>(m_orderTable->cellWidget(r, ColResult))) {
            row.result = box->currentData().toString();
        }
        if (m_orderTable->item(r, ColConfidence)) {
            bool ok = false;
            const double value = m_orderTable->item(r, ColConfidence)->text().toDouble(&ok);
            row.confidence = ok ? value : 0.0;
        }
        if (auto* box = qobject_cast<QComboBox*>(m_orderTable->cellWidget(r, ColAlertType))) {
            row.alertType = box->currentData().toString();
        }
        if (m_orderTable->item(r, ColAlertLevel)) {
            row.alertLevel = m_orderTable->item(r, ColAlertLevel)->text().toInt();
        }
        if (m_orderTable->item(r, ColReply)) {
            row.reply = m_orderTable->item(r, ColReply)->checkState() == Qt::Checked;
        }
        row.valid = !row.position.isEmpty();
        rows.append(row);
    }
    return rows;
}

/**
 * @brief 刷新清单统计文字、生成起点与顶部生效提示
 * @return 无
 * @note  统计里给出"总行数/本单行/实际发出条数"，测试人员一眼看出这次会发多少条给上位机
 */
void EdgeSimulator::refreshOrderStat()
{
    if (!m_orderTable) {
        return;
    }
    const QList<CustomRow> rows = orderRowsFromTable();
    int orderRows = 0;
    int sendRows  = 0;
    for (const CustomRow& row : rows) {
        if (row.belong != kBelongOther) {
            ++orderRows;
        }
        if (row.reply && row.valid) {
            ++sendRows;
        }
    }
    if (m_orderStat) {
        m_orderStat->setText(QStringLiteral("清单表共 %1 行（其中本单 %2 行，可发 %3 条）；发送以帧 JSON 为准%4")
                                 .arg(rows.size()).arg(orderRows).arg(sendRows)
                                 .arg(m_frameEdited ? QStringLiteral("（帧 JSON 已手改，表格变化不再覆盖）") : QString()));
    }
    if (m_frameText && !m_frameEdited) {
        loadFrameText(formatFrameText(customItemsArray()));
    }
    updateSceneHint();
}

/**
 * @brief 清单表某格被改动后的联动处理
 * @param row    改动的行号（从0开始）
 * @param column 改动的列（告警类型下拉独立刷新帧；识别结果与置信度列会带出协议默认告警类型）
 * @return 无
 * @note  识别结果变了，告警类型/级别/工具编号要跟着变：上位机靠这几个字段区分
 *        "正常/错放/未知物/缺失/置信度不足/货位空"，不改就会判错
 */
void EdgeSimulator::onOrderCellChanged(int row, int column)
{
    if (m_tableUpdating || row < 0 || row >= m_orderTable->rowCount()) {
        return;
    }
    auto* resultBox = qobject_cast<QComboBox*>(m_orderTable->cellWidget(row, ColResult));
    if (!resultBox) {
        refreshOrderStat();
        return;
    }
    const QString result = resultBox->currentData().toString();

    // 识别结果或置信度变化时带出协议默认告警类型；用户直接改下拉不回灌联动
    if (column == ColResult || column == ColConfidence) {
        if (auto* alertBox = qobject_cast<QComboBox*>(m_orderTable->cellWidget(row, ColAlertType))) {
            const int index = alertBox->findData(alertTypeOfResult(result));
            if (index >= 0 && alertBox->currentIndex() != index) {
                const QSignalBlocker blocker(alertBox);
                alertBox->setCurrentIndex(index);
            }
        }
    }
    if (QTableWidgetItem* levelItem = m_orderTable->item(row, ColAlertLevel)) {
        levelItem->setText(QString::number(alertLevelOfResult(result)));
    }
    if (QTableWidgetItem* codeItem = m_orderTable->item(row, ColToolCode)) {
        const QString newCode = toolCodeOfResult(result, codeItem->text());
        if (newCode != codeItem->text()) {
            codeItem->setText(newCode);
        }
    }
    if (column == ColConfidence) {
        if (QTableWidgetItem* confItem = m_orderTable->item(row, ColConfidence)) {
            bool ok = false;
            const double value = confItem->text().toDouble(&ok);
            if (ok && value >= 0.0 && value <= 1.0 && value < 0.85) {
                // 置信度低于门槛时自动标成"置信度不足"，与上位机判定口径一致
                if (auto* box = qobject_cast<QComboBox*>(m_orderTable->cellWidget(row, ColResult))) {
                    const int index = box->findData(kResultLowConf);
                    if (index >= 0 && box->currentIndex() != index) {
                        box->setCurrentIndex(index);
                        return;   // 下拉改动会再次触发本槽，联动由那一轮完成
                    }
                }
            }
        }
    }
    refreshOrderStat();
}

/**
 * @brief 从库读取本单候选（四类：待入库可入库位、在库工具、借出中转库工具、拟归还工具）
 * @return true=读取成功；false=连不上库或查询失败，原因已写到状态行与日志
 * @note  只做 SELECT，不改任何数据；候选项的货位按主工程同一套规则格式化，
 *        保证模拟器选出来的货位与上位机单据显示完全一致
 */
bool EdgeSimulator::loadOrderCandidates()
{
    if (!openToolDatabase()) {
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(QStringLiteral("edge_sim_tools"));
    QApplication::setOverrideCursor(Qt::WaitCursor);

    // 机组过滤：选了具体机组时，三类候选只取该机组（t 是 tool_info 别名）
    const QString gf = (m_selectedGroupId != 0)
        ? QStringLiteral(" AND t.machine_group_id = %1").arg(m_selectedGroupId)
        : QString();
    struct CandidateQuery {
        QString      sql;      ///< 取数 SQL（已拼入机组过滤条件）
        QListWidget* list;    ///< 填充到的列表
        const char* tip;      ///< 选项后缀说明
    };
    const CandidateQuery queries[] = {
        {QStringLiteral(
            "SELECT t.tool_code, t.tool_name, cb.cabinet_name, mpm.layer, mpm.position "
            "FROM tool_position_mapping mpm "
            "JOIN tool_info t ON t.tool_id = mpm.tool_id "
            "JOIN tool_cabinet cb ON cb.cabinet_id = mpm.cabinet_id "
            "WHERE mpm.status IN ('pending','checked_out')%1 "
            "ORDER BY cb.cabinet_name, mpm.layer, mpm.position").arg(gf),
         m_inList, "（可入库位置 %1）"},
        {QStringLiteral(
            "SELECT t.tool_code, t.tool_name, cb.cabinet_name, mpm.layer, mpm.position "
            "FROM tool_position_mapping mpm "
            "JOIN tool_info t ON t.tool_id = mpm.tool_id "
            "JOIN tool_cabinet cb ON cb.cabinet_id = mpm.cabinet_id "
            "WHERE mpm.status = 'in_stock'%1 "
            "ORDER BY cb.cabinet_name, mpm.layer, mpm.position").arg(gf),
         m_outList, "（当前货位 %1）"},
        // 与上位机 ToolDAO::findBorrowedOutPositions 完全对齐：借出状态写在
        // tool_position_mapping.status 上，tool_info.status 不反映单件位置状态，
        // 错用 tool_info 过滤会查不到任何借出工具（模拟器旧代码正是这个坑）
        {QStringLiteral(
            "SELECT t.tool_code, t.tool_name, cb.cabinet_name, mpm.layer, mpm.position "
            "FROM tool_position_mapping mpm "
            "JOIN tool_info t ON t.tool_id = mpm.tool_id "
            "JOIN tool_cabinet cb ON cb.cabinet_id = mpm.cabinet_id "
            "WHERE mpm.status = 'borrowed'%1 "
            "ORDER BY cb.cabinet_name, mpm.layer, mpm.position").arg(gf),
         m_borrowOutList, "（原位置 %1，借出中）"},
        // 同上：归还候选与借出转出库候选同源（都是借出中工具），过滤条件必须
        // 与上位机一致用 tool_position_mapping.status='borrowed'，不能错用 tool_info
        {QStringLiteral(
            "SELECT t.tool_code, t.tool_name, cb.cabinet_name, mpm.layer, mpm.position "
            "FROM tool_position_mapping mpm "
            "JOIN tool_info t ON t.tool_id = mpm.tool_id "
            "JOIN tool_cabinet cb ON cb.cabinet_id = mpm.cabinet_id "
            "WHERE mpm.status = 'borrowed'%1 "
            "ORDER BY cb.cabinet_name, mpm.layer, mpm.position").arg(gf),
         m_returnList, "（原位置 %1，拟归还）"}
    };

    QString err;
    int inCount = 0, outCount = 0, borrowCount = 0, returnCount = 0;
    for (const CandidateQuery& query : queries) {
        if (!query.list) {
            continue;
        }
        query.list->clear();
        QSqlQuery q(db);
        if (!q.exec(query.sql)) {
            err = q.lastError().text();
            break;
        }
        while (q.next()) {
            const QString code = q.value(0).toString();
            const QString name = q.value(1).toString();
            const QString pos = common::formatPosition(q.value(2).toString(),
                                                      q.value(3).toString(),
                                                      q.value(4).toString());
            QVariantMap data;
            data["toolCode"] = code;
            data["toolName"] = name;
            data["position"] = pos;
            auto* item = new QListWidgetItem(QStringLiteral("%1  %2  %3")
                                                  .arg(code, name, pos), query.list);
            item->setData(Qt::UserRole, data);
            item->setToolTip(QStringLiteral("编号 %1｜名称 %2｜货位 %3").arg(code, name, pos));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
        }
        if (query.list == m_inList)        inCount = query.list->count();
        if (query.list == m_outList)       outCount = query.list->count();
        if (query.list == m_borrowOutList) borrowCount = query.list->count();
        if (query.list == m_returnList)     returnCount = query.list->count();
    }
    QApplication::restoreOverrideCursor();
    QSqlDatabase::removeDatabase(QStringLiteral("edge_sim_tools"));

    if (!err.isEmpty()) {
        if (m_dbStat) {
            m_dbStat->setText(QStringLiteral("读取本单候选失败：%1").arg(err));
        }
        logEvent(QStringLiteral("读取本单候选失败：%1").arg(err));
        return false;
    }
    if (m_dbStat) {
        m_dbStat->setText(QStringLiteral("已读取：待入库 %1、在库 %2、借出中 %3、拟归还 %4（勾选后点【按本单生成清单】）")
                              .arg(inCount).arg(outCount).arg(borrowCount).arg(returnCount));
    }
    updateSceneHint();   // 顶部状态条里的数据库状态同步刷新
    logEvent(QStringLiteral("已从数据库读取本单候选：待入库 %1 条、在库 %2 条、借出中 %3 条、拟归还 %4 条")
                 .arg(inCount).arg(outCount).arg(borrowCount).arg(returnCount));
    return true;
}

/**
 * @brief 取库内全柜在库货位作为清单背景行
 * @param rows 出参：追加在库货位行（归属"非本单"，识别正常）
 * @return true=取数成功；false=连不上库或查询失败
 * @note  只做 SELECT，不改任何数据；这是"边缘机扫全柜"的真实基线
 */
bool EdgeSimulator::collectStockRows(QList<CustomRow>& rows)
{
    rows.clear();
    if (!openToolDatabase()) {
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(QStringLiteral("edge_sim_tools"));
    QSqlQuery q(db);
    QString stockSql = QStringLiteral(
        "SELECT cb.cabinet_name, mpm.layer, mpm.position, t.tool_code, t.tool_name "
        "FROM tool_position_mapping mpm "
        "JOIN tool_cabinet cb ON cb.cabinet_id = mpm.cabinet_id "
        "LEFT JOIN tool_info t ON t.tool_id = mpm.tool_id "
        "WHERE mpm.status = 'in_stock'");
    if (m_selectedGroupId != 0) {
        stockSql += QStringLiteral(" AND t.machine_group_id = %1").arg(m_selectedGroupId);
    }
    stockSql += QStringLiteral(" ORDER BY cb.cabinet_name, mpm.layer, mpm.position");
    const bool ok = q.exec(stockSql);
    if (ok) {
        while (q.next()) {
            CustomRow row;
            row.position   = common::formatPosition(q.value(0).toString(),
                                                    q.value(1).toString(),
                                                    q.value(2).toString());
            row.toolCode   = q.value(3).toString();
            row.toolName   = q.value(4).toString();
            row.belong     = kBelongOther;
            row.result     = kResultNormal;
            row.confidence = kGeneratedConfidence;
            row.alertType  = QStringLiteral("none");
            row.alertLevel = 0;
            row.reply      = true;
            rows.append(row);
        }
    }
    QSqlDatabase::removeDatabase(QStringLiteral("edge_sim_tools"));
    return ok;
}

/**
 * @brief 按勾选的候选生成本单清单
 * @return 无。生成结果直接写进清单表
 * @note  生成规则（与上位机判定口径一致）：
 *        背景行 = 库内全柜在库货位（归属"非本单"，模拟边缘机扫全柜）；
 *        本单-入库行 = 该工具的可入库位置上有这件工具、识别正常（上位机判"入库通过"）；
 *        本单-出库行 = 该货位上已经没有工具（工具编号留空、识别结果"货位空"，上位机判"已取走"）；
 *        本单-借出转出库行 = 借出中工具留在原位（上位机免开箱，不参与柜内核对）
 *        本单-归还行 = 借出中工具放回原位（柜内识别出现该工具，上位机判归还）
 */
void EdgeSimulator::generateOrderList()
{
    if (!m_orderTable) {
        return;
    }
    QList<CustomRow> rows;

    // 背景行：库内全柜在库货位
    if (m_bgBaseline && m_bgBaseline->isChecked()) {
        collectStockRows(rows);
    }

    // 货位 → 行号索引：出库行要覆盖同货位的背景行（一个货位只发一条）
    QHash<QString, int> rowByPosition;
    for (int i = 0; i < rows.size(); ++i) {
        rowByPosition.insert(rows.at(i).position, i);
    }

    // 本单-入库行：目标货位上出现该工具
    if (m_inList) {
        for (int i = 0; i < m_inList->count(); ++i) {
            QListWidgetItem* item = m_inList->item(i);
            if (!item || item->checkState() != Qt::Checked) {
                continue;
            }
            const QVariantMap data = item->data(Qt::UserRole).toMap();
            CustomRow row;
            row.position   = data.value(QStringLiteral("position")).toString();
            row.toolCode   = data.value(QStringLiteral("toolCode")).toString();
            row.toolName   = data.value(QStringLiteral("toolName")).toString();
            row.belong     = kBelongIn;
            row.result     = kResultNormal;
            row.confidence = kGeneratedConfidence;
            row.alertType  = QStringLiteral("none");
            row.alertLevel = 0;
            row.reply      = true;
            rowByPosition.insert(row.position, rows.size());
            rows.append(row);
        }
    }

    // 本单-出库行：该货位上已经没有工具
    if (m_outList) {
        for (int i = 0; i < m_outList->count(); ++i) {
            QListWidgetItem* item = m_outList->item(i);
            if (!item || item->checkState() != Qt::Checked) {
                continue;
            }
            const QVariantMap data = item->data(Qt::UserRole).toMap();
            const QString position = data.value(QStringLiteral("position")).toString();
            const int existing = rowByPosition.value(position, -1);
            if (existing >= 0) {
                CustomRow& row = rows[existing];
                row.belong     = kBelongOut;
                row.toolCode   = QString();   // 工具已取走，这个货位上认不出工具
                row.toolName   = QString();
                row.result     = kResultEmpty;
                row.confidence = kGeneratedConfidence;
                row.alertType  = QStringLiteral("none");
                row.alertLevel = 0;
                row.reply      = true;
            } else {
                CustomRow row;
                row.position   = position;
                row.belong     = kBelongOut;
                row.result     = kResultEmpty;
                row.confidence = kGeneratedConfidence;
                row.alertType  = QStringLiteral("none");
                row.alertLevel = 0;
                row.reply      = true;
                rowByPosition.insert(position, rows.size());
                rows.append(row);
            }
        }
    }

    // 本单-借出转出库行：借出中的工具仍在原位（免开箱，不参与柜内核对）
    if (m_borrowOutList) {
        for (int i = 0; i < m_borrowOutList->count(); ++i) {
            QListWidgetItem* item = m_borrowOutList->item(i);
            if (!item || item->checkState() != Qt::Checked) {
                continue;
            }
            const QVariantMap data = item->data(Qt::UserRole).toMap();
            CustomRow row;
            row.position   = data.value(QStringLiteral("position")).toString();
            row.toolCode   = data.value(QStringLiteral("toolCode")).toString();
            row.toolName   = data.value(QStringLiteral("toolName")).toString();
            row.belong     = kBelongBorrowOut;
            row.result     = kResultNormal;
            row.confidence = kGeneratedConfidence;
            row.alertType  = QStringLiteral("none");
            row.alertLevel = 0;
            row.reply      = true;
            rowByPosition.insert(row.position, rows.size());
            rows.append(row);
        }
    }

    // 本单-归还行：借出中的工具放回原位（柜内识别出现该工具，上位机判归还）
    if (m_returnList) {
        for (int i = 0; i < m_returnList->count(); ++i) {
            QListWidgetItem* item = m_returnList->item(i);
            if (!item || item->checkState() != Qt::Checked) {
                continue;
            }
            const QVariantMap data = item->data(Qt::UserRole).toMap();
            CustomRow row;
            row.position   = data.value(QStringLiteral("position")).toString();
            row.toolCode   = data.value(QStringLiteral("toolCode")).toString();
            row.toolName   = data.value(QStringLiteral("toolName")).toString();
            row.belong     = kBelongReturn;
            row.result     = kResultNormal;
            row.confidence = kGeneratedConfidence;
            row.alertType  = QStringLiteral("none");
            row.alertLevel = 0;
            row.reply      = true;
            rowByPosition.insert(row.position, rows.size());
            rows.append(row);
        }
    }

    fillOrderTable(rows);
    logEvent(QStringLiteral("已按勾选内容生成清单：共 %1 行（背景行 + 本单行），下一步确认识别回应场景为“自定义清单”")
                 .arg(rows.size()));
}

/**
 * @brief 把非出库行全部改成"识别正常"
 * @return 无
 * @note  出库行保持"货位空"不动：那一条表达的是"工具已被取走"，
 *        改成正常会让上位机判成"货位上仍有工具"，出库项就变成未通过
 */
void EdgeSimulator::setAllResultsNormal()
{
    if (!m_orderTable) {
        return;
    }
    m_tableUpdating = true;
    const int count = m_orderTable->rowCount();
    for (int r = 0; r < count; ++r) {
        auto* belongBox = qobject_cast<QComboBox*>(m_orderTable->cellWidget(r, ColBelong));
        const QString belong = belongBox ? belongBox->currentData().toString() : kBelongOther;
        if (belong == kBelongOut) {
            continue;
        }
        if (auto* resultBox = qobject_cast<QComboBox*>(m_orderTable->cellWidget(r, ColResult))) {
            const int index = resultBox->findData(kResultNormal);
            if (index >= 0) {
                resultBox->setCurrentIndex(index);
            }
        }
        if (auto* alertBox = qobject_cast<QComboBox*>(m_orderTable->cellWidget(r, ColAlertType))) {
            alertBox->setCurrentIndex(alertBox->findData(Proto::ALERT_TYPE_NONE));
        }
        if (QTableWidgetItem* levelItem = m_orderTable->item(r, ColAlertLevel)) {
            levelItem->setText(QStringLiteral("0"));
        }
        if (QTableWidgetItem* confItem = m_orderTable->item(r, ColConfidence)) {
            confItem->setText(QString::number(kGeneratedConfidence, 'f', 2));
        }
    }
    m_tableUpdating = false;
    refreshOrderStat();
    logEvent(QStringLiteral("已把非出库行统一改成“识别正常”（出库行保持货位空）"));
}

/**
 * @brief 只保留本单行：背景行取消回应
 * @return 无
 * @note  模拟"边缘机只扫了本单所在区域"的场景；背景行仍留在表里，方便看清被排除的是哪些货位
 */
void EdgeSimulator::keepOnlyOrderRows()
{
    if (!m_orderTable) {
        return;
    }
    m_tableUpdating = true;
    int hidden = 0;
    const int count = m_orderTable->rowCount();
    for (int r = 0; r < count; ++r) {
        auto* belongBox = qobject_cast<QComboBox*>(m_orderTable->cellWidget(r, ColBelong));
        const QString belong = belongBox ? belongBox->currentData().toString() : kBelongOther;
        if (belong == kBelongOther) {
            if (QTableWidgetItem* item = m_orderTable->item(r, ColReply)) {
                item->setCheckState(Qt::Unchecked);
                ++hidden;
            }
        }
    }
    m_tableUpdating = false;
    refreshOrderStat();
    logEvent(QStringLiteral("已取消 %1 条背景行的回应（模拟边缘机只扫本单区域）").arg(hidden));
}

/**
 * @brief 构建"数据库区"面板
 * @return 组装完成的面板控件
 * @note  连接参数取自 simulator.ini（本机联调库 root/root），界面默认不要求测试人员手输；
 *        需要临时换库时可直接改这里，改完点【连接并读取工具与货位】
 */
QWidget* EdgeSimulator::buildDbPanel()
{
    auto* box = new QGroupBox(QStringLiteral("数据库（与上位机同一个库；口令取自 simulator.ini，不必手输）"));
    auto* dbGrid = new QGridLayout(box);
    m_dbHost = new QLineEdit(QStringLiteral("127.0.0.1"));
    m_dbPort = new QSpinBox;
    m_dbPort->setRange(1, 65535);
    m_dbPort->setValue(3306);
    m_dbName = new QLineEdit(QStringLiteral("smart_cabinet"));
    m_dbUser = new QLineEdit(QStringLiteral("root"));
    m_dbPass = new QLineEdit(QStringLiteral("root"));
    m_dbPass->setEchoMode(QLineEdit::Password);
    m_dbPass->setPlaceholderText(QStringLiteral("数据库口令（默认 root，取自 simulator.ini）"));

    auto* btnLoadDb = new QPushButton(QStringLiteral("连接并读取工具与货位"));
    connect(btnLoadDb, &QPushButton::clicked, this, &EdgeSimulator::onLoadToolsFromDb);
    btnLoadDb->setToolTip(QStringLiteral("只做查询，不改任何数据。连不上也能继续手改帧 JSON，不影响其它功能"));

    auto* btnLoadCand = new QPushButton(QStringLiteral("读取本单候选"));
    connect(btnLoadCand, &QPushButton::clicked, this, &EdgeSimulator::onLoadOrderCandidates);
    btnLoadCand->setToolTip(QStringLiteral("读三类候选：待入库工具（可入库位置）、在库工具（当前货位）、已借出工具（原位置）"));

    dbGrid->addWidget(new QLabel(QStringLiteral("地址")), 0, 0);
    dbGrid->addWidget(m_dbHost, 0, 1);
    dbGrid->addWidget(m_dbPort, 0, 2);
    dbGrid->addWidget(new QLabel(QStringLiteral("库名")), 1, 0);
    dbGrid->addWidget(m_dbName, 1, 1);
    dbGrid->addWidget(new QLabel(QStringLiteral("账号")), 1, 2);
    dbGrid->addWidget(m_dbUser, 1, 3);
    dbGrid->addWidget(m_dbPass, 1, 4);
    dbGrid->addWidget(btnLoadDb, 0, 3, 1, 2);
    dbGrid->addWidget(btnLoadCand, 2, 3, 1, 2);

    // 机组筛选：选了具体机组后，读库生成的候选/货位/全柜基线只含该机组，
    // 避免把别的机组的货位一起推给上位机、在上位机全柜核对里被误报成异常
    m_groupCombo = new QComboBox;
    m_groupCombo->addItem(QStringLiteral("全部机组"), 0);
    m_groupCombo->setToolTip(QStringLiteral("选具体机组后，读取的候选/货位/全柜基线只含该机组；"
                                            "默认“全部机组”保持原有全量行为"));
    connect(m_groupCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                m_selectedGroupId = m_groupCombo->currentData().toInt();
                loadToolsAndPositions();   // 切换机组后按新范围刷新货位/工具下拉
            });
    dbGrid->addWidget(new QLabel(QStringLiteral("机组")), 0, 5);
    dbGrid->addWidget(m_groupCombo, 1, 5);

    m_dbStat = new QLabel(QStringLiteral("尚未读取；也可以直接在“单据模拟”页手改帧 JSON"));
    m_dbStat->setWordWrap(true);
    dbGrid->addWidget(m_dbStat, 3, 0, 1, 5);

    // 启动即读库：打开模拟器就把工具与货位读好，测试人员直接选数据，不用先点按钮
    auto* chkLoadOnStart = new QCheckBox(QStringLiteral("启动即读取工具与货位"));
    m_chkLoadOnStart = chkLoadOnStart;
    chkLoadOnStart->setChecked(m_dbLoadOnStart);
    chkLoadOnStart->setToolTip(QStringLiteral(
        "打勾：模拟器启动后按 simulator.ini 里的库连接参数自动读一次工具与货位；\n"
        "不勾：需要手动点【连接并读取工具与货位】"));
    connect(chkLoadOnStart, &QCheckBox::toggled, this, [this](bool on) { m_dbLoadOnStart = on; });
    dbGrid->addWidget(chkLoadOnStart, 4, 0, 1, 3);
    dbGrid->setColumnStretch(5, 1);
    return box;
}

/**
 * @brief 构建"单据模拟"页（页②）
 * @return 组装完成的页面控件
 * @note  页面分三块：本单配置（勾选本单内容）→ 识别清单表（唯一事实源）
 */
QWidget* EdgeSimulator::buildOrderPage()
{
    auto* page = new QWidget;
    auto* box  = new QVBoxLayout(page);

    m_sceneHint = new QLabel;
    m_sceneHint->setWordWrap(true);
    box->addWidget(m_sceneHint);

    // 识别清单反馈响应时间：只作用于识别清单回应，专测上位机超过 3 秒没响应时的提示
    auto* replyTimeBox = new QGroupBox(QStringLiteral("识别清单反馈响应时间（只作用于识别清单回应）"));
    auto* replyTimeLayout = new QHBoxLayout(replyTimeBox);
    m_delayMs = new QSpinBox;
    m_delayMs->setRange(0, 1000000);
    m_delayMs->setSingleStep(500);
    m_delayMs->setValue(200);
    m_delayMs->setSuffix(QStringLiteral(" ms"));
    m_delayMs->setToolTip(QStringLiteral(
        "0=立即回应；3000 ms=3 秒超时重发；要看到上位机“识别请求超时”失败提示须≥10000（建议30000），填4000仅触发重发但最终仍核对成功"));
    auto* replyTimeTip = new QLabel(QStringLiteral(
        "上位机 3 秒（3000 ms）收不到识别清单会重发（最多2次，共约9秒）。要看到“识别请求超时”失败提示，"
        "延迟须≥10000 ms（建议填 30000）：此时模拟器会收到3个 recognize_req，约9秒后上位机弹失败框；"
        "填 4000 仅触发一次重发、4 秒时清单到达仍核对成功；填 0 立即回应。"
        "本设置只作用于识别清单回应，开箱、版本查询、周期帧的回应时间不受影响。"));
    replyTimeTip->setWordWrap(true);
    replyTimeLayout->addWidget(new QLabel(QStringLiteral("响应时间")));
    replyTimeLayout->addWidget(m_delayMs);
    replyTimeLayout->addWidget(replyTimeTip, 1);
    box->addWidget(replyTimeBox);

    // ── 本单配置：三类候选勾选 ──
    auto* orderBox = new QGroupBox(QStringLiteral("① 本单配置（勾选本次要核对的内容，再点【按本单生成清单】）"));
    auto* orderGrid = new QGridLayout(orderBox);

    m_inList = new QListWidget;
    m_inList->setToolTip(QStringLiteral("待入库工具：勾选后生成“本单-入库”行（目标货位上出现该工具）"));
    m_inList->setMinimumHeight(90);
    m_inList->setMaximumHeight(170);
    m_outList = new QListWidget;
    m_outList->setToolTip(QStringLiteral("在库工具：勾选后生成“本单-出库”行（该货位上工具已被取走）"));
    m_outList->setMinimumHeight(90);
    m_outList->setMaximumHeight(170);
    m_borrowOutList = new QListWidget;
    m_borrowOutList->setToolTip(QStringLiteral("已借出工具：勾选后生成“本单-借出转出库”行（免开箱，不参与柜内核对）"));
    m_borrowOutList->setMinimumHeight(90);
    m_borrowOutList->setMaximumHeight(170);
    m_returnList = new QListWidget;
    m_returnList->setToolTip(QStringLiteral("已借出工具：勾选后生成“本单-归还”行（该货位上出现该工具，关柜核对判归还）"));
    m_returnList->setMinimumHeight(90);
    m_returnList->setMaximumHeight(170);

    auto* btnCand = new QPushButton(QStringLiteral("读取本单候选"));
    connect(btnCand, &QPushButton::clicked, this, &EdgeSimulator::onLoadOrderCandidates);
    auto* btnGen = new QPushButton(QStringLiteral("按本单生成清单"));
    connect(btnGen, &QPushButton::clicked, this, &EdgeSimulator::onGenerateOrderList);
    btnGen->setToolTip(QStringLiteral("按上面的勾选生成本单清单：\n"
                                       "· 本单-入库：目标货位上出现该工具（上位机判入库通过）\n"
                                       "· 本单-出库：该货位上工具已被取走（上位机判出库通过）\n"
                                       "· 本单-借出转出库：借出中的工具留在原位（免开箱）\n"
                                       "· 非本单：库内其它在库货位，模拟边缘机扫全柜"));
    m_bgBaseline = new QCheckBox(QStringLiteral("附带全柜在库背景行"));
    m_bgBaseline->setChecked(true);
    m_bgBaseline->setToolTip(QStringLiteral("勾选：清单里带上库内所有在库货位（边缘机回报全柜的真实形态）\n"
                                            "不勾：清单里只有本单相关货位"));

    orderGrid->addWidget(new QLabel(QStringLiteral("本单-入库\n（待入库工具）")), 0, 0);
    orderGrid->addWidget(m_inList, 0, 1);
    orderGrid->addWidget(new QLabel(QStringLiteral("本单-出库\n（在库工具）")), 0, 2);
    orderGrid->addWidget(m_outList, 0, 3);
    orderGrid->addWidget(new QLabel(QStringLiteral("本单-借出转出库\n（借出中工具）")), 0, 4);
    orderGrid->addWidget(m_borrowOutList, 0, 5);
    orderGrid->addWidget(new QLabel(QStringLiteral("本单-归还\n（借出中工具）")), 0, 6);
    orderGrid->addWidget(m_returnList, 0, 7);
    orderGrid->addWidget(btnCand, 1, 0, 1, 2);
    orderGrid->addWidget(btnGen, 1, 2, 1, 2);
    orderGrid->addWidget(m_bgBaseline, 1, 4, 1, 2);
    auto* returnHint = new QLabel(QStringLiteral("归还：借出中的工具放回原位，关柜核对时上位机判为归还（需该工具确为当前用户借出）"));
    returnHint->setWordWrap(true);
    orderGrid->addWidget(returnHint, 1, 6, 1, 2);
    orderGrid->setRowStretch(0, 1);
    box->addWidget(orderBox);

    // ── 识别清单表：唯一事实源 ──
    auto* listBox = new QGroupBox(QStringLiteral("② 识别清单（一行 = 一个货位的识别结果，这里只是生成起点，实际以帧 JSON 为准）"));
    auto* listLayout = new QVBoxLayout(listBox);

    m_orderTable = new QTableWidget(0, ColCount);
    m_orderTable->setHorizontalHeaderLabels(QStringList()
        << QStringLiteral("行号") << QStringLiteral("货位") << QStringLiteral("工具编号")
        << QStringLiteral("工具名") << QStringLiteral("本单归属") << QStringLiteral("识别结果")
        << QStringLiteral("置信度") << QStringLiteral("告警类型") << QStringLiteral("级别")
        << QStringLiteral("回应"));
    m_orderTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_orderTable->horizontalHeader()->setSectionResizeMode(ColToolName, QHeaderView::Stretch);
    // 下拉列给固定宽度：窗口稍窄时优先保证"本单归属/识别结果"这两列可见，工具名可压缩
    m_orderTable->setColumnWidth(ColBelong, 118);
    m_orderTable->setColumnWidth(ColResult, 96);
    m_orderTable->setColumnWidth(ColConfidence, 66);
    m_orderTable->setColumnWidth(ColAlertType, 132);
    m_orderTable->setColumnWidth(ColAlertLevel, 48);
    m_orderTable->setColumnWidth(ColReply, 44);
    m_orderTable->verticalHeader()->setVisible(false);
    m_orderTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_orderTable->setMinimumHeight(200);
    m_orderTable->setToolTip(QStringLiteral(
        "本单归属：本单-入库/本单-出库/本单-借出转出库/非本单 —— 决定上位机拿这一行跟单据的哪一项比\n"
        "识别结果：正常/错放/未知物品/缺失/置信度不足/货位空 —— 决定协议里的告警字段与工具编号\n"
        "告警类型：可下拉选，取值 none/misplaced/unknown/missing/low_confidence 与上位机一致\n"
        "回应：不勾的行不发出去（模拟这个货位没被扫到）"));
    connect(m_orderTable, &QTableWidget::cellChanged, this, &EdgeSimulator::onOrderCellChanged);
    listLayout->addWidget(m_orderTable, 1);

    auto* btnNormal = new QPushButton(QStringLiteral("全部改正常"));
    connect(btnNormal, &QPushButton::clicked, this, &EdgeSimulator::onSetAllNormal);
    btnNormal->setToolTip(QStringLiteral("把非出库行统一改成“识别正常”（出库行保持“货位空”，那是已取走的意思）"));
    auto* btnOnly = new QPushButton(QStringLiteral("只保留本单行"));
    connect(btnOnly, &QPushButton::clicked, this, &EdgeSimulator::onKeepOnlyOrderRows);
    btnOnly->setToolTip(QStringLiteral("背景行取消回应：模拟边缘机只扫了本单所在区域"));
    auto* btnClear = new QPushButton(QStringLiteral("清空清单"));
    connect(btnClear, &QPushButton::clicked, this, &EdgeSimulator::onClearOrderList);
    btnClear->setToolTip(QStringLiteral("清空后上位机收到空清单，会把整单判为“未检测到”"));
    auto* btnApply = new QPushButton(QStringLiteral("启用自定义清单"));
    connect(btnApply, &QPushButton::clicked, this, &EdgeSimulator::onCustomListActivate);
    btnApply->setToolTip(QStringLiteral("把识别回应场景切到“自定义清单”，下一条识别请求按②页帧 JSON 回"));
    auto* btnFrom = new QPushButton(QStringLiteral("载入③页场景帧"));
    connect(btnFrom, &QPushButton::clicked, this, &EdgeSimulator::onCustomListFromScene);
    auto* btnSave = new QPushButton(QStringLiteral("保存帧 JSON"));
    connect(btnSave, &QPushButton::clicked, this, &EdgeSimulator::onCustomListSaveToFile);
    auto* btnLoad = new QPushButton(QStringLiteral("载入帧 JSON"));
    connect(btnLoad, &QPushButton::clicked, this, &EdgeSimulator::onCustomListReload);
    btnSave->setToolTip(QStringLiteral("把当前帧 JSON 存成 exe 同目录 custom_frame.json"));
    btnLoad->setToolTip(QStringLiteral("从 exe 同目录 custom_frame.json 读回帧 JSON"));

    auto* btnRow1 = new QHBoxLayout;
    btnRow1->addWidget(btnNormal);
    btnRow1->addWidget(btnOnly);
    btnRow1->addWidget(btnClear);
    btnRow1->addWidget(btnApply);
    btnRow1->addStretch(1);
    listLayout->addLayout(btnRow1);
    auto* btnRebuild = new QPushButton(QStringLiteral("从表格重新生成帧"));
    btnRebuild->setToolTip(QStringLiteral("手改过帧 JSON 后，点这里才按上面表格重新生成，平时不会覆盖手改内容"));
    connect(btnRebuild, &QPushButton::clicked, this, [this] {
        refreshFrameTextFromTable();
    });

    auto* btnRow2 = new QHBoxLayout;
    btnRow2->addWidget(btnFrom);
    btnRow2->addWidget(btnSave);
    btnRow2->addWidget(btnLoad);
    btnRow2->addWidget(btnRebuild);
    btnRow2->addStretch(1);
    listLayout->addLayout(btnRow2);

    m_orderStat = new QLabel;
    listLayout->addWidget(m_orderStat);
    box->addWidget(listBox, 1);

    // ── 发送帧 JSON：这里的文字就是马上要发给上位机的帧，可自由改字段与结构 ──
    auto* frameBox = new QGroupBox(QStringLiteral("发送帧 JSON（可编辑：改结构、改字段就在这里，发出去就按这个发）"));
    auto* frameLayout = new QVBoxLayout(frameBox);
    m_frameText = new QTextEdit;
    m_frameText->setFontFamily(QStringLiteral("Consolas"));
    m_frameText->setToolTip(QStringLiteral(
        "这里的文字就是 recognize_ack 帧本体。request_id 用 {{request_id}} 占位，发送时自动回带本次编号。\n"
        "手工改过之后，上面表格的变化不会再覆盖这里；要按表格重来请点【从表格重新生成帧】。"));
    m_frameText->setMinimumHeight(240);
    connect(m_frameText, &QTextEdit::textChanged, this, [this] {
        if (!m_frameLoading) {
            m_frameEdited = true;
        }
    });
    frameLayout->addWidget(m_frameText);
    box->addWidget(frameBox, 1);

    return page;
}

/**
 * @brief 把非出库行统一改成识别正常
 * @return 无
 */
void EdgeSimulator::onSetAllNormal()
{
    setAllResultsNormal();
}

/**
 * @brief 取消背景行的回应（只保留本单行）
 * @return 无
 */
void EdgeSimulator::onKeepOnlyOrderRows()
{
    keepOnlyOrderRows();
}

/**
 * @brief 清空清单
 * @return 无。用于造"识别清单为空"场景
 */
void EdgeSimulator::onClearOrderList()
{
    fillOrderTable(QList<CustomRow>());
    logEvent(QStringLiteral("清单已清空：上位机收到空清单会把整单判为“未检测到”"));
}

/**
 * @brief 从库读取本单候选
 * @return 无。成功失败都写到状态行与日志区
 */
void EdgeSimulator::onLoadOrderCandidates()
{
    loadOrderCandidates();
}

/**
 * @brief 按勾选内容生成清单（界面按钮入口）
 * @return 无。生成结果写进清单表
 */
void EdgeSimulator::onGenerateOrderList()
{
    generateOrderList();
}
