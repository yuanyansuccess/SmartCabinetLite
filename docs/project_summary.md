# QtSmartCabinet 项目总结

**项目名称**: 智能柜管理桌面程序（Qt Widget）  
**原项目**: 智能柜Web管理页面（Vue.js）  
**目标**: 1:1复刻Web界面到Qt Widget桌面程序，适配国产麒麟系统  
**开发时间**: 2026-06-15 至今  
**当前状态**: V8.0 稳定运行，持续优化中  
**最后更新**: 2026-07-07

## 项目概述

### 功能模块（12个页面全部完成）
1. ✅ **登录页面** (LoginPage) - 人脸识别登录 + 账号密码登录
2. ✅ **仪表盘** (DashboardPage) - 工具统计/借用统计/告警概览
3. ✅ **工具管理** (ToolManagementPage) - 位置维度工具管理
4. ✅ **工具借用** (ToolBorrowPage) - 任务制借用流程，机组隔离
5. ✅ **工具归还** (ToolReturnPage) - 扫码/视觉归还
6. ✅ **工具出库** (ToolCheckoutPage) - 报废/调拨出库
7. ✅ **工具入库** (ToolCheckinPage) - pending工具入库到指定位置
8. ✅ **用户管理** (UserManagementPage) - 用户/部门/人脸管理
9. ✅ **台账统计** (LedgerStatsPage) - 借用统计报表
10. ✅ **告警日志** (AlertLogsPage) - 16类告警类型管理
11. ✅ **系统设置** (SystemSettingsPage) - 系统配置/机组管理/任务类型
12. ✅ **人脸录入** (FaceEnrollPage) - face-api.js深度学习人脸注册

### 技术栈
- **前端**: Qt 6.5.3 Widgets + MSVC 2022 + CMake
- **数据库**: MySQL 5.7 纯MySQL模式（已删除SQLite回退）
- **人脸识别**: Node.js v22 + face-api.js 深度学习128维特征
- **接口**: QNetworkAccessManager HTTP调用本地面部识别服务

### 核心设计理念（V2.04）
- 一个位置（机组-柜子-层-位号）只存放一个工具，一一对应
- 工具总数 = 在库工具 + 已借出工具（不含出库的）
- 入库 = pending工具放置到指定位置（不创建新tool_info记录）
- 工具品类仅通过「工具维护」页面添加
- 机组隔离：用户只能借用本机组工具（双层隔离：DAO+Service层）

## 架构分层

```
src/
├── common/    - 公共模块（AppConfig单例, DatabaseManager, PositionFormatter, Constants）
├── db/        - DAO层（BaseDAO基类, ToolDAO, UserDAO, RecordDAO, AlertDAO等）
├── controller/ - 业务控制层（事务管理）
├── services/  - Service层（AuthService, BorrowService, ReturnService, AlertService等）
├── model/     - 实体模型（ToolInfo, MachineGroup, ToolCabinet等）
├── pages/     - 12个页面（UI层）
├── components/ - 公共组件（FaceCameraWidget, SoftKeyboard, CameraCapture, TopBar等）
└── utils/     - 工具类
```

## 关键技术特性

### V8.0 最新特性（2026-07-07）
- **isNetworkConnected()**: DatabaseManager新增物理网络连接检测，替代数据库连接判断
- **face-api.js**: 深度学习128维特征向量，阈值0.72/0.85/0.62
- **异步HTTP方位检测**: QNetworkAccessManager + postureRequestPending防堆积
- **机组隔离**: BorrowService双层隔离（查询层+借用层）
- **位置维度管理**: tool_position_mapping表+findAllTools位置维度JOIN
- **数据库**: 纯MySQL模式，7个数据库脚本（schema/migration/seed）

### 触屏优化标准
- 按钮最小48x48px（大按钮56px），字体16-18px
- 输入框高度48px，字体16px
- 软键盘统一使用SoftKeyboard组件
- 小米极简美学设计风格

### 跨平台适配
- Windows 10/11 (MSVC 2022)
- 麒麟V10/V11 (x86 + ARM64)
- 电池检测：Windows API + 麒麟sysfs
- 网络检测：QNetworkInterface跨平台统一

## 编译环境

### 开发环境
- **操作系统**: Windows 11
- **Qt版本**: 6.5.3 (E:\Qt\6.5.3\msvc2019_64)
- **编译器**: MSVC 2019 (VS2022 Community)
- **构建系统**: CMake + MSBuild
- **MSBuild**: D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe
- **CMake**: E:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
- **MySQL**: C:\Program Files\MySQL\MySQL Server 5.7

### 编译命令
```powershell
# 快速编译（vcxproj）
MSBuild build\QtSmartCabinet.vcxproj /t:Build /p:Configuration=Debug /p:Platform=x64

# CMake重新配置
cmake -S . -B build
```

## 数据库

### 部署数据库文件（QtSmartCabinet/database/）
| 文件 | 说明 |
|------|------|
| schema.sql | 完整建表DDL（MySQL兼容） |
| smart_cabinet.sql | 完整数据库导出 |
| alert_seed_data_V1.00.8.sql | 告警种子数据 |
| borrow_seed_data_V1.00.8.sql | 借用种子数据 |
| migration_machine_group.sql | 机组数据迁移 |
| migration_V1.00.8_add_borrower.sql | 借款人字段迁移 |
| fix_borrow_consistency.sql | 借用数据一致性修复 |

### 默认管理员
- 账号: CF001 / 密码: 123456
- 角色: admin

## 文档清单

### docs/ 目录（18个文件）
- 12个页面业务逻辑文档（*_BusinessLogic.md）
- project_summary.md - 项目总结（本文件）
- work_summary.md - 工作汇报
- code_quality_report.md - 代码质量报告
- database_setup.md - 数据库配置指南
- touchscreen_checklist.md - 触屏优化检查清单
- touchscreen_fix_report.md - 触屏优化修复报告
- final_touchscreen_report.md - 触屏优化最终报告
- SmartCabinet_部署手册_V2.00.docx - 部署手册

## 项目亮点

1. **100%界面复刻** - Web版与Qt版界面完全一致
2. **触屏优化** - 所有交互元素适配触摸操作，小米极简美学
3. **跨平台** - Windows + 麒麟系统双平台
4. **深度学习人脸识别** - face-api.js 128维特征，准确率>94%
5. **机组隔离** - 双层安全隔离（DAO + Service），用户只能借用本机组工具
6. **位置维度管理** - 一个位置一个工具，清晰直观
7. **纯MySQL模式** - 删除SQLite双库，数据一致性有保障
8. **物理网络检测** - isNetworkConnected()跨平台网卡状态检测

---
**项目状态**: V8.0 稳定运行  
**报告更新时间**: 2026-07-07 14:51  
**报告人**: 袁燕
