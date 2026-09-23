@echo off
chcp 65001 >nul
echo ===================================
echo 下载Qt 6.5.3 MinGW版本
echo ===================================

REM Qt在线安装器下载链接（备用）
set QT_ONLINE=https://download.qt.io/official_releases/online_installers/qt-unified-windows-x64-4.6.1.exe
set QT_OFFLINE=https://download.qt.io/archive/qt/6.5/6.5.3/qt-opensource-windows-x64-6.5.3.exe

echo 请选择下载方式：
echo 1. 在线安装器（推荐，可选组件）
echo 2. 离线安装包（完整，约2GB）
echo 3. 仅MinGW组件（最小，约200MB）

set /p choice="输入选择(1-3): "

if "%choice%"=="1" (
    echo 下载在线安装器...
    start "" "%QT_ONLINE%"
) else if "%choice%"=="2" (
    echo 下载离线安装包...
    start "" "%QT_OFFLINE%"
) else if "%choice%"=="3" (
    echo 下载MinGW组件...
    echo 请从Qt官网手动下载：https://download.qt.io/archive/qt/6.5/6.5.3/
) else (
    echo 无效选择
)

echo.
echo 下载完成后，请安装到：C:\Qt\6.5.3\mingw_64
echo 然后重新运行编译脚本
pause
