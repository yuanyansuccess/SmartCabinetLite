#pragma once
/**
 * @file Logger.h
 * @brief 轻量统一日志框架（基于 qInstallMessageHandler）
 * @author 袁燕
 *
 * 目标：把散落的 qDebug/qWarning/qInfo 统一为「分级 + 分类 + 统一格式落盘」。
 * 设计原则：
 *   1) 零侵入——现有 qDebug/qWarning 等调用无需修改即可纳入统一落盘
 *   2) 可开关——级别、控制台输出、敏感日志均通过环境变量控制
 *   3) 渐进替换——先建框架，业务代码按需逐步改用便捷宏
 *
 * 环境变量：
 *   SC_LOG_LEVEL=debug|info|warning|critical|off   （默认 debug）
 *   SC_LOG_CONSOLE=0|1                              （默认 1，控制台同时输出）
 *   SC_LOG_SENSITIVE=0|1                            （默认 0，关闭相似度/阈值等敏感判定日志）
 *   SC_LOG_DIR=<目录>                               （默认：程序数据目录下的 logs）
 *
 * 日志文件：logs/app_yyyyMMdd.log（按天滚动）
 * 格式：<时间> [级别] [分类] 消息
 */
#include <QString>

namespace Log {

enum class Level { Debug = 0, Info, Warning, Critical, Off = 99 };

/// 安装全局消息处理器（main 中调用一次即可）
void install();

/// 当前日志级别（低于该级别的消息被丢弃）
Level level();
void setLevel(Level l);

/// 敏感判定日志（相似度/阈值/特征等）是否允许输出
bool sensitiveEnabled();
void setSensitiveEnabled(bool on);

/// 日志文件所在目录
QString logDirectory();

/// 便捷判定：某条消息是否属于敏感判定日志
bool isSensitiveMessage(const QString& msg);

}  // namespace Log
