$ErrorActionPreference = 'Stop'
$taskRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$taskGame = 'C:\Games\CubeWorld'
$taskTarget = Join-Path $taskGame 'NavigationLauncher.exe'
$taskSource = Join-Path $taskRoot 'build\bin\NavigationLauncher.exe'
$taskManifestPath = Join-Path $taskGame '.navigation-installed.json'
$taskManifest = Get-Content -LiteralPath $taskManifestPath -Raw | ConvertFrom-Json
if ($taskManifest.directory -ne $taskGame) { throw '安装记录的目标目录不匹配。' }
$taskOldHash = (Get-FileHash -LiteralPath $taskTarget -Algorithm SHA256).Hash
$taskExpected = @($taskManifest.installedFiles | Where-Object path -eq 'NavigationLauncher.exe')
if ($taskExpected.Count -ne 1 -or $taskOldHash -ne $taskExpected[0].sha256) { throw '启动器与安装记录不同，停止以保留未知改动。' }
foreach ($taskProcess in @(Get-CimInstance Win32_Process -Filter "Name='Cube.exe' OR Name='NavigationLauncher.exe' OR Name='Server.exe'")) {
    if (-not $taskProcess.ExecutablePath) { throw '无法确认正在运行的程序路径，停止更新。' }
    if ([System.IO.Path]::GetDirectoryName($taskProcess.ExecutablePath).Equals($taskGame,[System.StringComparison]::OrdinalIgnoreCase)) {
        throw '请先关闭正式目录中的游戏或启动失败提示框，再更新启动器。'
    }
}
$taskPreserve = @((Join-Path $taskGame 'Cube.exe'),(Join-Path $taskGame 'options.cfg')) + @(Get-ChildItem -LiteralPath (Join-Path $taskGame 'Save') -File -Recurse | Select-Object -ExpandProperty FullName)
$taskBefore = @($taskPreserve | ForEach-Object { @{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash} })
$taskBackup = Join-Path $taskRoot ('backups\launcher-before-740fix-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $taskBackup | Out-Null
Copy-Item -LiteralPath $taskTarget -Destination $taskBackup
Copy-Item -LiteralPath $taskManifestPath -Destination $taskBackup
if ((Get-FileHash -LiteralPath (Join-Path $taskBackup 'NavigationLauncher.exe') -Algorithm SHA256).Hash -ne $taskOldHash) { throw '启动器备份校验失败。' }
Copy-Item -LiteralPath $taskSource -Destination $taskTarget
$taskNewHash = (Get-FileHash -LiteralPath $taskTarget -Algorithm SHA256).Hash
if ($taskNewHash -ne (Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash) { throw '新启动器校验失败。' }
$taskRecord = @{updated=(Get-Date).ToString('o');reason='Handle ERROR_ELEVATION_REQUIRED (740) through normal UAC consent, once only';
    target=$taskTarget;backup=$taskBackup;beforeSha256=$taskOldHash;afterSha256=$taskNewHash;gameLaunched=$false;preserved=$taskBefore}
$taskExpected[0].sha256 = $taskNewHash
$taskUpdates = @()
if ($taskManifest.PSObject.Properties.Name -contains 'launcherUpdates') { $taskUpdates = @($taskManifest.launcherUpdates) }
$taskManifest | Add-Member -NotePropertyName launcherUpdates -NotePropertyValue @($taskUpdates + $taskRecord) -Force
$taskManifest | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath $taskManifestPath -Encoding utf8
foreach ($taskFile in $taskBefore) {
    if ((Get-FileHash -LiteralPath $taskFile.path -Algorithm SHA256).Hash -ne $taskFile.sha256) { throw "不应改动的文件发生变化：$($taskFile.path)" }
}
$taskRecord | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $taskRoot 'research\evidence\launcher-740-repair.json') -Encoding utf8
Write-Output '导航启动器已更新，游戏程序、配置和全部存档散列保持不变。'
Write-Output "旧启动器备份：$taskBackup"
