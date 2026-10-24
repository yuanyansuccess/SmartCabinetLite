/**
 * @file ToolDAO.cpp
 * @brief 工具数据访问对象实现 — 核心查询与 QJsonObject CRUD
 * @author 袁燕
 *
 * 说明：本文件按业务域物理拆分，类声明仍集中在 ToolDAO.h，接口零变化。
 * 其余实现分文件：ToolDAOToolEntity / ToolDAOCategory / ToolDAOMachineGroup /
 *                ToolDAOPosition / ToolDAOMaintenance / ToolDAOMapping
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
 * @brief 查询结果行转换为工具实体
 * @param q 已定位到有效行的查询结果
 * @return 填充完成的工具实体
 */
ToolInfo ToolDAO::fromQuery(const QSqlQuery& q) {
    ToolInfo t;
    t.toolId         = q.value("tool_id").toInt();
    // 读取mapping_id（位置维度查询时存在此字段）
    int mappingIdIdx = q.record().indexOf("mapping_id");
    if (mappingIdIdx >= 0) t.mappingId = q.value(mappingIdIdx).toInt();
    t.toolCode       = q.value("tool_code").toString();
    t.toolName       = q.value("tool_name").toString();
    t.spec           = q.value("spec").toString();
    t.categoryId     = q.value("category_id").toInt();
    t.cabinetId      = q.value("cabinet_id").toInt();
    t.machineGroupId = q.value("machine_group_id").toInt();
    t.layer          = q.value("layer").toString();
    t.position       = q.value("position").toString();
    t.totalQty       = q.value("total_qty").toInt();
    t.currentQty     = q.value("current_qty").toInt();
    t.visionTag        = q.value("vision_tag").toString();
    t.status         = q.value("status").toString();
    t.checkoutReason = q.value("checkout_reason").toString();
    t.isRecommended  = q.value("is_recommended").toInt();
    t.recognitionMethod = q.value("recognition_method").toString();
    t.documentPath   = q.value("document_path").toString();
    t.createdAt      = q.value("created_at").toDateTime();
    t.updatedAt      = q.value("updated_at").toDateTime();
    t.categoryName   = q.value("category_name").toString();
    t.cabinetName    = q.value("cabinet_name").toString();
    t.machineGroupName = q.value("group_name").toString();
    // 最近操作字段 
    t.latestOpType  = q.value("latest_op_type").toString();
    t.latestOpTime  = q.value("latest_op_time").toString();
    t.latestOpUser  = q.value("latest_op_user").toString();
    // 活跃借用数 
    t.activeBorrows = q.value("active_borrows").toInt();
    return t;
}

/**
 * @brief 位置维度查询工具列表（每个在库位置一行）
 * @param keyword 工具名称/编号关键字，为空时不筛选
 * @param category 分类筛选，为空时不筛选
 * @param status 位置状态筛选（如 in_stock/pending）
 * @param cabinetId 柜体ID，<=0 时不筛选
 * @param page 页码（从 1 开始）
 * @param pageSize 每页条数
 * @param machineGroupId 机组ID，>0 时启用机组隔离
 * @return 分页结果对象（含列表与总数）
 */
