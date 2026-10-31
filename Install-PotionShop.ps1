param(
    [ValidateSet('Experiment','Formal')][string]$Target='Experiment',
    [switch]$ValidateOnly
)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$taskPackage=Join-Path $taskRoot 'releases\potion-shop-0.2.0'
$taskInfo=Get-Content -LiteralPath (Join-Path $taskPackage 'release.json') -Raw | ConvertFrom-Json
$taskSource=Join-Path $taskPackage 'potion_shop.dll'
if ($taskInfo.version -ne '0.2.0' -or (Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash -ne $taskInfo.potionShopSha256) { throw '药水模组候选包校验失败。' }
$taskGame=if ($Target -eq 'Formal') {'C:\Games\CubeWorld'} else {Join-Path $taskRoot 'playtest'}
$taskMarker=if ($Target -eq 'Formal') {'.navigation-installed.json'} else {'.navigation-sandbox.json'}
if (-not (Test-Path -LiteralPath (Join-Path $taskGame $taskMarker))) { throw '目标不是已准备模组框架的游戏目录。' }
if ((Get-FileHash -LiteralPath (Join-Path $taskGame 'Cube.exe') -Algorithm SHA256).Hash -ne $taskInfo.gameSha256) { throw '游戏不是已核实的 2013 Alpha 版本。' }
if ((Get-FileHash -LiteralPath (Join-Path $taskGame 'cube_mod.dll') -Algorithm SHA256).Hash -ne $taskInfo.requiresFrameworkSha256) { throw '模组框架版本不符，停止安装。' }
function Test-GameRunning {
    $running=$false
    foreach ($process in @(Get-CimInstance Win32_Process -Filter "Name='Cube.exe' OR Name='NavigationLauncher.exe' OR Name='Server.exe'" -ErrorAction Stop)) {
        if (-not $process.ExecutablePath) { throw '无法核实运行中的游戏路径，停止安装。' }
        if ([IO.Path]::GetDirectoryName($process.ExecutablePath).Equals($taskGame,[StringComparison]::OrdinalIgnoreCase)) { $running=$true }
    }
    return $running
}
$taskRunning=Test-GameRunning
$taskDestination=Join-Path $taskGame 'mods\potion_shop.dll'
$taskPreviousHash=$null
if (Test-Path -LiteralPath $taskDestination) {
    $taskPreviousHash=(Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash
    if ($taskPreviousHash -eq $taskInfo.potionShopSha256) {
        Write-Output '同一份药水模组已安装，无须重复覆盖。'
        return
    }
    if ($taskPreviousHash -notin @('C6D08BAD9CACA91901B70B1A70521E5229F89072C0629087B6810E75A8998F18','8C1875289C33908BD0E62312ED4C0420D743A10B55140B426CF6696325F404BB')) { throw '已有未知药水模组，停止覆盖。' }
}
if ($ValidateOnly) {
    [pscustomobject]@{target=$taskGame;packageVerified=$true;gameRunning=$taskRunning;installed=$false;canInstall=(-not $taskRunning)}
    return
}
if ($taskRunning) { throw '目标游戏仍在运行。请正常退出后再安装药水商店。' }

# Back up the completed target save only after the game has exited. Never import another save.
$taskSave=Join-Path $taskGame 'Save'
$taskPreserve=@((Join-Path $taskGame 'Cube.exe'),(Join-Path $taskGame 'options.cfg'),(Join-Path $taskGame 'cube_mod.dll'),(Join-Path $taskGame 'NavigationLauncher.exe'))
$taskPreserve+=@(Get-ChildItem -LiteralPath (Join-Path $taskGame 'mods') -File | Where-Object FullName -ne $taskDestination | Select-Object -ExpandProperty FullName)
foreach ($taskConfig in @('mods.ini','cube_mod.ini')) {
    $taskConfigPath=Join-Path $taskGame $taskConfig
    if (Test-Path -LiteralPath $taskConfigPath) { $taskPreserve+=$taskConfigPath }
}
foreach ($taskDirectory in @('data','config')) {
    $taskDataPath=Join-Path $taskGame $taskDirectory
    if (Test-Path -LiteralPath $taskDataPath) { $taskPreserve+=@(Get-ChildItem -LiteralPath $taskDataPath -Recurse -File | Select-Object -ExpandProperty FullName) }
}
$taskPreserve += @(Get-ChildItem -LiteralPath $taskSave -Recurse -File | Select-Object -ExpandProperty FullName)
$taskBefore=@($taskPreserve | ForEach-Object {@{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash}})
$taskBackup=Join-Path $taskRoot ('backups\potion-shop-before-'+$Target.ToLower()+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $taskBackup | Out-Null
Copy-Item -LiteralPath $taskSave -Destination (Join-Path $taskBackup 'Save') -Recurse
Copy-Item -LiteralPath (Join-Path $taskGame $taskMarker) -Destination $taskBackup
if ($taskPreviousHash) {
    Copy-Item -LiteralPath $taskDestination -Destination (Join-Path $taskBackup 'potion_shop.dll')
    if ((Get-FileHash -LiteralPath (Join-Path $taskBackup 'potion_shop.dll') -Algorithm SHA256).Hash -ne $taskPreviousHash) { throw '旧药水模组备份校验失败，未安装。' }
}
$taskPreviousRecord=Join-Path $taskGame '.potion-shop-installed.json'
if (Test-Path -LiteralPath $taskPreviousRecord) { Copy-Item -LiteralPath $taskPreviousRecord -Destination $taskBackup }
foreach ($taskFile in $taskBefore) {
    if ($taskFile.path.StartsWith($taskSave+'\',[StringComparison]::OrdinalIgnoreCase)) {
        $taskRelative=$taskFile.path.Substring($taskSave.Length+1)
        if ((Get-FileHash -LiteralPath (Join-Path $taskBackup ('Save\'+$taskRelative)) -Algorithm SHA256).Hash -ne $taskFile.sha256) { throw '存档备份校验失败，未安装。' }
    }
}
$taskBefore | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskBackup 'before.json') -Encoding utf8
# A game started or a save changed during backup must prevent replacing the DLL.
if (Test-GameRunning) { throw '备份期间目标游戏已启动，未替换药水模组。' }
foreach ($taskFile in $taskBefore) {
    if ((Get-FileHash -LiteralPath $taskFile.path -Algorithm SHA256).Hash -ne $taskFile.sha256) { throw "备份期间受保护文件发生变化，未安装：$($taskFile.path)" }
}
Copy-Item -LiteralPath $taskSource -Destination $taskDestination
if ((Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash -ne $taskInfo.potionShopSha256) { throw '模组复制校验失败。' }
foreach ($taskFile in $taskBefore) {
    if ((Get-FileHash -LiteralPath $taskFile.path -Algorithm SHA256).Hash -ne $taskFile.sha256) { throw "游戏、导航或存档发生变化：$($taskFile.path)" }
}
$taskRecord=@{version='0.2.0';installed=(Get-Date).ToString('o');target=$taskGame;backup=$taskBackup;previousPotionShopSha256=$taskPreviousHash;potionShopSha256=$taskInfo.potionShopSha256;offerRule='highest unlocked LifePotion recipe minus one four-level tier';priceRule='ceil(10 + (nativeHealing - 200) / 40) copper';preserved=$taskBefore;gameLaunched=$false}
$taskRecord | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $taskGame '.potion-shop-installed.json') -Encoding utf8
$taskRecord | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $taskRoot ('research\evidence\potion-shop-0.2.0-'+$Target.ToLower()+'-install.json')) -Encoding utf8
Write-Output "药水商店 0.2.0 待测版已安装到 $taskGame。自动出售可制作最高档的上一档药水，9 级药水 15 铜币；沿用原启动入口和药水开关。"
Write-Output "完整存档备份：$taskBackup。尚未启动或验证游戏内购买。"
