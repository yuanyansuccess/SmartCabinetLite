/**
 * @file  EdgeSimulatorOrder.cpp
 * @brief 模拟器"单据模拟"页实现：清单表、本单候选、按本单生成清单
 * @author 袁燕
 */
#include "EdgeSimulator.h"

#include "../../src/common/PositionFormatter.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidget>
#include <QTextEdit>
#include <QVariant>
#include <QVBoxLayout>

namespace {

/// 清单表列序（与序列化文本字段顺序一致，改一处要同步改另一处）
enum OrderColumn {
    ColLine = 0,   ///< 行号（只读）
    ColPos,        ///< 货位
    ColToolCode,   ///< 工具编号
    ColToolName,   ///< 工具名（只读）
    ColBelong,     ///< 本单归属（下拉）
    ColResult,     ///< 识别结果（下拉）
    ColConfidence, ///< 置信度（0~1，可改）
    ColAlertType,  ///< 告警类型（随识别结果联动，只读）
    ColAlertLevel, ///< 告警级别（可改）
    ColReply,      ///< 是否回应（复选）
    ColCount       ///< 列数
};

/// 本单归属取值
const QString kBelongIn        = QStringLiteral("in");         ///< 本单-入库
const QString kBelongOut       = QStringLiteral("out");        ///< 本单-出库
const QString kBelongBorrowOut = QStringLiteral("borrow_out"); ///< 本单-借出转出库（免开箱）
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
    if (result == kResultMisplaced) return QStringLiteral("misplaced");
    if (result == kResultUnknown)   return QStringLiteral("unknown");
    if (result == kResultMissing)   return QStringLiteral("missing");
    if (result == kResultLowConf)   return QStringLiteral("low_confidence");
    return QStringLiteral("none");
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
 * @brief 解析清单文本
 * @param text 清单文本（每行一条）
 * @return 逐行解析结果；空行与注释行（# 或 // 开头）不返回
 * @note  行格式（9列）：货位,工具编号,工具名,归属,识别结果,置信度,告警类型,告警级别,是否回应；
 *        同时兼容旧格式（5列）：货位,工具编号,置信度,告警类型,告警级别——归属按"非本单"处理，
 *        识别结果由告警类型反推，旧文件导入后不会错乱
 */
QList<EdgeSimulator::CustomRow> EdgeSimulator::parseCustomRows(const QString& text) const
{
    QList<CustomRow> rows;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\r\n|\n|\r")));
    int lineNo = 0;
    for (const QString& raw : lines) {
        ++lineNo;
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(QStringLiteral("//"))) {
            continue;
        }
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("[,，\\t]|\\s{2,}")));
        QStringList fields;
        for (const QString& part : parts) {
            fields << part.trimmed();
        }

        CustomRow row;
        row.lineNo   = lineNo;
        row.position = fields.value(0);
        row.toolCode = fields.value(1);
        row.toolName = fields.value(2);
        row.belong   = fields.value(3);
        row.result   = fields.value(4);
        const QString confText  = fields.value(5);
        row.alertType = fields.value(6);
        const QString levelText = fields.value(7);
        const QString replyText = fields.value(8);

        if (fields.size() <= 5) {
            // 旧格式：货位,工具编号,置信度,告警类型,告警级别
            const QString oldAlert = fields.value(3);
            row.belong = kBelongOther;
            if (oldAlert == QStringLiteral("misplaced"))      row.result = kResultMisplaced;
            else if (oldAlert == QStringLiteral("unknown"))   row.result = kResultUnknown;
            else if (oldAlert == QStringLiteral("missing"))   row.result = kResultMissing;
            else if (oldAlert == QStringLiteral("low_confidence")) row.result = kResultLowConf;
            else                                               row.result = kResultNormal;
            row.alertType  = alertTypeOfResult(row.result);
            row.alertLevel = fields.value(4).toInt();
        } else {
            if (row.belong.isEmpty()) {
                row.belong = kBelongOther;
            }
            if (row.result.isEmpty()) {
                row.result = kResultNormal;
            }
            row.alertLevel = levelText.toInt();
            row.reply = replyText.isEmpty() ? true : (replyText == QStringLiteral("1"));
        }

        if (row.position.isEmpty()) {
            row.note  = QStringLiteral("缺货位，不发送该行（货位是上位机比对的依据，不能为空）");
            row.valid = false;
            rows.append(row);
            continue;
        }
        if (confText.isEmpty()) {
            row.confidence = kGeneratedConfidence;
        } else {
            bool ok = false;
            const double value = confText.toDouble(&ok);
            row.confidence = ok ? value : 0.0;
            if (!ok) {
                row.note = QStringLiteral("置信度“%1”不是数字，按 0 发送").arg(confText);
            }
        }
        if (row.alertType.isEmpty()) {
            row.alertType = alertTypeOfResult(row.result);
        }
        row.valid = true;
        rows.append(row);
    }
    return rows;
}

