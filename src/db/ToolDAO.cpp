/**
 * @file ToolDAO.cpp
 * @brief 工具数据访问对象实现 — QJsonObject API + 实体类API，含位置映射查询
 * @author 袁燕
 */
#include "ToolDAO.h"
#include "DatabaseManager.h"
#include "common/PositionFormatter.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

namespace db {

// ═══════════════════════════════════════════════

// ═══════════════════════════════════════════════
// 实体类转换 — 从dao/ToolDAO.cpp合并
// ═══════════════════════════════════════════════
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
    t.machineGroupId = q.value("machine_group_id").toInt();     // [V7.0]
    t.layer          = q.value("layer").toString();
    t.position       = q.value("position").toString();
    t.totalQty       = q.value("total_qty").toInt();
    t.currentQty     = q.value("current_qty").toInt();
    t.visionTag        = q.value("vision_tag").toString();
    t.status         = q.value("status").toString();
    t.checkoutReason = q.value("checkout_reason").toString();
    t.isRecommended  = q.value("is_recommended").toInt();
    t.recognitionMethod = q.value("recognition_method").toString();  // [V2.01]
    t.documentPath   = q.value("document_path").toString();          // [V2.01]
    t.createdAt      = q.value("created_at").toDateTime();
    t.updatedAt      = q.value("updated_at").toDateTime();
    t.categoryName   = q.value("category_name").toString();
    t.cabinetName    = q.value("cabinet_name").toString();
    t.machineGroupName = q.value("group_name").toString();      // [V7.0]
    // 最近操作字段 [V7.0]
    t.latestOpType  = q.value("latest_op_type").toString();
    t.latestOpTime  = q.value("latest_op_time").toString();
    t.latestOpUser  = q.value("latest_op_user").toString();
    // 活跃借用数 [2026-06-26v17]
    t.activeBorrows = q.value("active_borrows").toInt();
    return t;
}

// ═══════════════════════════════════════════════
// QJsonObject API（Service层使用）
// ═══════════════════════════════════════════════
QJsonObject ToolDAO::findAll(const QString& keyword, const QString& category,
                            const QString& status, int cabinetId, int page, int pageSize,
                            int machineGroupId) {
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
    if (!status.isEmpty()) {
        conditions << "ti.status = :st";
        bindValues[":st"] = status;
        // [V7.9 2026-06-27] 防御：in_stock状态必须current_qty>0，过滤数量为0的工具
        // 作者：袁燕 - 确保出库后数量为0的工具不再出现在待出库/待借用列表
        if (status == "in_stock") {
            conditions << "ti.current_qty > 0";
        }
    }
    if (cabinetId > 0) { conditions << "ti.cabinet_id = :cid"; bindValues[":cid"] = cabinetId; }
    // [2026-06-27] 机组隔离：只返回本机组工具，防止跨机组借用
    if (machineGroupId > 0) { conditions << "ti.machine_group_id = :mgid"; bindValues[":mgid"] = machineGroupId; }
    QString where = conditions.join(" AND ");

    QSqlQuery cq(db);
    cq.prepare("SELECT COUNT(*) FROM tool_info ti LEFT JOIN tool_category tc ON ti.category_id=tc.category_id WHERE " + where);
    for (auto it = bindValues.begin(); it != bindValues.end(); ++it) cq.bindValue(it.key(), it.value());
    safeExec(cq); cq.next(); int total = cq.value(0).toInt();

    QSqlQuery dq(db);
    // LEFT JOIN映射表改为position-based匹配
    // 原条件：mpm.tool_id=ti.tool_id AND mpm.cabinet_id=ti.cabinet_id AND ...
    // 问题：多件入库时后续件是新tool_info记录(tool_id不同)，但位置与映射表一致
    // 修复：只按cabinet_id+layer+position匹配（一个位置只有一条映射记录，UNIQUE约束保证）
    dq.prepare(
        "SELECT ti.tool_id, ti.tool_name, ti.spec, ti.tool_code, tc.category_name AS category, "
        "ti.cabinet_id, cb.cabinet_name, ti.machine_group_id, mg.group_name AS machine_group_name, "
        "ti.layer, ti.position, "
        // [V2.03u] 映射表位置（按位置匹配，权威数据源）
        "mpm.cabinet_id AS mpm_cab_id, mpm_cb.cabinet_name AS mpm_cab_name, mpm.layer AS mpm_layer, mpm.position AS mpm_pos, "
        "ti.total_qty, ti.current_qty, "
        "ti.vision_tag, ti.status, ti.checkout_reason, ti.is_recommended, "
        "ti.recognition_method, ti.document_path, ti.created_at "
        "FROM tool_info ti LEFT JOIN tool_category tc ON ti.category_id=tc.category_id "
        "LEFT JOIN tool_cabinet cb ON ti.cabinet_id=cb.cabinet_id "
        "LEFT JOIN machine_group mg ON ti.machine_group_id=mg.group_id "
        // [V2.03u] LEFT JOIN映射表：按位置匹配（不再要求tool_id匹配）
        "LEFT JOIN tool_position_mapping mpm ON mpm.cabinet_id=ti.cabinet_id AND mpm.layer=ti.layer AND mpm.position=ti.position "
        "LEFT JOIN tool_cabinet mpm_cb ON mpm.cabinet_id=mpm_cb.cabinet_id "
        // 排序改为按类别+位置（唯一标识排列）
        "WHERE " + where + " ORDER BY tc.category_name, cb.cabinet_name, ti.layer, ti.position, ti.created_at DESC LIMIT :lim OFFSET :off"
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
        // [V2.03r] 位置信息：优先从映射表取（权威数据源），映射表无匹配则从tool_info取
        QString cabName, layerStr, posStr;
        bool useMapping = (dq.value("mpm_cab_id").toInt() > 0);
        if (useMapping) {
            item["cabinetId"] = dq.value("mpm_cab_id").toInt();
            cabName = dq.value("mpm_cab_name").toString();
            layerStr = dq.value("mpm_layer").toString();
            posStr = dq.value("mpm_pos").toString();
        } else {
            item["cabinetId"] = dq.value("cabinet_id").toInt();
            cabName = dq.value("cabinet_name").toString();
            layerStr = dq.value("layer").toString();
            posStr = dq.value("position").toString();
        }
        item["cabinetName"] = cabName;
        item["machineGroupId"] = dq.value("machine_group_id").toInt();     // [V7.9]
        item["machineGroupName"] = dq.value("machine_group_name").toString();  // [V7.9]
        item["layer"] = layerStr;
        item["rawPosition"] = posStr;  // [V2.03g] 原始位号
        // 位置格式化为 柜号-层号-位号（两位补零，如A-01-03）
        item["position"] = common::formatPosition(cabName, layerStr, posStr);
        item["totalQty"] = dq.value("total_qty").toInt();
        item["currentQty"] = dq.value("current_qty").toInt();
        item["visionTag"] = dq.value("vision_tag").toString();
        item["status"] = dq.value("status").toString();
        item["isRecommended"] = dq.value("is_recommended").toBool();
        item["recognitionMethod"] = dq.value("recognition_method").toString();  // [V2.01]
        item["documentPath"] = dq.value("document_path").toString();            // [V2.01]
        item["createdAt"] = dq.value("created_at").toString();
        list.append(item);
    }
    QJsonObject result; result["list"] = list; result["total"] = total;
    return result;
}

