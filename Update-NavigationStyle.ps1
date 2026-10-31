param([ValidateSet('Experiment','Formal')][string]$Target = 'Experiment')
$ErrorActionPreference = 'Stop'
$taskRoot = [System.IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$taskGame = if ($Target -eq 'Formal') { 'C:\Games\CubeWorld' } else { Join-Path $taskRoot 'playtest' }
$taskMarker = if ($Target -eq 'Formal') { '.navigation-installed.json' } else { '.navigation-sandbox.json' }
if (-not (Test-Path -LiteralPath (Join-Path $taskGame $taskMarker))) { throw '未找到已准备的导航游戏目录标记。' }
if ((Get-FileHash -LiteralPath (Join-Path $taskGame 'Cube.exe') -Algorithm SHA256).Hash -ne '84A7A132A84D4282338E7EA45A1940D32066D64E39CBAFED8F2418D5A6DC30BF') { throw '游戏版本与已测试版本不符。' }
foreach ($taskProcess in @(Get-CimInstance Win32_Process -Filter "Name='Cube.exe' OR Name='NavigationLauncher.exe' OR Name='Server.exe'")) {
    if (-not $taskProcess.ExecutablePath) { throw '无法确认运行中的游戏路径，停止更新。' }
    if ([System.IO.Path]::GetDirectoryName($taskProcess.ExecutablePath).Equals($taskGame,[System.StringComparison]::OrdinalIgnoreCase)) { throw "$Target 游戏仍在运行，请正常退出后再更新。" }
}
$taskCopies=@(@{name='cube_mod.dll';relative='cube_mod.dll'},@{name='waypoint.dll';relative='mods\waypoint.dll'})
$taskManifest=$null
if ($Target -eq 'Formal') {
    $taskManifest=Get-Content -LiteralPath (Join-Path $taskGame $taskMarker) -Raw | ConvertFrom-Json
    foreach ($taskCopy in $taskCopies) {
        $taskEntry=@($taskManifest.installedFiles | Where-Object path -eq $taskCopy.relative)
        if ($taskEntry.Count -ne 1 -or $taskEntry[0].sha256 -ne (Get-FileHash -LiteralPath (Join-Path $taskGame $taskCopy.relative) -Algorithm SHA256).Hash) { throw '正式模组文件有未知变动，停止覆盖。' }
    }
}
$taskPreserve=@((Join-Path $taskGame 'Cube.exe'),(Join-Path $taskGame 'options.cfg')) + @(Get-ChildItem -LiteralPath (Join-Path $taskGame 'Save') -Recurse -File | Select-Object -ExpandProperty FullName)
$taskBefore=@($taskPreserve | ForEach-Object { @{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash} })
$taskBackup=Join-Path $taskRoot ('backups\ui-0.2.0-'+$Target.ToLower()+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $taskBackup | Out-Null
Copy-Item -LiteralPath (Join-Path $taskGame $taskMarker) -Destination $taskBackup
if (Test-Path -LiteralPath (Join-Path $taskGame 'cube_mod.log')) { Copy-Item -LiteralPath (Join-Path $taskGame 'cube_mod.log') -Destination $taskBackup }
$taskUpdated=@()
foreach ($taskCopy in $taskCopies) {
    $taskSource=Join-Path $taskRoot ('build\bin\'+$taskCopy.name)
    $taskDestination=Join-Path $taskGame $taskCopy.relative
    Copy-Item -LiteralPath $taskDestination -Destination (Join-Path $taskBackup $taskCopy.name)
    $taskOldHash=(Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash
    if ((Get-FileHash -LiteralPath (Join-Path $taskBackup $taskCopy.name) -Algorithm SHA256).Hash -ne $taskOldHash) { throw '旧模组备份校验失败。' }
    Copy-Item -LiteralPath $taskSource -Destination $taskDestination
    $taskNewHash=(Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash
    if ($taskNewHash -ne (Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash) { throw '新模组复制校验失败。' }
    $taskUpdated+=@{path=$taskCopy.relative;before=$taskOldHash;after=$taskNewHash}
    if ($taskManifest) { ($taskManifest.installedFiles | Where-Object path -eq $taskCopy.relative).sha256=$taskNewHash }
}
$taskRecord=@{version='0.2.0';updated=(Get-Date).ToString('o');target=$taskGame;backup=$taskBackup;files=$taskUpdated;
    nativeGameFont='resource1.dat read in place';preserved=$taskBefore;gameLaunched=$false}
if ($taskManifest) {
    $taskManifest.version='0.2.0'
    $taskHistory=@()
    if ($taskManifest.PSObject.Properties.Name -contains 'styleUpdates') { $taskHistory=@($taskManifest.styleUpdates) }
    $taskManifest | Add-Member -NotePropertyName styleUpdates -NotePropertyValue @($taskHistory+$taskRecord) -Force
    $taskManifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $taskGame $taskMarker) -Encoding utf8
}
foreach ($taskFile in $taskBefore) {
    if ((Get-FileHash -LiteralPath $taskFile.path -Algorithm SHA256).Hash -ne $taskFile.sha256) { throw "游戏、配置或存档发生变化：$($taskFile.path)" }
}
$taskRecord | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $taskRoot ('research\evidence\ui-0.2.0-'+$Target.ToLower()+'.json')) -Encoding utf8
Write-Output "已更新 $Target 导航界面到 0.2.0，原游戏、画面配置和存档散列保持不变。"
Write-Output "旧模组备份：$taskBackup"
