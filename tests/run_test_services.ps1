# run_test_services.ps1 - Run Service layer tests against the MySQL test database
#
# Why this wrapper exists:
#   Qt loads SQL driver plugins (qsqlodbcd.dll / qsqlmysql.dll) at runtime from
#   a plugin search path. The test executable lives in build/tests/Debug, which
#   Qt does not scan by default, so QSqlDatabase reports "driver not available".
#   This wrapper sets QT_PLUGIN_PATH to the directory that has sqldrivers/,
#   which is verified to work.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tests\run_test_services.ps1
#   powershell -ExecutionPolicy Bypass -File tests\run_test_services.ps1 -DbPass "root"

param(
    [string]$DbHost = "127.0.0.1",
    [string]$DbPort = "3306",
    [string]$DbName = "smart_cabinet_test",
    [string]$DbUser = "root",
    [string]$DbPass = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot

# Safety guard: never point the tests at a non-test database.
if (-not ($DbName -match "_test$" -or $DbName -match "_unittest$")) {
    Write-Output "REFUSED: database name must end with _test or _unittest (got: $DbName)"
    exit 2
}

$pluginDir = Join-Path $projectRoot "build\Debug"
if (-not (Test-Path (Join-Path $pluginDir "sqldrivers"))) {
    Write-Output "WARNING: sqldrivers not found under $pluginDir"
    Write-Output "         Build the main target first (windeployqt deploys the plugins)."
}

if ($DbPass -eq "") {
    $secure = Read-Host "MySQL password for user '$DbUser'" -AsSecureString
    $DbPass = [Runtime.InteropServices.Marshal]::PtrToStringAuto(
        [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure))
}

$env:QT_PLUGIN_PATH = $pluginDir
$env:SC_TEST_DB_HOST = $DbHost
$env:SC_TEST_DB_PORT = $DbPort
$env:SC_TEST_DB_NAME = $DbName
$env:SC_TEST_DB_USER = $DbUser
$env:SC_TEST_DB_PASS = $DbPass

$exe = Join-Path $projectRoot "build\tests\Debug\test_services.exe"
if (-not (Test-Path $exe)) {
    Write-Output "Test executable not found: $exe"
    Write-Output "Build it first: cmake --build build --target test_services"
    exit 3
}

Write-Output "Running $exe against $DbName ..."
& $exe
Write-Output "exit code: $LASTEXITCODE"