// 位置维度查询：每个在库位置一行
// 设计理念：一个工具可在多个位置入库，出库/借用列表按位置分行显示
// FROM tool_position_mapping JOIN tool_info，按映射表status筛选
// 返回每项含mappingId（唯一标识，用于选中）和toolId（用于出库操作）
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
        item["unit"] = dq.value("unit").toString();  // [V2.09] 补充unit字段（借用页面用到）
        list.append(item);
    }
    QJsonObject result; result["list"] = list; result["total"] = total;
    return result;
}

// 按工具种类聚合查询在库工具（借用页面用）
// 设计理念：借用列表按工具种类显示，同一工具一行，availableQty=可用位置数
// 用户选择数量后，借用时自动从映射表分配对应数量的in_stock位置
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
        "ti.cabinet_id, cb.cabinet_name, ti.layer, ti.position, "
        "ti.total_qty, ti.current_qty, ti.unit, ti.vision_tag, ti.status, "
        "ti.recognition_method, ti.document_path, ti.created_at, "
        "(SELECT COUNT(*) FROM tool_position_mapping m WHERE m.tool_id=ti.tool_id AND m.status='in_stock') AS available_qty "
        "FROM tool_info ti "
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
        item["availableQty"] = dq.value("available_qty").toInt();  // [V2.12] 可用位置数（借用数量上限）
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

QJsonObject ToolDAO::findById(int toolId) {
    QSqlDatabase db = getDb(); QSqlQuery q(db);
    // [V7.9 2026-06-24] 增加machine_group JOIN，返回机组名称
    // [V2.01 2026-06-27] 增加recognition_method/document_path列
    q.prepare("SELECT ti.tool_id, ti.tool_name, ti.spec, ti.tool_code, tc.category_name AS category, "
              "ti.cabinet_id, ti.machine_group_id, mg.group_name AS machine_group_name, "
              "ti.layer, ti.position, ti.total_qty, ti.current_qty, "
              "ti.vision_tag, ti.status, ti.recognition_method, ti.document_path FROM tool_info ti "
              "LEFT JOIN tool_category tc ON ti.category_id=tc.category_id "
              "LEFT JOIN machine_group mg ON ti.machine_group_id=mg.group_id WHERE ti.tool_id=:id");
    q.bindValue(":id", toolId);
    if (!safeExec(q) || !q.next()) return QJsonObject();
    QJsonObject t;
    t["toolId"]=q.value("tool_id").toInt(); t["toolName"]=q.value("tool_name").toString();
    t["spec"]=q.value("spec").toString(); t["toolCode"]=q.value("tool_code").toString();
    t["category"]=q.value("category").toString(); t["cabinetId"]=q.value("cabinet_id").toInt();
    t["machineGroupId"]=q.value("machine_group_id").toInt();           // [V7.9]
    t["machineGroupName"]=q.value("machine_group_name").toString();    // [V7.9]
    t["layer"]=q.value("layer").toString(); t["position"]=q.value("position").toString();
    t["totalQty"]=q.value("total_qty").toInt(); t["currentQty"]=q.value("current_qty").toInt();
    t["visionTag"]=q.value("vision_tag").toString(); t["status"]=q.value("status").toString();
    t["recognitionMethod"]=q.value("recognition_method").toString();   // [V2.01]
    t["documentPath"]=q.value("document_path").toString();             // [V2.01]
    return t;
}

QJsonObject ToolDAO::findByCode(const QString& code) {
    QSqlDatabase db = getDb(); QSqlQuery q(db);
    q.prepare("SELECT tool_id, tool_name, tool_code, current_qty, status FROM tool_info WHERE tool_code=:c");
    q.bindValue(":c", code);
    if (!safeExec(q) || !q.next()) return QJsonObject();
    QJsonObject t;
    t["toolId"]=q.value("tool_id").toInt(); t["toolName"]=q.value("tool_name").toString();
    t["toolCode"]=q.value("tool_code").toString(); t["currentQty"]=q.value("current_qty").toInt();
    t["status"]=q.value("status").toString();
    return t;
}

