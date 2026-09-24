#!/bin/bash
# ============================================================================
# 麒麟系统编译脚本
# 作者：袁燕
# 用途：在麒麟系统上编译Qt Widget智能工具柜程序
# ============================================================================

set -e  # 遇到错误立即退出

# ==================== 配置变量 ====================
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build-kylin"
INSTALL_DIR="${PROJECT_DIR}/deploy-kylin"

# Qt环境（根据实际安装路径调整）
export QT_DIR="/opt/Qt/5.15.2/gcc_64"  # 麒麟系统Qt安装路径
export PATH="${QT_DIR}/bin:${PATH}"
export LD_LIBRARY_PATH="${QT_DIR}/lib:${LD_LIBRARY_PATH}"

# ==================== 显示环境信息 ====================
echo "=========================================="
echo "麒麟系统编译脚本"
echo "=========================================="
echo "项目目录: ${PROJECT_DIR}"
echo "构建目录: ${BUILD_DIR}"
echo "安装目录: ${INSTALL_DIR}"
echo "Qt目录: ${QT_DIR}"
echo "=========================================="

# ==================== 检查依赖 ====================
echo "检查依赖..."

# 检查Qt
if [ ! -d "${QT_DIR}" ]; then
    echo "错误：Qt目录不存在: ${QT_DIR}"
    echo "请修改脚本中的QT_DIR变量"
    exit 1
fi

# 检查CMake
if ! command -v cmake &> /dev/null; then
    echo "错误：CMake未安装"
    echo "请运行: sudo apt-get install cmake"
    exit 1
fi

# 检查MySQL客户端库
if ! ldconfig -p | grep -q "libmysqlclient"; then
    echo "警告：MySQL客户端库未找到"
    echo "尝试安装: sudo apt-get install libmysqlclient-dev"
    read -p "是否继续编译？(y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

echo "依赖检查通过"
echo

# ==================== 清理构建目录 ====================
echo "清理构建目录..."
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# ==================== 配置项目 ====================
echo "配置项目（CMake）..."
cmake "${PROJECT_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
    -DCMAKE_PREFIX_PATH="${QT_DIR}" \
    -G "Unix Makefiles"

if [ $? -ne 0 ]; then
    echo "错误：CMake配置失败"
    exit 1
fi

echo "CMake配置完成"
echo

# ==================== 编译项目 ====================
echo "开始编译..."
make -j$(nproc)

if [ $? -ne 0 ]; then
    echo "错误：编译失败"
    exit 1
fi

echo "编译完成"
echo

# ==================== 安装项目 ====================
echo "安装项目..."
make install

if [ $? -ne 0 ]; then
    echo "错误：安装失败"
    exit 1
fi

echo "安装完成"
echo

# ==================== 人脸识别服务（face-server.js + 模型 + 依赖）====================
# [2026-09-24 袁燕] 主程序启动时自动拉起该服务，部署目录必须自带，
#   否则柜机上会复现"人脸录入/刷脸登录全部不可用"；脚本探测路径为 exe目录/tools/face-recognition
FACE_SRC="${PROJECT_DIR}/tools/face-recognition"
FACE_DST="${INSTALL_DIR}/bin/tools/face-recognition"
if [ -d "${FACE_SRC}" ]; then
    mkdir -p "${FACE_DST}"
    cp -r "${FACE_SRC}/." "${FACE_DST}/"   # 幂等：重复打包不会嵌套出 tools/tools
    echo "已复制人脸识别服务目录 -> ${FACE_DST}"
else
    echo "警告：未找到 ${FACE_SRC}，部署后人脸识别功能将不可用"
fi
echo

# ==================== 部署依赖 ====================
echo "部署依赖库..."

# 创建部署目录结构
mkdir -p "${INSTALL_DIR}/lib"
mkdir -p "${INSTALL_DIR}/plugins/sqldrivers"
mkdir -p "${INSTALL_DIR}/plugins/platforms"

# 复制MySQL驱动
if [ -f "${QT_DIR}/plugins/sqldrivers/libqsqlmysql.so" ]; then
    cp "${QT_DIR}/plugins/sqldrivers/libqsqlmysql.so" "${INSTALL_DIR}/plugins/sqldrivers/"
    echo "已复制MySQL驱动"
else
    echo "警告：MySQL驱动未找到"
fi

# 复制平台插件
if [ -f "${QT_DIR}/plugins/platforms/libqxcb.so" ]; then
    cp "${QT_DIR}/plugins/platforms/libqxcb.so" "${INSTALL_DIR}/plugins/platforms/"
    echo "已复制XCB平台插件"
fi

# 使用linuxdeployqt打包依赖（如果可用）
if command -v linuxdeployqt &> /dev/null; then
    echo "使用linuxdeployqt打包依赖..."
    cd "${INSTALL_DIR}"
    linuxdeployqt "${INSTALL_DIR}/bin/QtSmartCabinet" -appimage
else
    echo "警告：linuxdeployqt未安装，跳过自动依赖打包"
    echo "请手动确保以下库可用："
    echo "  - Qt5核心库（Qt5Core, Qt5Gui, Qt5Widgets等）"
    echo "  - MySQL客户端库（libmysqlclient.so）"
    echo "  - XCB相关库（libxcb.so等）"
fi

echo

# ==================== 创建启动脚本 ====================
echo "创建启动脚本..."
cat > "${INSTALL_DIR}/start.sh" << 'EOF'
#!/bin/bash
# 启动脚本
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="${SCRIPT_DIR}/lib:${LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="${SCRIPT_DIR}/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="${SCRIPT_DIR}/plugins/platforms"
"${SCRIPT_DIR}/bin/QtSmartCabinet" "$@"
EOF

chmod +x "${INSTALL_DIR}/start.sh"
echo "启动脚本已创建: ${INSTALL_DIR}/start.sh"
echo

# ==================== 完成 ====================
echo "=========================================="
echo "编译完成！"
echo "=========================================="
echo "可执行文件: ${INSTALL_DIR}/bin/QtSmartCabinet"
echo "启动脚本: ${INSTALL_DIR}/start.sh"
echo "直接运行: ${INSTALL_DIR}/start.sh"
echo "=========================================="

exit 0
