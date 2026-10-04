/**
 * @file DatabaseManagerSeed.cpp
 * @brief DatabaseManager 物理拆分 — 业务演示数据播种（seedBusinessData）
 * @author 袁燕
 *
 * 说明：由 DatabaseManager.cpp 按职责拆分而来，类声明仍在 DatabaseManager.h，仅实现分文件。
 * 新增方法请按职责放到对应文件，不要全部堆回 DatabaseManager.cpp。
 */
#include "DatabaseManager.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>

// SQLite回退模式播种完整业务数据（工具/记录/告警/操作日志）
// 确保仪表盘4个卡片 + 各列表页面都有数据展示
bool DatabaseManager::seedBusinessData() {
    QSqlQuery q(m_db);

  // ── 工具分类（7个）─ 空表检测，避免覆盖已有生产数据 ──
    {
        QSqlQuery chk(m_db);
        chk.exec("SELECT COUNT(*) FROM tool_category");
        if (!chk.next() || chk.value(0).toInt() == 0) {
            struct { int id; const char* name; int pid; int sort; const char* icon; } cats[] = {
                {1,"电动工具",0,1,"🔌"}, {2,"手动工具",0,2,"🔧"}, {3,"测量工具",0,3,"📏"},
                {4,"焊接工具",0,4,"🔥"}, {5,"照明工具",0,5,"🔦"}, {6,"紧固工具",1,1,"🔩"},
                {7,"切割工具",2,1,"✂️"},
            };
            for (auto& c : cats) {
                q.prepare("INSERT INTO tool_category(category_id,category_name,parent_id,sort_order,icon) VALUES(?,?,?,?,?)");
                q.addBindValue(c.id); q.addBindValue(c.name); q.addBindValue(c.pid); q.addBindValue(c.sort); q.addBindValue(c.icon);
                q.exec();
            }
        }
    }

  // ── 工具柜（3个）空表检测 ──
    {
        QSqlQuery chk(m_db);
        chk.exec("SELECT COUNT(*) FROM tool_cabinet");
        if (!chk.next() || chk.value(0).toInt() == 0) {
            q.exec("INSERT INTO tool_cabinet(cabinet_id,cabinet_name,cabinet_code,location,ip_address,status,created_at,updated_at) "
                   "VALUES(1,'A柜','CAB-A','维修车间东侧','192.168.1.101','active',NOW(),NOW())");
            q.exec("INSERT INTO tool_cabinet(cabinet_id,cabinet_name,cabinet_code,location,ip_address,status,created_at,updated_at) "
                   "VALUES(2,'B柜','CAB-B','维修车间西侧','192.168.1.102','active',NOW(),NOW())");
            q.exec("INSERT INTO tool_cabinet(cabinet_id,cabinet_name,cabinet_code,location,ip_address,status,created_at,updated_at) "
                   "VALUES(3,'C柜','CAB-C','备件仓库','192.168.1.103','active',NOW(),NOW())");
        }
    }

  // ── 工具信息（26个）空表检测 ──
    {
        QSqlQuery chk(m_db);
        chk.exec("SELECT COUNT(*) FROM tool_info");
        if (!chk.next() || chk.value(0).toInt() == 0) {
    struct { int id; const char* code; const char* name; const char* spec; int cat; int cab; int mg; const char* pos; int total; int avail; const char* status; int recommended; const char* vision; } tools[] = {
        {1,"JZ01-CDQ","充电式电动解锥","12V锂电池",1,1,1,"03-04位",4,4,"in_stock",1,"E28011005200000001A"},
        {2,"JZ01-KB","9# 开口扳手","9mm",2,1,1,"01-05位",6,6,"in_stock",1,"E28011005200000002B"},
        {3,"JZ01-BX","保险丝钳","6寸",2,1,1,"01-15位",3,3,"in_stock",1,"E28011005200000003C"},
        {4,"JZ01-PH2","十字解锥头 2#","PH2",2,1,2,"03-12位",10,10,"in_stock",0,""},
        {5,"JZ01-NLJ","内六角扳手","1.5-10mm",2,1,3,"01-06位",5,4,"in_stock",1,"E28011005200000005D"},
        {6,"JZ01-SB","塞尺","0.02-1.0mm",3,1,3,"02-01位",4,4,"in_stock",0,"E28011005200000006E"},
        {7,"JZ01-DB","电工刀","折叠式",7,1,2,"01-09位",5,4,"in_stock",0,"E28011005200000007F"},
        {8,"JZ01-YQ","压线钳","0.25-10mm²",2,1,2,"02-07位",3,3,"in_stock",0,"E28011005200000008A"},
        {9,"JZ01-CZ","锤子","1.5磅",2,1,3,"03-14位",4,4,"in_stock",0,"E28011005200000009B"},
        {10,"JZ01-TH","套筒扳手组","8-32mm",6,1,1,"02-13位",2,2,"in_stock",1,"E2801100520000000AC"},
        {11,"JZ01-WY","万用表","数字式",3,1,2,"02-01位",3,3,"in_stock",0,"E2801100520000000BD"},
        {12,"JZ01-CRV","开口扳手(20×22)","20×22mm",2,1,3,"01-05位",6,6,"in_stock",0,"E2801100520000000CE"},
        {13,"JZ02-ZD","十字螺丝刀","PH2×150mm",2,2,5,"01-03位",8,8,"in_stock",0,"E2801100520000000DF"},
        {14,"JZ02-YZ","一字解锥头","6mm",2,2,4,"02-12位",5,4,"in_stock",0,""},
        {15,"JZ02-JL","棘轮扳手","3/8寸",2,2,4,"02-03位",12,11,"in_stock",0,"E2801100520000000F1"},
        {16,"JZ02-HQ","焊枪","100W",4,2,5,"03-07位",3,3,"maintenance",0,"E280110052000000102"},
        {17,"JZ02-BZ","剥线钳","0.5-6mm²",2,2,5,"01-14位",4,4,"in_stock",0,"E280110052000000113"},
        {18,"JZ02-SG","手锯","300mm",7,2,4,"01-11位",3,3,"in_stock",0,"E280110052000000124"},
        {19,"JZ02-CL","游标卡尺","0-150mm",3,2,7,"02-06位",3,2,"in_stock",0,"E280110052000000135"},
        {20,"JZ02-DS","电刷","标准型",2,2,6,"01-08位",5,5,"in_stock",0,""},
        {21,"JZ03-QG","强光手电","LED 1000lm",5,2,6,"03-10位",7,7,"in_stock",0,"E280110052000000157"},
        {22,"JZ03-RF","热风枪","2000W",4,3,6,"01-11位",2,1,"in_stock",1,"E280110052000000168"},
        {23,"JZ03-JQ","剪刀","8寸",7,3,8,"03-02位",8,8,"maintenance",0,"E280110052000000179"},
        {24,"JZ03-DJ","电烙铁","60W",4,3,6,"02-09位",5,5,"in_stock",0,"E28011005200000018A"},
        {25,"JZ03-YG","验电笔","数字式",3,3,2,"01-02位",6,6,"in_stock",0,"E28011005200000019B"},
        {26,"JZ03-XY","吸锡器","手动式",4,3,7,"03-05位",4,4,"in_stock",0,"E2801100520000001AC"},
    };
    for (auto& t : tools) {
        q.prepare("INSERT INTO tool_info(tool_id,tool_code,tool_name,spec,category_id,cabinet_id,machine_group_id,position,total_qty,current_qty,vision_tag,status,is_recommended) "
                  "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)");
        q.addBindValue(t.id); q.addBindValue(t.code); q.addBindValue(t.name); q.addBindValue(t.spec);
        q.addBindValue(t.cat); q.addBindValue(t.cab); q.addBindValue(t.mg); q.addBindValue(t.pos);
        q.addBindValue(t.total); q.addBindValue(t.avail); q.addBindValue(t.vision); q.addBindValue(t.status);
        q.addBindValue(t.recommended);
        q.exec();
    }
        }  // 关闭 tool_info 空表检测 if 块
    }

  // ── 借用记录（20条）空表检测 ──
    {
        QSqlQuery chk(m_db);
        chk.exec("SELECT COUNT(*) FROM tool_borrow_record");
        if (!chk.next() || chk.value(0).toInt() == 0) {
    struct { int id; const char* flow; int uid; int tid; int qty; const char* reason; const char* btime; const char* etime; const char* atime; const char* status; } recs[] = {
        {1,"BR20260301001",2,1,1,"维修电动设备","2026-03-01 08:30","2026-03-01 17:00","2026-03-01 16:45","returned"},
        {2,"BR20260305001",3,2,2,"更换开口扳手组","2026-03-05 09:15","2026-03-05 18:00","2026-03-05 17:30","returned"},
        {3,"BR20260310001",4,5,1,"设备检修","2026-03-10 10:00","2026-03-10 17:00","2026-03-10 16:20","returned"},
        {4,"BR20260315001",2,10,1,"大修发动机","2026-03-15 08:45","2026-03-16 17:00","2026-03-16 15:30","returned"},
        {5,"BR20260402001",6,13,2,"日常维护","2026-04-02 11:00","2026-04-02 17:00","2026-04-02 16:50","returned"},
        {6,"BR20260408001",3,1,1,"紧急维修","2026-04-08 14:20","2026-04-08 18:00","2026-04-08 17:15","returned"},
        {7,"BR20260415001",5,4,3,"批量更换螺丝","2026-04-15 09:30","2026-04-15 18:00","2026-04-15 17:40","returned"},
        {8,"BR20260422001",2,11,1,"电气检测","2026-04-22 10:10","2026-04-22 17:00","2026-04-22 16:30","returned"},
        {9,"BR20260501001",4,7,1,"线路维修","2026-05-01 08:00","2026-05-01 12:00","2026-05-01 11:45","returned"},
        {10,"BR20260506002",1,6,1,"精密测量","2026-05-06 13:30","2026-05-06 18:00","2026-05-06 17:20","returned"},
        {11,"BR20260512001",6,22,1,"设备加热","2026-05-12 09:50","2026-05-12 17:00","2026-05-12 16:55","returned"},
        {12,"BR20260601001",3,9,1,"基础维修","2026-06-01 10:30","2026-06-01 17:00","2026-06-01 17:10","returned"},
        {13,"BR20260615001",5,19,1,"精密测量","2026-06-15 08:15","2026-06-15 17:00","2026-06-15 16:40","returned"},
        {14,"BR20260603002",4,3,1,"电器维修","2026-06-03 10:00","2026-06-03 17:00","2026-06-03 16:30","returned"},
        {15,"BR20260605003",2,14,2,"改锥头更换","2026-06-05 11:15","2026-06-05 18:00","2026-06-05 17:30","returned"},
        {16,"BR20260608001",5,22,1,"热风作业","2026-06-08 08:50","2026-06-08 17:00","","borrowing"},
        {17,"BR20260610001",4,7,1,"电工刀维修","2026-06-10 09:10","2026-06-10 17:00","","borrowing"},
        {18,"BR20260612001",2,14,1,"解锥头更换","2026-06-12 10:30","2026-06-12 18:00","","overdue"},
        {19,"BR20260609002",3,10,1,"发动机检修","2026-06-09 08:15","2026-06-09 17:00","2026-06-09 16:40","returned"},
        {20,"BR20260611003",6,5,1,"内六角保养","2026-06-11 13:00","2026-06-11 17:00","","borrowing"},
    };
    for (auto& r : recs) {
        q.prepare("INSERT INTO tool_borrow_record(record_id,flow_no,user_id,tool_id,borrow_qty,borrow_reason,"
                  "borrow_time,expected_return_time,actual_return_time,status) VALUES(?,?,?,?,?,?,?,?,?,?)");
        q.addBindValue(r.id); q.addBindValue(r.flow); q.addBindValue(r.uid); q.addBindValue(r.tid);
        q.addBindValue(r.qty); q.addBindValue(r.reason); q.addBindValue(r.btime);
        q.addBindValue(r.etime);
        q.addBindValue(strlen(r.atime) > 0 ? r.atime : QVariant());
        q.addBindValue(r.status);
        q.exec();
    }
        }  // 关闭 tool_borrow_record 空表检测 if 块
    }

    // ── 告警类型字典表 动态告警类型+级别管理 ──
    q.exec("CREATE TABLE IF NOT EXISTS sys_alert_type ("
           "  type_id     INTEGER PRIMARY KEY AUTO_INCREMENT,"
           "  type_code   VARCHAR(255)    NOT NULL UNIQUE,"
           "  type_name   TEXT    NOT NULL,"
           "  alert_level VARCHAR(255) DEFAULT 'warn',"
           "  sort_order  INTEGER DEFAULT 0,"
           "  is_active   INTEGER DEFAULT 1,"
           "  created_at  TEXT"
           ")");
    // 告警类型对齐MySQL现有映射（type_id必须一致）
    // MySQL当前映射：1-overdue,2-mismatch,3-missing,4-offline,5-unauthorized,
    // 6-low_stock,7-system,8-power,9-network_error,10-door_open,
    // 11-stranger,12-rack_mismatch,13-temp_high,14-power_low,
    // 15-sensor_fail,16-login_fail,17-hw_comm,18-hw_fault
    // 注意：先清空旧数据确保type_id从1开始自增
    q.exec("DELETE FROM sys_alert_type");
    struct { const char* code; const char* name; const char* level; int order; } types[] = {
        {"overdue","逾期未还","warn",1},       {"mismatch","工具错放","warn",2},
        {"missing","工具缺失","error",3},      {"offline","柜门异常","error",4},
        {"unauthorized","未授权操作","error",5},{"low_stock","库存不足","info",6},
        {"system","系统异常","info",7},         {"power","电源异常","error",8},
        {"network_error","网络故障","error",9}, {"door_open","柜门未关","warn",10},
        {"stranger","陌生人告警","warn",11},    {"rack_mismatch","货架错放","warn",12},
        {"temp_high","温度过高","warn",13},     {"power_low","电量不足","warn",14},
        {"sensor_fail","传感器故障","error",15},{"login_fail","登录失败","warn",16},
        // 袁总新增三类告警体系：工具错放/硬件通讯故障(视频、IO板卡)/系统硬件故障
        {"hw_comm","硬件通讯故障","error",17},  {"hw_fault","系统硬件故障","error",18},
    };
    // 告警类型启用范围：工具错放/硬件通讯故障/系统硬件故障（其余停用，筛选下拉不显示）
    auto typeActive = [](const char* code) -> int {
        return (qstrcmp(code, "mismatch") == 0 || qstrcmp(code, "hw_comm") == 0 ||
                qstrcmp(code, "hw_fault") == 0) ? 1 : 0;
    };
    int typeIdx = 1;
    for (auto& t : types) {
        q.prepare("INSERT INTO sys_alert_type(type_id,type_code,type_name,alert_level,sort_order,is_active) VALUES(?,?,?,?,?,?)");
        q.addBindValue(typeIdx);
        q.addBindValue(t.code); q.addBindValue(t.name); q.addBindValue(t.level); q.addBindValue(t.order);
        q.addBindValue(typeActive(t.code));
        q.exec();
        typeIdx++;
    }
    qInfo() << "[DB] Alert types seeded (cleared+reinserted with explicit IDs):" << (sizeof(types)/sizeof(types[0])) << "types";

    // ── 告警记录表 字段对齐MySQL：保留alert_type/alert_level兼容AlertDAO ──
    // 新增record_id列，关联tool_borrow_record用于借款人回退查询
    q.exec("CREATE TABLE IF NOT EXISTS sys_alert ("
           "  alert_id    INTEGER PRIMARY KEY AUTO_INCREMENT,"
           "  type_id     INTEGER NOT NULL DEFAULT 1,"
           "  alert_type  VARCHAR(255) DEFAULT '',"
           "  alert_level VARCHAR(255) DEFAULT 'warn',"
           "  tool_id     INTEGER DEFAULT NULL,"
           "  tool_code   VARCHAR(255) DEFAULT '',"
           "  content     TEXT,"
           "  status      VARCHAR(255) DEFAULT 'unhandled',"
           "  user_id     INTEGER DEFAULT NULL,"
           "  record_id   INTEGER DEFAULT 0,"
           "  created_at  TEXT,"
           "  handled_at  TEXT    DEFAULT NULL,"
           "  handler_id  INTEGER DEFAULT NULL,"
           "  remark      VARCHAR(255) DEFAULT ''"
           ")");
    // 兼容旧表迁移：添加可能缺失的列（SQLite ALTER不支持NOT NULL，用DEFAULT代替）
    q.exec("ALTER TABLE sys_alert ADD COLUMN type_id INTEGER DEFAULT 1");
    q.exec("ALTER TABLE sys_alert ADD COLUMN alert_type VARCHAR(255) DEFAULT ''");
    q.exec("ALTER TABLE sys_alert ADD COLUMN alert_level VARCHAR(255) DEFAULT 'warn'");
    q.exec("ALTER TABLE sys_alert ADD COLUMN tool_id INTEGER DEFAULT NULL");
    q.exec("ALTER TABLE sys_alert ADD COLUMN tool_code VARCHAR(255) DEFAULT ''");
    q.exec("ALTER TABLE sys_alert ADD COLUMN user_id INTEGER DEFAULT NULL");
    q.exec("ALTER TABLE sys_alert ADD COLUMN handled_at TEXT DEFAULT NULL");
    q.exec("ALTER TABLE sys_alert ADD COLUMN handler_id INTEGER DEFAULT NULL");
    q.exec("ALTER TABLE sys_alert ADD COLUMN remark VARCHAR(255) DEFAULT ''");
    q.exec("ALTER TABLE sys_alert ADD COLUMN record_id INTEGER DEFAULT 0");  // 关联借用记录
    // 检查sys_alert_type表是否存在type_code列，如缺失则重建
    {
        QSqlQuery colCheck(m_db);
        colCheck.exec("SELECT type_code FROM sys_alert_type LIMIT 1");
        if (colCheck.lastError().isValid()) {
            qWarning() << "[DB] sys_alert_type missing type_code column, recreating...";
            q.exec("DROP TABLE IF EXISTS sys_alert_type");
            q.exec("CREATE TABLE IF NOT EXISTS sys_alert_type ("
                   "  type_id     INTEGER PRIMARY KEY AUTO_INCREMENT,"
                   "  type_code   VARCHAR(255)    NOT NULL UNIQUE,"
                   "  type_name   TEXT    NOT NULL,"
                   "  alert_level VARCHAR(255) DEFAULT 'warn',"
                   "  sort_order  INTEGER DEFAULT 0,"
                   "  is_active   INTEGER DEFAULT 1,"
                   "  created_at  TEXT"
                   ")");
            int ti = 1;
            for (auto& t : types) {
                // 同步主路径：三类启用（工具错放/硬件通讯故障/系统硬件故障）
                q.prepare("INSERT INTO sys_alert_type(type_id,type_code,type_name,alert_level,sort_order,is_active) VALUES(?,?,?,?,?,?)");
                q.addBindValue(ti); q.addBindValue(t.code); q.addBindValue(t.name); q.addBindValue(t.level); q.addBindValue(t.order);
                q.addBindValue(typeActive(t.code));
                q.exec(); ti++;
            }
        }
    }

    // 种子告警数据修复根因：
    // 原逻辑检测hasLegacyData时包含"alert_type=''"条件，
    // 但insertAlert不填alert_type列→新告警alert_type为空→每次启动判定hasLegacyData=true
    // → DELETE FROM sys_alert清空全部告警→重插20条种子→"永远是20条"
    // 注意：只在sys_alert为空表时播种种子数据，有数据就不清空
    {
        QSqlQuery check(m_db);
        check.exec("SELECT COUNT(*) FROM sys_alert");
        int existingCount = (check.next()) ? check.value(0).toInt() : 0;
        qInfo() << "[DB] Existing alert count:" << existingCount;
        if (existingCount == 0) {
            qInfo() << "[DB] Seeding initial alert records (mismatch only)...";
            // 结构: type_id, alert_type, alert_level, tool_code, user_id, status, content, created_at
            // 告警三类仿真：工具错放/硬件通讯故障(视频、IO板卡)/系统硬件故障
            struct { int tid; const char* atype; const char* alevel; const char* tcode; int uid;
                     const char* st; const char* ct; const char* ctime; } alerts[] = {
                // ══════ 工具错放 type_id=2(mismatch) ══════
                {2,"mismatch","warn","TOOL-002",0,"unhandled",
                 "工具[保险丝钳]检测到放置在错误货位，当前货位：A-03，正确货位：B-02","2026-06-25 14:00"},
                {2,"mismatch","warn","TOOL-006",0,"unhandled",
                 "工具[十字解锥头 2#]检测到放置在错误货位，当前货位：C-01，正确货位：C-05","2026-06-24 16:30"},
                {2,"mismatch","warn","TOOL-008",7,"handled",
                 "工具[内六角扳手]检测到放置在错误货位，已由管理员复位","2026-06-19 10:00"},
                // ══════ 硬件通讯故障 type_id=17(hw_comm) 视频摄像头/IO板卡 ══════
                {17,"hw_comm","error","",0,"unhandled",
                 "1号柜视频摄像头通讯中断(心跳丢失3次)，人脸识别功能暂不可用，请检查摄像头线路","2026-06-26 09:15"},
                {17,"hw_comm","error","",0,"unhandled",
                 "2号柜IO板卡通讯超时(串口无响应)，工具位在柜检测暂停","2026-06-26 10:40"},
                {17,"hw_comm","error","",0,"handled",
                 "1号柜视频摄像头通讯恢复，已自动重连成功","2026-06-25 16:20"},
                // ══════ 系统硬件故障 type_id=18(hw_fault) 电源/温度/传感器 ══════
                {18,"hw_fault","error","",0,"unhandled",
                 "3号柜电源电压异常(10.8V)，低于正常工作范围(11.5-12.5V)，请检查供电线路","2026-06-26 11:05"},
                {18,"hw_fault","warn","",0,"unhandled",
                 "2号柜内部温度达到39.2°C，超出安全范围(25-35°C)，散热风扇已自动启动","2026-06-26 13:30"},
                {18,"hw_fault","error","",0,"handled",
                 "A-02-03位工具检测传感器无响应，现场排查为传感器接插件松动，已复位","2026-06-24 15:50"},
            };
            for (auto& a : alerts) {
                q.prepare("INSERT INTO sys_alert(type_id,alert_type,alert_level,tool_code,user_id,status,content,created_at)"
                          " VALUES(?,?,?,?,?,?,?,?)");
                q.addBindValue(a.tid);
                q.addBindValue(QString::fromUtf8(a.atype));
                q.addBindValue(QString::fromUtf8(a.alevel));
                q.addBindValue(QString::fromUtf8(a.tcode));
                q.addBindValue(a.uid);
                q.addBindValue(QString::fromUtf8(a.st));
                q.addBindValue(QString::fromUtf8(a.ct));
                q.addBindValue(QString::fromUtf8(a.ctime));
                if (!q.exec()) {
                    qWarning() << "[DB] Seed alert failed:" << q.lastError().text();
                }
            }
            qInfo() << "[DB] Alert seed data inserted (mismatch only, type_id aligned with MySQL)";
        }
    }

    // ── 操作日志（15条） ──
    q.exec("CREATE TABLE IF NOT EXISTS sys_operation_log ("
           "  log_id         INTEGER PRIMARY KEY AUTO_INCREMENT,"
           "  user_id        INTEGER DEFAULT NULL,"
           "  username       VARCHAR(255) DEFAULT '',"
           "  operation_type TEXT    NOT NULL,"
           "  target_type    VARCHAR(255) DEFAULT '',"
           "  target_id      VARCHAR(255) DEFAULT '',"
           "  content        TEXT,"
           "  ip_address     VARCHAR(255) DEFAULT '',"
           "  created_at     TEXT"
           ")");
    // 致命注意：原代码无条件DELETE清空所有操作日志
    // 导致test1等真实出库/入库记录每次启动都被删除
    // 空表检测：仅在表为空时插入模拟数据，保留运行时产生的真实记录
    //   多次反馈test1记录丢失，根因在此
    QSqlQuery logChk(m_db);
    logChk.exec("SELECT COUNT(*) FROM sys_operation_log");
    int logCnt = (logChk.next()) ? logChk.value(0).toInt() : 0;
    if (logCnt == 0) {
    struct { int uid; const char* uname; const char* op; const char* content; const char* ctime; } logs[] = {
        {1,"张三","login","管理员张三登录系统","2026-06-13 08:30"},
        {2,"李四","login","用户李四通过人脸识别登录","2026-06-13 08:35"},
        {3,"王五","borrow","王五借用工具:充电式电动解锥","2026-06-12 09:00"},
        {4,"赵六","return","赵六归还工具:内六角扳手","2026-06-12 09:30"},
        {5,"孙七","borrow","孙七借用工具:游标卡尺","2026-06-12 11:00"},
        {6,"周八","login","用户周八登录系统","2026-06-12 13:00"},
        {1,"张三","add_user","管理员新增测试用户","2026-06-12 10:00"},
        {1,"张三","edit_user","管理员编辑用户:赵六信息","2026-06-12 10:15"},
        {2,"李四","borrow","李四借用工具:热风枪","2026-06-12 14:00"},
        {3,"王五","return","王五归还工具:9#开口扳手","2026-06-11 09:00"},
        {1,"张三","disable_user","管理员禁用用户:孙七","2026-06-11 10:00"},
        {2,"李四","return","李四归还工具:万用表","2026-06-11 10:30"},
        {3,"王五","borrow","王五借用工具:锤子","2026-06-11 11:00"},
        {6,"周八","borrow","周八借用工具:焊枪","2026-06-11 14:00"},
        {1,"张三","export","管理员导出本月借用记录","2026-06-09 16:00"},
    };
    for (auto& l : logs) {
        q.prepare("INSERT INTO sys_operation_log(user_id,username,operation_type,content,created_at) VALUES(?,?,?,?,?)");
        q.addBindValue(l.uid); q.addBindValue(l.uname); q.addBindValue(l.op); q.addBindValue(l.content); q.addBindValue(l.ctime);
        q.exec();
    }
    }  // 闭合 if (logCnt == 0)

    // 人脸识别识别统计日志表（专利实测数据通道）
    // 用途：记录每次人脸识别尝试，用于统计误识率(FAR)/拒识率(FRR)/识别延迟，支撑专利交底书实测数据
    // 字段说明：
    // result 识别结果：success(通过)/stranger(判陌生人)/rejected(双验证失败)/error(异常)
    // best_sim 最佳候选余弦相似度（0~1，陌生人场景记为0）
    // best_dist 最佳候选欧氏距离（第二防线，陌生人场景记为1）
    // threshold 本次生效判定阈值（单人脸/多人脸动态取值）
    // mode 判定模式：single(单人脸)/multi(多人脸)
    // candidate_cnt 库内已录入人脸数（库容）
    // elapsed_ms 单次识别耗时（毫秒，含特征提取+比对）
    // matched_user 命中用户工号（stranger/error 为空）
    q.exec("CREATE TABLE IF NOT EXISTS face_recog_log ("
           "  log_id         INTEGER PRIMARY KEY AUTO_INCREMENT,"
           "  result         TEXT    NOT NULL,"
           "  best_sim       REAL    DEFAULT 0,"
           "  best_dist      REAL    DEFAULT 1,"
           "  threshold      REAL    DEFAULT 0,"
           "  mode           VARCHAR(255) DEFAULT '',"
           "  candidate_cnt  INTEGER DEFAULT 0,"
           "  elapsed_ms     INTEGER DEFAULT 0,"
           "  matched_user   VARCHAR(255) DEFAULT '',"
           "  created_at     TEXT"
           ")");

    // 出库操作历史记录（10条），content加工具编号，INSERT加target_id
    //   修复出库记录工具编号缺失问题
    // 1. 清理旧格式数据（content不含"编号["），保留真实出库记录
    // 2. 仅在新格式数据为0时插入模拟数据（避免重复）
    q.exec("DELETE FROM sys_operation_log WHERE operation_type='checkout' AND content NOT LIKE '%编号[%'");
    {
        QSqlQuery chkCheckout(m_db);
        chkCheckout.exec("SELECT COUNT(*) FROM sys_operation_log WHERE operation_type='checkout'");
        int checkoutCnt = (chkCheckout.next()) ? chkCheckout.value(0).toInt() : 0;
        if (checkoutCnt == 0) {
            // tool_id映射: 充电式电动解锥=1, 内六角扳手=5, 游标卡尺=19, 热风枪=22,
            // 9# 开口扳手=2, 万用表=11, 锤子=9, 焊枪=16
            struct { int uid; int targetId; const char* op; const char* content; const char* ctime; } checkoutLogs[] = {
                {1,1, "checkout","出库工具「充电式电动解锥」编号[JZ01-CDQ]×2，原因：报废更换","2026-06-20 09:15"},
                {2,5, "checkout","出库工具「内六角扳手」编号[JZ01-NLJ]×1，原因：损坏退役","2026-06-19 14:30"},
                {3,19,"checkout","出库工具「游标卡尺」编号[JZ02-CL]×1，原因：调拨其他机组","2026-06-18 10:00"},
                {1,22,"checkout","出库工具「热风枪」编号[JZ03-RF]×3，原因：升级替换","2026-06-17 15:45"},
                {2,2, "checkout","出库工具「9# 开口扳手」编号[JZ01-KB]×2，原因：超期淘汰","2026-06-16 11:20"},
                {3,11,"checkout","出库工具「万用表」编号[JZ01-WY]×1，原因：其他原因","2026-06-15 16:00"},
                {1,9, "checkout","出库工具「锤子」编号[JZ01-CZ]×1，原因：损坏退役","2026-06-14 09:30"},
                {2,16,"checkout","出库工具「焊枪」编号[JZ02-HQ]×1，原因：报废更换","2026-06-13 13:45"},
                {3,1, "checkout","出库工具「充电式电动解锥」编号[JZ01-CDQ]×1，原因：调拨其他机组","2026-06-12 10:15"},
                {1,5, "checkout","出库工具「内六角扳手」编号[JZ01-NLJ]×2，原因：升级替换","2026-06-11 14:30"},
            };
            for (auto& cl : checkoutLogs) {
                q.prepare("INSERT INTO sys_operation_log(user_id,operation_type,target_type,target_id,content,ip_address,created_at) VALUES(?,?,?,?,?,?,?)");
                q.addBindValue(cl.uid); q.addBindValue(cl.op); q.addBindValue("tool");
                q.addBindValue(cl.targetId); q.addBindValue(cl.content); q.addBindValue("127.0.0.1"); q.addBindValue(cl.ctime);
                q.exec();
            }
            qInfo() << "[DB] V8.0 checkout logs inserted (10 records with tool_code)";
        }
    }

    // 入库操作历史记录（10条），供入库记录Tab展示
    // content格式与出库统一：入库工具「名」编号[编号]×数量，供应商：xxx
    // 注意：只在sys_operation_log中没有checkin记录时插入（空表检测）
    {
        QSqlQuery chkCheckin(m_db);
        chkCheckin.exec("SELECT COUNT(*) FROM sys_operation_log WHERE operation_type='checkin'");
        int checkinCnt = (chkCheckin.next()) ? chkCheckin.value(0).toInt() : 0;
        if (checkinCnt == 0) {
            struct { int uid; const char* op; const char* content; const char* ctime; } checkinLogs[] = {
                {1,"checkin","入库工具「充电式电动解锥」编号[JZ01-CDQ]×2，供应商：史丹利","2026-06-20 10:30"},
                {2,"checkin","入库工具「内六角扳手」编号[JZ01-NLJ]×3，供应商：世达工具","2026-06-19 15:45"},
                {3,"checkin","入库工具「游标卡尺」编号[JZ02-CL]×1，供应商：上海量具","2026-06-18 11:15"},
                {1,"checkin","入库工具「热风枪」编号[JZ03-RF]×2，供应商：博世电动","2026-06-17 16:30"},
                {2,"checkin","入库工具「9#开口扳手」编号[JZ01-KB]×4，供应商：世达工具","2026-06-16 12:00"},
                {3,"checkin","入库工具「万用表」编号[JZ01-WY]×1，供应商：福禄克","2026-06-15 17:15"},
                {1,"checkin","入库工具「锤子」编号[JZ01-CZ]×2，供应商：史丹利","2026-06-14 10:45"},
                {2,"checkin","入库工具「焊枪」编号[JZ02-HQ]×1，供应商：博世电动","2026-06-13 14:15"},
                {3,"checkin","入库工具「充电式电动解锥」编号[JZ01-CDQ]×1，供应商：史丹利","2026-06-12 11:30"},
                {1,"checkin","入库工具「内六角扳手」编号[JZ01-NLJ]×2，供应商：世达工具","2026-06-11 15:00"},
            };
            for (auto& ci : checkinLogs) {
                q.prepare("INSERT INTO sys_operation_log(user_id,operation_type,target_type,content,ip_address,created_at) VALUES(?,?,?,?,?,?)");
                q.addBindValue(ci.uid); q.addBindValue(ci.op); q.addBindValue("tool");
                q.addBindValue(ci.content); q.addBindValue("127.0.0.1"); q.addBindValue(ci.ctime);
                q.exec();
            }
            qInfo() << "[DB] Checkin seed data inserted: 10 records";
        }
    }

    qInfo() << "[DB] Business seed data complete: 7 categories, 3 cabinets, 26 tools, 20 records, 16 alert_types, 8 alerts, 35 logs(含10出库+10入库)";

    // 验证种子数据完整性（写入文件供确认）
    {
        QSqlQuery vfy(m_db);
        vfy.exec("SELECT COUNT(*) FROM sys_alert");
        int alertCnt = (vfy.next()) ? vfy.value(0).toInt() : 0;
        vfy.exec("SELECT COUNT(*) FROM sys_alert_type");
        int typeCnt = (vfy.next()) ? vfy.value(0).toInt() : 0;
        vfy.exec("SELECT COUNT(*) FROM tool_info");
        int toolCnt = (vfy.next()) ? vfy.value(0).toInt() : 0;
        vfy.exec("SELECT COUNT(*) FROM sys_user");
        int userCnt = (vfy.next()) ? vfy.value(0).toInt() : 0;
        vfy.exec("SELECT COUNT(*) FROM tool_cabinet");
        int cabinetCnt = (vfy.next()) ? vfy.value(0).toInt() : 0;
        qInfo() << "[DB] VERIFY: alerts=" << alertCnt << "types=" << typeCnt << "tools=" << toolCnt << "users=" << userCnt << "cabinets=" << cabinetCnt;
        // 写入验证文件
        QFile vf(m_db.databaseName() + ".verify.txt");
        if (vf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QTextStream ts(&vf);
            ts << "SQLite DB Verify: " << m_db.databaseName() << "\n";
            ts << "alerts=" << alertCnt << " (expect 20)\n";
            ts << "alert_types=" << typeCnt << " (expect 16)\n";
            ts << "tools=" << toolCnt << " (expect 26)\n";
            ts << "users=" << userCnt << " (expect 7)\n";
            ts << "cabinets=" << cabinetCnt << " (expect 3)\n";
            // 额外验证：告警是否都有type_name
            vfy.exec("SELECT COUNT(*) FROM sys_alert a LEFT JOIN sys_alert_type t ON a.type_id=t.type_id WHERE t.type_name IS NULL");
            int nullTypes = (vfy.next()) ? vfy.value(0).toInt() : -1;
            ts << "alerts_with_null_type=" << nullTypes << " (expect 0)\n";
            vf.close();
        }
    }

    // 创建视图（SQLite版本，兼容MySQL语法）
    // v_tool_stats 工具统计视图 借用统计从tool_borrow_record获取
    // in_stock_qty 为所有工具的 current_qty 之和（不论状态），反映实际在库件数
    q.exec("DROP VIEW IF EXISTS v_tool_stats");
    q.exec("CREATE VIEW v_tool_stats AS "
           "SELECT (SELECT COUNT(*) FROM tool_info) AS total_tools,"
           "(SELECT COUNT(*) FROM tool_info WHERE status='in_stock') AS in_stock_count,"
           "(SELECT COUNT(DISTINCT tool_id) FROM tool_borrow_record WHERE status IN ('borrowing','overdue')) AS borrowed_count,"
           "(SELECT COUNT(*) FROM tool_info WHERE status='maintenance') AS maintenance_count,"
           "(SELECT COALESCE(SUM(total_qty),0) FROM tool_info) AS total_qty,"
           "(SELECT COALESCE(SUM(current_qty),0) FROM tool_info) AS in_stock_qty,"
           "(SELECT COALESCE(SUM(borrow_qty),0) FROM tool_borrow_record WHERE status IN ('borrowing','overdue')) AS borrowed_qty");

    // v_tool_latest_operation 最近操作视图
    // 机组隔离：只统计活跃借用(borrowing/overdue)
    // 修复视图重复行Bug（latest_op_user改子查询LIMIT 1）
    q.exec("DROP VIEW IF EXISTS v_tool_latest_operation");
    q.exec("CREATE VIEW v_tool_latest_operation AS "
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

    qInfo() << "[DB] V7.0 views created: v_tool_stats + v_tool_latest_operation";

    // 注意：删除DELETE+INSERT改为INSERT IGNORE
    // 根因：每次启动DELETE FROM task_type/task_type_tool清空了用户数据！
    // 用户在系统维护中添加的任务工具，重启后被种子数据覆盖。
    // 注意：INSERT IGNORE只插入不存在的主键，不覆盖已有数据。
    // 举一反三：对照关系(tool_position_mapping)种子数据不受影响（无DELETE）
    // 播种任务类型数据 — 只插入不存在的记录（INSERT IGNORE）
    // 默认任务类型：航前检查/航后维护等10种
    struct { int id; const char* code; const char* name; const char* desc; int dur; int sort; } taskTypes[] = {
        {1, "PRECHECK",   "航前检查",     "航班起飞前对工具柜工具进行全面检查与准备", 60, 1},
        {2, "POSTCHECK",  "航后维护",     "航班降落后对工具进行归位、清洁与维护", 60, 2},
        {3, "ENG_MAINT",  "发动机维护",   "发动机拆装、检查、更换部件等核心维护作业", 120, 3},
        {4, "AVIONICS",   "航电检修",     "航空电子设备、仪表、通信导航系统检测维修", 90, 4},
        {5, "HYDRAULIC",  "液压系统维护", "液压管路、泵阀、作动筒检查与更换", 60, 5},
        {6, "STRUCTURE",  "结构修理",     "机身蒙皮、框架、紧固件损伤修复", 180, 6},
        {7, "LANDING",    "起落架维护",   "起落架减震、刹车系统、轮胎更换", 90, 7},
        {8, "ELECTRICAL", "电气线路检修", "线路故障排查、线束修复、接插件更换", 60, 8},
        {9, "WELDING",    "焊接作业",     "金属结构焊接、修补、热处理", 120, 9},
        {10, "MEASURE",   "精密测量",     "三坐标测量、形位公差检测、校准", 60, 10},
    };
    for (auto& t : taskTypes) {
        q.prepare("INSERT IGNORE INTO task_type(type_id,type_code,type_name,description,default_duration,sort_order,is_active) "
                  "VALUES(?,?,?,?,?,?,1)");
        q.addBindValue(t.id); q.addBindValue(t.code); q.addBindValue(t.name);
        q.addBindValue(t.desc); q.addBindValue(t.dur); q.addBindValue(t.sort);
        q.exec();
    }

    // 播种任务类型-推荐工具关联数据 — 只插入不存在的记录
    // 不DELETE FROM task_type_tool！用户添加的工具关联必须保留
    struct { int typeId; int toolId; int sort; } ttRel[] = {
        // 航前检查(1) → 本机组所有工具（10个，覆盖全面检查场景）
        {1,1,1}, {1,2,2}, {1,3,3}, {1,5,4}, {1,6,5}, {1,9,6}, {1,10,7}, {1,11,8}, {1,13,9}, {1,19,10},
        // 航后维护(2) → 清洁维护工具（6个：清洁刷、抹布、扳手、螺丝刀、万用表、塞尺）
        {2,9,1}, {2,13,2}, {2,2,3}, {2,11,4}, {2,6,5}, {2,19,6},
        // 发动机维护(3) → 电动解锥、开口扳手、套筒扳手、内六角扳手、万用表、塞尺
        {3,1,1}, {3,2,2}, {3,10,3}, {3,5,4}, {3,11,5}, {3,6,6},
        // 航电检修(4) → 万用表、验电笔、剥线钳、压线钳、电烙铁、保险丝钳
        {4,11,1}, {4,25,2}, {4,17,3}, {4,8,4}, {4,24,5}, {4,3,6},
        // 液压系统维护(5) → 开口扳手、棘轮扳手、内六角扳手、锤子、十字螺丝刀
        {5,2,1}, {5,15,2}, {5,5,3}, {5,9,4}, {5,13,5},
        // 结构修理(6) → 锤子、手锯、电刷、十字螺丝刀、一字解锥头、剪刀
        {6,9,1}, {6,18,2}, {6,20,3}, {6,13,4}, {6,14,5}, {6,23,6},
        // 起落架维护(7) → 套筒扳手、棘轮扳手、开口扳手、强光手电、游标卡尺
        {7,10,1}, {7,15,2}, {7,12,3}, {7,21,4}, {7,19,5},
        // 电气线路检修(8) → 万用表、验电笔、剥线钳、电工刀、电烙铁、保险丝钳
        {8,11,1}, {8,25,2}, {8,17,3}, {8,7,4}, {8,24,5}, {8,3,6},
        // 焊接作业(9) → 焊枪、热风枪、电烙铁、吸锡器、锤子
        {9,16,1}, {9,22,2}, {9,24,3}, {9,26,4}, {9,9,5},
        // 精密测量(10) → 游标卡尺、塞尺、万用表、验电笔、内六角扳手
        {10,19,1}, {10,6,2}, {10,11,3}, {10,25,4}, {10,5,5},
    };
    for (auto& r : ttRel) {
        // INSERT IGNORE：如果(type_id,tool_id)组合已存在则跳过，不覆盖用户数据
        q.prepare("INSERT IGNORE INTO task_type_tool(type_id,tool_id,sort_order,recommended_qty) VALUES(?,?,?,1)");
        q.addBindValue(r.typeId); q.addBindValue(r.toolId); q.addBindValue(r.sort);
        q.exec();
    }

    qInfo() << "[DB] V1.00.10 task_type seed data complete: 10 types + 60 tool associations";
    return true;
}
