/**
 * @file ToolDAOToolEntity.cpp
 * @brief ToolDAO 物理拆分 — 工具实体查询与写操作（借用/归还/状态/文档）
 * @author 袁燕
 *
 * 说明：由 ToolDAO.cpp 按业务域拆分而来，类声明仍在 ToolDAO.h，仅实现分文件。
 * 新增方法请按职责放到对应文件，不要全部堆回 ToolDAO.cpp。
 */
#include "ToolDAO.h"
#include "DatabaseManager.h"
#include "common/PositionFormatter.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include "common/Constants.h"

namespace db {

/**
 * @brief 分页查询工具实体列表
 * @param page 页码，从1开始
 * @param pageSize 每页条数
 * @param keyword 关键字，为空不过滤
 * @param cat 分类名称，为空不过滤
 * @param cab 柜体名称，为空不过滤
 * @param status 工具状态，为空不过滤
 * @param machineGroupId 机组ID，0表示不限
 * @return 工具实体列表
 */
QList<ToolInfo> ToolDAO::findAllTools(int page, int pageSize, const QString& keyword,
                                       const QString& cat, const QString& cab, const QString& status,
                                       const QString& machineGroup) {
    // 位置维度查询 — 用映射表status判断位置占用
    // 映射表status: pending=待入库, in_stock=在库, borrowed=已借出
    // 不依赖tool_info的cabinet_id/layer/position判断位置占用
    // 一个工具可在多个位置入库，tool_info只存基础信息
    // 位置维度查询必须包含mapping_id字段
    // 根因：原posSql缺少mpm.mapping_id，fromQuery读不到mappingId→onDetailTool中t.mappingId=0
    // →走兜底findByToolId(toolId)而非findByMappingId(mappingId)→显示所有位置的借用记录
    // 注意：posSql显式SELECT mpm.mapping_id，确保每个位置的ToolInfo.mappingId正确
    QString posSql = "SELECT "
                  "  mpm.mapping_id, "
                  "  t.tool_id, t.tool_code, t.tool_name, t.spec, t.category_id, "
                  "  t.machine_group_id, t.total_qty, t.current_qty, t.vision_tag, "
                  "  COALESCE(mpm.status, 'pending') AS status, "
                  "  t.is_recommended, t.recognition_method, t.document_path, "
                  "  t.checkout_reason, t.created_at, t.updated_at, "
                  "  tc.category_name, "
                  "  mpm.cabinet_id, mpm_cb.cabinet_name, mpm.layer, mpm.position, "
                  "  mg.group_name, "
                  "  lo.latest_op_type, lo.latest_op_time, lo.latest_op_user, "
                  "  (SELECT COALESCE(SUM(borrow_qty),0) FROM tool_borrow_record "
                  "   WHERE tool_id=t.tool_id AND status IN ('borrowing','overdue')) AS active_borrows "
                  "FROM tool_position_mapping mpm "
                  "JOIN tool_cabinet mpm_cb ON mpm.cabinet_id = mpm_cb.cabinet_id "
                  "LEFT JOIN tool_info t ON t.tool_id = mpm.tool_id "
                  "LEFT JOIN tool_category tc ON t.category_id = tc.category_id "
                  "LEFT JOIN machine_group mg ON t.machine_group_id = mg.group_id "
                  "LEFT JOIN v_tool_latest_operation lo ON t.tool_id = lo.tool_id "
                  "WHERE 1=1";
    // checked_out不在位置上，从tool_info查
    QString nonPosSql = "SELECT t.*, c.category_name, cb.cabinet_name, mg.group_name, "
                  "lo.latest_op_type, lo.latest_op_time, lo.latest_op_user, "
                  "(SELECT COALESCE(SUM(borrow_qty),0) FROM tool_borrow_record "
                  " WHERE tool_id=t.tool_id AND status IN ('borrowing','overdue')) AS active_borrows, "
                  "t.cabinet_id AS mpm_cab_id, cb.cabinet_name AS mpm_cab_name, "
                  "t.layer AS mpm_layer, t.position AS mpm_pos "
                  "FROM tool_info t "
                  "LEFT JOIN tool_category c ON t.category_id=c.category_id "
                  "LEFT JOIN tool_cabinet cb ON t.cabinet_id=cb.cabinet_id "
                  "LEFT JOIN machine_group mg ON t.machine_group_id=mg.group_id "
                  "LEFT JOIN v_tool_latest_operation lo ON t.tool_id=lo.tool_id "
                  "WHERE t.status = 'checked_out'";
    QVariantList params;
    QVariantList nonPosParams;

    // 状态筛选逻辑
    // in_stock/borrowed → 位置上有对应状态的工具
    // pending → 空闲位置（t.tool_id IS NULL）
    // checked_out → 只查nonPosSql
    bool filterCheckedOut = (status == SC::TOOL_CHECKED_OUT);
    bool filterPending = (status == SC::TOOL_PENDING);
    bool filterInStockBorrowed = (!status.isEmpty() && !filterCheckedOut && !filterPending);

    if (filterInStockBorrowed) {
        // 按映射表status筛选
        posSql += " AND mpm.status = ?"; params << status;
        nonPosSql = "";
    } else if (filterCheckedOut) {
        // 只查checked_out（从tool_info）
        posSql = "";
    } else if (filterPending) {
        // pending=映射表status='pending'
        posSql += " AND mpm.status = 'pending'";
        nonPosSql = "";
    } else {
        // 默认：所有位置 + checked_out
    }

    // 关键字/类别/机组筛选
    if (!keyword.isEmpty()) {
        QString kw = "%" + keyword + "%";
        if (!posSql.isEmpty()) {
            posSql += " AND (t.tool_name LIKE ? OR t.tool_code LIKE ?)";
            params << kw << kw;
        }
        if (!nonPosSql.isEmpty()) {
            nonPosSql += " AND (t.tool_name LIKE ? OR t.tool_code LIKE ?)";
            nonPosParams << kw << kw;
        }
    }
    if (!cat.isEmpty()) {
        QStringList cats = cat.split(",", Qt::SkipEmptyParts);
        QString catCond;
        if (cats.size() == 1) {
            catCond = " AND tc.category_name = ?";
        } else {
            catCond = " AND tc.category_name IN (" + QString("?,").repeated(cats.size() - 1) + "?)";
        }
        QString nonPosCatCond;
        if (cats.size() == 1) {
            nonPosCatCond = " AND c.category_name = ?";
        } else {
            nonPosCatCond = " AND c.category_name IN (" + QString("?,").repeated(cats.size() - 1) + "?)";
        }
        if (!posSql.isEmpty()) { posSql += catCond; for (const QString& ct : cats) params << ct; }
        if (!nonPosSql.isEmpty()) { nonPosSql += nonPosCatCond; for (const QString& ct : cats) nonPosParams << ct; }
    }
    if (!cab.isEmpty()) {
        if (!posSql.isEmpty()) { posSql += " AND mpm_cb.cabinet_name = ?"; params << cab; }
        if (!nonPosSql.isEmpty()) { nonPosSql += " AND cb.cabinet_name = ?"; nonPosParams << cab; }
    }
    if (!machineGroup.isEmpty()) {
        QStringList mgs = machineGroup.split(",", Qt::SkipEmptyParts);
        QString mgCond;
        if (mgs.size() == 1) {
            mgCond = " AND mg.group_name = ?";
        } else {
            mgCond = " AND mg.group_name IN (" + QString("?,").repeated(mgs.size() - 1) + "?)";
        }
        if (!posSql.isEmpty()) { posSql += mgCond; for (const QString& m : mgs) params << m; }
        if (!nonPosSql.isEmpty()) { nonPosSql += mgCond; for (const QString& m : mgs) nonPosParams << m; }
    }

    // 排序：类别→柜名→层→位号
    QString orderBy = "tc.category_name, mpm_cb.cabinet_name, mpm.layer, mpm.position, t.created_at DESC";
    QString nonPosOrderBy = "c.category_name, cb.cabinet_name, t.layer, t.position, t.created_at DESC";

    // 修复分页Bug：原逻辑对pos/nonPos分别分页导致每页数量不一致
    // 新逻辑：统一偏移量分配
    // 1. 先查pos部分总数posCount
    // 2. 计算当前页offset，从pos和nonPos各取对应条数
    // 效果：每页固定返回pageSize条，total与list一致
    int offset = (page - 1) * pageSize;

    QList<ToolInfo> list;
    int posCount = 0;
    // 先查pos部分总数（count()自动包装为SELECT COUNT(*) FROM (...) AS _cnt）
    if (!posSql.isEmpty()) {
        posCount = count(posSql, params);
    }

    // 位置维度部分：从offset开始取，最多pageSize条
    if (!posSql.isEmpty() && offset < posCount) {
        int posLimit = qMin(pageSize, posCount - offset);
        QString pagedSql = posSql + " ORDER BY " + orderBy +
                          " LIMIT " + QString::number(posLimit) +
                          " OFFSET " + QString::number(offset);
        QSqlQuery posQ = query(pagedSql, params);
        while (posQ.next()) {
            ToolInfo t = fromQuery(posQ);
            list.append(t);
        }
    }

    // 非位置维度部分：取剩余条数
    if (!nonPosSql.isEmpty()) {
        int remaining = pageSize - list.size();
        if (remaining > 0) {
            int nonPosOffset = qMax(0, offset - posCount);
            QString pagedNonPosSql = nonPosSql + " ORDER BY " + nonPosOrderBy +
                                    " LIMIT " + QString::number(remaining) +
                                    " OFFSET " + QString::number(nonPosOffset);
            QSqlQuery nonPosQ = query(pagedNonPosSql, nonPosParams);
            while (nonPosQ.next()) {
                ToolInfo t = fromQuery(nonPosQ);
                bool useMapping = (nonPosQ.value("mpm_cab_id").toInt() > 0);
                if (useMapping) {
                    t.cabinetId = nonPosQ.value("mpm_cab_id").toInt();
                    t.cabinetName = nonPosQ.value("mpm_cab_name").toString();
                    t.layer = nonPosQ.value("mpm_layer").toString();
                    t.position = nonPosQ.value("mpm_pos").toString();
                }
                list.append(t);
            }
        }
    }
    return list;
}

/**
 * @brief 统计符合条件的工具数量
 * @return 工具总数；筛选条件与 findAllTools 保持一致
 */
int ToolDAO::countTools(const QString& keyword, const QString& cat,
                         const QString& cab, const QString& status,
                         const QString& machineGroup) {
    // 计数同步findAllTools的位置维度逻辑
    // 位置维度：所有映射表位置（有工具占用的显示在库/已借用，空闲的显示待入库）
    // 非位置维度：checked_out（不在位置上的已出库工具）
    bool filterCheckedOut = (status == SC::TOOL_CHECKED_OUT);
    bool filterPending = (status == SC::TOOL_PENDING);
    bool filterInStockBorrowed = (!status.isEmpty() && !filterCheckedOut && !filterPending);

    int total = 0;
    QVariantList posParams, nonPosParams;

    // 位置维度计数
    if (!filterCheckedOut) {
        // 用映射表status判断位置占用
        QString posSql = "SELECT mpm.mapping_id FROM tool_position_mapping mpm "
                         "JOIN tool_cabinet mpm_cb ON mpm.cabinet_id = mpm_cb.cabinet_id "
                         "LEFT JOIN tool_info t ON t.tool_id = mpm.tool_id "
                         "LEFT JOIN tool_category tc ON t.category_id = tc.category_id "
                         "LEFT JOIN machine_group mg ON t.machine_group_id = mg.group_id "
                         "WHERE 1=1";
        if (filterInStockBorrowed) {
            posSql += " AND mpm.status = ?"; posParams << status;
        } else if (filterPending) {
            posSql += " AND mpm.status = 'pending'";
        }
        if (!keyword.isEmpty()) {
            posSql += " AND (t.tool_name LIKE ? OR t.tool_code LIKE ?)";
            posParams << "%" + keyword + "%" << "%" + keyword + "%";
        }
        if (!cat.isEmpty()) {
            QStringList cats = cat.split(",", Qt::SkipEmptyParts);
            if (cats.size() == 1) { posSql += " AND tc.category_name = ?"; posParams << cats[0]; }
            else { posSql += " AND tc.category_name IN (" + QString("?,").repeated(cats.size()-1) + "?)"; for (const QString& c : cats) posParams << c; }
        }
        if (!cab.isEmpty())   { posSql += " AND mpm_cb.cabinet_name = ?"; posParams << cab; }
        if (!machineGroup.isEmpty()) {
            QStringList mgs = machineGroup.split(",", Qt::SkipEmptyParts);
            if (mgs.size() == 1) { posSql += " AND mg.group_name = ?"; posParams << mgs[0]; }
            else { posSql += " AND mg.group_name IN (" + QString("?,").repeated(mgs.size()-1) + "?)"; for (const QString& m : mgs) posParams << m; }
        }
        total += count(posSql, posParams);
    }

    // 非位置维度计数（checked_out）
    if (filterCheckedOut || status.isEmpty()) {
        QString nonPosSql = "SELECT t.tool_id FROM tool_info t "
                            "LEFT JOIN tool_category c ON t.category_id=c.category_id "
                            "LEFT JOIN tool_cabinet cb ON t.cabinet_id=cb.cabinet_id "
                            "LEFT JOIN machine_group mg ON t.machine_group_id=mg.group_id "
                            "WHERE t.status = 'checked_out'";
        if (!keyword.isEmpty()) {
            nonPosSql += " AND (t.tool_name LIKE ? OR t.tool_code LIKE ?)";
            nonPosParams << "%" + keyword + "%" << "%" + keyword + "%";
        }
        if (!cat.isEmpty()) {
            QStringList cats = cat.split(",", Qt::SkipEmptyParts);
            if (cats.size() == 1) { nonPosSql += " AND c.category_name = ?"; nonPosParams << cats[0]; }
            else { nonPosSql += " AND c.category_name IN (" + QString("?,").repeated(cats.size()-1) + "?)"; for (const QString& c : cats) nonPosParams << c; }
        }
        if (!cab.isEmpty())   { nonPosSql += " AND cb.cabinet_name = ?"; nonPosParams << cab; }
        if (!machineGroup.isEmpty()) {
            QStringList mgs = machineGroup.split(",", Qt::SkipEmptyParts);
            if (mgs.size() == 1) { nonPosSql += " AND mg.group_name = ?"; nonPosParams << mgs[0]; }
            else { nonPosSql += " AND mg.group_name IN (" + QString("?,").repeated(mgs.size()-1) + "?)"; for (const QString& m : mgs) nonPosParams << m; }
        }
        total += count(nonPosSql, nonPosParams);
    }

    return total;
}


// ==============================================================
// 【⑤ 工具操作（ToolInfo 接口）
//   按结构体的新增/更新/删除与状态
// ==============================================================
/**
 * @brief 按工具ID查询工具实体
 * @param toolId 工具ID
 * @return 工具实体；不存在时返回默认构造的空实体
 */
ToolInfo ToolDAO::findToolById(int toolId) {
    QSqlQuery q = query("SELECT t.*, c.category_name, cb.cabinet_name, mg.group_name FROM tool_info t "
                         "LEFT JOIN tool_category c ON t.category_id=c.category_id "
                         "LEFT JOIN tool_cabinet cb ON t.cabinet_id=cb.cabinet_id "
                         "LEFT JOIN machine_group mg ON t.machine_group_id=mg.group_id "
                         "WHERE t.tool_id = ?", {toolId});
    if (q.next()) return fromQuery(q);
    return ToolInfo();
}

/**
 * @brief 按工具编号查询工具实体
 * @param code 工具编号
 * @return 工具实体；不存在时返回默认构造的空实体
 */
ToolInfo ToolDAO::findToolByCode(const QString& code) {
    QSqlQuery q = query("SELECT t.*, c.category_name, cb.cabinet_name, mg.group_name FROM tool_info t "
                         "LEFT JOIN tool_category c ON t.category_id=c.category_id "
                         "LEFT JOIN tool_cabinet cb ON t.cabinet_id=cb.cabinet_id "
                         "LEFT JOIN machine_group mg ON t.machine_group_id=mg.group_id "
                         "WHERE t.tool_code = ?", {code});
    if (q.next()) return fromQuery(q);
    return ToolInfo();
}

/**
 * @brief 新增工具实体
 * @param t 待写入的工具实体
 * @return 新记录的ID；写入失败返回 -1
 */
int ToolDAO::insertTool(const ToolInfo& t) {
    // 增加recognition_method/document_path列
    return insertAndGetId("INSERT INTO tool_info (tool_code,tool_name,spec,category_id,"
                          "cabinet_id,machine_group_id,layer,position,total_qty,current_qty,vision_tag,status,"
                          "is_recommended,recognition_method,document_path) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                          {t.toolCode, t.toolName, t.spec, t.categoryId, t.cabinetId,
                           t.machineGroupId, t.layer, t.position, t.totalQty, t.currentQty, t.visionTag,
                           t.status, t.isRecommended, t.recognitionMethod, t.documentPath});
}

