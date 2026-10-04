<#
.SYNOPSIS
    Prune runtime files that are not needed by SmartCabinetLite deployment.
.DESCRIPTION
    Author: Yuan Yan
    Removes the following from the deployment directory:
      - opengl32sw.dll : SwiftShader software renderer (~19.7MB), unused on
                         cabinet machines that have GPU or system OpenGL.
      - qsqlite*.dll   : Qt SQLite driver. The project is MySQL-only
                         (QODBC), this plugin is never loaded.
      - vc_redist*.exe : VC runtime installer (24MB). Kept by default because
                         cabinet machines may not have the VC runtime
                         installed. Use -DropVcRedist to remove it when the
                         target machine is known to have it.
.PARAMETER TargetDir
    Deployment directory to prune. Defaults to build/Release.
.PARAMETER DropVcRedist
    Also remove the VC++ runtime installer. Only use when the target machine
    already has the VC++ redistributable installed.
#>
param(
    [string]$TargetDir = "D:\CFDZ\smartCabinet\trunk\SmartCabinetLite\build\Release",
    [switch]$DropVcRedist
)

$ErrorActionPreference = "Continue"

if (-not (Test-Path $TargetDir)) {
    Write-Host "[FAIL] Target directory not found: $TargetDir"
    exit 1
}

function Get-DirSizeMB {
    param([string]$Path)
    $sum = (Get-ChildItem -Recurse -File $Path -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
    return [math]::Round($sum / 1MB, 1)
}

$before = Get-DirSizeMB $TargetDir

$patterns = @("opengl32sw.dll", "qsqlite.dll", "qsqlited.dll")
if ($DropVcRedist) {
    $patterns += "vc_redist*.exe"
}
$removedCount = 0
$removedBytes = 0

foreach ($pattern in $patterns) {
    Get-ChildItem -Recurse -File -Filter $pattern -Path $TargetDir -ErrorAction SilentlyContinue | ForEach-Object {
        $removedBytes += $_.Length
        $removedCount++
        Write-Host ("  removed: " + $_.FullName.Replace($TargetDir, ""))
        Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue
    }
}

$after = Get-DirSizeMB $TargetDir

Write-Host ""
Write-Host ("[prune] removed files : " + $removedCount)
Write-Host ("[prune] freed         : " + [math]::Round($removedBytes / 1MB, 1) + " MB")
Write-Host ("[prune] dir size      : " + $before + " MB -> " + $after + " MB")
Write-Host "[done] runtime prune finished"
