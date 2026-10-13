/**
 * @file  CommTypes.h
 * @brief 与边缘计算机通信接口的公共结构体——全部通信参数打包处，禁止散参
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
struct RecognizeItem {
    QString position;      // A-01-05 货位
    QString toolId;        // C01-T0012，认不出为空串
    QString classId;       // socket / torx ... 类别透传
    double  confidence = 0.0;  // 0~1，<0.60 按 low_confidence 告警
    QString alertType;     // none/misplaced/unknown/missing/low_confidence
    int     alertLevel = 0;    // 0无 1提示 2一般 3严重
};

struct RecognizeResult {
    QString requestId;                 // 与请求配对（协议3.6）
    QList<RecognizeItem> items;        // 全柜实况清单
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