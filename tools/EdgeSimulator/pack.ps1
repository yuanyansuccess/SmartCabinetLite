# 打包发布 zip + 生成桌面中文名手册（由 build_and_pack.bat 调用）
$desk = "E:\360MoveData\Users\25007\Desktop"
$src = Join-Path $env:TEMP "sim_pub"
# 1) 桌面中文名 docx（便于直接发测试）
Copy-Item (Join-Path $desk "EdgeSimulator_manual.docx") (Join-Path $desk "边缘计算机模拟器使用手册.docx") -Force
# 2) 发布 zip
$zip = Join-Path $desk "EdgeSimulator_边缘机模拟器.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $src "*") -DestinationPath $zip -Force
$item = Get-Item $zip
$mb = [math]::Round($item.Length / 1MB, 2)
Write-Output ($item.Name + " 打包完成 大小 " + $mb + " MB")
