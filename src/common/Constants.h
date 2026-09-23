#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0
// 日期：2026-06-21  全局常量定义
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
const QString DB_PASS     = "root";  // [2026-06-23] 修复：空密码导致MySQL连接失败回退SQLite

// ── 用户角色 ──
const QString ROLE_ADMIN  = "admin";
const QString ROLE_USER   = "user";

// ── 用户状态 ──
const QString USER_ACTIVE   = "active";
const QString USER_INACTIVE = "inactive";
const QString USER_PENDING  = "pending";
const QString USER_DELETED  = "deleted";

// ── 工具状态 ──
const QString TOOL_IN_STOCK    = "in_stock";
const QString TOOL_BORROWED    = "borrowed";
const QString TOOL_MAINTENANCE = "maintenance";

// ── 识别方式 [V2.01 2026-06-27] RFID/视觉识别二选一 ──
const QString RECOGNITION_RFID   = "rfid";    // RFID识别
const QString RECOGNITION_VISION = "vision";  // 视觉识别

// ── 工具文档配置 [V2.01] ──
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
const int BTN_HEIGHT       = 48;
const int BTN_HEIGHT_LARGE = 56;
const int INPUT_HEIGHT     = 48;
const int FONT_SIZE_TITLE  = 20;
const int FONT_SIZE_BODY   = 16;
const int FONT_SIZE_SMALL  = 14;
const int BORDER_RADIUS    = 12;
const int BORDER_RADIUS_LG = 16;

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
const bool ALERT_RFID_ENABLED     = true;
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
