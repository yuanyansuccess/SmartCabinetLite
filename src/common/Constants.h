/**
 * @file Constants.h
 * @brief 全局常量定义（SC:: 命名空间）：应用信息、状态值、分页、触屏尺寸与人脸阈值
 * @author 袁燕
 */
#pragma once
#include <QString>
#include <QStringList>
#include <QStandardPaths>  // 备份默认路径按平台落到用户主目录（麒麟普通用户无 /mnt 写入权限）

namespace SC {
// ── 应用信息 ──
const QString APP_VERSION = "V2.00";

// ── 通用格式（时间/正则等单一来源，避免重复字面量漂移） ──
// 三种时间格式各管一处，改显示精度时只动这里：
//   带秒  用于数据库读写与日志，保证排序与回读一致
//   到分钟用于界面展示，省掉对用户无意义的秒
//   纯日期用于只需年月日的场景（日期标签、每日一次的备份判断）
const QString TIMESTAMP_FORMAT = "yyyy-MM-dd HH:mm:ss";  // 数据库与界面统一使用的时间戳格式
const QString DATE_TIME_FORMAT = "yyyy-MM-dd HH:mm";     // 界面展示用，精确到分钟
const QString DATE_FORMAT      = "yyyy-MM-dd";           // 只要年月日的场景

// ── 数据库配置（默认值，运行时可修改） ──
const QString DB_HOST     = "127.0.0.1";
const int     DB_PORT     = 3306;
const QString DB_NAME     = "smart_cabinet";
const QString DB_USER     = "root";
const QString DB_PASS     = "root";  // 默认口令占位；实际以 system.ini [Database]pass 为准（可用 SC_DB_PASS 环境变量覆盖）

// ── 用户角色 ──
const QString ROLE_ADMIN   = "admin";    // 管理员：登录后直接进管理界面
const QString ROLE_MANAGER = "manager";  // 管理用户（管理员+普通用户双重身份）：登录后先选身份
const QString ROLE_USER    = "user";     // 普通用户：登录后进功能选择页（借用归还/查询）

// ── 用户状态（值与页面实际写入/播种数据一致，新人勿另造值） ──
const QString USER_ACTIVE   = "active";
const QString USER_DISABLED = "disabled";  // 禁用（UserManagementPage启用/禁用切换写入值）
const QString USER_DELETED  = "deleted";

// ── 新用户初始密码（批量导入/新建用户统一来源，修改此处即可全局生效）──
const QString DEFAULT_INIT_PASSWORD = "123456";

// ── 口令规则 ──
const int PASSWORD_MIN_LENGTH = 6;        // 口令最小长度（登录校验与修改口令共用）
const int PASSWORD_MAX_LENGTH = 12;       // 口令最大长度（设置与修改口令共用，闭区间含边界）
// 忘记密码刷脸身份验证：最多尝试次数，超过则关闭入口防止暴力试探
const int FACE_VERIFY_MAX_ATTEMPTS = 3;

// ── 密码登录防爆破锁定（整机级：任意工号的失败都累计到同一计数） ──
// 观察窗口(毫秒)：滑动窗口内累计密码失败达到阈值即触发锁定，窗口外的旧失败自动失效
const int PWD_FAIL_WINDOW_MS  = 5 * 60 * 1000;   // 5分钟
// 锁定阈值：观察窗口内第4次密码失败触发锁定（连续错3次后仍失败即锁）
const int PWD_FAIL_LOCK_TIMES = 4;
// 锁定时长(毫秒)：锁定期内拒绝一切密码登录，刷脸登录不受影响，到期自动恢复
const int PWD_LOCK_MS         = 5 * 60 * 1000;   // 5分钟

// ── 操作日志类型（sys_operation_log.operation_type） ──
const QString OP_CHECKIN  = "checkin";
const QString OP_CHECKOUT = "checkout";

// ── 出入库识别流程（边缘机识别后写库） ──
// 重开柜整理轮次上限：每轮识别有未过项时可重新开柜整理，达到上限仍没全部通过就按当前实况写库
const int ORDER_MAX_RETRY_ROUNDS = 3;
// 入库项"正确放入"的识别置信度门槛：货位上识别出的工具置信度达到该值才判定为放入正确
const double RECOG_MIN_CONFIDENCE = 0.85;
// 轮次日志操作类型：每轮识别写一条操作日志，content 为 JSON（轮次/各项结果/错放明细）
const QString ORDER_ROUND_OP_CHECKIN  = "checkin_round";
const QString ORDER_ROUND_OP_CHECKOUT = "checkout_round";
// 关柜等待超时（毫秒）：开箱后等待 IO 板"已关柜"信号的上限，超时提示用户确认柜门已关
const int CABINET_CLOSE_WAIT_MS = 2 * 60 * 1000;

// ── 工具状态 ──
const QString TOOL_IN_STOCK    = "in_stock";
const QString TOOL_BORROWED    = "borrowed";
const QString TOOL_MAINTENANCE = "maintenance";
const QString TOOL_CHECKED_OUT = "checked_out";  // 已出库（区别于借用）
const QString TOOL_PENDING     = "pending";      // 待入库（配置层已建未物理入库）

// ── 工具状态显示映射（全系统唯一定义点，新人改文案/颜色只改这里） ──
// borrowed 状态存在历史显示差异：工具管理页="已借用"，其余页="已借出"。
// keepLegacyBorrowText=true 时保持工具管理页旧行为，默认为新口径"已借出"。
inline QString toolStatusText(const QString& status, bool keepLegacyBorrowText = false) {
    if (status == TOOL_IN_STOCK)    return QStringLiteral("在库");
    if (status == TOOL_CHECKED_OUT) return QStringLiteral("已出库");
    if (status == TOOL_MAINTENANCE) return QStringLiteral("维护中");
    if (status == TOOL_PENDING)     return QStringLiteral("待入库");
    if (status == TOOL_BORROWED)    return keepLegacyBorrowText ? QStringLiteral("已借用") : QStringLiteral("已借出");
    return status;  // 未知状态原样显示
}
// 工具状态对应的状态色（维护中及未知状态统一灰色）
inline QString toolStatusColor(const QString& status) {
    if (status == TOOL_IN_STOCK)    return QStringLiteral("#43a047");
    if (status == TOOL_CHECKED_OUT) return QStringLiteral("#e53935");
    if (status == TOOL_BORROWED)    return QStringLiteral("#f57c00");
    if (status == TOOL_PENDING)     return QStringLiteral("#1890ff");
    return QStringLiteral("#999999");
}
// 位置状态显示（空状态视为待入库；不含"维护中"，未知状态原样）
inline QString positionStatusText(const QString& status) {
    if (status.isEmpty() || status == TOOL_PENDING) return QStringLiteral("待入库");
    return toolStatusText(status);
}

// ── 借用记录状态 ──
const QString RECORD_BORROWING = "borrowing";    // 借用中
const QString RECORD_RETURNED  = "returned";     // 已归还
const QString RECORD_OVERDUE   = "overdue";      // 已逾期

// ── 人脸识别结果（face_recog_log.result）──
// 写入方：FaceRecognitionService（识别判定）、LoginPage::faceResultName（枚举转字符串）
// 使用方：FaceRecognitionService（识别判定写入）、LoginPage::faceResultName（枚举转字符串）
const QString FACE_RESULT_SUCCESS  = "success";   // 识别成功
const QString FACE_RESULT_STRANGER = "stranger";  // 陌生人
const QString FACE_RESULT_REJECTED = "rejected";  // 相似度不足被拒
const QString FACE_RESULT_ERROR    = "error";     // 识别过程异常

// ── 告警处理状态 ──
const QString ALERT_UNHANDLED = "unhandled";     // 待处理

// ── 识别方式 全系统统一为视觉识别 ──
const QString RECOGNITION_VISION = "vision";  // 视觉识别

// ── 工具文档配置 ──
const int     TOOL_DOC_MAX_SIZE_MB = 50;  // 文档最大50MB
// 支持的文档格式（小写比较）
const QStringList TOOL_DOC_SUFFIXES = { "doc", "docx", "pdf" };

// 告警类型编码（sys_alert_type.type_code）：写告警时经 AlertDAO::findTypeIdByCode 查回 type_id
const QString ALERT_TYPE_LOGIN_FAIL = "login_fail";  // 登录失败（密码连续错误触发整机锁定）
const QString ALERT_TYPE_HW_FAULT   = "hw_fault";    // 系统硬件故障（边缘机摄像头状态变化等设备类告警复用此激活类型）
const QString ALERT_TYPE_HW_COMM    = "hw_comm";     // 硬件通讯故障（与边缘计算机链路离线等通信类告警复用此激活类型）

// ── 告警大类（告警日志页两个入口共用同一页面，按类型编码集合区分显示范围）──
// 系统告警：设备与系统状态类；操作告警：用户操作行为类（错放/未授权/登录失败等）。
// 逾期未还(overdue)本期不展示，不入任何一类（数据库中该类型已停用）。
// 类型编码须与 sys_alert_type.type_code 一致：货架错放库中编码为 rack_mismatch。
const QStringList ALERT_SYSTEM_TYPES = {
    "missing", "offline", "low_stock", "system", "power",
    "network_error", "door_open", "stranger",
    "temp_high", "power_low", "sensor_fail", "hw_comm", "hw_fault"
};
const QStringList ALERT_OPERATION_TYPES = {
    "login_fail", "unauthorized", "mismatch", "rack_mismatch"
};

/// 告警级别显示值→库值 单一映射（历史上 SettingService 与 AlertDAO 各硬编码一份）
/// "严重/一般/提示"→error/warn/info；传入已是库值（crit/warn/info 等）则原样返回
inline QString alertLevelCodeFromText(const QString& level)
{
    if (level == QStringLiteral("严重")) return QStringLiteral("error");
    if (level == QStringLiteral("一般")) return QStringLiteral("warn");
    if (level == QStringLiteral("提示")) return QStringLiteral("info");
    return level;
}

// ── 分页 ──
const int PAGE_SIZE_DEFAULT   = 20;
const int PAGE_SIZE_UNLIMITED = 9999;   // 取全量数据的虚拟分页大小

// ── 触屏优化尺寸 ──
// 控件高度/圆角/字号统一由 StyleHelper::Token 提供，此处不再重复定义第二套来源
const int FONT_SIZE_SMALL  = 14;

// ── 人脸服务（face-server.js）监听地址 ──
// HTTP 调用统一入口：/health、/extract、/posture 三处均用此地址，改端口只改这里
const QString FACE_SERVER_HOST = "127.0.0.1";
const int     FACE_SERVER_PORT = 8089;

// ── 人脸识别采集质量 ──
// 采集分辨率 640x480 下的人脸框边长下限(px)：低于此值视为距离过远，
// 特征像素不足会导致相似度下降而识别失败，此时应提示用户靠近而非放宽阈值。
// 实测校准：正常登录距离约 95~105px，明显偏远约 55~62px，
// 故取 70 作为分界——既不误报又能在真远时给出提示。
// 恢复阈值需高于进入阈值形成"滞回区"，避免人脸框在 70px 附近抖动
// 导致提示来回闪烁。
const int FACE_MIN_SIZE   = 70;
const int FACE_RECOVER_SIZE = 85;   // 滞回：超过此值才判定已靠近

// ── 人脸识别采样预算 ──
// 单次登录最多比对的帧数与最长等待时间(毫秒)。
// 远距离或角度不佳时特征相似度天然偏低，"多帧取最优"可在不降低任何阈值的
// 前提下提升可用性——判定标准不变，陌生人仍无法通过，安全性不受影响。
// 阈值定在 0.94 后，多数人 1~2 帧就能通过，留 8 帧 / 6 秒作为上限已经够用。
// 上限放到 12 帧 / 12 秒时，用户会以为界面卡死，等待体验太差，所以收紧一半。
const int  FACE_MATCH_MAX_FRAMES = 8;
const int  FACE_MATCH_MAX_WAIT_MS = 6000;

// 采集置信度下限：低于此值的帧直接丢弃，不浪费比对机会。
// 实测低质量帧(约0.66)提取的特征无法产生任何候选(相似度0)，
// 而 0.75 以上基本都能稳定通过，过滤后可显著提升识别成功率。
const double FACE_MIN_CONFIDENCE = 0.75;

// ── 人脸录入查重 ──
// 一张脸只允许挂在一个账户上：录入时若新采集特征与库内已有特征的余弦相似度
// 达到此值，且库内那条不属于本次录入的用户，则判定为"同一张脸重复录入"，拒绝写入。
// 取 0.95 的标定依据（2026-10-15 face_recog_log 实测分布）：
//   跨人上限 0.9375（两位不同用户帧间实测最高值），同人下限 0.9507（本人成功帧最低值），
//   0.95 居中——不同人不误拦、同人重复不漏判。
// 原 0.90 落在跨人相似度区间内（长得像的两个人实测可达 0.90~0.94），
// 会把不同人误判为重复录入，造成"登录失败+录入被拦"死锁（登录侧单人脸 0.96 拒绝
// 是正确行为：该脸确实不属于库内任何账户，问题出在录入侧拦错了人）。
// 漏检代价评估：0.94~0.95 的同人低质量帧若漏检，登录侧 margin/单人脸阈值仍会拦截
// 并给出密码登录出路，危害可控。
const double FACE_DUPLICATE_SIMILARITY = 0.95;

// ── 统计卡片尺寸 ──
const int STAT_CARD_HEIGHT      = 140;  // 卡片高度
const int STAT_CARD_ICON_SIZE   = 80;   // 图标尺寸
const int STAT_CARD_ICON_RADIUS = 20;   // 图标圆角
const int STAT_CARD_SPACING     = 18;   // 卡片内边距间距
const int STAT_CARD_ICON_FONT   = 40;   // 图标字体大小（emoji）
const int STAT_CARD_VALUE_FONT  = 46;   // 数值字体大小

// ── 网络配置默认值 ──
const QString NET_IP            = "192.168.1.100";
const QString NET_MASK          = "255.255.255.0";
const QString NET_GATEWAY       = "192.168.1.1";
const QString NET_DNS           = "8.8.8.8";
const QString NET_SERVER        = "";
const int     NET_SPEED_MODE    = 0;  // 0=1000M自适应
const int     NET_NETWORK_MODE  = 0;  // 0=单机运行

// ── 告警配置默认值 ──
const int  ALERT_BUZZER_VOL       = 85;
const bool ALERT_LED_ENABLED      = true;
const int  ALERT_OVERDUE_HOURS    = 24;
const int  ALERT_DOOR_TIMEOUT     = 30;
const bool ALERT_VISION_ENABLED   = true;  // 视觉识别异常告警
const int  ALERT_POWER_ALARM_MODE = 0;
const bool ALERT_AUTO_CONFIRM     = true;

// ── 借还配置默认值 ──
const int  BORROW_MAX_COUNT        = 5;
const int  BORROW_DEFAULT_PERIOD   = 48;
const int  BORROW_RETURN_BUFFER    = 30;
const bool BORROW_MANUAL_UNLOCK    = false;
const int  BORROW_BRIGHTNESS       = 80;
const int  BORROW_FACE_SENSITIVITY = 0;

// ── 人脸识别超时/延迟配置 ──
const int FACE_RETRY_DELAY_MS        = 500;    // 人脸识别重试延迟

// ── UI延迟配置 ──
const int UI_LOGIN_FAIL_KEYBOARD_DELAY_MS = 500; // 登录失败后重弹密码键盘延迟
const int UI_LOCK_COUNTDOWN_MS      = 1000;  // 密码锁定倒计时刷新间隔
const bool    BACKUP_AUTO_ENABLED = true;
const int     BACKUP_PERIOD       = 0;   // 0=每日
const int     BACKUP_CACHE_HOURS  = 4;
const QString BACKUP_PATH         = "/mnt/backup";

/**
 * @brief 备份目录默认值（分平台，保证开箱即用可写）
 * @return 备份文件存放目录路径
 * @note 麒麟普通用户对 /mnt 无写入权限，默认落到用户主目录下；
 *       Windows 沿用 /mnt/backup（运行时落在 <盘符>:\mnt\backup，开发机可写）。
 */
inline QString defaultBackupPath() {
#ifdef Q_OS_LINUX
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
           + QStringLiteral("/smartcabinet_backup");
#else
    return BACKUP_PATH;
#endif
}

// ── 磁盘容量保护（导出/备份/日志/文档写入前自动清理，用户无感知，见 DiskSpaceGuard） ──
// 磁盘可用比例低于该百分比时，先清理目标目录下最旧的文件再执行写操作
const int DISK_MIN_FREE_PERCENT = 30;
// 每次清理至少保留的最新文件个数，防止把目录清空（备份目录尤其需要保底）
const int DISK_KEEP_NEWEST_FILES = 3;
// 同一目录两次清理的最小间隔(毫秒)：清理后仍不达标的目录，间隔内直接放行不再扫描，
// 避免日志高频写入时每行都做目录枚举
const int DISK_CLEANUP_INTERVAL_MS = 5 * 60 * 1000;
} // namespace SC
