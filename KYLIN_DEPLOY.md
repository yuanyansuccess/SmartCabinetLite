# QtSmartCabinet 麒麟系统部署指南

## 系统要求

### 硬件要求
- CPU: x86_64 或 ARM64 (飞腾/鲲鹏)
- 内存: 4GB 以上
- 硬盘: 10GB 可用空间
- 显示器: 支持1024x768以上分辨率

### 软件要求
- 操作系统: 银河麒麟 V10 SP1 或更高版本
- Qt版本: 6.5.3 或更高版本
- 编译器: GCC 9+ 或 Clang 10+
- 数据库: MySQL 8.0+ (可选，支持离线SQLite)

## 编译步骤

### 1. 安装依赖

#### x86_64架构:
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake qt6-base-dev qt6-multimedia-dev \
    libqt6serialport6-dev qt6-l10n-tools libmysqlclient-dev
```

#### ARM64架构 (飞腾/鲲鹏):
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake qt6-base-dev qt6-multimedia-dev \
    libqt6serialport6-dev qt6-l10n-tools libmysqlclient-dev
```

### 2. 编译项目

使用提供的编译脚本:
```bash
cd QtSmartCabinet
chmod +x scripts/build_kylin.sh
./scripts/build_kylin.sh
```

或手动编译:
```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/opt/Qt/6.5.3/gcc_64 -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 3. 部署

编译成功后，使用部署脚本:
```bash
cd build
sudo ../scripts/deploy_kylin.sh
```

或手动部署:
```bash
sudo mkdir -p /opt/QtSmartCabinet
sudo cp QtSmartCabinet /opt/QtSmartCabinet/
sudo chmod +x /opt/QtSmartCabinet/QtSmartCabinet

# 创建桌面快捷方式
sudo tee /usr/share/applications/QtSmartCabinet.desktop > /dev/null << EOF
[Desktop Entry]
Name=智能工具柜管理系统
Exec=/opt/QtSmartCabinet/QtSmartCabinet
Icon=/opt/QtSmartCabinet/app_icon.png
Terminal=false
Type=Application
Categories=Utility;
EOF
```

## 高分屏适配

系统已自动适配高分屏DPI缩放:
- 自动检测屏幕DPI
- 启用Qt高DPI缩放
- 支持1.0x到2.5x缩放范围

如需手动设置缩放比例:
```bash
# 在终端中设置环境变量
export QT_SCALE_FACTOR=1.5
/opt/QtSmartCabinet/QtSmartCabinet
```

## 中文字体支持

系统自动检测并优先使用以下中文字体:
1. 文泉驿微米黑 (WenQuanYi Micro Hei)
2. Noto Sans CJK SC
3. Droid Sans Fallback
4. AR PL UMing/UKai

如需安装额外字体:
```bash
# 安装文泉驿字体
sudo apt-get install -y fonts-wqy-microhei fonts-wqy-zenhei

# 安装Noto Sans CJK
sudo apt-get install -y fonts-noto-cjk
```

## 外设支持

### 摄像头
- 自动检测系统摄像头
- 使用Qt Multimedia框架
- 支持USB摄像头和内置摄像头

### RFID/USB读卡器
- 使用QSerialPort通信
- 支持常见RFID读写器
- 波特率: 9600-115200可配置

### 串口智能柜
- 支持RS232/RS485
- 使用QSerialPort
- 可配置串口号和参数

## 常见问题

### 1. 编译时找不到Qt6
**解决方案**:
```bash
# 检查Qt6是否安装
dpkg -l | grep qt6-base-dev

# 如果未安装，安装Qt6
sudo apt-get install -y qt6-base-dev
```

### 2. 运行时提示缺少库
**解决方案**:
```bash
# 检查缺少的库
ldd /opt/QtSmartCabinet/QtSmartCabinet

# 安装缺少的库
sudo apt-get install -y libqt6multimedia6 libqt6serialport6
```

### 3. 摄像头无法使用
**解决方案**:
```bash
# 检查摄像头设备
ls -l /dev/video*

# 检查用户权限
sudo usermod -a -G video $USER
# 注销后重新登录

# 测试摄像头
ffplay /dev/video0
```

### 4. 中文显示乱码
**解决方案**:
```bash
# 安装中文字体
sudo apt-get install -y fonts-wqy-microhei fonts-noto-cjk

# 设置中文环境
sudo dpkg-reconfigure locales
# 选择 zh_CN.UTF-8
```

### 5. 数据库连接失败
**解决方案**:
1. 检查MySQL服务是否运行: `sudo systemctl status mysql`
2. 检查配置文件: `/opt/QtSmartCabinet/config.ini`
3. 检查网络连接和防火墙设置

## 卸载

```bash
sudo rm -rf /opt/QtSmartCabinet
sudo rm /usr/share/applications/QtSmartCabinet.desktop
```

## 技术支持

如有问题，请联系:
- 技术支持: support@smartcabinet.com
- 作者: 袁燕
- 版本: V1.0.0
- 更新日期: 2026-06-20

## 附录: 离线部署包制作

### 制作离线包
```bash
# 在已编译的机器上
cd QtSmartCabinet/build
mkdir QtSmartCabinet_Offline
cp QtSmartCabinet QtSmartCabinet_Offline/
cp -r ../resources QtSmartCabinet_Offline/
cp ../scripts/deploy_kylin.sh QtSmartCabinet_Offline/
chmod +x QtSmartCabinet_Offline/deploy_kylin.sh

# 打包
tar -czvf QtSmartCabinet_Kylin_Offline.tar.gz QtSmartCabinet_Offline/
```

### 在目标机器上部署
```bash
# 解压
tar -xzvf QtSmartCabinet_Kylin_Offline.tar.gz
cd QtSmartCabinet_Offline

# 运行部署脚本
sudo ./deploy_kylin.sh
```
