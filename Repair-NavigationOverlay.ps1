$ErrorActionPreference = 'Stop'
$taskRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$taskGame = 'C:\Games\CubeWorld'
$taskTarget = Join-Path $taskGame 'cube_mod.dll'
$taskSource = Join-Path $taskRoot 'build\bin\cube_mod.dll'
$taskManifestPath = Join-Path $taskGame '.navigation-installed.json'
$taskManifest = Get-Content -LiteralPath $taskManifestPath -Raw | ConvertFrom-Json
if ($taskManifest.directory -ne $taskGame) { throw '安装记录的目标目录不匹配。' }
$taskOldHash = (Get-FileHash -LiteralPath $taskTarget -Algorithm SHA256).Hash
$taskExpected = @($taskManifest.installedFiles | Where-Object path -eq 'cube_mod.dll')
if ($taskExpected.Count -ne 1 -or $taskOldHash -ne $taskExpected[0].sha256) { throw '框架与安装记录不同，停止以保留未知改动。' }
foreach ($taskProcess in @(Get-CimInstance Win32_Process -Filter "Name='Cube.exe' OR Name='NavigationLauncher.exe' OR Name='Server.exe'")) {
    if (-not $taskProcess.ExecutablePath) { throw '无法确认正在运行的程序路径，停止更新。' }
    if ([System.IO.Path]::GetDirectoryName($taskProcess.ExecutablePath).Equals($taskGame,[System.StringComparison]::OrdinalIgnoreCase)) {
        throw '请先正常退出正式游戏和启动器，再更新绘制框架。'
    }
}
$taskPreserve = @((Join-Path $taskGame 'Cube.exe'),(Join-Path $taskGame 'options.cfg')) + @(Get-ChildItem -LiteralPath (Join-Path $taskGame 'Save') -File -Recurse | Select-Object -ExpandProperty FullName)
$taskBefore = @($taskPreserve | ForEach-Object { @{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash} })
$taskBackup = Join-Path $taskRoot ('backups\overlay-before-dpi-fix-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $taskBackup | Out-Null
Copy-Item -LiteralPath $taskTarget -Destination $taskBackup
Copy-Item -LiteralPath $taskManifestPath -Destination $taskBackup
Copy-Item -LiteralPath (Join-Path $taskGame 'cube_mod.log') -Destination $taskBackup
if ((Get-FileHash -LiteralPath (Join-Path $taskBackup 'cube_mod.dll') -Algorithm SHA256).Hash -ne $taskOldHash) { throw '框架备份校验失败。' }
Copy-Item -LiteralPath $taskSource -Destination $taskTarget
$taskNewHash = (Get-FileHash -LiteralPath $taskTarget -Algorithm SHA256).Hash
if ($taskNewHash -ne (Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash) { throw '新框架校验失败。' }
$taskRecord = @{updated=(Get-Date).ToString('o');reason='Use a hidden popup and explicit 64x64 back buffer for high-DPI D3D9 initialization';
    target=$taskTarget;backup=$taskBackup;beforeSha256=$taskOldHash;afterSha256=$taskNewHash;gameLaunched=$false;preserved=$taskBefore;
    regression=@{dpi=168;originalClient='212x0';originalHr='0x80070057';fixedClient='64x64';fixedHr='0x00000000'}}
$taskExpected[0].sha256 = $taskNewHash
$taskUpdates = @()
if ($taskManifest.PSObject.Properties.Name -contains 'overlayUpdates') { $taskUpdates = @($taskManifest.overlayUpdates) }
$taskManifest | Add-Member -NotePropertyName overlayUpdates -NotePropertyValue @($taskUpdates + $taskRecord) -Force
$taskManifest | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath $taskManifestPath -Encoding utf8
foreach ($taskFile in $taskBefore) {
    if ((Get-FileHash -LiteralPath $taskFile.path -Algorithm SHA256).Hash -ne $taskFile.sha256) { throw "不应改动的文件发生变化：$($taskFile.path)" }
}
$taskRecord | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $taskRoot 'research\evidence\overlay-dpi-repair.json') -Encoding utf8
Write-Output '导航绘制框架已更新，游戏程序、配置和全部存档散列保持不变。'
Write-Output "旧框架与故障日志备份：$taskBackup"