QJsonObject ToolDAO::findAllByPosition(const QString& keyword, const QString& category,
                                        const QString& status, int cabinetId, int page, int pageSize,
                                        int machineGroupId) {
    QSqlDatabase db = getDb();
    QStringList conditions; conditions << "1=1";
    QMap<QString, QVariant> bindValues;

    if (!keyword.isEmpty()) {
        QString escaped = keyword;
        escaped.replace('\\', "\\\\").replace('%', "\\%").replace('_', "\\_");
        QString like = "%" + escaped + "%";
        conditions << "(t.tool_name LIKE :kw ESCAPE '\\' OR t.spec LIKE :kw2 ESCAPE '\\' OR t.tool_code LIKE :kw3 ESCAPE '\\')";
        bindValues[":kw"] = like; bindValues[":kw2"] = like; bindValues[":kw3"] = like;
    }
    if (!category.isEmpty()) { conditions << "tc.category_name = :cat"; bindValues[":cat"] = category; }
    if (!status.isEmpty()) {
        conditions << "mpm.status = :st";
        bindValues[":st"] = status;
    }
    if (cabinetId > 0) { conditions << "mpm.cabinet_id = :cid"; bindValues[":cid"] = cabinetId; }
    if (machineGroupId > 0) { conditions << "t.machine_group_id = :mgid"; bindValues[":mgid"] = machineGroupId; }
    QString where = conditions.join(" AND ");

    // COUNT
    QSqlQuery cq(db);
    cq.prepare(
        "SELECT COUNT(*) FROM tool_position_mapping mpm "
        "LEFT JOIN tool_info t ON t.tool_id = mpm.tool_id "
        "LEFT JOIN tool_category tc ON t.category_id = tc.category_id "
        "WHERE " + where
    );
    for (auto it = bindValues.begin(); it != bindValues.end(); ++it) cq.bindValue(it.key(), it.value());
    safeExec(cq); cq.next(); int total = cq.value(0).toInt();

    // DATA
    QSqlQuery dq(db);
    dq.prepare(
        "SELECT mpm.mapping_id, mpm.status AS pos_status, "
        "  t.tool_id, t.tool_code, t.tool_name, t.spec, "
        "  tc.category_name AS category, "
        "  mpm.cabinet_id, cb.cabinet_name, "
        "  t.machine_group_id, mg.group_name AS machine_group_name, "
        "  mpm.layer, mpm.position, "
        "  t.total_qty, t.current_qty, t.vision_tag, t.status AS tool_status, "
        "  t.recognition_method, t.document_path, t.created_at, "
        "  t.unit "
        "FROM tool_position_mapping mpm "
        "JOIN tool_cabinet cb ON mpm.cabinet_id = cb.cabinet_id "
        "LEFT JOIN tool_info t ON t.tool_id = mpm.tool_id "
        "LEFT JOIN tool_category tc ON t.category_id = tc.category_id "
        "LEFT JOIN machine_group mg ON t.machine_group_id = mg.group_id "
        "WHERE " + where + " ORDER BY tc.category_name, cb.cabinet_name, mpm.layer, mpm.position LIMIT :lim OFFSET :off"
    );
    for (auto it = bindValues.begin(); it != bindValues.end(); ++it) dq.bindValue(it.key(), it.value());
    dq.bindValue(":lim", pageSize); dq.bindValue(":off", (page-1)*pageSize);
    safeExec(dq);

    QJsonArray list;
    while (dq.next()) {
        QJsonObject item;
        item["mappingId"] = dq.value("mapping_id").toInt();
        item["toolId"] = dq.value("tool_id").toInt();
        item["toolName"] = dq.value("tool_name").toString();
        item["spec"] = dq.value("spec").toString();
        item["toolCode"] = dq.value("tool_code").toString();
        item["category"] = dq.value("category").toString();
        item["cabinetId"] = dq.value("cabinet_id").toInt();
        item["cabinetName"] = dq.value("cabinet_name").toString();
        item["machineGroupId"] = dq.value("machine_group_id").toInt();
        item["machineGroupName"] = dq.value("machine_group_name").toString();
        item["layer"] = dq.value("layer").toString();
        item["rawPosition"] = dq.value("position").toString();
        // 位置格式化为 柜号-层号-位号（如A-01-03）
        item["position"] = common::formatPosition(
            dq.value("cabinet_name").toString(),
            dq.value("layer").toString(),
            dq.value("position").toString());
        item["totalQty"] = dq.value("total_qty").toInt();
        item["currentQty"] = dq.value("current_qty").toInt();
        item["visionTag"] = dq.value("vision_tag").toString();
        item["status"] = dq.value("pos_status").toString();  // 映射表status（位置占用状态）
        item["toolStatus"] = dq.value("tool_status").toString();  // tool_info.status（工具整体状态）
        item["recognitionMethod"] = dq.value("recognition_method").toString();
        item["documentPath"] = dq.value("document_path").toString();
        item["createdAt"] = dq.value("created_at").toString();
        item["unit"] = dq.value("unit").toString();  // 补充unit字段（借用页面用到）
        list.append(item);
    }
    QJsonObject result; result["list"] = list; result["total"] = total;
    return result;
}

