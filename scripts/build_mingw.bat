@echo off
chcp 65001 >nul
echo ===================================
echo QtSmartCabinet MinGW 编译脚本
echo ===================================

REM 设置MinGW路径（请修改为实际路径）
set MINGW_PATH=C:\Qt\Tools\mingw1120_64\bin
set PATH=%MINGW_PATH%;%PATH%

REM 设置Qt路径
set QT_PATH=C:\Qt\6.5.3\mingw_64
set CMAKE_PREFIX_PATH=%QT_PATH%

echo.
echo 1. 检查编译环境...
where gcc
if %ERRORLEVEL% NEQ 0 (
    echo [错误] 未找到gcc编译器，请安装MinGW或修改脚本中的MINGW_PATH
    pause
    exit /b 1
)

echo.
echo 2. 创建build目录...
if not exist build-mingw mkdir build-mingw
cd build-mingw

echo.
echo 3. 运行CMake配置...
cmake -G "MinGW Makefiles" ^
    -DCMAKE_C_COMPILER=gcc ^
    -DCMAKE_CXX_COMPILER=g++ ^
    -DCMAKE_PREFIX_PATH=%QT_PATH% ^
    ..

if %ERRORLEVEL% NEQ 0 (
    echo [错误] CMake配置失败
    pause
    exit /b 1
)

echo.
echo 4. 开始编译...
cmake --build . --config Release

if %ERRORLEVEL% NEQ 0 (
    echo [错误] 编译失败
    pause
    exit /b 1
)

echo.
echo ===================================
echo 编译成功！
echo 可执行文件: build-mingw\QtSmartCabinet.exe
echo ===================================
pause
