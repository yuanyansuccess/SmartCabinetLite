/**
 * @file DatabaseManagerSchema.cpp
 * @brief DatabaseManager 物理拆分 — Schema/视图 DDL（initSchemaIfNeeded + createViewsIfNeeded）
 * @author 袁燕
 *
 * 说明：由 DatabaseManager.cpp 按职责拆分而来，类声明仍在 DatabaseManager.h，仅实现分文件。
 * 新增方法请按职责放到对应文件，不要全部堆回 DatabaseManager.cpp。
 */
#include "DatabaseManager.h"
#include "Logger.h"  // 统一日志写入入口（Log::appendLog）
#include <QCryptographicHash>  // 播种默认管理员密码SHA256
#include <QStandardPaths>
#include <QDebug>

// 自动建表+播种默认管理员 (CF001/123456)
bool DatabaseManager::initSchemaIfNeeded() {
    QSqlQuery q(m_db);

    // 诊断lambda：记录建表/播种失败信息到文件（统一走 Logger 入口）
    auto logFail = [](const char* step, const QString& err) {
        Log::appendLog(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/db_schema.log",
                       QStringLiteral("FAIL[%1] %2").arg(QString::fromLatin1(step), err));
    };

    // 不整体跳过schema初始化（早期return会阻止新增表）
    // 每个CREATE TABLE用的都是IF NOT EXISTS，安全幂等
    // 种子数据使用INSERT OR IGNORE，也安全幂等
    bool hasExistingSchema = false;
    {
        QSqlQuery check(m_db);
        // 删除sqlite_master分支——纯MySQL模式
        check.exec("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='sys_user'");
        hasExistingSchema = (check.next() && check.value(0).toInt() > 0);
    }

  // MySQL中表已存在时跳过建表（AUTO_INCREMENT是SQLite专用语法，MySQL用AUTO_INCREMENT）
    // MySQL在解析SQL时就检查语法，即使表已存在CREATE TABLE IF NOT EXISTS也会因AUTO_INCREMENT语法错误而失败
    // MySQL表由schema.sql预先创建，这里只需播种数据
    // 删除m_usingSqlite判断——纯MySQL模式
    bool skipCreateTables = hasExistingSchema;

    qInfo() << "[DB] First run: initializing schema...";

  // MySQL中表已存在时跳过建表（AUTO_INCREMENT语法不兼容MySQL）
    if (!skipCreateTables) {
    // ── 建表：sys_user ──
    bool ok = q.exec(
        "CREATE TABLE IF NOT EXISTS sys_user ("
        "  user_id       INTEGER PRIMARY KEY AUTO_INCREMENT,"
        "  username      VARCHAR(255)    NOT NULL UNIQUE,"
        "  password_hash TEXT    NOT NULL,"
        "  password_salt TEXT    NOT NULL,"
        "  real_name     TEXT    NOT NULL,"
        "  work_no       VARCHAR(255)    NOT NULL UNIQUE,"
        "  dept_id       INTEGER DEFAULT 0,"
        "  department    VARCHAR(255) DEFAULT '',"
        "  role          VARCHAR(255) DEFAULT 'user',"
        "  face_feature  TEXT    DEFAULT NULL,"
        "  phone         VARCHAR(255) DEFAULT '',"
        "  email         VARCHAR(255) DEFAULT '',"
        "  status        VARCHAR(255) DEFAULT 'active',"
        "  last_login_at TEXT    DEFAULT NULL,"
        "  created_at    TEXT,"
        "  updated_at    TEXT"
        ")"
    );
    if (!ok) { logFail("sys_user", q.lastError().text()); qWarning() << "[DB] Create sys_user failed:" << q.lastError().text(); return false; }

    // 建表：sys_department（部门表，dao/UserDAO::findAll LEFT JOIN需要）
    ok = q.exec(
        "CREATE TABLE IF NOT EXISTS sys_department ("
        "  dept_id       INTEGER PRIMARY KEY AUTO_INCREMENT,"
        "  dept_name     TEXT    NOT NULL,"
        "  parent_id     INTEGER DEFAULT 0,"
        "  sort_order    INTEGER DEFAULT 0,"
        "  created_at    TEXT,"
        "  updated_at    TEXT"
        ")"
    );
    if (!ok) { logFail("sys_department", q.lastError().text()); qWarning() << "[DB] Create sys_department failed:" << q.lastError().text(); return false; }

    // ── 建表：tool_category（工具分类） ──
    ok = q.exec(
        "CREATE TABLE IF NOT EXISTS tool_category ("
        "  category_id   INTEGER PRIMARY KEY AUTO_INCREMENT,"
        "  category_name TEXT    NOT NULL,"
        "  parent_id     INTEGER DEFAULT 0,"
        "  sort_order    INTEGER DEFAULT 0,"
        "  icon          VARCHAR(255) DEFAULT '',"
        "  created_at    TEXT,"
        "  updated_at    TEXT"
        ")"
    );
    if (!ok) { logFail("tool_category", q.lastError().text()); qWarning() << "[DB] Create tool_category failed:" << q.lastError().text(); return false; }

    // ── 建表：tool_cabinet（工具柜） ──
    ok = q.exec(
        "CREATE TABLE IF NOT EXISTS tool_cabinet ("
        "  cabinet_id    INTEGER PRIMARY KEY AUTO_INCREMENT,"
        "  cabinet_name  TEXT    NOT NULL,"
        "  location      VARCHAR(255) DEFAULT '',"
        "  capacity      INTEGER DEFAULT 0,"
        "  status        VARCHAR(255) DEFAULT 'active',"
        "  created_at    TEXT,"
        "  updated_at    TEXT"
        ")"
    );
    if (!ok) { logFail("tool_cabinet", q.lastError().text()); qWarning() << "[DB] Create tool_cabinet failed:" << q.lastError().text(); return false; }

    // ── 建表：tool_info（工具信息） 列名对齐MySQL schema.sql
    // 新增 recognition_method/document_path 列
    ok = q.exec(
        "CREATE TABLE IF NOT EXISTS tool_info ("
        "  tool_id          INTEGER PRIMARY KEY AUTO_INCREMENT,"
        "  tool_name        TEXT    NOT NULL,"
        "  tool_code        VARCHAR(255)    NOT NULL UNIQUE,"
        "  spec             VARCHAR(255) DEFAULT '',"
        "  category_id      INTEGER DEFAULT 0,"
        "  cabinet_id       INTEGER DEFAULT 0,"
        "  machine_group_id INTEGER DEFAULT NULL,"
        "  layer            VARCHAR(255) DEFAULT '',"
        "  position         VARCHAR(255) DEFAULT '',"
        "  total_qty        INTEGER DEFAULT 0,"
        "  current_qty      INTEGER DEFAULT 0,"
        "  vision_tag         VARCHAR(255) DEFAULT '',"
        "  status           VARCHAR(255) DEFAULT 'in_stock',"
        "  checkout_reason  VARCHAR(255) DEFAULT '',"
        "  is_recommended   INTEGER DEFAULT 0,"
        "  recognition_method VARCHAR(255) DEFAULT 'vision',"
        "  document_path    VARCHAR(255) DEFAULT '',"
        "  created_at       TEXT,"
        "  updated_at       TEXT"
        ")"
    );
    if (!ok) { logFail("tool_info", q.lastError().text()); qWarning() << "[DB] Create tool_info failed:" << q.lastError().text(); return false; }

    // 为已有SQLite数据库迁移：添加缺失列（IF NOT EXISTS）
    // SQLite不支持 ADD COLUMN IF NOT EXISTS，用try-catch忽略"duplicate column"错误
    const char* alterCols[] = {
        "ALTER TABLE tool_info ADD COLUMN spec VARCHAR(255) DEFAULT ''",
        "ALTER TABLE tool_info ADD COLUMN machine_group_id INTEGER DEFAULT NULL",
        "ALTER TABLE tool_info ADD COLUMN layer VARCHAR(255) DEFAULT ''",
        "ALTER TABLE tool_info ADD COLUMN current_qty INTEGER DEFAULT 0",
        "ALTER TABLE tool_info ADD COLUMN vision_tag VARCHAR(255) DEFAULT ''",
        "ALTER TABLE tool_info ADD COLUMN checkout_reason VARCHAR(255) DEFAULT ''",
        "ALTER TABLE tool_info ADD COLUMN is_recommended INTEGER DEFAULT 0",
        "ALTER TABLE tool_info ADD COLUMN recognition_method VARCHAR(255) DEFAULT 'vision'",
        "ALTER TABLE tool_info ADD COLUMN document_path VARCHAR(255) DEFAULT ''",
    };
    for (const char* alterSql : alterCols) {
        q.exec(alterSql);  // 忽略"duplicate column"错误
    }
    // 迁移：将available_qty数据复制到current_qty
    q.exec("UPDATE tool_info SET current_qty = available_qty WHERE current_qty = 0 AND available_qty > 0");

    // 建表：machine_group（工程机组） ──
    ok = q.exec(
        "CREATE TABLE IF NOT EXISTS machine_group ("
        "  group_id      INTEGER PRIMARY KEY AUTO_INCREMENT,"
        "  group_name    VARCHAR(255)    NOT NULL UNIQUE,"
        "  dept_id       INTEGER DEFAULT NULL,"
        "  leader_name   VARCHAR(255) DEFAULT '',"
        "  leader_phone  VARCHAR(255) DEFAULT '',"
        "  description   TEXT,"
        "  status        VARCHAR(255) DEFAULT 'active',"
        "  created_at    TEXT,"
        "  updated_at    TEXT"
        ")"
    );
    if (!ok) { logFail("machine_group", q.lastError().text()); qWarning() << "[DB] Create machine_group failed:" << q.lastError().text(); return false; }

    // 建表：system_config（系统配置持久化）
    // TIMESTAMP类型+CURRENT_TIMESTAMP兼容MySQL5.7+SQLite（TEXT类型在MySQL不支持DEFAULT CURRENT_TIMESTAMP）
    ok = q.exec(
        "CREATE TABLE IF NOT EXISTS system_config ("
        "  config_key   VARCHAR(64) PRIMARY KEY,"
        "  config_value TEXT,"
        "  updated_at   TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ")"
    );
    if (!ok) { logFail("system_config", q.lastError().text()); qWarning() << "[DB] Create system_config failed:" << q.lastError().text(); return false; }

    // ── 建表：tool_borrow_record（借用记录） ──
    // 增加 mapping_id 列（位置维度借用记录）
    ok = q.exec(
        "CREATE TABLE IF NOT EXISTS tool_borrow_record ("
        "  record_id            INTEGER PRIMARY KEY AUTO_INCREMENT,"
        "  flow_no              TEXT    NOT NULL,"
        "  tool_id              INTEGER NOT NULL,"
        "  user_id              INTEGER NOT NULL,"
        "  borrow_qty           INTEGER DEFAULT 1,"
        "  borrow_reason        VARCHAR(255) DEFAULT '',"
        "  borrow_time          TEXT    DEFAULT NULL,"
        "  expected_return_time TEXT    DEFAULT NULL,"
        "  actual_return_time   TEXT    DEFAULT NULL,"
        "  status               VARCHAR(255) DEFAULT 'borrowing',"
        "  operator_id          INTEGER DEFAULT NULL,"
        "  remark               VARCHAR(255) DEFAULT '',"
        "  mapping_id           INTEGER DEFAULT NULL,"  // 位置映射ID
        "  created_at           TEXT,"
        "  updated_at           TEXT"
        ")"
    );
    if (!ok) { logFail("tool_borrow_record", q.lastError().text()); qWarning() << "[DB] Create tool_borrow_record failed:" << q.lastError().text(); return false; }

    // 已有数据库迁移：tool_borrow_record增加mapping_id列
    q.exec("ALTER TABLE tool_borrow_record ADD COLUMN mapping_id INTEGER DEFAULT NULL");

    // 播种部门数据（人员管理页面需要）
  // REPLACE INTO兼容MySQL+SQLite
    q.exec("REPLACE INTO sys_department(dept_id, dept_name, sort_order) "
           "VALUES (1, '技术部', 1)");
    q.exec("REPLACE INTO sys_department(dept_id, dept_name, sort_order) "
           "VALUES (2, '生产部', 2)");
    q.exec("REPLACE INTO sys_department(dept_id, dept_name, sort_order) "
           "VALUES (3, '质控部', 3)");
    q.exec("REPLACE INTO sys_department(dept_id, dept_name, sort_order) "
           "VALUES (4, '仓储部', 4)");
    q.exec("REPLACE INTO sys_department(dept_id, dept_name, sort_order) "
           "VALUES (5, '行政部', 5)");

    // 播种工程机组数据（8个机组） ──
    struct MgSeed { int id; const char* name; int did; const char* leader; const char* phone; const char* desc; } mgs[] = {
        {1,"发动机维护机组",1,"张三","138****6789","负责发动机拆装、检查、更换部件等核心维护作业"},
        {2,"航电检修机组",2,"李四","139****8901","负责航空电子设备、仪表、通信导航系统检测维修"},
        {3,"液压系统机组",3,"王五","137****2345","负责液压管路、泵阀、作动筒检查与更换"},
        {4,"结构修理机组",4,"赵六","136****7890","负责机身蒙皮、框架、紧固件损伤修复"},
        {5,"起落架维护机组",1,"周八","133****9012","负责起落架减震、刹车系统、轮胎更换"},
        {6,"电气线路机组",2,"孙七","135****3456","负责线路故障排查、线束修复、接插件更换"},
        {7,"焊接作业机组",3,"李四","139****8901","负责金属结构焊接、修补、热处理"},
        {8,"精密测量机组",4,"赵六","136****7890","负责三坐标测量、形位公差检测、校准"},
    };
    for (auto& mg : mgs) {
  // REPLACE INTO兼容MySQL+SQLite
        q.prepare("REPLACE INTO machine_group(group_id,group_name,dept_id,leader_name,leader_phone,description,status) "
                  "VALUES(?,?,?,?,?,?,'active')");
        q.addBindValue(mg.id); q.addBindValue(mg.name); q.addBindValue(mg.did);
        q.addBindValue(mg.leader); q.addBindValue(mg.phone); q.addBindValue(mg.desc);
        q.exec();
    }

    // ── 播种默认管理员：CF001 / 123456 ──
    QString salt = "K7mP2xQ9vL5nR3";  // 固定盐值，与schema.sql一致
    QString pwdHash = QString(QCryptographicHash::hash(
        (salt + QStringLiteral("123456")).toUtf8(),
        QCryptographicHash::Sha256).toHex());

  // 先查后插：避免REPLACE INTO触发外键约束（tool_borrow_record引用sys_user.user_id）
    // MySQL和SQLite都兼容，已有记录时跳过不覆盖
    {
        QSqlQuery adminChk(m_db);
        adminChk.exec("SELECT COUNT(*) FROM sys_user WHERE user_id=1");
        int adminCnt = (adminChk.next()) ? adminChk.value(0).toInt() : 0;
        if (adminCnt == 0) {
            // 工号改纯数字：username=work_no=001（账号与工号统一）
            q.prepare("INSERT INTO sys_user "
                      "(user_id, username, password_hash, password_salt, real_name, "
                      " work_no, dept_id, department, role, phone, status) "
                      "VALUES (1, '001', :hash, :salt, '张三', "
                      " '001', 1, '技术部', 'admin', '138****6789', 'active')");
            q.bindValue(":hash", pwdHash);
            q.bindValue(":salt", salt);
            if (!q.exec()) { logFail("admin_user", q.lastError().text()); qWarning() << "[DB] Seed admin user failed:" << q.lastError().text(); return false; }
        }
    }

    // 播种测试用户数据（人员管理页面有数据可查）
    // 工号改纯数字：username=work_no统一为数字（002~006）
    struct TestUser { int id; QString uname; QString rname; QString wno; int did; QString dept; QString role; QString phone; QString status; };
    QList<TestUser> testUsers = {
        {2, "002",  "李四",   "002", 2, "生产部", "user", "139****1234", "active"},
        {3, "003",  "王五",   "003", 3, "质控部", "user", "137****5678", "active"},
        {4, "004",  "赵六",   "004", 4, "仓储部", "user", "136****9012", "active"},
        {5, "005",  "孙七",   "005", 1, "技术部", "user", "135****3456", "active"},
        {6, "006",  "周八",   "006", 2, "生产部", "user", "134****7890", "inactive"},
    };
    for (const auto& u : testUsers) {
  // 先查后插：避免REPLACE INTO触发外键约束
        QSqlQuery uChk(m_db);
        uChk.prepare("SELECT COUNT(*) FROM sys_user WHERE user_id=?");
        uChk.addBindValue(u.id);
        uChk.exec();
        int uCnt = (uChk.next()) ? uChk.value(0).toInt() : 0;
        if (uCnt > 0) continue;  // 已存在则跳过

        QSqlQuery ins(m_db);
        ins.prepare("INSERT INTO sys_user "
                    "(user_id, username, password_hash, password_salt, real_name, "
                    " work_no, dept_id, department, role, phone, status) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
        ins.addBindValue(u.id);
        ins.addBindValue(u.uname);
        ins.addBindValue(pwdHash);
        ins.addBindValue(salt);
        ins.addBindValue(u.rname);
        ins.addBindValue(u.wno);
        ins.addBindValue(u.did);
        ins.addBindValue(u.dept);
        ins.addBindValue(u.role);
        ins.addBindValue(u.phone);
        ins.addBindValue(u.status);
        if (!ins.exec()) { qWarning() << "[DB] Seed test user failed:" << u.rname << ins.lastError().text(); }
    }

  // 建表：task_type（任务类型）
    ok = q.exec(
        "CREATE TABLE IF NOT EXISTS task_type ("
        "  type_id       INTEGER PRIMARY KEY AUTO_INCREMENT,"
        "  type_code     VARCHAR(255)    NOT NULL UNIQUE,"
        "  type_name     TEXT    NOT NULL,"
        "  description   TEXT,"
        "  default_duration INTEGER DEFAULT 30,"
        "  sort_order    INTEGER DEFAULT 0,"
        "  is_active     INTEGER DEFAULT 1,"
        "  created_at    TEXT,"
        "  updated_at    TEXT"
        ")"
    );
    if (!ok) { logFail("task_type", q.lastError().text()); qWarning() << "[DB] Create task_type failed:" << q.lastError().text(); return false; }

  // 建表：task_type_tool（任务类型-推荐工具关联表）
    ok = q.exec(
        "CREATE TABLE IF NOT EXISTS task_type_tool ("
        "  id            INTEGER PRIMARY KEY AUTO_INCREMENT,"
        "  type_id       INTEGER NOT NULL,"
        "  tool_id       INTEGER NOT NULL,"
        "  sort_order    INTEGER DEFAULT 0,"
        "  created_at    TEXT,"
        "  FOREIGN KEY (type_id) REFERENCES task_type(type_id),"
        "  FOREIGN KEY (tool_id) REFERENCES tool_info(tool_id),"
        "  UNIQUE(type_id, tool_id)"
        ")"
    );
    if (!ok) { logFail("task_type_tool", q.lastError().text()); qWarning() << "[DB] Create task_type_tool failed:" << q.lastError().text(); return false; }

    // 建表：tool_position_mapping（工具-位置对照关系）
    // 设计理念：一个工具可对应多个位置（一对多），一个位置只对应一个工具（位置唯一）
    // 对照关系维护=配置层，tool_info.cabinet_id/layer/position=实际入库层
    // 入库时从映射表查该工具的可用位置（未被其他在库工具占用）
    // status字段移入建表语句，修复MySQL端建表漏字段的致命Bug
    // 根因：原MySQL兼容建表（建表块外）未包含status字段，导致入库UPDATE SET status失败
    // 注意：SQLite建表和MySQL建表都显式包含status字段，DEFAULT 'pending'
    ok = q.exec(
        "CREATE TABLE IF NOT EXISTS tool_position_mapping ("
        "  mapping_id    INTEGER PRIMARY KEY AUTO_INCREMENT,"
        "  tool_id       INTEGER NOT NULL,"
        "  cabinet_id    INTEGER NOT NULL,"
        "  layer         VARCHAR(255) NOT NULL,"
        "  position      VARCHAR(255) NOT NULL,"
        "  status        VARCHAR(255) DEFAULT 'pending',"
        "  created_at    TEXT,"
        "  FOREIGN KEY (tool_id) REFERENCES tool_info(tool_id),"
        "  FOREIGN KEY (cabinet_id) REFERENCES tool_cabinet(cabinet_id),"
        "  UNIQUE(cabinet_id, layer, position)"
        ")"
    );
    if (!ok) { logFail("tool_position_mapping", q.lastError().text()); qWarning() << "[DB] Create tool_position_mapping failed:" << q.lastError().text(); return false; }
    }  // 关闭 if (!skipCreateTables) 建表块

  // 为已有SQLite数据库迁移：添加缺失列（tool_info.cabinet_name兼容）
    q.exec("ALTER TABLE tool_info ADD COLUMN cabinet_name VARCHAR(255) DEFAULT ''");
    q.exec("ALTER TABLE tool_info ADD COLUMN unit VARCHAR(255) DEFAULT '件'");
    // tool_cabinet增加cabinet_code和ip_address列（对齐ToolCabinet模型）
    q.exec("ALTER TABLE tool_cabinet ADD COLUMN cabinet_code VARCHAR(255) DEFAULT ''");
    q.exec("ALTER TABLE tool_cabinet ADD COLUMN ip_address VARCHAR(255) DEFAULT ''");
    // 映射表增加status字段
    // 入库只更新映射表status，不新建tool_info记录
    // pending=待入库（配置了对照关系但未实际放入工具）
    // in_stock=在库，borrowed=已借出
    q.exec("ALTER TABLE tool_position_mapping ADD COLUMN status VARCHAR(16) DEFAULT 'pending'");
    // 删除启动时用tool_info重置映射表status的脏数据修复代码
    // 根因：这段代码每次启动都用tool_info的cabinet_id/layer/position匹配映射表重置status，
    // 但V2.08+已为位置维度管理，tool_info的位置不代表实际占用状态，
    // 导致已borrowed的位置被强制改回in_stock→数据不一致
    // 规则：代码层面不处理脏数据，所有数据修复直接在数据库操作
  // MySQL的task_type表缺少default_duration列，seedBusinessData的INSERT需要此列
    q.exec("ALTER TABLE task_type ADD COLUMN default_duration INTEGER DEFAULT 30");
  // MySQL的tool_info表缺少recognition_method/document_path列
    // MySQL 5.7不允许TEXT类型设DEFAULT值，VARCHAR兼容MySQL+SQLite
    q.exec("ALTER TABLE tool_info ADD COLUMN recognition_method VARCHAR(16) DEFAULT 'vision'");
    q.exec("ALTER TABLE tool_info ADD COLUMN document_path VARCHAR(512) DEFAULT ''");

    // MySQL兼容建表+旧数据迁移（在建表块外，不受skipCreateTables控制）
    // 根因：skipCreateTables=true时跳过建表，导致MySQL端无tool_position_mapping表
    // ODBC驱动报"Unable to execute statement"就是因为表不存在
    // 用MySQL兼容语法（AUTO_INCREMENT而非AUTO_INCREMENT，CURRENT_TIMESTAMP而非datetime()）
    q.exec("CREATE TABLE IF NOT EXISTS tool_position_mapping ("
           "  mapping_id    INT AUTO_INCREMENT PRIMARY KEY,"
           "  tool_id       INT NOT NULL,"
           "  cabinet_id    INT NOT NULL,"
           "  layer         VARCHAR(16) NOT NULL,"
           "  position      VARCHAR(16) NOT NULL,"
           "  status        VARCHAR(16) NOT NULL DEFAULT 'pending',"
           "  created_at    TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
           "  FOREIGN KEY (tool_id) REFERENCES tool_info(tool_id),"
           "  FOREIGN KEY (cabinet_id) REFERENCES tool_cabinet(cabinet_id),"
           "  UNIQUE(cabinet_id, layer, position)"
           ")");
    if (q.lastError().isValid()) {
        qWarning() << "[DB] Create tool_position_mapping (MySQL compat) error:" << q.lastError().text();
    }
    // 迁移旧数据：从tool_info中已有位置的工具导入到映射表
    // INSERT IGNORE避免重复（UNIQUE约束兜底）
    q.exec("INSERT IGNORE INTO tool_position_mapping (tool_id, cabinet_id, layer, position) "
           "SELECT tool_id, cabinet_id, layer, position FROM tool_info "
           "WHERE cabinet_id > 0 AND layer IS NOT NULL AND layer != '' AND position IS NOT NULL AND position != ''");
    if (q.lastError().isValid()) {
        qWarning() << "[DB] Migrate tool_position_mapping data error:" << q.lastError().text();
    }

    // 在播种数据前先确保视图存在（修复已有数据库视图缺失导致表格无数据的问题）
    createViewsIfNeeded();

    // 丰富SQLite种子数据：工具+借用记录+告警，确保仪表盘和各页面有数据可看
    seedBusinessData();

    if (hasExistingSchema) {
        qInfo() << "[DB] Schema updated (v4.6: added sys_department table). Default admin seeded (workNo CF001, initial password not logged)";
    } else {
        qInfo() << "[DB] Schema initialized (6 tables + 5 departments + 6 users). Default admin seeded (workNo CF001, initial password not logged)";
    }
    return true;
}

