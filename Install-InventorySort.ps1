param(
    [ValidateSet('Experiment','Formal')][string]$Target='Experiment',
    [switch]$ValidateOnly
)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$taskPackage=Join-Path $taskRoot 'releases\inventory-sort-0.1.7'
$taskInfo=Get-Content -LiteralPath (Join-Path $taskPackage 'release.json') -Raw | ConvertFrom-Json
$taskSource=Join-Path $taskPackage 'inventory_sort.dll'
if ($taskInfo.version -ne '0.1.7' -or (Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash -ne $taskInfo.inventorySortSha256) {throw '背包分类候选包校验失败。'}
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
    $own=@($state.mods | Where-Object id -eq 'InventorySort')
    if ($own.Count -ne 1 -or $null -eq $own[0].enabled) {throw '背包分类的配置状态无法核实，停止安装。'}
    return $state
}
foreach ($path in @($taskPackage,$taskSource,$taskGame,(Join-Path $taskGame 'Save'),(Join-Path $taskGame 'data'),(Join-Path $taskGame 'config'))) {Assert-Plain $path}
if ((Get-FileHash -LiteralPath (Join-Path $taskGame 'Cube.exe') -Algorithm SHA256).Hash -ne $taskInfo.gameSha256) {throw '游戏不是已核实的 2013 Alpha 版本。'}
if ((Get-FileHash -LiteralPath (Join-Path $taskGame 'cube_mod.dll') -Algorithm SHA256).Hash -ne $taskInfo.requiresFrameworkSha256) {throw '公共加载器版本不符，停止安装。'}
$taskDestination=Join-Path $taskGame 'mods\inventory_sort.dll'
$taskPreviousHash=$null
if (Test-Path -LiteralPath $taskDestination) {
    Assert-Plain $taskDestination
    $taskPreviousHash=(Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash
    if ($taskPreviousHash -notin @($taskInfo.inventorySortSha256,'9A1CA0FF978A8AA724288A22870D6E94E5555006B3EFFE94B2097D3B686DB923','590CAEA5923D6DDD6E01DD9CC83D40DE46532A078540231B45A648F68D04DAC9','86BF70D3B61EF99FC26800BCF5AEA883EDD8C1D1F3FAC284FDACD5C46E76D2E4','EF44591F4D9ECEDE22FE34778AE87987585C14C30BC7F7EFB0AF87DB5410B6EA','6254B593764638859642B8CDE91FCDC03F38C8B1D58B1088480C54236D9B0E19','AE38FA8B730AF97EF9B135275F0B8E12BAC793057D615EF0AE1022EDB99943CA','788FBE4D57DE6C9C0FCC743FE55E8930B0C67189927D1D8F8E80B55372E6D592','DC92553061A9504AACBD9B9668C589A41120FD96D5DEFCC2E1EE371551046696')) {throw '已有未知背包分类模组，停止覆盖。'}
}
$taskState=& (Join-Path $taskRoot 'mod_manager\Get-ModManagerState.ps1') -Target $Target | ConvertFrom-Json
if ($ValidateOnly) {
    $taskOwn=@($taskState.mods | Where-Object id -eq 'InventorySort')
    $taskOwnOk=$taskOwn.Count -eq 1 -and $null -ne $taskOwn[0].enabled
    $taskReason=if($taskState.blockReason){$taskState.blockReason}elseif(-not $taskOwnOk){'背包分类的配置状态无法核实。'}else{''}
    [pscustomobject]@{target=$taskGame;packageVerified=$true;gameRunning=$taskState.gameRunning;installed=[bool]$taskPreviousHash;canInstall=($taskState.canManage -and $taskOwnOk);reason=$taskReason}
    return
}
$null=Assert-Stopped
if ($taskPreviousHash -eq $taskInfo.inventorySortSha256) {Write-Output '同一份背包分类模组已安装，无须重复覆盖。';return}

# This installer owns only inventory_sort.dll and its installation record.
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
$taskBackup=Join-Path $taskRoot ('backups\inventory-sort-before-'+$Target.ToLower()+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $taskBackup | Out-Null
Copy-Item -LiteralPath $taskSave -Destination (Join-Path $taskBackup 'Save') -Recurse
Copy-Item -LiteralPath (Join-Path $taskGame $taskMarker) -Destination $taskBackup
if ($taskPreviousHash) {
    Copy-Item -LiteralPath $taskDestination -Destination (Join-Path $taskBackup 'inventory_sort.dll')
    if ((Get-FileHash -LiteralPath (Join-Path $taskBackup 'inventory_sort.dll') -Algorithm SHA256).Hash -ne $taskPreviousHash) {throw '旧背包分类模组备份校验失败，未更新。'}
}
foreach ($relative in @('mods.ini','cube_mod.ini','.inventory-sort-installed.json','config\inventory_sort.ini','data\inventory_sort')) {
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
if ((Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash -ne $taskInfo.inventorySortSha256) {throw '背包分类模组复制校验失败。'}
foreach ($file in $taskBefore) {
    if ((Get-FileHash -LiteralPath $file.path -Algorithm SHA256).Hash -ne $file.sha256) {throw "其他模组、配置或存档发生变化：$($file.path)"}
}
$taskRecord=@{version='0.1.7';installed=(Get-Date).ToString('o');target=$taskGame;backup=$taskBackup;previousInventorySortSha256=$taskPreviousHash;inventorySortSha256=$taskInfo.inventorySortSha256;preserved=$taskBefore;gameLaunched=$false}
$taskRecord | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $taskGame '.inventory-sort-installed.json') -Encoding utf8
$taskRecord | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $taskRoot ('research\evidence\inventory-sort-0.1.7-'+$Target.ToLower()+'-install.json')) -Encoding utf8
Write-Output "背包分类测试版已安装到 $taskGame。游戏尚未启动。"
Write-Output "完整存档与配置备份：$taskBackup"
