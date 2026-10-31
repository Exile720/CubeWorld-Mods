param(
    [ValidateSet('Experiment','Formal')][string]$Target='Experiment',
    [switch]$ValidateOnly
)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$taskPackage=Join-Path $taskRoot 'releases\batch-actions-0.1.2'
$taskInfo=Get-Content -LiteralPath (Join-Path $taskPackage 'release.json') -Raw | ConvertFrom-Json
$taskSource=Join-Path $taskPackage 'batch_actions.dll'
if ($taskInfo.version -ne '0.1.2' -or (Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash -ne $taskInfo.batchActionsSha256) {throw '批量补给候选包校验失败。'}
$taskGame=if ($Target -eq 'Formal') {'C:\Games\CubeWorld'} else {Join-Path $taskRoot 'playtest'}
$taskMarker=if ($Target -eq 'Formal') {'.navigation-installed.json'} else {'.navigation-sandbox.json'}
function Assert-Plain([string]$Path) {
    $probe=[IO.Path]::GetFullPath($Path)
    while ($probe) {
        if ((Test-Path -LiteralPath $probe) -and ((Get-Item -LiteralPath $probe -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {throw "拒绝链接路径：$probe"}
        $parent=[IO.Path]::GetDirectoryName($probe.TrimEnd('\'))
        if (-not $parent -or $parent -eq $probe) {break}
        $probe=$parent
    }
}
function Assert-Stopped {
    $state=& (Join-Path $taskRoot 'mod_manager\Get-ModManagerState.ps1') -Target $Target | ConvertFrom-Json
    if (-not $state.processCheckOk) {throw $state.blockReason}
    if ($state.gameRunning) {throw '目标游戏仍在运行，请正常退出游戏和启动器。'}
    if (-not $state.canManage) {throw $state.blockReason}
    $own=@($state.mods | Where-Object id -eq 'BatchActions')
    if ($own.Count -ne 1 -or $null -eq $own[0].enabled) {throw '批量补给的配置状态无法核实，停止安装。'}
    return $state
}
foreach ($path in @($taskPackage,$taskSource,$taskGame,(Join-Path $taskGame 'Save'),(Join-Path $taskGame 'data'),(Join-Path $taskGame 'config'))) {Assert-Plain $path}
if ((Get-FileHash -LiteralPath (Join-Path $taskGame 'Cube.exe') -Algorithm SHA256).Hash -ne $taskInfo.gameSha256) {throw '游戏不是已核实的 2013 Alpha 版本。'}
if ((Get-FileHash -LiteralPath (Join-Path $taskGame 'cube_mod.dll') -Algorithm SHA256).Hash -ne $taskInfo.requiresFrameworkSha256) {throw '公共加载器版本不符，停止安装。'}
$taskDestination=Join-Path $taskGame 'mods\batch_actions.dll'
$taskPreviousHash=$null
if (Test-Path -LiteralPath $taskDestination) {
    Assert-Plain $taskDestination
    $taskPreviousHash=(Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash
    if ($taskPreviousHash -notin @($taskInfo.batchActionsSha256,'29EF53CB3E8FB0D79F836A11B4525031D8EAA29860E2DD5E4FE7DD44CBD8D835','0A9F3CACD4147595F51D542D5F92D1A60E00920EDA3BB21187CB133F9206E7D0')) {throw '已有未知批量补给模组，停止覆盖。'}
}
$taskState=& (Join-Path $taskRoot 'mod_manager\Get-ModManagerState.ps1') -Target $Target | ConvertFrom-Json
if ($ValidateOnly) {
    $taskOwn=@($taskState.mods | Where-Object id -eq 'BatchActions')
    $taskOwnOk=$taskOwn.Count -eq 1 -and $null -ne $taskOwn[0].enabled
    $taskReason=if($taskState.blockReason){$taskState.blockReason}elseif(-not $taskOwnOk){'批量补给的配置状态无法核实。'}else{''}
    [pscustomobject]@{target=$taskGame;packageVerified=$true;gameRunning=$taskState.gameRunning;installed=[bool]$taskPreviousHash;canInstall=($taskState.canManage -and $taskOwnOk);reason=$taskReason}
    return
}
$null=Assert-Stopped
if ($taskPreviousHash -eq $taskInfo.batchActionsSha256) {Write-Output '同一份批量补给模组已安装，无须重复覆盖。';return}

# This installer owns only batch_actions.dll and its installation record.
# No other Mod or shared framework is replaced by this installer.
$taskSave=Join-Path $taskGame 'Save'
$taskPreserve=@((Join-Path $taskGame 'Cube.exe'),(Join-Path $taskGame 'options.cfg'),(Join-Path $taskGame 'cube_mod.dll'),(Join-Path $taskGame 'NavigationLauncher.exe'),(Join-Path $taskGame $taskMarker))
$taskPreserve+=@(Get-ChildItem -LiteralPath (Join-Path $taskGame 'mods') -File | Where-Object FullName -ne $taskDestination | Select-Object -ExpandProperty FullName)
foreach ($name in @('mods.ini','cube_mod.ini')) {
    $path=Join-Path $taskGame $name
    if (Test-Path -LiteralPath $path) {$taskPreserve+=$path}
}
foreach ($directory in @('Save','data','config')) {
    $path=Join-Path $taskGame $directory
    if (Test-Path -LiteralPath $path) {
        foreach ($item in @(Get-ChildItem -LiteralPath $path -Force -Recurse)) {Assert-Plain $item.FullName}
        $taskPreserve+=@(Get-ChildItem -LiteralPath $path -Recurse -File -Force | Select-Object -ExpandProperty FullName)
    }
}
foreach ($path in $taskPreserve) {Assert-Plain $path}
$taskBefore=@($taskPreserve | ForEach-Object {@{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash}})
$taskBackup=Join-Path $taskRoot ('backups\batch-actions-before-'+$Target.ToLower()+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $taskBackup | Out-Null
Copy-Item -LiteralPath $taskSave -Destination (Join-Path $taskBackup 'Save') -Recurse
Copy-Item -LiteralPath (Join-Path $taskGame $taskMarker) -Destination $taskBackup
if ($taskPreviousHash) {
    Copy-Item -LiteralPath $taskDestination -Destination (Join-Path $taskBackup 'batch_actions.dll')
    if ((Get-FileHash -LiteralPath (Join-Path $taskBackup 'batch_actions.dll') -Algorithm SHA256).Hash -ne $taskPreviousHash) {throw '旧批量补给模组备份校验失败，未更新。'}
}
foreach ($relative in @('mods.ini','cube_mod.ini','.batch-actions-installed.json','config\batch_actions.ini','data\batch_actions')) {
    $path=Join-Path $taskGame $relative
    if (Test-Path -LiteralPath $path) {Copy-Item -LiteralPath $path -Destination (Join-Path $taskBackup ([IO.Path]::GetFileName($path))) -Recurse}
}
foreach ($file in $taskBefore) {
    if ($file.path.StartsWith($taskSave+'\',[StringComparison]::OrdinalIgnoreCase)) {
        $copy=Join-Path $taskBackup $file.path.Substring($taskGame.Length+1)
        if ((Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -ne $file.sha256) {throw '完整存档备份校验失败，未安装。'}
    }
}
$taskBefore | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskBackup 'before.json') -Encoding utf8
$null=Assert-Stopped
foreach ($file in $taskBefore) {
    if ((Get-FileHash -LiteralPath $file.path -Algorithm SHA256).Hash -ne $file.sha256) {throw "备份期间受保护文件发生变化，未安装：$($file.path)"}
}
Copy-Item -LiteralPath $taskSource -Destination $taskDestination
if ((Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash -ne $taskInfo.batchActionsSha256) {throw '批量补给模组复制校验失败。'}
foreach ($file in $taskBefore) {
    if ((Get-FileHash -LiteralPath $file.path -Algorithm SHA256).Hash -ne $file.sha256) {throw "其他模组、配置或存档发生变化：$($file.path)"}
}
$taskRecord=@{version='0.1.2';installed=(Get-Date).ToString('o');target=$taskGame;backup=$taskBackup;previousBatchActionsSha256=$taskPreviousHash;batchActionsSha256=$taskInfo.batchActionsSha256;preserved=$taskBefore;gameLaunched=$false}
$taskRecord | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $taskGame '.batch-actions-installed.json') -Encoding utf8
$taskRecord | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $taskRoot ('research\evidence\batch-actions-0.1.2-'+$Target.ToLower()+'-install.json')) -Encoding utf8
Write-Output "批量补给测试版已安装到 $taskGame。游戏尚未启动。"
Write-Output "完整存档与配置备份：$taskBackup"
