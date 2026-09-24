# 智能工具柜管理系统 — 编译与部署指南（小白版）

> 适用对象：第一次在麒麟系统上编译、部署本软件的人员。
> 适用系统：银河麒麟 V10 SP1 及以上（x86_64 或 ARM64 飞腾/鲲鹏）。
> 软件版本：QtSmartCabinet V2.00（Qt 6.5.3 + MySQL）。
> 作者：袁燕

---

## 0. 先看懂整体流程（两步走）

本软件要在柜子上跑起来，分两件事：

1. **编译**：在一台“开发机/编译机”（也是麒麟系统）上，把源代码变成可执行程序 `QtSmartCabinet`。
2. **部署**：把编译好的程序连同依赖，复制到真正放在现场的那台“柜机”上运行。

本指南就是手把手教你怎么把这两步走完。即使你不太懂 Linux，只要照着命令一行行复制执行即可。

**重要事实（先看，避免踩坑）**：
- 本软件**只使用 MySQL 数据库**，没有 SQLite，也没有“离线 SQLite”模式。必须有一台 MySQL 数据库（可以是柜机本机装的，也可以是局域网内的一台服务器）。
- 程序**第一次启动会自动建表并写入初始数据**（默认管理员账号：`CF001` / 密码 `123456`），所以你**不需要**手动建库建表。
- 人脸识别功能依赖一个 Node.js 写的小服务（`tools/face-recognition`）。部署时这个目录**必须**跟着程序一起拷过去，否则刷脸登录和人脸录入都用不了。
- 数据库默认连接参数（在 `src/common/Constants.h` 里）：主机 `127.0.0.1`、端口 `3306`、库名 `smart_cabinet`、用户 `root`、密码 `root`。如果你的 MySQL 不是这个，请改 `Constants.h` 后重新编译，或在程序“系统设置”里改。

---

## 1. 准备清单（出发前核对）

| 项目 | 用途 | 版本/要求 | 怎么获取 |
|------|------|-----------|----------|
| 麒麟系统 | 运行/编译环境 | 银河麒麟 V10 SP1+ | 系统自带 |
| Qt 6.5.3 | 界面库 + 编译依赖 | 6.5.3（桌面 gcc_64 或 gcc_arm64） | 官方在线/离线安装器，装到 `/opt/Qt/6.5.3/...` |
| GCC / G++ | C++ 编译器 | 9.0 以上 | 系统源 `build-essential` |
| CMake | 构建工具 | 3.16 以上 | 系统源 `cmake` |
| MySQL 客户端库 | 程序连数据库用 | `libmysqlclient` | 系统源 `libmysqlclient-dev` |
| MySQL 服务端 | 真正存数据的库 | 8.0（或 5.7） | 系统源 `mysql-server` |
| Node.js | 人脸识别服务运行环境 | 16 以上 | 官网或系统源 `nodejs` |
| linuxdeployqt | 自动打包 Qt 依赖（可选） | 任意 | 官网下载 |

> 提示：ARM64（飞腾/鲲鹏）机器上，Qt 要选 **gcc_arm64** 版本；x86_64 机器选 **gcc_64** 版本。两者编译脚本不同，见第 5 节。

---

## 2. 麒麟系统基础准备（一步步）

### 2.1 确认你的系统架构
打开终端，执行：
```bash
uname -m
```
- 显示 `x86_64` → 你是 x86 机器，用第 5.2 节的脚本。
- 显示 `aarch64` → 你是 ARM64（飞腾/鲲鹏）机器，用第 5.3 节的脚本。

### 2.2 拿到管理员权限（root）
本指南很多命令需要管理员权限。最简单的方式是终端里执行：
```bash
sudo -i
```
输入你的登录密码后，提示符变成 `#`，表示已是 root，后续命令不用再加 `sudo`。

### 2.3 更新软件源（第一次必做）
```bash
apt-get update
```

---

## 3. 安装编译依赖

### 3.1 一键安装（最简单，推荐小白）
```bash
apt-get install -y build-essential cmake \
    qt6-base-dev qt6-multimedia-dev libqt6serialport6-dev qt6-l10n-tools \
    libmysqlclient-dev mysql-server nodejs
```
这条命令一次性装好：编译器、CMake、Qt6 开发包、MySQL 客户端库、MySQL 服务端、Node.js。

> 如果你是用**官方 Qt 离线安装器**单独装的 Qt（而不是用上面 `qt6-base-dev`），请记住你安装到的目录，例如 `/opt/Qt/6.5.3/gcc_64`，后面编译脚本要填这个路径。