/**
 * @brief 按工具种类聚合查询在库工具（借用页面用）
 * @param keyword 工具名称/编号关键字，为空时不筛选
 * @param category 分类筛选，为空时不筛选
 * @param page 页码（从 1 开始）
 * @param pageSize 每页条数
 * @param machineGroupId 机组ID，>0 时启用机组隔离
 * @return 分页结果对象，每项 availableQty 为该工具可用位置数
 */
QJsonObject ToolDAO::findAllInStockByTool(const QString& keyword, const QString& category,
                                           int page, int pageSize, int machineGroupId) {
    QSqlDatabase db = getDb();
    QStringList conditions; conditions << "1=1";
    QMap<QString, QVariant> bindValues;

    if (!keyword.isEmpty()) {
        QString escaped = keyword;
        escaped.replace('\\', "\\\\").replace('%', "\\%").replace('_', "\\_");
        QString like = "%" + escaped + "%";
        conditions << "(ti.tool_name LIKE :kw ESCAPE '\\' OR ti.spec LIKE :kw2 ESCAPE '\\' OR ti.tool_code LIKE :kw3 ESCAPE '\\')";
        bindValues[":kw"] = like; bindValues[":kw2"] = like; bindValues[":kw3"] = like;
    }
    if (!category.isEmpty()) { conditions << "tc.category_name = :cat"; bindValues[":cat"] = category; }
    if (machineGroupId > 0) { conditions << "ti.machine_group_id = :mgid"; bindValues[":mgid"] = machineGroupId; }
    // 必须有in_stock位置
    conditions << "EXISTS(SELECT 1 FROM tool_position_mapping m WHERE m.tool_id=ti.tool_id AND m.status='in_stock')";
    QString where = conditions.join(" AND ");

    // COUNT
    QSqlQuery cq(db);
    cq.prepare("SELECT COUNT(*) FROM tool_info ti LEFT JOIN tool_category tc ON ti.category_id=tc.category_id WHERE " + where);
    for (auto it = bindValues.begin(); it != bindValues.end(); ++it) cq.bindValue(it.key(), it.value());
    safeExec(cq); cq.next(); int total = cq.value(0).toInt();

    // DATA — 按工具种类聚合，availableQty=映射表in_stock位置数
    QSqlQuery dq(db);
    dq.prepare(
        "SELECT ti.tool_id, ti.tool_code, ti.tool_name, ti.spec, "
        "tc.category_name AS category, "
        "ti.machine_group_id, mg.group_name AS machine_group_name, "
        "COALESCE(mpm.cabinet_id, ti.cabinet_id) AS cabinet_id, COALESCE(mpm_cb.cabinet_name, cb.cabinet_name) AS cabinet_name, COALESCE(mpm.layer, ti.layer) AS layer, COALESCE(mpm.position, ti.position) AS position, "
        "ti.total_qty, ti.current_qty, ti.unit, ti.vision_tag, ti.status, "
        "ti.recognition_method, ti.document_path, ti.created_at, "
        "(SELECT COUNT(*) FROM tool_position_mapping m WHERE m.tool_id=ti.tool_id AND m.status='in_stock') AS available_qty "
        "FROM tool_info ti "
        "LEFT JOIN tool_position_mapping mpm ON mpm.mapping_id=(SELECT MIN(mm.mapping_id) FROM tool_position_mapping mm WHERE mm.tool_id=ti.tool_id AND mm.status='in_stock') "
        "LEFT JOIN tool_cabinet mpm_cb ON mpm.cabinet_id=mpm_cb.cabinet_id "
        "LEFT JOIN tool_category tc ON ti.category_id=tc.category_id "
        "LEFT JOIN tool_cabinet cb ON ti.cabinet_id=cb.cabinet_id "
        "LEFT JOIN machine_group mg ON ti.machine_group_id=mg.group_id "
        "WHERE " + where + " ORDER BY tc.category_name, ti.tool_name LIMIT :lim OFFSET :off"
    );
    for (auto it = bindValues.begin(); it != bindValues.end(); ++it) dq.bindValue(it.key(), it.value());
    dq.bindValue(":lim", pageSize); dq.bindValue(":off", (page-1)*pageSize);
    safeExec(dq);

    QJsonArray list;
    while (dq.next()) {
        QJsonObject item;
        item["toolId"] = dq.value("tool_id").toInt();
        item["toolName"] = dq.value("tool_name").toString();
        item["spec"] = dq.value("spec").toString();
        item["toolCode"] = dq.value("tool_code").toString();
        item["category"] = dq.value("category").toString();
        item["cabinetId"] = dq.value("cabinet_id").toInt();
        item["cabinetName"] = dq.value("cabinet_name").toString();
        item["machineGroupId"] = dq.value("machine_group_id").toInt();
        item["machineGroupName"] = dq.value("machine_group_name").toString();
        item["layer"] = dq.value("layer").toString();
        item["rawPosition"] = dq.value("position").toString();
        item["position"] = common::formatPosition(
            dq.value("cabinet_name").toString(),
            dq.value("layer").toString(),
            dq.value("position").toString());
        item["totalQty"] = dq.value("total_qty").toInt();
        item["currentQty"] = dq.value("current_qty").toInt();
        item["availableQty"] = dq.value("available_qty").toInt();  // 可用位置数（借用数量上限）
        item["unit"] = dq.value("unit").toString();
        item["visionTag"] = dq.value("vision_tag").toString();
        item["status"] = dq.value("status").toString();
        item["recognitionMethod"] = dq.value("recognition_method").toString();
        item["documentPath"] = dq.value("document_path").toString();
        item["createdAt"] = dq.value("created_at").toString();
        list.append(item);
    }
    QJsonObject result; result["list"] = list; result["total"] = total;
    return result;
}

