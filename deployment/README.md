# 部署环境工具清单（deployment/）

本文件夹集中管理智能工具柜管理系统在**麒麟系统**上编译与部署所需的全部环境工具说明与脚本。
由于 Qt 安装包、MySQL 安装包等体积大且强依赖具体 CPU 架构（x86_64 / ARM64），无法全部塞进 Git，
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

作者：袁燕 ｜ 更新日期：2026-09-24
