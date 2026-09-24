#!/bin/bash
# ============================================================================
# build-kylin-arm64.sh - 麒麟系统ARM64编译脚本
# 适配：银河麒麟V10/V11 ARM64 (飞腾/鲲鹏处理器)
# Qt版本：6.5.3
# 编译器：GCC 9+ ARM64交叉编译或原生编译
# 作者：袁燕  创建：2026-06-21
# ============================================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}智能柜Qt程序 - 麒麟ARM64编译脚本${NC}"
echo -e "${GREEN}========================================${NC}"

# ==================== 环境检测 ====================
echo -e "${YELLOW}[1/6] 检测编译环境...${NC}"

# 检测架构
ARCH=$(uname -m)
echo "当前架构: $ARCH"

# 检查Qt安装 (ARM64路径)
if [ -z "$QT_HOME" ]; then
    QT_CANDIDATES=(
        "/opt/Qt/6.5.3/gcc_arm64"
        "/usr/local/Qt/6.5.3/gcc_arm64"
        "$HOME/Qt/6.5.3/gcc_arm64"
        "/opt/Qt/6.5.3/gcc_64"          # 某些ARM64系统使用通用路径
    )
    
    for qt_path in "${QT_CANDIDATES[@]}"; do
        if [ -f "$qt_path/bin/qmake" ]; then
            QT_HOME="$qt_path"
            break
        fi
    done
fi

if [ -z "$QT_HOME" ]; then
    echo -e "${RED}错误：未找到Qt6 ARM64安装路径${NC}"
    echo "请设置环境变量：export QT_HOME=/path/to/qt6-arm64"
    exit 1
fi

echo "Qt路径: $QT_HOME"

# 检查CMake
if ! command -v cmake &> /dev/null; then
    echo -e "${RED}错误：未找到cmake${NC}"
    echo "请安装：sudo apt-get install cmake"
    exit 1
fi

# 检查MySQL客户端库
if ! ldconfig -p | grep -q "libmysqlclient"; then
    echo -e "${YELLOW}警告：未找到MySQL客户端库${NC}"
    echo "请安装：sudo apt-get install libmysqlclient-dev"
fi

# ==================== 配置环境变量 ====================
echo -e "${YELLOW}[2/6] 配置环境变量...${NC}"

export PATH="$QT_HOME/bin:$PATH"
export LD_LIBRARY_PATH="$QT_HOME/lib:$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$QT_HOME/plugins"

# ==================== 准备构建目录 ====================
echo -e "${YELLOW}[3/6] 准备构建目录...${NC}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build-kylin-arm64"
INSTALL_DIR="$PROJECT_DIR/install-kylin-arm64"

if [ -d "$BUILD_DIR" ]; then
    echo "清理旧的构建目录..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
mkdir -p "$INSTALL_DIR"

cd "$BUILD_DIR"

# ==================== CMake配置 ====================
echo -e "${YELLOW}[4/6] CMake配置...${NC}"

cmake "$PROJECT_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DCMAKE_PREFIX_PATH="$QT_HOME" \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_CXX_FLAGS="-march=armv8-a" \
    -DARM64_BUILD=ON

if [ $? -ne 0 ]; then
    echo -e "${RED}CMake配置失败${NC}"
    exit 1
fi

# ==================== 编译 ====================
echo -e "${YELLOW}[5/6] 编译项目...${NC}"

NPROC=$(nproc)
echo "使用 $NPROC 个核心并行编译"

make -j$NPROC

if [ $? -ne 0 ]; then
    echo -e "${RED}编译失败${NC}"
    exit 1
fi

# ==================== 安装 ====================
echo -e "${YELLOW}[6/6] 安装...${NC}"

make install

# ==================== 人脸识别服务（face-server.js + 模型 + 依赖）====================
# [2026-09-24 袁燕] 主程序启动时自动拉起该服务，部署目录必须自带，
#   否则柜机会复现"人脸录入/刷脸登录全部不可用"；脚本探测路径为 exe目录/tools/face-recognition
FACE_SRC="$PROJECT_DIR/tools/face-recognition"
FACE_DST="$INSTALL_DIR/bin/tools/face-recognition"
if [ -d "$FACE_SRC" ]; then
    mkdir -p "$FACE_DST"
    cp -r "$FACE_SRC/." "$FACE_DST/"   # 幂等：重复打包不会嵌套出 tools/tools
    echo -e "${GREEN}已复制人脸识别服务目录 -> $FACE_DST${NC}"