/**
 * @brief 按工具ID查询工具详情
 * @param toolId 工具ID
 * @return 含分类名、机组名、识别方式与文档路径的详情对象；不存在时返回空对象
 */
QJsonObject ToolDAO::findById(int toolId) {
    QSqlDatabase db = getDb(); QSqlQuery q(db);
    q.prepare("SELECT ti.tool_id, ti.tool_name, ti.spec, ti.tool_code, tc.category_name AS category, "
              "COALESCE(mpm.cabinet_id, ti.cabinet_id) AS cabinet_id, ti.machine_group_id, mg.group_name AS machine_group_name, "
              "COALESCE(mpm.layer, ti.layer) AS layer, COALESCE(mpm.position, ti.position) AS position, "
              "ti.total_qty, ti.current_qty, "
              "ti.vision_tag, ti.status, ti.recognition_method, ti.document_path FROM tool_info ti "
              "LEFT JOIN tool_category tc ON ti.category_id=tc.category_id "
              "LEFT JOIN machine_group mg ON ti.machine_group_id=mg.group_id "
              "LEFT JOIN tool_position_mapping mpm ON mpm.mapping_id=(SELECT MIN(m.mapping_id) FROM tool_position_mapping m WHERE m.tool_id=ti.tool_id AND m.status='in_stock') "
              "WHERE ti.tool_id=:id");
    q.bindValue(":id", toolId);
    if (!safeExec(q) || !q.next()) return QJsonObject();
    QJsonObject t;
    t["toolId"]=q.value("tool_id").toInt(); t["toolName"]=q.value("tool_name").toString();
    t["spec"]=q.value("spec").toString(); t["toolCode"]=q.value("tool_code").toString();
    t["category"]=q.value("category").toString(); t["cabinetId"]=q.value("cabinet_id").toInt();
    t["machineGroupId"]=q.value("machine_group_id").toInt();
    t["machineGroupName"]=q.value("machine_group_name").toString();
    t["layer"]=q.value("layer").toString(); t["position"]=q.value("position").toString();
    t["totalQty"]=q.value("total_qty").toInt(); t["currentQty"]=q.value("current_qty").toInt();
    t["visionTag"]=q.value("vision_tag").toString(); t["status"]=q.value("status").toString();
    t["recognitionMethod"]=q.value("recognition_method").toString();
    t["documentPath"]=q.value("document_path").toString();
    return t;
}

