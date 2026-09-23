@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d D:\CFDZ\smartCabinet\trunk\SmartCabinetLite
set CMAKE=D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
"%CMAKE%" -S . -B build -DCMAKE_PREFIX_PATH=E:\Qt\6.5.3\msvc2019_64 > build_cmake.log 2>&1
"%CMAKE%" --build build --config Release >> build_cmake.log 2>&1
echo BUILD_EXIT=%ERRORLEVEL% >> build_cmake.log
type build_cmake.log
