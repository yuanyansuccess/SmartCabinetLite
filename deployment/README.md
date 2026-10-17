# 部署环境工具清单（deployment/）

本文件夹集中管理智能工具柜管理系统在**麒麟系统**上编译与部署所需的全部环境工具说明与脚本。
由于 Qhaiyt 安装包、MySQL 安装包等体积大且强依赖具体 CPU 架构（x86_64 / ARM64），无法全部塞进 Git，
因此这里以**“脚本 + 清单 + 获取地址”**的形式组织，现场实施时对照下载即可，不用每次从零回忆。

---

## 一、本文件夹内容

| 文件 | 作用 |
|------|------|
| `build_kylin.sh` | x86_64 架构编译脚本（Qt6.5.3 + CMake + MySQL 客户端） |
| `build-kylin-arm64.sh` | ARM64（飞腾/鲲鹏）架构编译脚本 |
| `README.md` | 本说明 |

> 脚本与 `scripts/` 目录保持一致，单独放一份便于整体打包离线交付。

---

## 二、环境工具清单（按架构各取所需）

### 公共依赖（两种架构都要）
| 工具 | 版本/要求 | 获取方式 |
|------|-----------|----------|
| GCC / G++ | 9.0+ | 系统源：`apt-get install build-essential` |
| CMake | 3.16+ | 系统源：`apt-get install cmake` |
| MySQL 客户端库 | `libmysqlclient` | 系统源：`apt-get install libmysqlclient-dev` |
| MySQL 服务端 | 8.0 / 5.7 | 系统源：`apt-get install mysql-server` |
| Node.js | 16+ | 系统源 `nodejs` 或官网 https://nodejs.org |
| 中文字体 | 文泉驿 / Noto CJK | 系统源：`apt-get install fonts-wqy-microhei fonts-noto-cjk` |
| XCB 系统库 | libxcb-* 一系列 | 见下方“必装系统库” |
| unixODBC 运行时 | `unixodbc` | 系统源：`apt-get install unixodbc unixodbc-dev`（QODBC 连库依赖） |
| MySQL ODBC 连接器 | `mysql-connector-odbc` | 系统源：`apt-get install mysql-connector-odbc`（提供 `MySQL ODBC 8.0 Unicode Driver`） |

### x86_64 专用
| 工具 | 版本/要求 | 获取方式 |
|------|-----------|----------|
| Qt 6.5.3 | gcc_64 | Qt 官方在线安装器 https://www.qt.io ，安装到 `/opt/Qt/6.5.3/gcc_64` |
| linuxdeployqt | 任意 | https://github.com/probonopd/linuxdeployqt |

### ARM64（飞腾/鲲鹏）专用
| 工具 | 版本/要求 | 获取方式 |
|------|-----------|----------|
| Qt 6.5.3 | gcc_arm64 | Qt 官方在线安装器，安装到 `/opt/Qt/6.5.3/gcc_arm64` |

---

## 三、必装系统库（运行时缺了程序起不来）

柜机/编译机上务必安装下面这些（已在主文档第 3.2 节列出，这里汇总）：
```bash
apt-get install -y libxcb-xinerama0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
    libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-sync1 libxcb-xfixes0 \
    libxcb-xkb1 libxkbcommon-x11-0 libxcb-cursor0
```

---

## 四、人脸识别服务的依赖（不要漏）

程序运行依赖 `tools/face-recognition/`（项目根目录下），里面包含：
- `face-server.js`：Node.js 常驻服务
- `node_modules/`：`@vladmandic/face-api`、`@tensorflow/tfjs` 等
- `models/`：tiny_face_detector 等模型文件

编译脚本会自动把它复制到部署包的 `bin/tools/face-recognition/`。**离线交付时这个目录必须一并带上**，否则刷脸登录、人脸录入不可用。

---

## 五、离线包制作步骤