/**
 * @brief 按传入字段增量更新工具信息
 * @param toolId 工具ID
 * @param ups 待更新字段，同时接受驼峰与下划线两种键名
 * @return true=至少更新了一个字段；false=入参为空或无可更新字段
 */
bool ToolDAO::update(int toolId, const QJsonObject& ups) {
    if(ups.isEmpty()) return false;
    QSqlDatabase db=getDb(); QSqlQuery q(db);
    QStringList sets; QMap<QString,QVariant> binds;
    QMap<QString,QString> keyMap;
    keyMap["toolName"]="tool_name"; keyMap["toolCode"]="tool_code";
    keyMap["totalQty"]="total_qty";  // 注：currentQty 已从 keyMap 移除，current_qty 只由映射表触发器维护
    keyMap["categoryId"]="category_id";
    keyMap["machineGroupId"]="machine_group_id";  // 机组ID映射
    keyMap["visionTag"]="vision_tag";
    keyMap["recognitionMethod"]="recognition_method";  // 识别方式映射
    keyMap["documentPath"]="document_path";  // 文档路径映射
    keyMap["supplier"]="supplier";  // 供应商映射
    keyMap["unit"]="unit";  // 单位映射
    // 注意：dbCols 需覆盖供应商与单位，否则入库时更新不到这两个字段
    // 位置只认 tool_position_mapping 表，这里不写 cabinet_id/layer/position
    // current_qty 由映射表的触发器维护，不允许在这里改
    QStringList dbCols={"tool_name","spec","tool_code","category_id","machine_group_id","total_qty","vision_tag","status","supplier","unit","recognition_method","document_path"};
    for(const auto& col:dbCols){
        QString val;
        if(ups.contains(col)) val=ups[col].toVariant().toString();
        else if(keyMap.values().contains(col)){
            QString camelKey=keyMap.key(col);
            if(ups.contains(camelKey)) val=ups[camelKey].toVariant().toString();
        }
        if(!val.isNull()){
            sets<<(col+" = :"+col);
            binds[":"+col]=val;
        }
    }
    if(sets.isEmpty()) return false;
    binds[":id"]=toolId;
    q.prepare("UPDATE tool_info SET "+sets.join(", ")+" WHERE tool_id=:id");
    for(auto it=binds.begin();it!=binds.end();++it) q.bindValue(it.key(),it.value());
    return safeExec(q);
}

/**
 * @brief 更新工具状态
 * @param toolId 工具ID
 * @param s 目标状态，取 SC::TOOL_* 常量
 * @return true=状态已更新
 */
bool ToolDAO::updateStatus(int toolId, const QString& s) {
    QSqlDatabase db=getDb(); QSqlQuery q(db);
    q.prepare("UPDATE tool_info SET status=:s WHERE tool_id=:id");
    q.bindValue(":s",s); q.bindValue(":id",toolId);
    return safeExec(q);
}

// ==============================================================
// 【④ 工具查询（ToolInfo 接口 + 分页）
//   分页列表与计数
// ==============================================================
// ═══════════════════════════════════════════════
// 实体类API（Controller层使用）— 从dao/ToolDAO.cpp合并
// ═══════════════════════════════════════════════

} // namespace db
