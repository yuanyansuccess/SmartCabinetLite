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


// ==============================================================
// 【② 工具查询（JSON 接口）
//   列表/按位置/在库筛选/按ID/按编号查询
// ==============================================================
// ═══════════════════════════════════════════════
// QJsonObject API（Service层使用）
// ═══════════════════════════════════════════════
/**
 * @brief 分页查询工具列表，支持关键字、分类、状态、柜体与机组过滤
 * @return 含 list 数组与 total 总数的对象
 */
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
        // 防御：in_stock状态必须current_qty>0，过滤数量为0的工具
        // 确保出库后数量为0的工具不出现在待出库/待借用列表
        if (status == SC::TOOL_IN_STOCK) {
            conditions << "ti.current_qty > 0";
        }
    }
    if (cabinetId > 0) { conditions << "ti.cabinet_id = :cid"; bindValues[":cid"] = cabinetId; }
    // 机组隔离：只返回本机组工具，防止跨机组借用
    if (machineGroupId > 0) { conditions << "ti.machine_group_id = :mgid"; bindValues[":mgid"] = machineGroupId; }
    QString where = conditions.join(" AND ");

    QSqlQuery cq(db);
    cq.prepare("SELECT COUNT(*) FROM tool_info ti LEFT JOIN tool_category tc ON ti.category_id=tc.category_id WHERE " + where);
    for (auto it = bindValues.begin(); it != bindValues.end(); ++it) cq.bindValue(it.key(), it.value());
    safeExec(cq); cq.next(); int total = cq.value(0).toInt();

    QSqlQuery dq(db);
    // LEFT JOIN映射表为position-based匹配
    // 原条件：mpm.tool_id=ti.tool_id AND mpm.cabinet_id=ti.cabinet_id AND ...
    // 问题：多件入库时后续件是新tool_info记录(tool_id不同)，但位置与映射表一致
    // 注意：只按cabinet_id+layer+position匹配（一个位置只有一条映射记录，UNIQUE约束保证）
    dq.prepare(
        "SELECT ti.tool_id, ti.tool_name, ti.spec, ti.tool_code, tc.category_name AS category, "
        "ti.cabinet_id, cb.cabinet_name, ti.machine_group_id, mg.group_name AS machine_group_name, "
        "ti.layer, ti.position, "
        // 映射表位置（按位置匹配，权威数据源）
        "mpm.cabinet_id AS mpm_cab_id, mpm_cb.cabinet_name AS mpm_cab_name, mpm.layer AS mpm_layer, mpm.position AS mpm_pos, "
        "ti.total_qty, ti.current_qty, "
        "ti.vision_tag, ti.status, ti.checkout_reason, ti.is_recommended, "
        "ti.recognition_method, ti.document_path, ti.created_at "
        "FROM tool_info ti LEFT JOIN tool_category tc ON ti.category_id=tc.category_id "
        "LEFT JOIN tool_cabinet cb ON ti.cabinet_id=cb.cabinet_id "
        "LEFT JOIN machine_group mg ON ti.machine_group_id=mg.group_id "
        // LEFT JOIN映射表：按位置匹配（不要求tool_id匹配）
        "LEFT JOIN tool_position_mapping mpm ON mpm.cabinet_id=ti.cabinet_id AND mpm.layer=ti.layer AND mpm.position=ti.position "
        "LEFT JOIN tool_cabinet mpm_cb ON mpm.cabinet_id=mpm_cb.cabinet_id "
        // 排序为按类别+位置（唯一标识排列）
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
        // 位置信息：优先从映射表取（权威数据源），映射表无匹配则从tool_info取
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
        item["machineGroupId"] = dq.value("machine_group_id").toInt();
        item["machineGroupName"] = dq.value("machine_group_name").toString();
        item["layer"] = layerStr;
        item["rawPosition"] = posStr;  // 原始位号
        // 位置格式化为 柜号-层号-位号（两位补零，如A-01-03）
        item["position"] = common::formatPosition(cabName, layerStr, posStr);
        item["totalQty"] = dq.value("total_qty").toInt();
        item["currentQty"] = dq.value("current_qty").toInt();
        item["visionTag"] = dq.value("vision_tag").toString();
        item["status"] = dq.value("status").toString();
        item["isRecommended"] = dq.value("is_recommended").toBool();
        item["recognitionMethod"] = dq.value("recognition_method").toString();
        item["documentPath"] = dq.value("document_path").toString();
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
        item["unit"] = dq.value("unit").toString();  // 补充unit字段（借用页面用到）
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
 * @brief 按工具编号查询工具基本信息
 * @param code 工具编号
 * @return 含工具ID、名称、编号、当前数量与状态的对象；不存在时返回空对象
 */
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


// ==============================================================
// 【③ 工具写入（JSON 接口）
//   新增/更新/软删/库存与状态变更
// ==============================================================
/**
 * @brief 新增工具记录
 * @param info 工具字段集合，键名使用驼峰形式；缺省值取 SC:: 常量
 * @return 新记录的工具ID；写入失败返回 -1
 */
int ToolDAO::insert(const QJsonObject& info) {
    QSqlDatabase db = getDb(); QSqlQuery q(db);
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
    q.bindValue(":st",info["status"].toString(SC::TOOL_IN_STOCK));
    q.bindValue(":rm",info["recognitionMethod"].toString(SC::RECOGNITION_VISION));  // 默认视觉
    q.bindValue(":dp",info["documentPath"].toString(""));  // 文档路径
    if(!safeExec(q)){return -1;}
    return q.lastInsertId().toInt();
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
    keyMap["totalQty"]="total_qty"; keyMap["currentQty"]="current_qty";
    keyMap["categoryId"]="category_id"; keyMap["cabinetId"]="cabinet_id";
    keyMap["machineGroupId"]="machine_group_id";  // 机组ID映射
    keyMap["visionTag"]="vision_tag";
    keyMap["recognitionMethod"]="recognition_method";  // 识别方式映射
    keyMap["documentPath"]="document_path";  // 文档路径映射
    keyMap["supplier"]="supplier";  // 供应商映射
    keyMap["unit"]="unit";  // 单位映射
    // 注意：dbCols 需覆盖供应商与单位，否则入库时更新不到这两个字段
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

/**
 * @brief 停用工具：状态置为维护中，保留记录不物理删除
 * @param toolId 工具ID
 * @return true=状态已更新
 */
bool ToolDAO::softDelete(int toolId) {
    QSqlDatabase db=getDb(); QSqlQuery q(db);
    q.prepare("UPDATE tool_info SET status='maintenance' WHERE tool_id=:id");
    q.bindValue(":id",toolId);
    return safeExec(q);
}

/**
 * @brief 调整工具在库数量，并按调整结果同步工具状态
 * @param toolId 工具ID
 * @param delta 增减数量（负数为减少），调整后数量不允许为负
 * @return true=数量已变更；false=库存不足或写入失败
 */
bool ToolDAO::updateStock(int toolId, int delta) {
    QSqlDatabase db=getDb(); QSqlQuery q(db);
    q.prepare("UPDATE tool_info SET current_qty=current_qty+:d WHERE tool_id=:id AND current_qty+:d2>=0");
    q.bindValue(":d",delta); q.bindValue(":d2",delta); q.bindValue(":id",toolId);
    if(!safeExec(q)){return false;}
    bool affected = q.numRowsAffected()>0;
    // 数量归零即视为借出，否则回到在库，保证状态与数量一致
    if (affected) {
        QSqlQuery q2(db);
        q2.prepare("UPDATE tool_info SET status=CASE WHEN current_qty<=0 THEN 'borrowed' ELSE 'in_stock' END WHERE tool_id=:id");
        q2.bindValue(":id",toolId);
        if(!safeExec(q2)){}
    }
    return affected;
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
