#pragma once
// 智能柜Qt Widget 2.0
// 全局常量定义
#include <QString>
#include <QStringList>

namespace SC {
// ── 应用信息 ──
const QString APP_NAME    = "智能工具柜管理系统";
const QString APP_VERSION = "V2.00";
const QString APP_ORG     = "SmartCabinet";

// ── 数据库配置（默认值，运行时可修改） ──
const QString DB_HOST     = "127.0.0.1";
const int     DB_PORT     = 3306;
const QString DB_NAME     = "smart_cabinet";
const QString DB_USER     = "root";
const QString DB_PASS     = "root";  // 注意：空密码导致MySQL连接失败回退SQLite

// ── 用户角色 ──
const QString ROLE_ADMIN  = "admin";
const QString ROLE_USER   = "user";

// ── 用户状态（值与页面实际写入/播种数据一致，新人勿另造值） ──
const QString USER_ACTIVE   = "active";
const QString USER_PENDING  = "pending";
const QString USER_DISABLED = "disabled";  // 禁用（UserManagementPage启用/禁用切换写入值）
const QString USER_LOCKED   = "locked";    // 已锁定
const QString USER_DELETED  = "deleted";

// ── 新用户初始密码（批量导入/新建用户统一来源，修改此处即可全局生效）──
const QString DEFAULT_INIT_PASSWORD = "123456";

// ── 操作日志类型（sys_operation_log.operation_type） ──
const QString OP_CHECKIN  = "checkin";
const QString OP_CHECKOUT = "checkout";

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

// ── 告警处理状态 ──
const QString ALERT_UNHANDLED = "unhandled";     // 待处理
const QString ALERT_HANDLED   = "handled";       // 已处理
const QString ALERT_IGNORED   = "ignored";       // 已忽略

// ── 识别方式 全系统统一为视觉识别 ──
const QString RECOGNITION_VISION = "vision";  // 视觉识别

// ── 工具文档配置 ──
const int     TOOL_DOC_MAX_SIZE_MB = 50;  // 文档最大50MB
// 支持的文档格式（小写比较）
const QStringList TOOL_DOC_SUFFIXES = { "doc", "docx", "pdf" };

// ── 设备状态 ──
const QString DEV_ONLINE  = "online";
const QString DEV_OFFLINE = "offline";
const QString DEV_ERROR   = "error";

// ── 告警级别 ──
const QString ALERT_INFO    = "info";
const QString ALERT_WARNING = "warning";
const QString ALERT_ERROR   = "error";

// ── 分页 ──
const int PAGE_SIZE_DEFAULT   = 20;
const int PAGE_SIZE_UNLIMITED = 9999;   // 取全量数据的虚拟分页大小
const int PAGE_SIZE_ALERT     = 200;    // 告警列表默认分页
const int PAGE_SIZE_TOOLS     = 100;    // 工具列表大分页（借用页全量加载）

// ── 触屏优化尺寸 ──
// 控件高度/圆角/字号统一由 StyleHelper::Token 提供，此处不再重复定义第二套来源
const int FONT_SIZE_SMALL  = 14;

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
// 实测服务端抓帧失败率较高(约2/3)，故采样预算放宽到 12 帧，
// 配合置信度过滤(见 FACE_MIN_CONFIDENCE)后有效帧比例大幅提升。
const int  FACE_MATCH_MAX_FRAMES = 12;
const int  FACE_MATCH_MAX_WAIT_MS = 12000;

// 采集置信度下限：低于此值的帧直接丢弃，不浪费比对机会。
// 实测低质量帧(约0.66)提取的特征无法产生任何候选(相似度0)，
// 而 0.75 以上基本都能稳定通过，过滤后可显著提升识别成功率。
const double FACE_MIN_CONFIDENCE = 0.75;

// ── 统计卡片尺寸 ──
const int STAT_CARD_HEIGHT      = 100;  // 卡片高度
const int STAT_CARD_ICON_SIZE   = 56;   // 图标尺寸
const int STAT_CARD_ICON_RADIUS = 14;   // 图标圆角
const int STAT_CARD_SPACING     = 14;   // 卡片内边距间距
const int STAT_CARD_ICON_FONT   = 28;   // 图标字体大小（emoji）
const int STAT_CARD_VALUE_FONT  = 32;   // 数值字体大小

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
const int  BORROW_LOCK_TIME        = 5;
const int  BORROW_FACE_SENSITIVITY = 0;

// ── 人脸识别超时/延迟配置 ──
const int FACE_LOGIN_TIMEOUT_MS      = 30000;  // 登录人脸识别总超时
const int FACE_ENROLL_DELAY_MS       = 2000;   // 人脸录入准备延迟
const int FACE_ENROLL_CLOSE_DELAY_MS = 800;    // 人脸录入成功关闭延迟
const int FACE_SERVICE_POLL_MS       = 500;    // 人脸服务就绪轮询间隔
const int FACE_NETWORK_BUFFER_MS     = 1000;   // 网络请求缓冲超时
const int FACE_RETRY_DELAY_MS        = 500;    // 人脸识别重试延迟

// ── UI延迟配置 ──
const int UI_KEYBOARD_POPUP_DELAY_MS = 150;    // 软键盘弹出延迟
const int UI_CAMERA_INIT_DELAY_MS    = 100;    // 摄像头初始化延迟
const int UI_DASHBOARD_LOAD_DELAY_MS = 300;    // Dashboard数据加载延迟
const int UI_CPU_STATS_DELAY_MS      = 500;    // CPU统计刷新延迟
const bool    BACKUP_AUTO_ENABLED = true;
const int     BACKUP_PERIOD       = 0;   // 0=每日
const int     BACKUP_CACHE_HOURS  = 4;
const QString BACKUP_PATH         = "/mnt/backup";
} // namespace SC
