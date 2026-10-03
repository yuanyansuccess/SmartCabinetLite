# QtSmartCabinet 智能工具柜管理系统

## 项目简介

基于 **Qt 6.5.3 Widget** 框架开发的智能工具柜管理系统，采用严格五层架构，**1:1像素级复刻** Web前端（Vue.js）全部页面和功能。

支持 **Windows / 银河麒麟V10/V11 (x86_64 + aarch64)** 多平台部署。

---

## 技术栈

| 层级 | 技术 | 说明 |
|------|------|------|
| UI框架 | Qt 6.5.3 Widget | 原生C++，禁用QML |
| 编译器 | MSVC 2019 / GCC | 跨平台编译 |
| 数据库 | MySQL 8.0 | 直连，双驱动(QMYSQL/QODBC) |
| 构建工具 | CMake 3.16+ | Ninja / MSBuild / Make |
| 架构模式 | 五层架构 | 表示层→服务层→DAO→基础设施→模型 |
| 编码标准 | C++17 | 智能指针、禁止全局变量 |

---

## 架构说明：UI 与逻辑分离

### 页面层（src/pages/）—— 三件套模式

每个页面由三个文件组成，职责单一：

| 文件 | 职责 | 维护方式 |
|------|------|----------|
| `XxxPage.ui` | 静态布局（控件树/间距/样式/文字） | **Qt Creator 双击可视化拖拽编辑** |
| `XxxPage.h` | 类声明、槽函数、业务成员 | 代码维护 |
| `XxxPage.cpp` | 业务逻辑（槽函数/数据加载/动态渲染） | 代码维护 |

页面构造固定模式：`ui->setupUi(this)` 加载布局 → 桥接控件到 `m_xxx` 成员 → 动态组件装入预留槽位。**改布局不用碰 C++，改业务不用关心布局。**

### 组件层（src/components/）—— setupUI 规范

全部自定义组件统一模式：构造函数只调 `setupUI()`（视图构建），业务逻辑独立成方法，对外通过信号交互。自绘控件（软键盘/数字键盘/摄像头预览）的绘制代码即视图，绘制参数与输入逻辑分离。

### 数据流

```
读路径（查询/展示）：Page槽函数 → DAO(参数化SQL) → MySQL
写路径（增删改业务）：Page槽函数 → Service(业务校验+编排) → DAO → MySQL
结果回显：Page渲染 ← QJsonObject/QJsonArray ← DAO/Service
```

**为什么不是一律走 Controller**：读多写少的查询类直接由 Page 调 DAO（少一层无意义转发），
涉及业务规则/跨表一致性的写操作必须经 Service。Controller 仅用于需要缓存或聚合多次调用的场景。

规则：
1. pages 层**禁止**写 SQL（实测已 0 违规，改动时保持）
2. 状态值/常量统一走 `SC::`（`common/Constants.h`），禁止裸字符串
3. 样式统一走 `utils/StyleHelper`（含 `Token` 尺寸/字号令牌），**不要再引入 qss 文件**
4. 控件尺寸/字号必须用 `StyleHelper::Token::*` 令牌，禁止裸写数字

---

## 快速开始

### Windows 编译运行
```bash
# 方法1：VS2022 打开 build/QtSmartCabinet.sln 生成后编译
# 方法2：命令行
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug
build\Debug\QtSmartCabinet.exe

# 方法3：一键脚本
build.bat
```

### 麒麟编译部署
```bash
# 一键编译（x86_64）
chmod +x deployment/build_kylin.sh
./deployment/build_kylin.sh

# 一键编译（ARM64 / 鲲鹏等）
chmod +x deployment/build-kylin-arm64.sh
./deployment/build-kylin-arm64.sh
```
> 完整部署说明见 **`KYLIN_DEPLOY.md`**（含环境安装、目录结构、故障排查）。
> ⚠ 部署后 `bin/tools/face-recognition/` 必须存在且柜机装了 Node.js，否则人脸功能全废。

---

## 项目结构

```
SmartCabinetLite/
├── src/
│   ├── pages/          # 表示层 - 16个页面/对话框实现
│   ├── components/     # 组件层 - 触屏组件(BaseDialog/FormFactory/摄像头/软键盘/提示框)
│   ├── controller/     # 控制层 - 需要缓存/多次聚合调用时使用
│   ├── services/       # 服务层 - 业务规则校验+跨表编排
│   ├── db/             # 数据访问层 - DAO参数化查询(SQL只允许出现在这里)
│   ├── model/          # 领域模型 - 纯数据结构
│   ├── common/         # 基础设施 - 连接管理/配置/常量(SC::)
│   └── utils/          # 工具类 - StyleHelper(颜色+Token令牌)
├── tests/              # 单元测试(test_components)
├── tools/face-recognition/  # 人脸识别Node服务(face-server.js, 8089端口)
├── kylin/              # 麒麟适配脚本
├── docs/               # 业务逻辑与架构文档
└── CMakeLists.txt      # 编译配置
```

> 人脸识别依赖 `tools/face-recognition`（Node 服务，8089 端口）：程序启动时自动拉起，
> 随程序退出自动停止。打包/部署时**必须**把该目录一起带上，否则人脸功能全废。

