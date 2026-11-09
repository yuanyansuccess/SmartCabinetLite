/**
 * @file  CommTypes.h
 * @brief 与边缘计算机通信接口的公共结构体——全部通信参数打包处，禁止散参
 * @author 袁燕
 * @note  依赖方向：本文件只被 comm/ 和 services/ 包含，pages/ 不许直接包含。
 *        JSON 字段为下划线（tool_id），结构体为驼峰（toolId），序列化时转换。
 *        与IO下位机通信接口（Modbus TCP）实现时再追加 IoConfig/IoResult。
 */
#pragma once
#include <QString>
#include <QList>

// ---------- 通信配置（由 CommConfig 加载，缺省值即协议规定值） ----------
struct EdgeConfig {
    bool enabled = true;
    int  port = 9000;                 // TCP Server 监听端口（协议第1章）
    int  offlineTimeoutMs = 15000;    // 周期帧15秒未达判离线（协议第6章）
    int  requestTimeoutMs = 3000;     // 应答超时（协议3.6）
    int  maxRetry = 2;                // 最大重发次数（协议3.6/第5章）
    int  frameMaxBytes = 1024 * 1024; // 单帧上限，防异常数据撑爆缓冲
};

// ---------- recognize_ack：识别清单 ----------
// 协议3.3字段规整约定：一项一货位，position / tool_id / confidence 三项每项必填。
// 任一项类型不符、取值越界或货位重复时，整帧判为不可信并拒绝本次核对：
// 空的工具编号会被当成"货位已空"，工具未取走却被写成已出库。
// alert_type 可缺席（按 none 处理），兼容未升级的边缘机；一旦带上，类型与取值必须合规。
// 表13：items 每项下发 position/tool_id/confidence/alert_type 四个字段；tool_id 为空表示该货位无工具。
// 边缘机须全柜逐货位回报，空货位也要回报一条 tool_id 为空的项，不得从清单中省略。
// alert_type 为告警类型，与上位机告警字典对应。空 items 数组是合法的"柜内无变化"清单。
struct RecognizeItem {
    QString position;      // A-01-05 货位（必填，非空字符串）
    QString toolId;        // C01-T0012，认不出为空串（必填字符串，空串=该货位认不出）
    double  confidence = 0.0;  // 0~1（必填数值）
    QString alertType = QStringLiteral("none"); // 表13：告警类型，与上位机告警字典对应；可缺席取 none；取值限 none/misplaced/unknown/missing/low_confidence
};

struct RecognizeResult {
    QString requestId;                 // 与请求配对（协议3.6）
    QList<RecognizeItem> items;        // 全柜实况清单
};

// ---------- open_ack：开箱（开锁）结果 ----------
struct OpenResult {
    QString requestId;   // 与开箱请求配对
    bool    success = false;  // true=柜体已真实打开（开锁成功）
    QString message;      // 失败原因（人读，success=false 时有效）
};

// ---------- periodic：周期帧（边缘机健康状态） ----------
struct CameraStatusItem {
    QString cameraId;      // CAM1
    QString status;        // ok/offline/low_light/dirty/comm_error（依赖硬件选型，可缺席）
};

struct PeriodicInfo {
    qint64 uptimeSec = 0;              // 边缘机本次启动累计秒数
    QString softwareVersion;            // 边缘机软件版本（版本合帧：随周期帧带出，免去 version_query/version_ack 握手）
    QList<CameraStatusItem> cameras;   // 可为空（自检功能未启用时）
};