int ToolDAO::insert(const QJsonObject& info) {
    QSqlDatabase db = getDb(); QSqlQuery q(db);
    // [V7.9 2026-06-24] 增加machine_group_id列，入库时关联机组
    // [V2.01 2026-06-27] 增加recognition_method/document_path列
    q.prepare("INSERT INTO tool_info (tool_name, spec, tool_code, category_id, cabinet_id, "
              "machine_group_id, layer, position, total_qty, current_qty, vision_tag, status, "
              "recognition_method, document_path) "
              "VALUES (:n,:s,:c,:cat,:cab,:mg,:l,:p,:tq,:cq,:rf,:st,:rm,:dp)");
    q.bindValue(":n",info["toolName"].toString()); q.bindValue(":s",info["spec"].toString(""));
    q.bindValue(":c",info["toolCode"].toString()); q.bindValue(":cat",info["categoryId"].toInt(0));
    q.bindValue(":cab",info["cabinetId"].toInt(0)); q.bindValue(":mg",info["machineGroupId"].toInt(0));
    q.bindValue(":l",info["layer"].toString("")); q.bindValue(":p",info["position"].toString(""));
    q.bindValue(":tq",info["totalQty"].toInt(1)); q.bindValue(":cq",info["currentQty"].toInt(0));
    q.bindValue(":rf",info["visionTag"].toString(""));
    q.bindValue(":st",info["status"].toString("in_stock"));
    q.bindValue(":rm",info["recognitionMethod"].toString("vision"));      // [V2.01] 默认视觉
    q.bindValue(":dp",info["documentPath"].toString(""));               // [V2.01] 文档路径
    if(!safeExec(q)){return -1;}
    return q.lastInsertId().toInt();
}

