# QtSmartCabinet - Qt Creator 打开指南

## 方法一：直接打开 CMakeLists.txt（推荐）

1. 打开 Qt Creator
2. 菜单：`文件` → `打开文件或项目`
3. 选择 `D:\CFDZ\smartCabinet\trunk\QtSmartCabinet\CMakeLists.txt`
4. 点击 `打开`
5. 在 `构建和运行` 配置中：
   - 选择 Qt 版本（Qt 6.5+）
   - 构建目录：`build2`（已存在）或新建 `build`
6. 点击 `配置项目`

**优点**：
- 跨平台（Windows/Linux 通用）
- 与现有构建系统完全一致
- 支持 Ninja 构建（速度快）

---

## 方法二：打开 .pro 文件（传统方式）

1. 打开 Qt Creator
2. 菜单：`文件` → `打开文件或项目`
3. 选择 `D:\CFDZ\smartCabinet\trunk\QtSmartCabinet\QtSmartCabinet.pro`
4. 点击 `打开`
5. 配置构建套件（Kit）

**优点**：
- Qt Creator 原生支持
- 适合纯 Qt Widgets 项目
- Linux 下可直接 `qmake && make`

---

## Linux 下编译（银河麒麟 KylinOS）

### 前置依赖
```bash
# 安装 Qt 6.5+
sudo apt install qt6-base-dev qt6-tools-dev qt6-multimedia-dev
# 或从 Qt 官网下载 Qt 6.5+ 安装包

# 安装 MySQL 客户端库
sudo apt install libmysqlclient-dev

# 安装 CMake
sudo apt install cmake ninja-build
```

### 编译步骤
```bash
cd /path/to/QtSmartCabinet
mkdir -p build && cd build
cmake -G Ninja -DCMAKE_PREFIX_PATH=/opt/Qt/6.5.3/gcc_64 ..
ninja
```

### 运行
```bash
./QtSmartCabinet
```

---

## 项目结构

```
QtSmartCabinet/
├── CMakeLists.txt          # CMake 构建文件（推荐）
├── QtSmartCabinet.pro     # qmake 项目文件（备选）
├── resources/
│   ├── logo.png          # 公司 Logo
│   └── resources.qrc     # Qt 资源文件
├── src/
│   ├── main.cpp         # 程序入口
│   ├── MainWindow.cpp/h  # 主窗口
│   ├── components/       # 组件（摄像头、软键盘）
│   ├── pages/           # 页面（登录、管理等）
│   ├── services/        # 服务层（认证、借用等）
│   ├── db/              # 数据库层（DAO）
│   └── utils/           # 工具类
├── build2/              # Windows 构建目录（已配置）
└── README_QtCreator.md  # 本文件
```

---

## 注意事项

1. **Logo 图片**：已添加到 `resources.qrc`，跨平台自动加载
2. **摄像头**：
   - Windows: Media Foundation（自动）
   - Linux: Qt Multimedia（需安装 `qt6-multimedia-dev`）
3. **数据库**：MySQL 连接需要 `libmysqlclient-dev`（Linux）或 MySQL Connector/C++（Windows）
4. **构建目录**：建议使用 `build2`（已配置好），或新建 `build` 目录

---

## 作者

袁燕 - 高级软件全栈技术总监  
创建日期：2026-06-15
