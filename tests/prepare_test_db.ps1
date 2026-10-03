# prepare_test_db.ps1 - Create an isolated MySQL test database for test_services
#
# SAFETY CONTRACT (important):
#   This script ONLY executes: CREATE DATABASE IF NOT EXISTS smart_cabinet_test
#   It NEVER runs DROP / ALTER / TRUNCATE / DELETE against any database.
#   Production data is therefore untouched by design.
#
# Usage (run from project root):
#   powershell -ExecutionPolicy Bypass -File tests\prepare_test_db.ps1
#   powershell -ExecutionPolicy Bypass -File tests\prepare_test_db.ps1 -RootPass "your-mysql-root-password"

param(
    [string]$RootPass = "",
    [string]$TestDb   = "smart_cabinet_test",
    [string]$HostAddr = "127.0.0.1",
    [string]$Port     = "3306"
)

$ErrorActionPreference = "Stop"

# Guard: the target name must look like a test database
if (-not ($TestDb -match "_test$" -or $TestDb -match "_unittest$")) {
    Write-Output "REFUSED: test database name must end with _test or _unittest (got: $TestDb)"
    Write-Output "This guard exists to prevent accidentally targeting a production database."
    exit 2
}

if ($RootPass -eq "") {
    $secure = Read-Host "Enter MySQL root password" -AsSecureString
    $RootPass = [Runtime.InteropServices.Marshal]::PtrToStringAuto(
        [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure))
}

$mysql = "C:\Program Files\MySQL\MySQL Server 5.7\bin\mysql.exe"
if (-not (Test-Path $mysql)) {
    Write-Output "mysql.exe not found at $mysql"
    Write-Output "Please run this script with the correct client path, or put mysql.exe in PATH."
    exit 3
}

Write-Output "Listing existing databases (read-only)..."
$args = @("-h", $HostAddr, "-P", $Port, "-u", "root", "-p$RootPass",
          "-e", "SHOW DATABASES;")
& $mysql $args 2>&1 | ForEach-Object { Write-Output "  $_" }

Write-Output ""
Write-Output "Creating isolated test database: $TestDb"
$createArgs = @("-h", $HostAddr, "-P", $Port, "-u", "root", "-p$RootPass",
                "-e", "CREATE DATABASE IF NOT EXISTS $TestDb CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;")
& $mysql $createArgs 2>&1 | ForEach-Object { Write-Output "  $_" }

if ($LASTEXITCODE -ne 0) {
    Write-Output "FAILED: could not create test database (check credentials). Nothing was modified."
    exit 4
}

Write-Output ""
Write-Output "SUCCESS. Test database ready: $TestDb"
Write-Output ""
Write-Output "Next - export env vars and run the service tests:"
Write-Output "  set SC_TEST_DB_HOST=$HostAddr"
Write-Output "  set SC_TEST_DB_PORT=$Port"
Write-Output "  set SC_TEST_DB_NAME=$TestDb"
Write-Output "  set SC_TEST_DB_USER=root"
Write-Output "  set SC_TEST_DB_PASS=<same password used above>"
Write-Output "  build\tests\Debug\test_services.exe"