bool ToolDAO::update(int toolId, const QJsonObject& ups) {
    if(ups.isEmpty()) return false;
    QSqlDatabase db=getDb(); QSqlQuery q(db);
    QStringList sets; QMap<QString,QVariant> binds;
    QMap<QString,QString> keyMap;
    keyMap["toolName"]="tool_name"; keyMap["toolCode"]="tool_code";
    keyMap["totalQty"]="total_qty"; keyMap["currentQty"]="current_qty";
    keyMap["categoryId"]="category_id"; keyMap["cabinetId"]="cabinet_id";
    keyMap["machineGroupId"]="machine_group_id";  // [V7.9] 机组ID映射
    keyMap["visionTag"]="vision_tag";
    keyMap["recognitionMethod"]="recognition_method";  // [V2.01] 识别方式映射
    keyMap["documentPath"]="document_path";            // [V2.01] 文档路径映射
    keyMap["supplier"]="supplier";                    // [V2.03l] 供应商映射
    keyMap["unit"]="unit";                            // [V2.03l] 单位映射
    // [V2.03l 2026-06-30] Bug修复：dbCols缺少supplier/unit列，导致入库时无法更新供应商字段
    // 举一反三：ToolService::checkinTool用dao.update更新pending→in_stock时需要更新supplier
    QStringList dbCols={"tool_name","spec","tool_code","category_id","cabinet_id","machine_group_id","layer","position","total_qty","current_qty","vision_tag","status","supplier","unit","recognition_method","document_path"};
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

bool ToolDAO::softDelete(int toolId) {
    QSqlDatabase db=getDb(); QSqlQuery q(db);
    q.prepare("UPDATE tool_info SET status='maintenance' WHERE tool_id=:id");
    q.bindValue(":id",toolId);
    return safeExec(q);
}

bool ToolDAO::updateStock(int toolId, int delta) {
    QSqlDatabase db=getDb(); QSqlQuery q(db);
    q.prepare("UPDATE tool_info SET current_qty=current_qty+:d WHERE tool_id=:id AND current_qty+:d2>=0");
    q.bindValue(":d",delta); q.bindValue(":d2",delta); q.bindValue(":id",toolId);
    if(!safeExec(q)){return false;}
    bool affected = q.numRowsAffected()>0;
    // [V7.9 2026-06-27] 同步status字段：current_qty<=0设为borrowed，>0设为in_stock
    // 作者：袁燕 - 修复出库后数量为0的工具仍出现在待出库列表的Bug
    if (affected) {
        QSqlQuery q2(db);
        q2.prepare("UPDATE tool_info SET status=CASE WHEN current_qty<=0 THEN 'borrowed' ELSE 'in_stock' END WHERE tool_id=:id");
        q2.bindValue(":id",toolId);
        if(!safeExec(q2)){}
    }
    return affected;
}

bool ToolDAO::updateStatus(int toolId, const QString& s) {
    QSqlDatabase db=getDb(); QSqlQuery q(db);
    q.prepare("UPDATE tool_info SET status=:s WHERE tool_id=:id");
    q.bindValue(":s",s); q.bindValue(":id",toolId);
    return safeExec(q);
}

// ═══════════════════════════════════════════════
// 实体类API（Controller层使用）— 从dao/ToolDAO.cpp合并
// ═══════════════════════════════════════════════
QList<ToolInfo> ToolDAO::findAllTools(int page, int pageSize, const QString& keyword,
                                       const QString& cat, const QString& cab, const QString& status,
                                       const QString& machineGroup) {
    // 位置维度查询 — 用映射表status判断位置占用
    // 映射表status: pending=待入库, in_stock=在库, borrowed=已借出
    // 不再依赖tool_info的cabinet_id/layer/position判断位置占用
    // 一个工具可在多个位置入库，tool_info只存基础信息
    // 位置维度查询必须包含mapping_id字段
    // 根因：原posSql缺少mpm.mapping_id，fromQuery读不到mappingId→onDetailTool中t.mappingId=0
    // →走兜底findByToolId(toolId)而非findByMappingId(mappingId)→显示所有位置的借用记录
    // 修复：posSql显式SELECT mpm.mapping_id，确保每个位置的ToolInfo.mappingId正确
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
    bool filterCheckedOut = (status == "checked_out");
    bool filterPending = (status == "pending");
    bool filterInStockBorrowed = (!status.isEmpty() && !filterCheckedOut && !filterPending);

    if (filterInStockBorrowed) {
        // [V2.08] 按映射表status筛选
        posSql += " AND mpm.status = ?"; params << status;
        nonPosSql = "";
    } else if (filterCheckedOut) {
        // 只查checked_out（从tool_info）
        posSql = "";
    } else if (filterPending) {
        // [V2.08] pending=映射表status='pending'
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

int ToolDAO::countTools(const QString& keyword, const QString& cat,
                         const QString& cab, const QString& status,
                         const QString& machineGroup) {
    // 计数同步findAllTools的位置维度逻辑
    // 位置维度：所有映射表位置（有工具占用的显示在库/已借用，空闲的显示待入库）
    // 非位置维度：checked_out（不在位置上的已出库工具）
    bool filterCheckedOut = (status == "checked_out");
    bool filterPending = (status == "pending");
    bool filterInStockBorrowed = (!status.isEmpty() && !filterCheckedOut && !filterPending);

    int total = 0;
    QVariantList posParams, nonPosParams;

    // 位置维度计数
    if (!filterCheckedOut) {
        // [V2.08] 用映射表status判断位置占用
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

ToolInfo ToolDAO::findToolById(int toolId) {
    // [V7.0] 增加machine_group JOIN
    QSqlQuery q = query("SELECT t.*, c.category_name, cb.cabinet_name, mg.group_name FROM tool_info t "
                         "LEFT JOIN tool_category c ON t.category_id=c.category_id "
                         "LEFT JOIN tool_cabinet cb ON t.cabinet_id=cb.cabinet_id "
                         "LEFT JOIN machine_group mg ON t.machine_group_id=mg.group_id "
                         "WHERE t.tool_id = ?", {toolId});
    if (q.next()) return fromQuery(q);
    return ToolInfo();
}

ToolInfo ToolDAO::findToolByCode(const QString& code) {
    // [V7.0] 增加machine_group JOIN
    QSqlQuery q = query("SELECT t.*, c.category_name, cb.cabinet_name, mg.group_name FROM tool_info t "
                         "LEFT JOIN tool_category c ON t.category_id=c.category_id "
                         "LEFT JOIN tool_cabinet cb ON t.cabinet_id=cb.cabinet_id "
                         "LEFT JOIN machine_group mg ON t.machine_group_id=mg.group_id "
                         "WHERE t.tool_code = ?", {code});
    if (q.next()) return fromQuery(q);
    return ToolInfo();
}

int ToolDAO::insertTool(const ToolInfo& t) {
    // [V7.9 2026-06-24] 增加machine_group_id列
    // [V2.01 2026-06-27] 增加recognition_method/document_path列
    return insertAndGetId("INSERT INTO tool_info (tool_code,tool_name,spec,category_id,"
                          "cabinet_id,machine_group_id,layer,position,total_qty,current_qty,vision_tag,status,"
                          "is_recommended,recognition_method,document_path) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                          {t.toolCode, t.toolName, t.spec, t.categoryId, t.cabinetId,
                           t.machineGroupId, t.layer, t.position, t.totalQty, t.currentQty, t.visionTag,
                           t.status, t.isRecommended, t.recognitionMethod, t.documentPath});
}

bool ToolDAO::updateTool(const ToolInfo& t) {
    // [V7.9 2026-06-24] 增加machine_group_id更新
    // [V2.01 2026-06-27] 增加recognition_method/document_path更新
    return execute("UPDATE tool_info SET tool_name=?,spec=?,category_id=?,cabinet_id=?,"
                   "machine_group_id=?,layer=?,position=?,total_qty=?,vision_tag=?,status=?,is_recommended=?,"
                   "recognition_method=?,document_path=? WHERE tool_id=?",
                   {t.toolName, t.spec, t.categoryId, t.cabinetId, t.machineGroupId,
                    t.layer, t.position, t.totalQty, t.visionTag, t.status, t.isRecommended,
                    t.recognitionMethod, t.documentPath, t.toolId});
}

bool ToolDAO::deleteToolById(int toolId) {
    return execute("DELETE FROM tool_info WHERE tool_id = ?", {toolId});
}

bool ToolDAO::updateToolStatus(int toolId, const QString& status) {
    return execute("UPDATE tool_info SET status=? WHERE tool_id=?", {status, toolId});
}

// [V2.02 2026-06-28] 轻量更新文档路径 — 详情页上传文档专用
// 作者：袁燕 — 只更新document_path一个字段，避免全字段updateTool的副作用
bool ToolDAO::updateDocumentPath(int toolId, const QString& docPath) {
    return execute("UPDATE tool_info SET document_path=? WHERE tool_id=?",
                   {docPath, toolId});
}

bool ToolDAO::borrowTool(int toolId, int qty) {
    return execute("UPDATE tool_info SET current_qty=current_qty-?, "
                   "status=IF(current_qty-?<=0,'borrowed','in_stock') WHERE tool_id=?",
                   {qty, qty, toolId});
}

bool ToolDAO::returnTool(int toolId, int qty) {
    return execute("UPDATE tool_info SET current_qty=current_qty+?, "
                   "status='in_stock' WHERE tool_id=?", {qty, toolId});
}

// ── 分类 ──
QList<ToolCategory> ToolDAO::allCategories() {
    QList<ToolCategory> list;
    QSqlQuery q = query("SELECT * FROM tool_category ORDER BY sort_order");
    while (q.next()) {
        ToolCategory category;
        category.categoryId   = q.value("category_id").toInt();
        category.categoryName = q.value("category_name").toString();
        category.parentId     = q.value("parent_id").toInt();
        category.sortOrder    = q.value("sort_order").toInt();
        category.icon         = q.value("icon").toString();
        list.append(category);
    }
    return list;
}

QStringList ToolDAO::allCategoryNames() {
    QStringList names;
    QSqlQuery q = query("SELECT category_name FROM tool_category ORDER BY sort_order");
    while (q.next()) names << q.value(0).toString();
    return names;
}

QList<ToolCabinet> ToolDAO::allCabinets() {
    QList<ToolCabinet> list;
    QSqlQuery q = query("SELECT * FROM tool_cabinet ORDER BY cabinet_id");
    while (q.next()) {
        ToolCabinet cabinet;
        cabinet.cabinetId   = q.value("cabinet_id").toInt();
        cabinet.cabinetName = q.value("cabinet_name").toString();
        cabinet.cabinetCode = q.value("cabinet_code").toString();
        cabinet.location    = q.value("location").toString();
        cabinet.ipAddress   = q.value("ip_address").toString();
        cabinet.status      = q.value("status").toString();
        list.append(cabinet);
    }
    return list;
}

QStringList ToolDAO::allCabinetNames() {
    QStringList names;
    QSqlQuery q = query("SELECT cabinet_name FROM tool_cabinet");
    while (q.next()) names << q.value(0).toString();
    return names;
}

// [V7.0] 工具统计数据 [2026-06-26v15] 增加borrowedQty字段，与v_tool_stats新列对齐
QJsonObject ToolDAO::getToolStats() {
    QJsonObject stats;
    // 统计按映射表status计算
    // 在库 = 映射表status='in_stock'
    // 已借出 = 映射表status='borrowed'
    // 待入库 = 映射表status='pending'
    // 已出库 = tool_info中checked_out状态（不在位置上）
    QSqlQuery q = query(
        "SELECT "
        "  (SELECT COUNT(*) FROM tool_position_mapping WHERE status='in_stock') AS in_stock_count, "
        "  (SELECT COUNT(*) FROM tool_position_mapping WHERE status='borrowed') AS borrowed_count, "
        "  (SELECT COUNT(*) FROM tool_position_mapping WHERE status='pending') AS pending_count, "
        "  (SELECT COUNT(*) FROM tool_info WHERE status='checked_out') AS checked_out_count, "
        "  (SELECT COUNT(*) FROM tool_info WHERE status='maintenance') AS maintenance_count"
    );
    if (q.next()) {
        int inStock = q.value("in_stock_count").toInt();
        int borrowed = q.value("borrowed_count").toInt();
        int pending = q.value("pending_count").toInt();
        int checkedOut = q.value("checked_out_count").toInt();
        int maintenance = q.value("maintenance_count").toInt();
        stats["inStockCount"]     = inStock;
        stats["borrowedCount"]    = borrowed;
        stats["pendingCount"]     = pending;
        stats["checkedOutCount"]  = checkedOut;
        stats["maintenanceCount"] = maintenance;
        stats["totalCount"]       = inStock + borrowed + pending + checkedOut;
    }
    return stats;
}

// [V7.0] 工程机组列表
QList<QJsonObject> ToolDAO::allMachineGroups() {
    QList<QJsonObject> list;
    QSqlQuery q = query(
        "SELECT mg.*, d.dept_name, "
        "(SELECT COUNT(*) FROM tool_info ti WHERE ti.machine_group_id = mg.group_id) AS tool_count "
        "FROM machine_group mg "
        "LEFT JOIN sys_department d ON mg.dept_id = d.dept_id "
        "ORDER BY mg.group_id"
    );
    while (q.next()) {
        QJsonObject obj;
        obj["groupId"]      = q.value("group_id").toInt();
        obj["groupName"]    = q.value("group_name").toString();
        obj["deptId"]       = q.value("dept_id").toInt();
        obj["deptName"]     = q.value("dept_name").toString();
        obj["leaderName"]   = q.value("leader_name").toString();
        obj["leaderPhone"]  = q.value("leader_phone").toString();
        obj["description"]  = q.value("description").toString();
        obj["status"]       = q.value("status").toString();
        obj["toolCount"]    = q.value("tool_count").toInt();
        obj["createdAt"]    = q.value("created_at").toString();
        list.append(obj);
    }
    return list;
}

// [V7.0] 机组详情
QJsonObject ToolDAO::getMachineGroupById(int groupId) {
    QJsonObject obj;
    QSqlQuery q = query(
        "SELECT mg.*, d.dept_name, "
        "(SELECT COUNT(*) FROM tool_info ti WHERE ti.machine_group_id = mg.group_id) AS tool_count "
        "FROM machine_group mg "
        "LEFT JOIN sys_department d ON mg.dept_id = d.dept_id "
        "WHERE mg.group_id = ?", {groupId}
    );
    if (q.next()) {
        obj["groupId"]      = q.value("group_id").toInt();
        obj["groupName"]    = q.value("group_name").toString();
        obj["deptId"]       = q.value("dept_id").toInt();
        obj["deptName"]     = q.value("dept_name").toString();
        obj["leaderName"]   = q.value("leader_name").toString();
        obj["leaderPhone"]  = q.value("leader_phone").toString();
        obj["description"]  = q.value("description").toString();
        obj["status"]       = q.value("status").toString();
        obj["toolCount"]    = q.value("tool_count").toInt();
    }
    return obj;
}

// 查询映射表中in_stock位置，返回 [{mappingId, cabinetId, layer, position, cabinetName}, ...]
QJsonArray ToolDAO::findInStockPositions(int toolId, int limit)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT mpm.mapping_id, mpm.cabinet_id, mpm.layer, mpm.position, "
              "cb.cabinet_name "
              "FROM tool_position_mapping mpm "
              "JOIN tool_cabinet cb ON mpm.cabinet_id=cb.cabinet_id "
              "WHERE mpm.tool_id=? AND mpm.status='in_stock' "
              "ORDER BY cb.cabinet_name, mpm.layer, mpm.position LIMIT ?");
    q.addBindValue(toolId);
    q.addBindValue(limit);
    safeExec(q);
    QJsonArray arr;
    while (q.next()) {
        QJsonObject obj;
        obj["mappingId"]   = q.value(0).toInt();
        obj["cabinetId"]   = q.value(1).toInt();
        obj["layer"]       = q.value(2).toString();
        obj["position"]    = q.value(3).toString();
        obj["cabinetName"] = q.value(4).toString();
        arr.append(obj);
    }
    return arr;
}

// 更新映射表status（checked_out/pending/in_stock等）
bool ToolDAO::updateMappingStatus(int mappingId, const QString& status)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("UPDATE tool_position_mapping SET status=? WHERE mapping_id=?");
    q.addBindValue(status);
    q.addBindValue(mappingId);
    if (!safeExec(q)) {
        return false;
    }
    return true;
}

// 查找待入库工具（映射表中有status='pending'空闲位置）
QJsonArray ToolDAO::findPendingTools(int categoryId, int machineGroupId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    if (categoryId <= 0) {
        q.prepare(
            "SELECT ti.tool_id, ti.tool_code, ti.tool_name, ti.status, "
            "(SELECT COUNT(*) FROM tool_position_mapping mpm "
            " WHERE mpm.tool_id=ti.tool_id AND mpm.status='pending') AS free_pos_count "
            "FROM tool_info ti "
            "WHERE ti.machine_group_id=? "
            "  AND ti.status IN ('pending','in_stock','checked_out') "
            "  AND EXISTS (SELECT 1 FROM tool_position_mapping m WHERE m.tool_id=ti.tool_id AND m.status='pending') "
            "ORDER BY ti.tool_name");
        q.addBindValue(machineGroupId);
    } else {
        q.prepare(
            "SELECT ti.tool_id, ti.tool_code, ti.tool_name, ti.status, "
            "(SELECT COUNT(*) FROM tool_position_mapping mpm "
            " WHERE mpm.tool_id=ti.tool_id AND mpm.status='pending') AS free_pos_count "
            "FROM tool_info ti "
            "WHERE ti.category_id=? AND ti.machine_group_id=? "
            "  AND ti.status IN ('pending','in_stock','checked_out') "
            "  AND EXISTS (SELECT 1 FROM tool_position_mapping m WHERE m.tool_id=ti.tool_id AND m.status='pending') "
            "ORDER BY ti.tool_name");
        q.addBindValue(categoryId);
        q.addBindValue(machineGroupId);
    }
    safeExec(q);
    QJsonArray arr;
    while (q.next()) {
        QJsonObject obj;
        obj["toolId"]        = q.value(0).toInt();
        obj["toolCode"]      = q.value(1).toString();
        obj["toolName"]      = q.value(2).toString();
        obj["status"]        = q.value(3).toString();
        obj["freePosCount"]  = q.value(4).toInt();
        arr.append(obj);
    }
    return arr;
}

// 查找工具基本信息
QJsonObject ToolDAO::findToolBasicInfo(int toolId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT tool_name, tool_code, spec FROM tool_info WHERE tool_id=?");
    q.addBindValue(toolId);
    safeExec(q);
    QJsonObject obj;
    if (q.next()) {
        obj["toolName"] = q.value(0).toString();
        obj["toolCode"] = q.value(1).toString();
        obj["spec"]     = q.value(2).toString();
    }
    return obj;
}

// 查找工具的待入库位置（映射表status='pending'）
QJsonArray ToolDAO::findPendingPositions(int toolId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare(
        "SELECT mpm.cabinet_id, mpm.layer, mpm.position, "
        "       cb.cabinet_name, cb.cabinet_code "
        "FROM tool_position_mapping mpm "
        "JOIN tool_cabinet cb ON mpm.cabinet_id = cb.cabinet_id "
        "WHERE mpm.tool_id=? AND mpm.status='pending' "
        "ORDER BY cb.cabinet_name, mpm.layer, mpm.position");
    q.addBindValue(toolId);
    safeExec(q);
    QJsonArray arr;
    while (q.next()) {
        QJsonObject obj;
        obj["cabinetId"]   = q.value(0).toInt();
        obj["layer"]       = q.value(1).toString();
        obj["position"]    = q.value(2).toString();
        obj["cabinetName"] = q.value(3).toString();
        obj["cabinetCode"] = q.value(4).toString();
        arr.append(obj);
    }
    return arr;
}

// ═══════════════════════════════════════════════
// 系统维护页：任务配置
// ═══════════════════════════════════════════════

QJsonArray ToolDAO::allTaskTypes()
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT type_id, type_name FROM task_type WHERE is_active=1 ORDER BY sort_order");
    safeExec(q);
    QJsonArray arr;
    while (q.next()) {
        QJsonObject obj;
        obj["typeId"]   = q.value(0).toInt();
        obj["typeName"] = q.value(1).toString();
        arr.append(obj);
    }
    return arr;
}

