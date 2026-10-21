@echo off
rem ============================================================
rem  EdgeSimulator one-click build (Release) + Qt runtime deploy
rem  Usage: build_release.bat          (interactive, pauses at end)
rem         build_release.bat nopause  (no pause, for automation)
rem  Why: plain "nmake release" only produces the exe; without
rem  windeployqt the exe cannot find Qt6Widgets.dll on a machine
rem  that has no Qt installed.
rem ============================================================
setlocal

set "VSDIR=D:\Program Files\Microsoft Visual Studio\2022\Community"
set "QTDIR=E:\Qt\6.5.3\msvc2019_64"

if not exist "%QTDIR%\bin\qmake.exe" (
    echo [ERROR] qmake not found: %QTDIR%\bin\qmake.exe
    goto :fail
)
if not exist "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" (
    echo [ERROR] vcvars64.bat not found under %VSDIR%
    goto :fail
)

rem vcvars first: it sets VCINSTALLDIR so windeployqt can copy the MSVC runtime,
rem and it keeps PATH from picking up an unrelated qmake (e.g. Anaconda Qt5).
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [ERROR] vcvars64.bat failed
    goto :fail
)

cd /d "%~dp0"

rem Close a running instance, otherwise link fails with LNK1168.
taskkill /F /IM EdgeSimulator.exe >nul 2>&1

echo [1/3] qmake EdgeSimulator.pro
"%QTDIR%\bin\qmake.exe" EdgeSimulator.pro
if errorlevel 1 goto :fail

echo [2/3] nmake release
nmake release
if errorlevel 1 goto :fail

echo [3/3] windeployqt (Qt dll + plugins + MSVC runtime)
"%QTDIR%\bin\windeployqt.exe" --release --no-system-d3d-compiler --compiler-runtime "release\EdgeSimulator.exe"
if errorlevel 1 goto :fail

rem windeployqt only drops vc_redist.x64.exe (an installer) when VCINSTALLDIR is set,
rem which still fails on a target machine without the VC runtime. Copy the CRT dlls
rem directly and drop the installer so the folder runs as-is.
if defined VCToolsRedistDir if exist "%VCToolsRedistDir%x64\Microsoft.VC143.CRT\*.dll" (
    copy /Y "%VCToolsRedistDir%x64\Microsoft.VC143.CRT\*.dll" "release\" >nul
)
if exist "release\vc_redist.x64.exe" del /q "release\vc_redist.x64.exe" >nul 2>&1

echo.
echo [OK] release\EdgeSimulator.exe is ready to run standalone.
if /i not "%1"=="nopause" pause
endlocal
exit /b 0

:fail
echo.
echo [FAILED] build or deploy step failed, see messages above.
if /i not "%1"=="nopause" pause
endlocal
exit /b 1
