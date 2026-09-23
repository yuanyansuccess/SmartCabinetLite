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

## 快速开始

### Windows 编译运行
```bash
# 方法1：使用VS2022
cd QtSmartCabinet
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug
build\Debug\QtSmartCabinet.exe

# 方法2：使用build_qt.bat（自动编译+运行）
build_qt.bat
```

### 麒麟编译部署
```bash
# 一键编译
chmod +x kylin/build-kylin.sh
./kylin/build-kylin.sh Release

# 部署打包
./kylin/deploy-kylin.sh
sudo dpkg -i qtsmartcabinet_2.0.0_*.deb
```

---

## 项目结构

```
QtSmartCabinet/
├── src/
│   ├── pages/          # 表示层 - 13个业务页面
│   ├── components/     # 表示层 - 触屏组件(软键盘/摄像头/顶栏)
│   ├── services/       # 服务层 - 业务逻辑+流程编排
│   ├── db/             # 数据访问层 - DAO参数化查询
│   ├── model/          # 领域模型 - 纯数据结构
│   ├── common/         # 基础设施 - 连接管理/配置
│   └── utils/          # 工具类
├── kylin/              # 麒麟适配脚本
├── resources/          # 资源文件
└── CMakeLists.txt      # 编译配置
```

---

## 功能模块

### 页面列表（13页）
| 页面 | 文件 | 功能 |
|------|------|------|
| 登录页 | LoginPage | 人脸/密码双认证，陌生人拦截 |
| 仪表盘 | DashboardPage | 统计卡片+借用列表+告警面板 |
| 工具借出 | ToolBorrowPage | 工具选择、数量调整、借用确认 |
| 工具归还 | ToolReturnPage | 借出列表、归还操作 |
| 工具管理 | ToolManagementPage | 分类树+工具CRUD |
| 工具入库 | ToolCheckinPage | 新工具入库登记 |
| 工具出库 | ToolCheckoutPage | 工具出库登记 |
| 用户管理 | UserManagementPage | 用户CRUD+批量导入 |
| 台账统计 | LedgerStatsPage | 分类/部门/日期多维度统计 |
| 告警日志 | AlertLogsPage | 告警列表+筛选+处理 |
| 系统设置 | SystemSettingsPage | 系统参数配置 |
| 人脸录入 | FaceEnrollPage | 人脸信息采集注册 |
| 主窗口 | MainWindow | 导航+页面切换 |

### 触屏组件
- **SoftKeyboard**：全屏数字/字母/中文软键盘
- **FaceCameraWidget**：摄像头实时预览+人脸捕获
- **TopBar**：顶部导航栏+用户信息

---

## 设计规范

### 视觉规范（1:1复刻Web前端）
- 主色：#1890ff | 成功：#52c41a | 警告：#faad14 | 危险：#ff4d4f
- 背景：#f0f2f5 | 卡片：白色/圆角12px | 标题：#1a1a2e
- 表格：无斑马纹 | th:13px/#aaa | td:14px/#333
- 触屏：按钮≥48x48px | 输入框≥48px高 | 字体≥16px

### 架构规范
- 五层架构严格单向依赖
- 参数化SQL查询防注入
- 智能指针管理资源
- 文件头统一注明作者袁燕

---

## 数据库

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