// 每次启动确保视图存在（解决已有数据库缺失v_tool_latest_operation等视图导致表格无数据的问题）
// 使用 CREATE OR REPLACE VIEW 兼容 MySQL 和 SQLite
bool DatabaseManager::createViewsIfNeeded() {
    QSqlQuery q(m_db);

    // v_tool_stats 工具统计视图 借用统计从tool_borrow_record获取，确保与记录一致
    // in_stock_qty 为所有工具的 current_qty 之和（不论状态），反映实际在库件数
    q.exec("CREATE OR REPLACE VIEW v_tool_stats AS "
           "SELECT (SELECT COUNT(*) FROM tool_info) AS total_tools,"
           "(SELECT COUNT(*) FROM tool_info WHERE status='in_stock') AS in_stock_count,"
           "(SELECT COUNT(DISTINCT tool_id) FROM tool_borrow_record WHERE status IN ('borrowing','overdue')) AS borrowed_count,"
           "(SELECT COUNT(*) FROM tool_info WHERE status='maintenance') AS maintenance_count,"
           "(SELECT COALESCE(SUM(total_qty),0) FROM tool_info) AS total_qty,"
           "(SELECT COALESCE(SUM(current_qty),0) FROM tool_info) AS in_stock_qty,"
           "(SELECT COALESCE(SUM(borrow_qty),0) FROM tool_borrow_record WHERE status IN ('borrowing','overdue')) AS borrowed_qty");
    if (q.lastError().isValid())
        qWarning() << "[DB] Create v_tool_stats failed:" << q.lastError().text();

    // v_tool_latest_operation 最近操作视图
    // 机组隔离：只统计活跃借用(borrowing/overdue)，已归还记录不作为最近操作
    // 修复视图产生重复行导致工具管理列表每位置多一行的致命Bug
    // 根因：原LEFT JOIN tool_borrow_record tbr ON borrow_time=latest_borrow_time，
    // 同一秒借用多条记录时tbr匹配多行→视图对同一tool_id返回多行→findAllTools行翻倍
    // 注意：latest_op_user子查询LIMIT 1，去掉tbr的JOIN，确保每个tool_id只返回1行
    q.exec("CREATE OR REPLACE VIEW v_tool_latest_operation AS "
           "SELECT t.tool_id,"
           "COALESCE(br.latest_borrow_time,'') AS latest_op_time,"
           "CASE "
           "  WHEN br.latest_borrow_time IS NOT NULL THEN 'borrow'"
           "  ELSE 'checkin'"
           "END AS latest_op_type,"
           "COALESCE((SELECT u.real_name FROM tool_borrow_record tbr2 "
           "          LEFT JOIN sys_user u ON u.user_id=tbr2.user_id "
           "          WHERE tbr2.tool_id=t.tool_id AND tbr2.status IN ('borrowing','overdue') "
           "          ORDER BY tbr2.borrow_time DESC LIMIT 1),'') AS latest_op_user "
           "FROM tool_info t "
           "LEFT JOIN ("
           "  SELECT tool_id, MAX(borrow_time) AS latest_borrow_time "
           "  FROM tool_borrow_record WHERE status IN ('borrowing','overdue') GROUP BY tool_id"
           ") br ON t.tool_id=br.tool_id");
    if (q.lastError().isValid())
        qWarning() << "[DB] Create v_tool_latest_operation failed:" << q.lastError().text();

    qInfo() << "[DB] Views ensured: v_tool_stats + v_tool_latest_operation";
    return true;
}
