@echo off
chcp 65001 >nul
echo ===================================
echo 快速编译测试
echo ===================================

REM 检查Qt路径
if exist "C:\Qt\6.5.3\msvc2019_64\bin\qmake.exe" (
    echo 找到Qt MSVC版本
    set QT_PATH=C:\Qt\6.5.3\msvc2019_64
    goto :try_msvc
)

if exist "C:\Qt\6.5.3\mingw_64\bin\qmake.exe" (
    echo 找到Qt MinGW版本
    set QT_PATH=C:\Qt\6.5.3\mingw_64
    goto :try_mingw
)

echo 未找到Qt安装，请先安装Qt 6.5.3
pause
exit /b 1

:try_msvc
echo.
echo 尝试MSVC编译...
cd /d "%~dp0\.."
if not exist build-msvc mkdir build-msvc
cd build-msvc
cmake -G "Visual Studio 17 2022" -A x64 ..
if %ERRORLEVEL% EQU 0 (
    cmake --build . --config Release
    if %ERRORLEVEL% EQU 0 (
        echo 编译成功！
        copy Release\QtSmartCabinet.exe ..\QtSmartCabinet.exe
        echo 可执行文件已复制到项目根目录
    )
) else (
    echo MSVC编译失败，尝试MinGW...
    goto :try_mingw
)
goto :end

:try_mingw
echo.
echo 尝试MinGW编译...
cd /d "%~dp0\.."
if not exist build-mingw mkdir build-mingw
cd build-mingw
cmake -G "MinGW Makefiles" ..
if %ERRORLEVEL% EQU 0 (
    cmake --build . --config Release
    if %ERRORLEVEL% EQU 0 (
        echo 编译成功！
        copy QtSmartCabinet.exe ..\QtSmartCabinet.exe
        echo 可执行文件已复制到项目根目录
    )
) else (
    echo MinGW编译失败
)
goto :end

:end
echo.
echo ===================================
echo 编译完成
echo ===================================
pause