/**
 * @brief 用解析结果重建清单表
 * @param rows 逐行清单数据
 * @return 无。表格被整体重建，结构变化（插入/删除/生成/导入）后调用
 * @note  重建期间会屏蔽单元格改动信号，避免"填表格"被当成"用户编辑"触发联动
 */
void EdgeSimulator::fillOrderTable(const QList<CustomRow>& rows)
{
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

        QTableWidgetItem* alertItem = new QTableWidgetItem(row.alertType);
        alertItem->setFlags(alertItem->flags() & ~Qt::ItemIsEditable);
        m_orderTable->setItem(r, ColAlertType, alertItem);

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
        if (m_orderTable->item(r, ColAlertType)) {
            row.alertType = m_orderTable->item(r, ColAlertType)->text();
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
 * @brief 把清单表序列化成文本
 * @return 每行一条的清单文本（9 列，逗号分隔）
 * @note  文本格式与解析格式一一对应，可直接存 ini 或写进 custom_items.txt
 */
QString EdgeSimulator::serializeOrderTable() const
{
    QStringList lines;
    const QList<CustomRow> rows = orderRowsFromTable();
    for (const CustomRow& row : rows) {
        QStringList parts;
        parts << row.position
              << row.toolCode
              << row.toolName
              << row.belong
              << row.result
              << QString::number(row.confidence, 'f', 2)
              << row.alertType
              << QString::number(row.alertLevel)
              << (row.reply ? QStringLiteral("1") : QStringLiteral("0"));
        lines << parts.join(QLatin1Char(','));
    }
    return lines.join(QLatin1Char('\n'));
}

/**
 * @brief 刷新清单统计文字、文本镜像与顶部生效提示
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
        m_orderStat->setText(QStringLiteral("清单共 %1 行（其中本单 %2 行），实际发给上位机 %3 条")
                                 .arg(rows.size()).arg(orderRows).arg(sendRows));
    }
    if (m_orderText) {
        m_orderText->setPlainText(serializeOrderTable());
    }
    updateSceneHint();
}

/**
 * @brief 用文本整体替换清单表内容
 * @param text 清单文本（9 列新格式或 5 列旧格式都能解析）
 * @return 无。解析结果直接重建清单表
 */
void EdgeSimulator::applyOrderText(const QString& text)
{
    fillOrderTable(parseCustomRows(text));
}

/**
 * @brief 清单表某格被改动后的联动处理
 * @param row    改动的行号（从0开始）
 * @param column 改动的列（归属列或识别结果列会带下拉，其余列只改数值）
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

    // 识别结果决定协议里的告警字段与工具编号
    if (QTableWidgetItem* alertItem = m_orderTable->item(row, ColAlertType)) {
        alertItem->setText(alertTypeOfResult(result));
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
 * @brief 清单表选中行变化：把该行内容回填到下方选择区
 * @return 无
 * @note  这样改单行不用重敲：选中哪行，下方选择区就显示那行，改完点【插入为新行】即可替换
 */
void EdgeSimulator::onOrderRowSelected()
{
    if (!m_orderTable || !m_pickPos || !m_pickTool) {
        return;
    }
    const int row = m_orderTable->currentRow();
    if (row < 0 || row >= m_orderTable->rowCount()) {
        return;
    }
    const QList<CustomRow> rows = orderRowsFromTable();
    if (row >= rows.size()) {
        return;
    }
    const CustomRow& current = rows.at(row);

    // 货位：能在下拉里找到就选中，找不到就照原样填进去（清单里允许手改货位）
    const int posIndex = m_pickPos->findText(current.position);
    if (posIndex >= 0) {
        m_pickPos->setCurrentIndex(posIndex);
    } else {
        m_pickPos->setCurrentIndex(0);
        m_pickPos->setEditText(current.position);
    }
    // 工具编号
    const int toolIndex = m_pickTool->findData(current.toolCode);
    if (toolIndex >= 0) {
        m_pickTool->setCurrentIndex(toolIndex);
    }
    if (m_pickConf) {
        m_pickConf->setValue(qRound(current.confidence * 100.0));
    }
    if (m_pickLevel) {
        m_pickLevel->setValue(current.alertLevel > 0 ? current.alertLevel : 1);
    }
    if (m_pickAlert) {
        int index = m_pickAlert->findData(current.alertType);
        if (index < 0) {
            index = m_pickAlert->findData(QString());
        }
        m_pickAlert->setCurrentIndex(index);
    }
}

/**
 * @brief 从库读取本单候选（三类：待入库可入库位、在库工具、已借出工具）
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

    struct CandidateQuery {
        const char* sql;      ///< 取数 SQL
        QListWidget* list;    ///< 填充到的列表
        const char* tip;      ///< 选项后缀说明
    };
    const CandidateQuery queries[] = {
        {"SELECT t.tool_code, t.tool_name, cb.cabinet_name, mpm.layer, mpm.position "
         "FROM tool_position_mapping mpm "
         "JOIN tool_info t ON t.tool_id = mpm.tool_id "
         "JOIN tool_cabinet cb ON cb.cabinet_id = mpm.cabinet_id "
         "WHERE mpm.status IN ('pending','checked_out') "
         "ORDER BY cb.cabinet_name, mpm.layer, mpm.position",
         m_inList, "（可入库位置 %1）"},
        {"SELECT t.tool_code, t.tool_name, cb.cabinet_name, mpm.layer, mpm.position "
         "FROM tool_position_mapping mpm "
         "JOIN tool_info t ON t.tool_id = mpm.tool_id "
         "JOIN tool_cabinet cb ON cb.cabinet_id = mpm.cabinet_id "
         "WHERE mpm.status = 'in_stock' "
         "ORDER BY cb.cabinet_name, mpm.layer, mpm.position",
         m_outList, "（当前货位 %1）"},
        {"SELECT t.tool_code, t.tool_name, cb.cabinet_name, mpm.layer, mpm.position "
         "FROM tool_info t "
         "JOIN tool_position_mapping mpm ON mpm.tool_id = t.tool_id "
         "JOIN tool_cabinet cb ON cb.cabinet_id = mpm.cabinet_id "
         "WHERE t.status = 'borrowed' "
         "ORDER BY cb.cabinet_name, mpm.layer, mpm.position",
         m_borrowOutList, "（原位置 %1，借出中）"}
    };

    QString err;
    int inCount = 0, outCount = 0, borrowCount = 0;
    for (const CandidateQuery& query : queries) {
        if (!query.list) {
            continue;
        }
        query.list->clear();
        QSqlQuery q(db);
        if (!q.exec(QString::fromLatin1(query.sql))) {
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
        m_dbStat->setText(QStringLiteral("已读取：待入库 %1、在库 %2、借出中 %3（勾选后点【按本单生成清单】）")
                              .arg(inCount).arg(outCount).arg(borrowCount));
    }
    updateSceneHint();   // 顶部状态条里的数据库状态同步刷新
    logEvent(QStringLiteral("已从数据库读取本单候选：待入库 %1 条、在库 %2 条、借出中 %3 条")
                 .arg(inCount).arg(outCount).arg(borrowCount));
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
    const bool ok = q.exec(QStringLiteral(
        "SELECT cb.cabinet_name, mpm.layer, mpm.position, t.tool_code, t.tool_name "
        "FROM tool_position_mapping mpm "
        "JOIN tool_cabinet cb ON cb.cabinet_id = mpm.cabinet_id "
        "LEFT JOIN tool_info t ON t.tool_id = mpm.tool_id "
        "WHERE mpm.status = 'in_stock' "
        "ORDER BY cb.cabinet_name, mpm.layer, mpm.position"));
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
        if (QTableWidgetItem* alertItem = m_orderTable->item(r, ColAlertType)) {
            alertItem->setText(QStringLiteral("none"));
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
    btnLoadDb->setToolTip(QStringLiteral("只做查询，不改任何数据。连不上也能继续手填清单，不影响其它功能"));

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

    m_dbStat = new QLabel(QStringLiteral("尚未读取；也可以直接在“单据模拟”页手填清单"));
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
 * @note  页面分三块：本单配置（勾选本单内容）→ 识别清单表（唯一事实源）→ 手动加一行（补个别货位）
 */
QWidget* EdgeSimulator::buildOrderPage()
{
    auto* page = new QWidget;
    auto* box  = new QVBoxLayout(page);

    m_sceneHint = new QLabel;
    m_sceneHint->setWordWrap(true);
    box->addWidget(m_sceneHint);

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
    orderGrid->addWidget(btnCand, 1, 0, 1, 2);
    orderGrid->addWidget(btnGen, 1, 2, 1, 2);
    orderGrid->addWidget(m_bgBaseline, 1, 4, 1, 2);
    orderGrid->setRowStretch(0, 1);
    box->addWidget(orderBox);

    // ── 识别清单表：唯一事实源 ──
    auto* listBox = new QGroupBox(QStringLiteral("② 识别清单（一行 = 一个货位的识别结果，表格里所见就是上位机收到的）"));
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
    m_orderTable->setColumnWidth(ColAlertType, 96);
    m_orderTable->setColumnWidth(ColAlertLevel, 48);
    m_orderTable->setColumnWidth(ColReply, 44);
    m_orderTable->verticalHeader()->setVisible(false);
    m_orderTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_orderTable->setMinimumHeight(200);
    m_orderTable->setToolTip(QStringLiteral(
        "本单归属：本单-入库/本单-出库/本单-借出转出库/非本单 —— 决定上位机拿这一行跟单据的哪一项比\n"
        "识别结果：正常/错放/未知物品/缺失/置信度不足/货位空 —— 决定协议里的告警字段与工具编号\n"
        "回应：不勾的行不发出去（模拟这个货位没被扫到）"));
    connect(m_orderTable, &QTableWidget::cellChanged, this, &EdgeSimulator::onOrderCellChanged);
    connect(m_orderTable, &QTableWidget::currentCellChanged, this,
            [this](int, int, int, int) { onOrderRowSelected(); });
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
    btnApply->setToolTip(QStringLiteral("把识别回应场景切到“自定义清单”，下一条识别请求就按这张表回"));
    auto* btnFrom = new QPushButton(QStringLiteral("取当前场景样例"));
    connect(btnFrom, &QPushButton::clicked, this, &EdgeSimulator::onCustomListFromScene);
    auto* btnSave = new QPushButton(QStringLiteral("保存到 custom_items.txt"));
    connect(btnSave, &QPushButton::clicked, this, &EdgeSimulator::onCustomListSaveToFile);
    auto* btnLoad = new QPushButton(QStringLiteral("从 custom_items.txt 重载"));
    connect(btnLoad, &QPushButton::clicked, this, &EdgeSimulator::onCustomListReload);
    btnSave->setToolTip(QStringLiteral("把这张表存成 exe 同目录 custom_items.txt（UTF-8），可用 Excel 编辑后再重载"));
    btnLoad->setToolTip(QStringLiteral("从 exe 同目录 custom_items.txt 读回清单；文件不存在时提示先保存生成模板"));

    auto* btnRow1 = new QHBoxLayout;
    btnRow1->addWidget(btnNormal);
    btnRow1->addWidget(btnOnly);
    btnRow1->addWidget(btnClear);
    btnRow1->addWidget(btnApply);
    btnRow1->addStretch(1);
    listLayout->addLayout(btnRow1);
    auto* btnRow2 = new QHBoxLayout;
    btnRow2->addWidget(btnFrom);
    btnRow2->addWidget(btnSave);
    btnRow2->addWidget(btnLoad);
    btnRow2->addStretch(1);
    listLayout->addLayout(btnRow2);

    m_orderStat = new QLabel;
    listLayout->addWidget(m_orderStat);
    box->addWidget(listBox, 1);

    // ── 手动加一行：补个别货位用 ──
    auto* pickBox = new QGroupBox(QStringLiteral("③ 手动加一行（选中清单表里的行会把内容带到这里，改完点【插入为新行】）"));
    auto* pickGrid = new QGridLayout(pickBox);

    m_pickPos = new QComboBox;
    m_pickPos->setToolTip(QStringLiteral("选货位后会自动带出该货位上的工具编号；选“(空货位)”可造“工具没放进去”的场景"));
    connect(m_pickPos, &QComboBox::currentIndexChanged, this, &EdgeSimulator::onPositionPicked);
    m_pickTool = new QComboBox;
    m_pickTool->setToolTip(QStringLiteral("工具编号来自 tool_info；选“(认不出)”可造未知物场景"));
    m_pickConf = new QSpinBox;
    m_pickConf->setRange(0, 100);
    m_pickConf->setValue(95);
    m_pickConf->setSuffix(QStringLiteral(" %"));
    m_pickAlert = new QComboBox;
    // 下拉项文字较长，限制最小宽度，避免把整页撑宽导致右侧按钮被裁掉
    m_pickAlert->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_pickAlert->setMinimumContentsLength(12);
    m_pickAlert->addItem(QStringLiteral("正常项（不带告警字段）"), QString());
    m_pickAlert->addItem(QStringLiteral("misplaced 错放"),   QStringLiteral("misplaced"));
    m_pickAlert->addItem(QStringLiteral("unknown 未知物"),    QStringLiteral("unknown"));
    m_pickAlert->addItem(QStringLiteral("missing 缺失"),      QStringLiteral("missing"));
    m_pickAlert->addItem(QStringLiteral("low_confidence 置信度不足"), QStringLiteral("low_confidence"));
    m_pickLevel = new QSpinBox;
    m_pickLevel->setRange(0, 3);
    m_pickLevel->setValue(0);
    auto* btnInsert = new QPushButton(QStringLiteral("插入为新行"));
    connect(btnInsert, &QPushButton::clicked, this, &EdgeSimulator::onInsertOrderRow);
    auto* btnDelete = new QPushButton(QStringLiteral("删除选中行"));
    connect(btnDelete, &QPushButton::clicked, this, &EdgeSimulator::onDeleteOrderRow);

    pickGrid->addWidget(new QLabel(QStringLiteral("货位")), 0, 0);
    pickGrid->addWidget(m_pickPos, 0, 1, 1, 3);
    pickGrid->addWidget(new QLabel(QStringLiteral("工具")), 1, 0);
    pickGrid->addWidget(m_pickTool, 1, 1, 1, 3);
    pickGrid->addWidget(new QLabel(QStringLiteral("置信度")), 2, 0);
    pickGrid->addWidget(m_pickConf, 2, 1);
    pickGrid->addWidget(m_pickAlert, 2, 2, 1, 2);
    pickGrid->addWidget(new QLabel(QStringLiteral("级别")), 3, 0);
    pickGrid->addWidget(m_pickLevel, 3, 1);
    pickGrid->addWidget(btnInsert, 3, 2, 1, 2);
    pickGrid->addWidget(btnDelete, 4, 2, 1, 2);
    pickGrid->setColumnStretch(1, 1);
    pickGrid->setColumnStretch(3, 1);
    box->addWidget(pickBox);

    // 清单文本镜像：只读，可复制到 Excel 备用
    auto* textBox = new QGroupBox(QStringLiteral("清单文本（只读，可复制到 Excel 备用）"));
    auto* textLayout = new QVBoxLayout(textBox);
    m_orderText = new QTextEdit;
    m_orderText->setReadOnly(true);
    m_orderText->setFontFamily(QStringLiteral("Consolas"));
    m_orderText->setToolTip(QStringLiteral("清单文本镜像（只读）：可复制到 Excel 备用；格式为"
                                           "货位,工具编号,工具名,归属,识别结果,置信度,告警类型,级别,是否回应"));
    m_orderText->setMinimumHeight(90);
    m_orderText->setMaximumHeight(130);
    textLayout->addWidget(m_orderText);
    box->addWidget(textBox);

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
 * @brief 删除清单表里选中的行
 * @return 无
 */
void EdgeSimulator::onDeleteOrderRow()
{
    if (!m_orderTable) {
        return;
    }
    const int row = m_orderTable->currentRow();
    if (row < 0) {
        logEvent(QStringLiteral("请先在清单表里选中要删除的行"));
        return;
    }
    QList<CustomRow> rows = orderRowsFromTable();
    if (row >= rows.size()) {
        return;
    }
    const QString removed = rows.at(row).position;
    rows.removeAt(row);
    fillOrderTable(rows);
    logEvent(QStringLiteral("已删除清单第 %1 行（货位 %2）").arg(row + 1).arg(removed));
}

/**
 * @brief 按下方选择区内容新增一行
 * @return 无
 * @note  选中清单表某行时选择区已被回填，这时插入相当于"用新内容替换选中行"
 */
void EdgeSimulator::onInsertOrderRow()
{
    if (!m_orderTable) {
        return;
    }
    QList<CustomRow> rows = orderRowsFromTable();
    const int current = m_orderTable->currentRow();

    CustomRow row;
    row.position   = m_pickPos ? m_pickPos->currentData().toString() : QString();
    if (row.position.isEmpty() && m_pickPos) {
        row.position = m_pickPos->currentText();
    }
    row.toolCode   = m_pickTool ? m_pickTool->currentData().toString() : QString();
    row.toolName   = m_pickTool ? m_pickTool->currentText().section(QStringLiteral("  "), 0, 0) : QString();
    row.confidence = m_pickConf ? m_pickConf->value() / 100.0 : kGeneratedConfidence;
    row.alertType  = m_pickAlert ? m_pickAlert->currentData().toString() : QString();
    if (row.alertType.isEmpty()) {
        row.alertType = QStringLiteral("none");
    }
    row.alertLevel = m_pickLevel ? m_pickLevel->value() : 0;
    row.belong     = kBelongOther;
    row.result     = kResultNormal;
    if (row.alertType == QStringLiteral("misplaced"))      row.result = kResultMisplaced;
    else if (row.alertType == QStringLiteral("unknown"))   row.result = kResultUnknown;
    else if (row.alertType == QStringLiteral("missing"))   row.result = kResultMissing;
    else if (row.alertType == QStringLiteral("low_confidence")) row.result = kResultLowConf;
    row.reply      = true;

    if (row.position.isEmpty()) {
        logEvent(QStringLiteral("请先选择货位再插入"));
        return;
    }
    if (current >= 0 && current < rows.size()) {
        rows[current] = row;    // 选中了行：用选择区的内容替换它
        fillOrderTable(rows);
        logEvent(QStringLiteral("已按选择区内容更新清单第 %1 行").arg(current + 1));
        return;
    }
    rows.append(row);
    fillOrderTable(rows);
    logEvent(QStringLiteral("已新增清单行：%1 %2").arg(row.position).arg(row.toolCode));
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