QJsonArray ToolDAO::findTaskTypeTools(int typeId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT ttt.tool_id, ti.tool_name, ti.tool_code, ti.spec, ttt.recommended_qty "
              "FROM task_type_tool ttt JOIN tool_info ti ON ttt.tool_id=ti.tool_id "
              "WHERE ttt.type_id=? ORDER BY ti.tool_name");
    q.addBindValue(typeId);
    safeExec(q);
    QJsonArray arr;
    while (q.next()) {
        QJsonObject obj;
        obj["toolId"]         = q.value(0).toInt();
        obj["toolName"]       = q.value(1).toString();
        obj["toolCode"]       = q.value(2).toString();
        obj["spec"]           = q.value(3).toString();
        obj["recommendedQty"] = q.value(4).toInt();
        arr.append(obj);
    }
    return arr;
}

bool ToolDAO::updateTaskTypeToolQty(int typeId, int toolId, int qty)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("UPDATE task_type_tool SET recommended_qty=? WHERE type_id=? AND tool_id=?");
    q.addBindValue(qty);
    q.addBindValue(typeId);
    q.addBindValue(toolId);
    return safeExec(q);
}

bool ToolDAO::checkTaskTypeToolExists(int typeId, int toolId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT 1 FROM task_type_tool WHERE type_id=? AND tool_id=?");
    q.addBindValue(typeId);
    q.addBindValue(toolId);
    safeExec(q);
    return q.next();
}