/**
 * @brief 按实体更新工具信息
 * @param t 含工具ID与待更新字段的实体
 * @return true=更新成功
 */
bool ToolDAO::updateTool(const ToolInfo& t) {
    // 增加recognition_method/document_path更新
    return execute("UPDATE tool_info SET tool_name=?,spec=?,category_id=?,cabinet_id=?,"
                   "machine_group_id=?,layer=?,position=?,total_qty=?,vision_tag=?,status=?,is_recommended=?,"
                   "recognition_method=?,document_path=? WHERE tool_id=?",
                   {t.toolName, t.spec, t.categoryId, t.cabinetId, t.machineGroupId,
                    t.layer, t.position, t.totalQty, t.visionTag, t.status, t.isRecommended,
                    t.recognitionMethod, t.documentPath, t.toolId});
}

/**
 * @brief 按工具ID删除工具记录
 * @param toolId 工具ID
 * @return true=删除成功
 */
bool ToolDAO::deleteToolById(int toolId) {
    return execute("DELETE FROM tool_info WHERE tool_id = ?", {toolId});
}

/**
 * @brief 更新工具状态
 * @param toolId 工具ID
 * @param status 目标状态，取 SC::TOOL_* 常量
 * @return true=更新成功
 */
bool ToolDAO::updateToolStatus(int toolId, const QString& status) {
    return execute("UPDATE tool_info SET status=? WHERE tool_id=?", {status, toolId});
}

