@echo off
rem ============================================================
rem  One-stop build for EdgeSimulator:
rem   1) build_release.bat  -> qmake + nmake release + windeployqt
rem   2) sync_dist.ps1      -> mirror release\ into dist\ (standalone folder)
rem  Run this after ANY change to EdgeSimulator source so the
rem  distributable folder (dist\) is always regenerated.
rem ============================================================
setlocal
call "%~dp0build_release.bat" nopause
if errorlevel 1 (
    echo [FAILED] build_release.bat failed, dist NOT updated.
    pause
    exit /b 1
)
powershell -ExecutionPolicy Bypass -File "%~dp0sync_dist.ps1"
echo.
echo [DONE] EdgeSimulator dist folder regenerated.
endlocal