### 3.2 安装现场运行所需的系统库（很关键，漏了程序起不来）
柜机上（或编译机上）还需要这些 XCB 相关库，否则程序运行时报“找不到平台插件”：
```bash
apt-get install -y libxcb-xinerama0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
    libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-sync1 libxcb-xfixes0 \
    libxcb-xkb1 libxkbcommon-x11-0 libxcb-cursor0
```

### 3.3 中文字体（避免界面中文变方块）
```bash
apt-get install -y fonts-wqy-microhei fonts-wqy-zenhei fonts-noto-cjk
```

---

## 4. 获取源代码

方式一：用 Git 拉取（推荐，便于后续更新）
```bash
git clone git@github.com:yuanyansuccess/SmartCabinetLite.git
cd SmartCabinetLite
```

方式二：直接把整个源码文件夹 `SmartCabinetLite` 拷贝到机器上。

---

## 5. 编译（用脚本最省心）

项目里已经准备好了两个编译脚本，分别在 `scripts/` 目录：
- `scripts/build_kylin.sh` —— x86_64 机器用
- `scripts/build-kylin-arm64.sh` —— ARM64 机器用

### 5.1 给脚本加执行权限
```bash
cd SmartCabinetLite
chmod +x scripts/build_kylin.sh          # x86 用这条
# chmod +x scripts/build-kylin-arm64.sh  # ARM 用这条
```

### 5.2 x86_64 编译
如果你的 Qt 不是装在 `/opt/Qt/6.5.3/gcc_64`，请**先用编辑器打开 `scripts/build_kylin.sh`**，把第 16 行左右的
```bash
export QT_DIR="/opt/Qt/6.5.3/gcc_64"
```
改成你实际的 Qt 路径，然后执行：
```bash
./scripts/build_kylin.sh
```
脚本会自动：检查依赖 → 清理旧构建 → CMake 配置 → 编译 → 安装到 `deploy-kylin/` → 复制人脸识别服务与人脸识别依赖 → 生成 `start.sh` 启动脚本。

### 5.3 ARM64 编译
ARM64 脚本会自动探测 Qt 路径（按 `/opt/Qt/6.5.3/gcc_arm64` 等常见位置）。如果探测不到，先设置环境变量：
```bash
export QT_HOME=/你的/qt6/arm64路径
./scripts/build-kylin-arm64.sh
```
编译产物在 `install-kylin-arm64/`，同样自带 `start.sh` 和 `smartcabinet.service`。

### 5.4 不想用脚本？手动编译也行
```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/opt/Qt/6.5.3/gcc_64 -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 5.5 编译常见报错排查
- **“Qt6 not found”**：Qt 路径没配对。检查 `QT_DIR` / `QT_HOME` 是否指向带 `lib/cmake/Qt6` 的目录。
- **“libmysqlclient not found”**：没装 3.1 里的 `libmysqlclient-dev`，重装即可。
- **CMake 报错 C++ 标准**：确保 GCC ≥ 9。用 `gcc --version` 查看。

---

## 6. 部署到现场柜机

### 6.1 部署包里有什么
无论是 `deploy-kylin/` 还是 `install-kylin-arm64/`，里面都包含：
- `bin/QtSmartCabinet`：主程序
- `bin/tools/face-recognition/`：人脸识别服务（Node.js + 模型，必须保留）
- `plugins/`、`lib/`：Qt 运行库与数据库驱动
- `start.sh`：一键启动脚本

把**整个目录**拷贝到柜机，例如 `/opt/smartcabinet/`。

### 6.2 在柜机上准备 MySQL 数据库
程序首次启动会自动建表，但你得先有一个能连的 MySQL 服务：
```bash
# 如果柜机没装 MySQL 服务端，先装（见 3.1）
systemctl start mysql
systemctl enable mysql

# 登录 MySQL，创建库（程序会自动建表，这里只需建空库）
mysql -u root -p
CREATE DATABASE IF NOT EXISTS smart_cabinet DEFAULT CHARSET utf8mb4;
```
- 若你的 MySQL 密码不是 `root`，请改 `src/common/Constants.h` 的 `DB_PASS` 后**重新编译**；或在程序“系统设置”里修改连接参数。
- 还原历史数据请看第 7 节。

### 6.3 启动程序
```bash
cd /opt/smartcabinet
./start.sh
```
首次启动会看到日志里出现 “Schema initialized ... Default admin: CF001 / 123456”，说明数据库已自动初始化完成。

### 6.4 设置开机自启（可选）
把 `install-kylin-arm64/smartcabinet.service` 复制到 `/etc/systemd/system/`，然后：
```bash
systemctl daemon-reload
systemctl enable smartcabinet.service
systemctl start smartcabinet.service
```

---

## 7. 数据库备份与恢复（运维必看）

### 7.1 备份（导出）
在**任何一台能连上 MySQL 的机器**上执行（以默认 `root/root` 为例）：
```bash
mysqldump -h 127.0.0.1 -P 3306 -u root -proot \
    --single-transaction --routines --events \
    smart_cabinet > smart_cabinet_$(date +%Y%m%d_%H%M%S).sql
