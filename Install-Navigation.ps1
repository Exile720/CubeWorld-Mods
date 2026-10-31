param([string]$GameDirectory = 'C:\Games\CubeWorld')
$ErrorActionPreference = 'Stop'
$taskRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$taskGame = (Resolve-Path -LiteralPath $GameDirectory).Path.TrimEnd('\')
if (-not $taskGame.Equals('C:\Games\CubeWorld', [System.StringComparison]::OrdinalIgnoreCase)) {
    throw '本安装脚本只用于用户指定的正式游戏目录。'
}
$taskExe = Join-Path $taskGame 'Cube.exe'
$taskExpectedHash = '84A7A132A84D4282338E7EA45A1940D32066D64E39CBAFED8F2418D5A6DC30BF'
if ((Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash -ne $taskExpectedHash) { throw '游戏版本与已测试版本不同，停止安装。' }
$taskProcesses = @(Get-CimInstance Win32_Process -Filter "Name='Cube.exe' OR Name='Server.exe'")
foreach ($taskProcess in $taskProcesses) {
    if (-not $taskProcess.ExecutablePath) { throw '无法确认正在运行的游戏路径，停止安装。' }
    if ([System.IO.Path]::GetDirectoryName($taskProcess.ExecutablePath).Equals($taskGame, [System.StringComparison]::OrdinalIgnoreCase)) { throw '正式游戏或服务器仍在运行，请正常退出后再安装。' }
}
$taskBin = Join-Path $taskRoot 'build\bin'
$taskCopies = @(
    @{source=(Join-Path $taskBin 'NavigationLauncher.exe'); relative='NavigationLauncher.exe'},
    @{source=(Join-Path $taskBin 'cube_mod.dll'); relative='cube_mod.dll'},
    @{source=(Join-Path $taskBin 'waypoint.dll'); relative='mods\waypoint.dll'},
    @{source=(Join-Path $taskRoot 'external\Qube-Loader\LICENSE'); relative='Qube-Loader-LICENSE.txt'}
)
$taskNewFiles = @($taskCopies | ForEach-Object { $_.relative }) + @('cube_mod.ini', '.navigation-installed.json', '导航启动.lnk', '导航模组使用说明.txt')
foreach ($taskCopy in $taskCopies) { if (-not (Test-Path -LiteralPath $taskCopy.source -PathType Leaf)) { throw "缺少构建文件：$($taskCopy.source)" } }
foreach ($taskRelative in $taskNewFiles) {
    if (Test-Path -LiteralPath (Join-Path $taskGame $taskRelative)) { throw "为保留已有文件，拒绝覆盖：$taskRelative" }
}

# The original game is stopped. Freeze Save and options before adding any mod files.
$taskBackup = Join-Path $taskRoot ('backups\formal-before-navigation-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $taskBackup | Out-Null
Copy-Item -LiteralPath (Join-Path $taskGame 'Save') -Destination (Join-Path $taskBackup 'Save') -Recurse
Copy-Item -LiteralPath (Join-Path $taskGame 'options.cfg') -Destination $taskBackup
$taskSaveHashes = @()
foreach ($taskFile in Get-ChildItem -LiteralPath (Join-Path $taskGame 'Save') -Recurse -File) {
    $taskRelative = [System.IO.Path]::GetRelativePath($taskGame, $taskFile.FullName)
    $taskSourceHash = (Get-FileHash -LiteralPath $taskFile.FullName -Algorithm SHA256).Hash
    if ((Get-FileHash -LiteralPath (Join-Path $taskBackup $taskRelative) -Algorithm SHA256).Hash -ne $taskSourceHash) { throw "备份校验失败：$taskRelative" }
    $taskSaveHashes += @{path=$taskRelative; bytes=$taskFile.Length; sha256=$taskSourceHash}
}
$taskOptionsHash = (Get-FileHash -LiteralPath (Join-Path $taskGame 'options.cfg') -Algorithm SHA256).Hash
if ((Get-FileHash -LiteralPath (Join-Path $taskBackup 'options.cfg') -Algorithm SHA256).Hash -ne $taskOptionsHash) { throw '画面配置备份校验失败。' }
$taskInstalled = @()
New-Item -ItemType Directory -Path (Join-Path $taskGame 'mods') -Force | Out-Null
foreach ($taskCopy in $taskCopies) {
    $taskDestination = Join-Path $taskGame $taskCopy.relative
    Copy-Item -LiteralPath $taskCopy.source -Destination $taskDestination
    $taskHash = (Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash
    if ($taskHash -ne (Get-FileHash -LiteralPath $taskCopy.source -Algorithm SHA256).Hash) { throw "安装文件校验失败：$($taskCopy.relative)" }
    $taskInstalled += @{path=$taskCopy.relative; sha256=$taskHash}
}
@'
log_level = info
console = 0
capture_game_log = 0
fault_isolation = 1
overlay = 1
allow_build_mismatch = 0
'@ | Set-Content -LiteralPath (Join-Path $taskGame 'cube_mod.ini') -Encoding ascii
$taskShell = New-Object -ComObject WScript.Shell
$taskLink = $taskShell.CreateShortcut((Join-Path $taskGame '导航启动.lnk'))
$taskLink.TargetPath = Join-Path $taskGame 'NavigationLauncher.exe'
$taskLink.WorkingDirectory = $taskGame
$taskLink.Description = '使用原角色和世界启动魔方世界，加载地图标点导航'
$taskLink.Save()
@"
地图标点导航 0.2.0

先正常退出正在运行的游戏，再双击本目录的“导航启动”。
如果游戏要求管理员权限，启动器会请求 Windows 授权，请由你本人确认；取消授权则不启动。
这是你原来正式玩的游戏目录，使用原来的角色和世界。
按 M 打开地图，右键标记目的地；关闭地图后看顶部方向和距离。
地图中按 Delete 取消标点。实验副本中的测试标点不会复制到这里。

原来的启动方式保留，使用原入口可以继续不带导航模组游玩。
游戏程序、options.cfg、高分屏兼容设置和存档未被安装脚本修改。
模组在游戏运行时读取位置、绘制导航；标点单独存于 data\waypoint。
原存档和配置备份：$taskBackup
"@ | Set-Content -LiteralPath (Join-Path $taskGame '导航模组使用说明.txt') -Encoding utf8
$taskRecord = @{version='0.2.0'; installed=(Get-Date).ToString('o'); directory=$taskGame; backup=$taskBackup;
    gameSha256=$taskExpectedHash; optionsSha256=$taskOptionsHash; saves=$taskSaveHashes; installedFiles=$taskInstalled;
    addedFiles=$taskNewFiles; originalLauncherRetained=$true; gameLaunched=$false; displayCompatibilityChanged=$false}
$taskRecord | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $taskGame '.navigation-installed.json') -Encoding utf8
# Check that installation itself has not altered executable, options or saves.
if ((Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash -ne $taskExpectedHash) { throw '游戏程序发生变化。' }
if ((Get-FileHash -LiteralPath (Join-Path $taskGame 'options.cfg') -Algorithm SHA256).Hash -ne $taskOptionsHash) { throw '画面配置发生变化。' }
foreach ($taskSave in $taskSaveHashes) {
    if ((Get-FileHash -LiteralPath (Join-Path $taskGame $taskSave.path) -Algorithm SHA256).Hash -ne $taskSave.sha256) { throw "存档发生变化：$($taskSave.path)" }
}
$taskRecord | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $taskRoot 'research\evidence\formal-navigation-install.json') -Encoding utf8
Write-Output "已安装到：$taskGame"
Write-Output "存档与配置备份：$taskBackup"
Write-Output '未启动正式游戏。请使用该目录中的“导航启动”。'