bool ToolDAO::addTaskTypeTool(int typeId, int toolId, int qty)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("INSERT INTO task_type_tool (type_id, tool_id, recommended_qty) VALUES (?, ?, ?)");
    q.addBindValue(typeId);
    q.addBindValue(toolId);
    q.addBindValue(qty);
    return safeExec(q);
}

bool ToolDAO::deleteTaskTypeTool(int typeId, int toolId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("DELETE FROM task_type_tool WHERE type_id=? AND tool_id=?");
    q.addBindValue(typeId);
    q.addBindValue(toolId);
    return safeExec(q);
}

// ═══════════════════════════════════════════════
// 系统维护页：工具维护
// ═══════════════════════════════════════════════

QJsonArray ToolDAO::allToolsForMaintenance()
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT t.tool_id, t.tool_code, t.tool_name, c.category_name, t.spec, t.unit, t.status "
           "FROM tool_info t LEFT JOIN tool_category c ON t.category_id=c.category_id "
           "ORDER BY t.tool_id");
    safeExec(q);
    QJsonArray arr;
    while (q.next()) {
        QJsonObject obj;
        obj["toolId"]       = q.value(0).toInt();
        obj["toolCode"]     = q.value(1).toString();
        obj["toolName"]     = q.value(2).toString();
        obj["categoryName"] = q.value(3).toString();
        obj["spec"]         = q.value(4).toString();
        obj["unit"]         = q.value(5).toString();
        obj["status"]       = q.value(6).toString();
        arr.append(obj);
    }
    return arr;
}