// 轻量更新文档路径 — 详情页上传文档专用
// 只更新document_path一个字段，避免全字段updateTool的副作用
bool ToolDAO::updateDocumentPath(int toolId, const QString& docPath) {
    return execute("UPDATE tool_info SET document_path=? WHERE tool_id=?",
                   {docPath, toolId});
}

/**
 * @brief 借用工具并扣减在库数量
 * @param toolId 工具ID
 * @param qty 借用数量
 * @return true=扣减成功；false=库存不足或写入失败
 */
bool ToolDAO::borrowTool(int toolId, int qty) {
    return execute("UPDATE tool_info SET current_qty=current_qty-?, "
                   "status=IF(current_qty-?<=0,'borrowed','in_stock') WHERE tool_id=?",
                   {qty, qty, toolId});
}

/**
 * @brief 归还工具并回增在库数量
 * @param toolId 工具ID
 * @param qty 归还数量
 * @return true=回增成功
 */
bool ToolDAO::returnTool(int toolId, int qty) {
    return execute("UPDATE tool_info SET current_qty=current_qty+?, "
                   "status='in_stock' WHERE tool_id=?", {qty, toolId});
}


// ==============================================================
// 【⑥ 配置数据（分类/柜体/机组/任务类型）
//   枚举类配置读取，登录页与维护页的下拉数据来源
// ==============================================================
// ── 分类 ──

} // namespace db