```
- `-proot` 里没有空格，直接跟密码。
- 导出的 `.sql` 文件就是备份。建议统一放到项目里的 `db_backup/` 文件夹，方便管理。

### 7.2 恢复（导入）
```bash
mysql -u root -p smart_cabinet < smart_cabinet_20260924_142027.sql
```
> 注意：导入会**覆盖**现有数据，恢复前请确认已对当前库做好备份。

### 7.3 项目内的备份文件夹
本仓库根目录的 `db_backup/` 文件夹用于存放数据库备份文件。每次重要操作前后，建议照 7.1 导一份放进去。

---

## 8. 离线部署包制作（无外网环境）

现场柜机常常不能联网。按下面步骤做一个“拿走就能用”的离线包：

1. 在**能联网的那台同架构机器**上完成第 3~5 节（编译成功）。
2. 用 `deployment/` 文件夹里整理的清单，把下面这些东西一起打进一个压缩包：
   - 编译产物目录（`deploy-kylin/` 或 `install-kylin-arm64/`）
   - 第 3.2 节那一堆 `libxcb-*` 系统库（柜机若无网，需提前用 `apt-get download` 下载 `.deb` 带来）
   - 中文字体包 `.deb`（同上）
3. 打包：
```bash
tar -czvf SmartCabinetLite_Offline.tar.gz deploy-kylin/
```
4. 到柜机上解压、按第 6 节放到 `/opt/smartcabinet/` 并 `./start.sh`。

---

## 9. 常见问题 FAQ（小白向）

**Q1：编译时找不到 Qt6？**
A：确认 Qt 安装路径，并正确设置 `QT_DIR`（x86）或 `QT_HOME`（ARM）。可用 `ls /opt/Qt/6.5.3/gcc_64/lib/cmake/Qt6` 验证路径是否存在。

**Q2：程序一启动就退出，报“xcb”“platform plugin”之类？**
A：缺系统库，执行第 3.2 节那一长串 `apt-get install libxcb-*` 命令即可。

**Q3：摄像头打不开？**
A：检查设备 `ls -l /dev/video*`，把当前用户加入 `video` 组：`usermod -a -G video $USER`，注销重登。可用 `ffplay /dev/video0` 测试摄像头本身是否正常。

**Q4：界面中文显示成方块/乱码？**
A：执行第 3.3 节安装中文字体；并用 `dpkg-reconfigure locales` 选 `zh_CN.UTF-8`。

**Q5：提示连不上数据库？**
A：①`systemctl status mysql` 看 MySQL 起了没；②确认 `Constants.h` 里的 host/port/user/pass 与实际一致；③确认库 `smart_cabinet` 已创建。

**Q6：刷脸登录、人脸录入完全用不了？**
A：多半是 `bin/tools/face-recognition/` 没跟着拷过来，或柜机没装 Node.js。确认该目录存在且里面有 `face-server.js` 和 `node_modules` 与 `models`。

**Q7：触摸屏点了没反应？**
A：USB 触屏需 `apt-get install libts-dev tslib`，并设置 `export QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS=/dev/input/event0`（event 编号按实际改）。

---

## 10. 部署工具文件夹 `deployment/` 说明

为了方便管理，仓库里新建了 `deployment/` 文件夹，集中放“部署相关的一切”：
- `deployment/build_kylin.sh`、`deployment/build-kylin-arm64.sh`：编译脚本副本（与 `scripts/` 一致）。
- `deployment/README.md`：部署环境工具清单与获取方式、离线包制作步骤。

**把所有“环境工具”放这一个文件夹**的做法：由于 Qt 安装包、MySQL 安装包等体积大且依赖具体架构，无法都塞进 Git，因此 `deployment/` 以“清单 + 获取地址 + 脚本”的形式组织。现场实施时，对照 `deployment/README.md` 把对应安装包下载到本机即可，无需每次从零回忆要装什么。

---

## 附录 A：数据库默认参数（改前请三思）

| 参数 | 默认值 | 位置 |
|------|--------|------|
| 主机 | 127.0.0.1 | `src/common/Constants.h` `DB_HOST` |
| 端口 | 3306 | `DB_PORT` |
| 库名 | smart_cabinet | `DB_NAME` |
| 用户 | root | `DB_USER` |
| 密码 | root | `DB_PASS` |
| 默认管理员 | CF001 / 123456 | 程序首次启动自动播种 |

## 附录 B：技术支持
- 作者：袁燕
- 版本：V2.00
- 更新日期：2026-09-24