QJsonObject ToolDAO::findToolForEdit(int toolId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT tool_name, tool_code, category_id, spec, unit, supplier, recognition_method, document_path "
              "FROM tool_info WHERE tool_id=?");
    q.addBindValue(toolId);
    safeExec(q);
    QJsonObject obj;
    if (q.next()) {
        obj["toolName"]          = q.value(0).toString();
        obj["toolCode"]          = q.value(1).toString();
        obj["categoryId"]        = q.value(2).toInt();
        obj["spec"]              = q.value(3).toString();
        obj["unit"]              = q.value(4).toString();
        obj["supplier"]          = q.value(5).toString();
        obj["recognitionMethod"] = q.value(6).toString();
        obj["documentPath"]      = q.value(7).toString();
    }
    return obj;
}

bool ToolDAO::insertToolFull(const QJsonObject& tool)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("INSERT INTO tool_info (tool_name, tool_code, category_id, spec, unit, supplier, "
              "recognition_method, document_path, status, total_qty, current_qty, machine_group_id) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'pending', 1, 1, ?)");
    q.addBindValue(tool["toolName"].toString());
    q.addBindValue(tool["toolCode"].toString());
    q.addBindValue(tool["categoryId"].toInt());
    q.addBindValue(tool["spec"].toString());
    q.addBindValue(tool["unit"].toString());
    q.addBindValue(tool["supplier"].toString());
    q.addBindValue(tool["recognitionMethod"].toString());
    q.addBindValue(tool["documentPath"].toString());
    q.addBindValue(tool["machineGroupId"].toInt());
    return safeExec(q);
}

bool ToolDAO::updateToolFull(int toolId, const QJsonObject& updates)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("UPDATE tool_info SET tool_name=?, tool_code=?, category_id=?, spec=?, unit=?, "
              "supplier=?, recognition_method=?, document_path=? WHERE tool_id=?");
    q.addBindValue(updates["toolName"].toString());
    q.addBindValue(updates["toolCode"].toString());
    q.addBindValue(updates["categoryId"].toInt());
    q.addBindValue(updates["spec"].toString());
    q.addBindValue(updates["unit"].toString());
    q.addBindValue(updates["supplier"].toString());
    q.addBindValue(updates["recognitionMethod"].toString());
    q.addBindValue(updates["documentPath"].toString());
    q.addBindValue(toolId);
    return safeExec(q);
}

bool ToolDAO::deleteToolFully(int toolId)
{
    QSqlDatabase db = getDb();
    // 先删除映射表记录
    QSqlQuery delMapping(db);
    delMapping.prepare("DELETE FROM tool_position_mapping WHERE tool_id=?");
    delMapping.addBindValue(toolId);
    safeExec(delMapping);
    // 再删除工具记录
    QSqlQuery q(db);
    q.prepare("DELETE FROM tool_info WHERE tool_id=?");
    q.addBindValue(toolId);
    return safeExec(q);
}

// ═══════════════════════════════════════════════
// 系统维护页：位置对照
// ═══════════════════════════════════════════════

QJsonArray ToolDAO::allToolsSimple()
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT tool_id, tool_code, tool_name FROM tool_info ORDER BY tool_id");
    safeExec(q);
    QJsonArray arr;
    while (q.next()) {
        QJsonObject obj;
        obj["toolId"]   = q.value(0).toInt();
        obj["toolCode"] = q.value(1).toString();
        obj["toolName"] = q.value(2).toString();
        arr.append(obj);
    }
    return arr;
}

