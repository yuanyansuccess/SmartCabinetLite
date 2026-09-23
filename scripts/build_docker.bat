@echo off
REM Docker编译脚本 - Windows版
REM 使用Docker容器编译Qt项目

echo 正在启动Docker编译...

REM 检查Docker是否运行
docker version >nul 2>&1
if errorlevel 1 (
    echo Docker未运行，请先启动Docker Desktop
    pause
    exit /b 1
)

REM 构建Docker镜像
echo 正在构建编译镜像...
docker build -t qtsmartcabinet-build -f Dockerfile .

REM 运行编译
echo 正在编译项目...
docker run --rm -v "%CD%:/project" qtsmartcabinet-build bash -c "cd /project && mkdir -p build && cd build && cmake .. && make -j4"

echo 编译完成！
pause
