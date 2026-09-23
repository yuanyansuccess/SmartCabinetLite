@echo off
chcp 65001 >nul
echo ===================================
echo QtSmartCabinet 编译测试
echo ===================================

REM 检查Qt安装
if exist "C:\Qt\6.5.3\msvc2019_64\bin\qmake.exe" (
    echo [找到] Qt MSVC版本
    set QT_PATH=C:\Qt\6.5.3\msvc2019_64
    goto :build_msvc
)

if exist "C:\Qt\6.5.3\mingw_64\bin\qmake.exe" (
    echo [找到] Qt MinGW版本
    set QT_PATH=C:\Qt\6.5.3\mingw_64
    goto :build_mingw
)

echo [错误] 未找到Qt 6.5.3安装
echo 请安装Qt 6.5.3到C:\Qt\6.5.3\
pause
exit /b 1

:build_msvc
echo.
echo 1. 使用MSVC编译器...
set PATH=%QT_PATH%\bin;%PATH%
cd /d "%~dp0\.."
if not exist build-msvc mkdir build-msvc
cd build-msvc
cmake -G "Visual Studio 17 2022" -A x64 ..
if %ERRORLEVEL% NEQ 0 (
    echo [错误] CMake配置失败
    pause
    exit /b 1
)
cmake --build . --config Release
goto :end

:build_mingw
echo.
echo 1. 使用MinGW编译器...
set PATH=%QT_PATH%\bin;%PATH%
cd /d "%~dp0\.."
if not exist build-mingw mkdir build-mingw
cd build-mingw
cmake -G "MinGW Makefiles" ..
if %ERRORLEVEL% NEQ 0 (
    echo [错误] CMake配置失败
    pause
    exit /b 1
)
cmake --build . --config Release
goto :end

:end
echo.
echo ===================================
echo 编译完成！
echo ===================================
pause
