@echo off
chcp 65001 >nul
echo ===================================
echo 使用Docker编译QtSmartCabinet (Windows)
echo ===================================

REM 检查Docker
docker --version
if %ERRORLEVEL% NEQ 0 (
    echo [错误] Docker未安装或未启动
    echo 请先安装Docker Desktop
    pause
    exit /b 1
)

REM 构建Docker镜像
echo.
echo 1. 构建Docker镜像...
docker build -t qtsmartcabinet-build .
if %ERRORLEVEL% NEQ 0 (
    echo [错误] Docker镜像构建失败
    pause
    exit /b 1
)

REM 运行编译
echo.
echo 2. 运行编译...
docker run --rm -v "%cd%:/project" qtsmartcabinet-build
if %ERRORLEVEL% NEQ 0 (
    echo [错误] Docker编译失败
    pause
    exit /b 1
)

echo.
echo ===================================
echo Docker编译完成！
echo ===================================
pause
