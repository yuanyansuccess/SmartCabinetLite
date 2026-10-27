# Sync release/ to dist/ : produce a standalone runnable simulator folder.
# Called by build_dist.bat after build_release.bat finishes.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Definition
$src  = Join-Path $root 'release'
$dst  = Join-Path $root 'dist'
if (!(Test-Path $src)) { Write-Error "release dir missing, run build_release.bat first"; exit 1 }
if (!(Test-Path $dst)) { New-Item -ItemType Directory -Path $dst | Out-Null }

# Copy built binaries/plugins, drop intermediate & log artifacts.
robocopy $src $dst /E /XF *.obj *.ilk *.pdb *.lnk *.log moc_*.* EdgeSimulator_log*.txt vc_redist*.exe

# Copy the Chinese doc files from the project root into dist.
Get-ChildItem $root -File -Filter '*.txt' |
    Where-Object { $_.Name -notmatch 'EdgeSimulator_log' } |
    ForEach-Object { Copy-Item $_.FullName (Join-Path $dst $_.Name) -Force }

Write-Output "[OK] dist updated: $dst"