QJsonObject ToolDAO::checkPositionMappingExists(int cabinetId, const QString& layer, const QString& position)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT m.mapping_id, t.tool_name FROM tool_position_mapping m "
              "JOIN tool_info t ON m.tool_id=t.tool_id "
              "WHERE m.cabinet_id=? AND m.layer=? AND m.position=?");
    q.addBindValue(cabinetId);
    q.addBindValue(layer);
    q.addBindValue(position);
    safeExec(q);
    QJsonObject obj;
    if (q.next()) {
        obj["mappingId"] = q.value(0).toInt();
        obj["occupier"]  = q.value(1).toString();
    }
    return obj;
}

bool ToolDAO::insertPositionMapping(int toolId, int cabinetId, const QString& layer, const QString& position, const QString& status)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("INSERT INTO tool_position_mapping (tool_id, cabinet_id, layer, position, status) VALUES (?, ?, ?, ?, ?)");
    q.addBindValue(toolId);
    q.addBindValue(cabinetId);
    q.addBindValue(layer);
    q.addBindValue(position);
    q.addBindValue(status);
    return safeExec(q);
}

QJsonObject ToolDAO::findPositionMappingDetail(int mappingId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT m.cabinet_id, m.layer, m.position, t.tool_name, t.status "
              "FROM tool_position_mapping m JOIN tool_info t ON m.tool_id=t.tool_id "
              "WHERE m.mapping_id=?");
    q.addBindValue(mappingId);
    safeExec(q);
    QJsonObject obj;
    if (q.next()) {
        obj["cabinetId"] = q.value(0).toInt();
        obj["layer"]     = q.value(1).toString();
        obj["position"]  = q.value(2).toString();
        obj["toolName"]  = q.value(3).toString();
        obj["toolStatus"] = q.value(4).toString();
    }
    return obj;
}

bool ToolDAO::isPositionMappingOccupied(int mappingId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT m.status FROM tool_position_mapping m WHERE m.mapping_id=? AND m.status IN ('in_stock','borrowed')");
    q.addBindValue(mappingId);
    safeExec(q);
    return q.next();
}

bool ToolDAO::deletePositionMapping(int mappingId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("DELETE FROM tool_position_mapping WHERE mapping_id=?");
    q.addBindValue(mappingId);
    return safeExec(q);
}

QJsonArray ToolDAO::findAllPositionMappings()
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT m.mapping_id, t.tool_name, t.tool_code, "
           "c.cabinet_name, m.layer, m.position, "
           "m.status AS position_status "
           "FROM tool_position_mapping m "
           "JOIN tool_info t ON m.tool_id=t.tool_id "
           "JOIN tool_cabinet c ON m.cabinet_id=c.cabinet_id "
           "ORDER BY c.cabinet_name, m.layer, m.position");
    safeExec(q);
    QJsonArray arr;
    while (q.next()) {
        QJsonObject obj;
        obj["mappingId"]      = q.value(0).toInt();
        obj["toolName"]       = q.value(1).toString();
        obj["toolCode"]       = q.value(2).toString();
        obj["cabinetName"]    = q.value(3).toString();
        obj["layer"]          = q.value(4).toString();
        obj["position"]       = q.value(5).toString();
        obj["positionStatus"] = q.value(6).toString();
        arr.append(obj);
    }
    return arr;
}

// 按分类名查询category_id，找不到返回-1
int ToolDAO::findCategoryIdByName(const QString& name)
{
    if (name.isEmpty()) return -1;
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT category_id FROM tool_category WHERE category_name=? LIMIT 1");
    q.addBindValue(name);
    if (safeExec(q) && q.next()) return q.value(0).toInt();
    return -1;
}

// 按柜体名查询cabinet_id，找不到返回-1
int ToolDAO::findCabinetIdByName(const QString& name)
{
    if (name.isEmpty()) return -1;
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT cabinet_id FROM tool_cabinet WHERE cabinet_name=? LIMIT 1");
    q.addBindValue(name);
    if (safeExec(q) && q.next()) return q.value(0).toInt();
    return -1;
}

// 统计工具in_stock位置数（迁移自ToolService::checkinTool COUNT查询）
int ToolDAO::countInStockPositions(int toolId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT COUNT(*) FROM tool_position_mapping WHERE tool_id=? AND status='in_stock'");
    q.addBindValue(toolId);
    if (safeExec(q) && q.next()) return q.value(0).toInt();
    return 0;
}

// 查询映射表状态（迁移自BorrowService::borrowTool状态校验）
QString ToolDAO::findMappingStatus(int mappingId)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT status FROM tool_position_mapping WHERE mapping_id=?");
    q.addBindValue(mappingId);
    if (safeExec(q) && q.next()) return q.value(0).toString();
    return QString();
}

// 按位置条件更新映射表状态（原子CAS，防止并发覆盖）
// 入参：toolId工具ID, cabinetId柜体ID, layer层, position位, newStatus目标状态, expectedStatus期望当前状态
// 返回：true=更新成功（匹配到1行），false=更新失败（无匹配行或状态已变更）
bool ToolDAO::updateMappingByPosition(int toolId, int cabinetId, const QString& layer,
                                       const QString& position, const QString& newStatus,
                                       const QString& expectedStatus)
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("UPDATE tool_position_mapping SET status=? "
              "WHERE tool_id=? AND cabinet_id=? AND layer=? AND position=? AND status=?");
    q.addBindValue(newStatus);
    q.addBindValue(toolId);
    q.addBindValue(cabinetId);
    q.addBindValue(layer);
    q.addBindValue(position);
    q.addBindValue(expectedStatus);
    return safeExec(q) && q.numRowsAffected() > 0;
}

} // namespace db
