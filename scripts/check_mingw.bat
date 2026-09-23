@echo off
chcp 65001 >nul
echo ===================================
echo 检查MinGW环境
echo ===================================

REM 尝试常见MinGW路径
set MINGW_PATHS=C:\Qt\Tools\mingw1120_64\bin;C:\MinGW\bin;C:\Qt\5.15.2\mingw81_64\bin

for %%p in (%MINGW_PATHS%) do (
    if exist "%%p\gcc.exe" (
        echo [找到] MinGW: %%p
        set PATH=%%p;%PATH%
        goto :check_version
    )
)

echo [错误] 未找到MinGW
echo 请安装MinGW或修改此脚本
pause
exit /b 1

:check_version
echo.
echo GCC版本:
gcc --version | findstr /C:"gcc"
if %ERRORLEVEL% NEQ 0 (
    echo [错误] 无法执行gcc
    pause
    exit /b 1
)

echo.
echo CMake版本:
cmake --version | findstr /C:"cmake"
if %ERRORLEVEL% NEQ 0 (
    echo [错误] 无法执行cmake
    pause
    exit /b 1
)

echo.
echo ===================================
echo MinGW环境检查完成
echo ===================================
pause