---

## 功能模块

### 页面列表（src/pages，16 个实现）
| 页面 | 文件 | 功能 |
|------|------|------|
| 登录页 | LoginPage | 人脸/密码双认证，陌生人拦截 |
| 功能选择 | UserEntryDialog | 普通用户入口（借用归还/查询/退出） |
| 借还会话 | CabinetSessionDialog | 开柜-取还-清单确认-落库 |
| 仪表盘 | DashboardPage | 统计卡片+借用列表+告警面板 |
| 工具借用 | ToolBorrowPage | 任务类型+工具选择+借用登记 |
| 工具归还 | ToolReturnPage | 借用记录+归还登记 |
| 工具管理 | ToolManagementPage | 工具CRUD+详情三Tab |
| 工具入库 | ToolCheckinPage | pending工具入库到位置 |
| 工具出库 | ToolCheckoutPage | 位置工具出库（移除识别信息） |
| 用户管理 | UserManagementPage | 用户CRUD+批量导入+人脸录入 |
| 批量导入 | BatchImportDialog | Excel 批量导入用户 |
| 台账统计 | LedgerStatsPage | 分类/部门/日期多维度统计 |
| 告警日志 | AlertLogsPage | 告警列表+筛选+处理闭环 |
| 系统设置 | SystemSettingsPage | 系统参数（网络/告警/借还/备份+系统信息） |
| 系统维护 | SystemMaintenancePage | 工具维护/对照关系/位置映射维护 |
| 主窗口 | MainWindow | 登录态+导航+页面切换 |

> 人脸录入有两条路径：独立页 `FaceEnrollPage`（旧）与用户管理内弹窗（现役）。

### 触屏组件
- **SoftKeyboard**：全屏数字/字母/中文软键盘
- **FaceCameraWidget**：摄像头实时预览+人脸捕获
- **TopBar**：顶部导航栏+用户信息

---

## 设计规范

### 视觉规范（以 utils/StyleHelper.h 为唯一来源）
- 主色：#4da3ff | 成功：#43a047 | 警告：#f57c00 | 危险：#e53935
- 背景：#f0f2f5 | 卡片：白色 | 圆角：10~14px | 主色文字：#1a1a2e
- 尺寸/字号令牌：`StyleHelper::Token::*`（控件高 32/40/44/48/56、字号 12~22）
- 触屏：主交互控件高 ≥48px | 输入框 48px | 正文字号 ≥16px

> 颜色与尺寸**不要在页面里硬写**，一律取 `StyleHelper` 的方法/令牌，改一处全局生效。

### 架构规范
- 五层架构严格单向依赖
- 参数化SQL查询防注入
- 智能指针管理资源
- 文件头统一注明作者袁燕

---

## 数据库

### 表结构
以 `src/db/` 下各 DAO 为准（表数量随版本演进，README 不再列举避免过期）。
主要表：`sys_user`、`sys_department`、`tool_info`、`tool_category`、`tool_cabinet`、
`machine_group`、`tool_position_mapping`（位置映射，位置维度的核心表）、
`tool_borrow_record`（借用记录）、`sys_operation_log`（出入库操作日志）、
`sys_alert`（告警）、`sys_alert_type`（告警类型字典）、`task_type`（任务类型）、
`sys_face`（人脸特征）、`sys_face_log`（识别日志）、`stranger_access_log`（陌生人记录）。

> 排查数据问题直接查 MySQL，**不要**在代码里写数据修复函数（项目铁律）。

---

## 📖 文档导航

| 文档 | 适合谁 |
|---|---|
| [`docs/智能工具柜系统-新人维护与麒麟部署手册.docx`](docs/智能工具柜系统-新人维护与麒麟部署手册.docx) | **唯一维护手册**（新人第一份）：系统概述、整体架构、数据库、核心业务规则、16 个页面业务逻辑、人脸链路、开发调试手段、麒麟部署、规范红线、故障速查、附录速查表 |
| [`KYLIN_DEPLOY.md`](KYLIN_DEPLOY.md) | 麒麟部署速查（内容已并入手册第 8 章） |
| [`README_QtCreator.md`](README_QtCreator.md) | 用 Qt Creator 打开工程的说明 |

---

### 表结构（15张表）
`sys_user`, `tool_info`, `tool_borrow_record`, `tool_checkin_record`, `tool_checkout_record`, `sys_alert`, `sys_operation_log`, `sys_department`, `tool_category`, `tool_cabinet`, `checkout_reason`, `task_type`, `task_type_tool`, `stranger_access_log`, `cabinet_snapshot`

### 视图（2个）
`v_borrowing_tools`, `v_dashboard_stats`

---

## 版本历史

| 版本 | 日期 | 主要变更 |
|------|------|----------|
| V2.0.0 | 2026-06-21 | 五层架构重构+麒麟适配 |
| V1.0.13 | 2026-06-15 | 陌生人页面+人脸识别修复 |
| V1.0.8 | 2026-06-13 | 数据层设计+DAO封装 |
| V1.0.1 | 2026-06-01 | 初始版本 |

---

## 作者

**袁燕**  -  高级全栈技术总监

---

*最后更新：2026-06-21*
