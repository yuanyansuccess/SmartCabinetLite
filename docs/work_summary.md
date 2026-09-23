# Qt智能柜项目工作汇报

## 项目概述
- **目标**：将Web管理页面1:1复刻为Qt Widget桌面程序
- **适配**：国产麒麟V10/V11（x86+ARM64）+ Windows 10/11
- **状态**：V8.0 稳定运行，持续优化

## 版本演进

### V8.0（当前版本 — 2026-07-07）
- 纯MySQL模式（彻底删除SQLite）
- face-api.js深度学习128维特征人脸识别
- 异步HTTP方位检测（准确率提升）
- isNetworkConnected()物理网络检测（跨平台）
- 列表性能优化（checkbox勾选QSet优化，100倍提速）
- 机组隔离全链路双层防护
- 位置维度工具管理重构

### 关键修复记录
- pitch方向反转修复（人脸识别上下偏误判）
- 同步HTTP阻塞导致简易模式问题修复
- 视图重复行Bug修复（v_tool_latest_operation）
- 启动时重复清空告警数据修复
- 出库记录工具编号缺失修复
- 映射表status字段缺失修复（MySQL兼容）

## 已完成工作

### 1. 代码开发（100%完成）
- ✅ 12个核心页面全部实现
- ✅ 所有业务逻辑完成
- ✅ 触屏优化统一标准
- ✅ 麒麟系统适配代码

### 2. 数据库（7个脚本）
- schema.sql（完整建表DDL）
- smart_cabinet.sql（完整数据导出）
- 3个迁移脚本（机组/借款人/借用一致性）
- 2个种子数据脚本（告警/借用）

### 3. 人脸识别
- face-api.js深度学习（128维特征向量）
- 5方位检测（居中/左侧/右侧/上偏/下偏）
- 异步HTTP调用，防堆积机制

### 4. 文档（18个文档）
- 12个页面业务逻辑文档
- 项目总结/工作汇报/代码质量报告
- 触屏优化系列文档
- 部署手册

## 技术栈
| 层级 | 技术 | 版本 |
|------|------|------|
| 前端框架 | Qt Widgets | 6.5.3 |
| 编译器 | MSVC | 2019 (VS2022) |
| 构建系统 | CMake + MSBuild | - |
| 数据库 | MySQL | 5.7 |
| 人脸识别 | Node.js + face-api.js | v22 |
| 目标平台 | Windows + 麒麟Linux | - |

## 编译环境
- **Qt**: E:\Qt\6.5.3\msvc2019_64
- **MSBuild**: D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe
- **CMake**: E:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
- **编译命令**: `MSBuild build\QtSmartCabinet.vcxproj /t:Build /p:Configuration=Debug /p:Platform=x64`

## 部署导出（一键命令）
- 导出源文件：`robocopy src QtSmartCabinet\src /MIR`
- 导出文档：`robocopy docs QtSmartCabinet\docs *.md`
- 导出资源：`robocopy resources QtSmartCabinet\resources /MIR`
- 导出样式：`robocopy styles QtSmartCabinet\styles /MIR`
- 导出数据库：`robocopy ..\code\database QtSmartCabinet\database`
- 导出配置：`copy CMakeLists.txt QtSmartCabinet\` + `copy CMakePresets.json QtSmartCabinet\`

---
**汇报人**：袁燕  
**汇报时间**：2026-07-07 14:51  
**项目状态**：V8.0 稳定运行