else
    echo -e "${YELLOW}警告：未找到 $FACE_SRC，部署后人脸识别功能将不可用${NC}"
fi

# ==================== 部署Qt运行时 ====================
echo -e "${YELLOW}[额外] 部署Qt运行时...${NC}"

# Linux ARM64 - 手动复制依赖
echo "复制Qt插件和库..."
mkdir -p "$INSTALL_DIR/plugins"
cp -r "$QT_HOME/plugins/platforms" "$INSTALL_DIR/plugins/"
cp -r "$QT_HOME/plugins/styles" "$INSTALL_DIR/plugins/" 2>/dev/null || true
cp -r "$QT_HOME/plugins/sqldrivers" "$INSTALL_DIR/plugins/" 2>/dev/null || true
cp -r "$QT_HOME/plugins/imageformats" "$INSTALL_DIR/plugins/" 2>/dev/null || true

# 复制Qt核心库
mkdir -p "$INSTALL_DIR/lib"
cp "$QT_HOME/lib/libQt6Core.so.6" "$INSTALL_DIR/lib/" 2>/dev/null || true
cp "$QT_HOME/lib/libQt6Widgets.so.6" "$INSTALL_DIR/lib/" 2>/dev/null || true
cp "$QT_HOME/lib/libQt6Sql.so.6" "$INSTALL_DIR/lib/" 2>/dev/null || true
cp "$QT_HOME/lib/libQt6Network.so.6" "$INSTALL_DIR/lib/" 2>/dev/null || true
cp "$QT_HOME/lib/libQt6Gui.so.6" "$INSTALL_DIR/lib/" 2>/dev/null || true

# ==================== 创建启动脚本 ====================
echo -e "${YELLOW}[额外] 创建启动脚本...${NC}"

cat > "$INSTALL_DIR/start.sh" << 'STARTEOF'
#!/bin/bash
# 智能柜系统 - 麒麟ARM64启动脚本
# 作者：袁燕

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$SCRIPT_DIR/lib:$SCRIPT_DIR/plugins/platforms:$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$SCRIPT_DIR/plugins"

# ARM64高分屏缩放适配（飞腾D2000/鲲鹏920常见分辨率为1920x1080或2560x1440）
export QT_AUTO_SCREEN_SCALE_FACTOR=1
export QT_SCALE_FACTOR=1
export QT_SCREEN_SCALE_FACTORS=1

# 麒麟系统中文字体适配
export QT_QPA_PLATFORM=xcb
export QT_IM_MODULE=fcitx

# 启动程序
"$SCRIPT_DIR/bin/QtSmartCabinet" "$@"
STARTEOF

chmod +x "$INSTALL_DIR/start.sh"

# ==================== 创建服务文件 ====================
cat > "$INSTALL_DIR/smartcabinet.service" << 'SERVICEEOF'
[Unit]
Description=智能工具柜系统
After=network.target mysql.service

[Service]
Type=simple
User=root
WorkingDirectory=/opt/smartcabinet
ExecStart=/opt/smartcabinet/start.sh
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
SERVICEEOF

# ==================== 完成 ====================
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}麒麟ARM64编译完成！${NC}"
echo -e "${GREEN}========================================${NC}"
echo "构建目录: $BUILD_DIR"
echo "安装目录: $INSTALL_DIR"
echo "启动命令: $INSTALL_DIR/start.sh"
echo ""
echo -e "${YELLOW}部署说明：${NC}"
echo "1. 将 install-kylin-arm64/ 整个目录复制到目标ARM64机器"
echo "2. 运行目标机器上的: sudo apt-get install libmysqlclient-dev libxcb-xinerama0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-sync1 libxcb-xfixes0 libxcb-xkb1 libxkbcommon-x11-0"
echo "3. 启动: ./start.sh"
echo ""
echo -e "${YELLOW}触摸屏适配（麒麟V10）：${NC}"
echo "- 如使用集成了华为/中兴触屏驱动的系统，Qt会自动识别"
echo "- 如使用USB触屏，需安装: sudo apt-get install libts-dev tslib"
echo "- 设置环境变量: export QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS=/dev/input/event0"