1. 在同架构、能联网的机器上完成编译（见主文档第 5 节），得到 `deploy-kylin/` 或 `install-kylin-arm64/`。
2. 用 `apt-get download <包名>` 把上面的“必装系统库”和“中文字体”的 `.deb` 下载到本机，和编译产物放一起。
3. 打压缩包：
```bash
tar -czvf SmartCabinetLite_Offline.tar.gz deploy-kylin/ debs/ fonts/
```
4. 到目标柜机解压，按主文档第 6 节放置并 `./start.sh` 启动。

---

## 六、改了脚本后如何同步

若修改了 `scripts/` 下的编译脚本，请同步复制一份到本文件夹，保持两份一致：
```bash
cp scripts/build_kylin.sh deployment/
cp scripts/build-kylin-arm64.sh deployment/
```

## 七、ODBC 驱动注册（连 MySQL 前置，务必在柜机执行）

本程序通过 **QODBC + `MySQL ODBC 8.0 Unicode Driver`** 直连 MySQL（Qt6.5.3 已不含 QMYSQL）。
编译脚本只负责把 `libqsqlodbc.so` 打进安装包，**不会**在柜机注册 ODBC 驱动名，
因此目标柜机必须手动完成以下三步，否则启动即连不上库：

```bash
# 1) 安装 unixODBC 与 MySQL ODBC 连接器
sudo apt-get install unixodbc unixodbc-dev mysql-connector-odbc

# 2) 注册驱动（驱动名必须与代码 connection string 完全一致）
sudo myodbc-installer -d -a -n "MySQL ODBC 8.0 Unicode Driver" \
    -t "Driver=/usr/lib/x86_64-linux-gnu/odbc/libmyodbc8w.so"

# 3) 验证：列出已注册驱动，应能看到 "MySQL ODBC 8.0 Unicode Driver"
odbcinst -q -d
```

> ARM64 柜机步骤相同，仅第 2 步的 `.so` 路径改为对应架构（如 `/usr/lib/aarch64-linux-gnu/odbc/libmyodbc8w.so`）。

---

## 八、自动备份机制（系统设置-备份管理面板）

自动备份由程序内定时器驱动，**无需外部 cron**，但依赖柜机上的 `mysqldump` 命令：

1. **前置依赖**：柜机需安装 MySQL 客户端（提供 `mysqldump`）：
   ```bash
   sudo apt-get install mysql-client     # 或 mariadb-client（同样提供 mysqldump）
   ```
   若未安装，`startMysqlDumpBackup` 启动失败，自动备份会静默写入 `logs/backup.log`（`BACKUP FAIL cannot start mysqldump`），**不弹窗打扰**；手动备份则会弹错误提示。

2. **调度逻辑**：程序启动后启动一个**每小时轮询**的定时器，按以下规则判断今天是否应备份一次：
   - 周期=每日：今天尚未成功备份过即执行；
   - 周期=每周一：仅当今天是周一且本周一尚未备份时执行；
   - 周期=每周日：仅当今天是周日且本周日尚未备份时执行。
   判定依据为 INI 中 `Backup/last_backup_date`（格式 `yyyy-MM-dd`），备份成功后写入；**当天已备份则跳过，不会重复导出**。程序启动时也会立即检查一次，捕捉“当天是备份日但程序在备份时间点之后才开机”的场景。

3. **并发保护**：自动（定时器）与手动（按钮）/保存触发之间用 `m_backingUp` 标志互斥，避免同时启动多个 `mysqldump`。

4. **磁盘保护**：每次导出前若可用空间不足，`DiskSpaceGuard` 会静默删掉最旧的 `smartcabinet_backup_*.sql`，用户无感知。

5. **备份产物**：`<备份存储路径>/smartcabinet_backup_YYYYMMDD_HHMMSS.sql`（路径在面板设置，默认见 `SC::BACKUP_PATH`）。

> 注意：若柜机在“每周一/每周日”当天关机，则该周备份会跳过（程序仅在运行期间轮询）。如需严格按日历补备，可改用外部 cron 兜底。

作者：袁燕 ｜ 更新日期：2026-09-24
