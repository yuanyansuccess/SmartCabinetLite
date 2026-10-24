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
// 协议3.3字段规整约定：items 的每一项都必须带全下列6个字段，禁止字段时有时无，
// 上位机不再为缺字段猜默认值（缺字段的项按非法项记日志并跳过）。
// present 用于区分"这个货位扫过了、确实是空的"与"这个货位根本没有回报"：
//   全柜核对（库内货位 vs 实况清单）只有靠它才能精确判定缺失，不会把没扫到的货位误报成工具丢失。
struct RecognizeItem {
    QString position;      // A-01-05 货位（必填，非空）
    QString toolId;        // C01-T0012，认不出为空串（必填，空串=该货位认不出）
    QString classId;       // socket / torx ... 类别透传（必填，无法分类时为空串）
    double  confidence = 0.0;  // 0~1，认不出时为 0（必填）
    QString alertType = QStringLiteral("none"); // none/misplaced/unknown/missing/low_confidence（必填）
    int     alertLevel = 0;    // 0无 1提示 2一般 3严重（必填，正常项为 0）
    bool    present = true;    // true=摄像头扫到该货位；false=该货位未覆盖/未扫到
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
    QList<CameraStatusItem> cameras;   // 可为空（自检功能未启用时）
};
