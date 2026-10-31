param([ValidateSet('Experiment','Formal')][string]$Target = 'Experiment')
$ErrorActionPreference = 'Stop'
$taskRoot=[System.IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$taskRelease=Join-Path $taskRoot 'releases\0.3.8-font-clarity'
$taskReleaseInfo=Get-Content -LiteralPath (Join-Path $taskRelease 'release.json') -Raw | ConvertFrom-Json
$taskSource=Join-Path $taskRelease 'waypoint.dll'
if ($taskReleaseInfo.version -ne '0.3.8' -or (Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash -ne $taskReleaseInfo.waypointSha256) { throw '候选包版本或散列校验失败。' }
$taskGame=if ($Target -eq 'Formal') { 'C:\Games\CubeWorld' } else { Join-Path $taskRoot 'playtest' }
$taskMarker=if ($Target -eq 'Formal') { '.navigation-installed.json' } else { '.navigation-sandbox.json' }
if (-not (Test-Path -LiteralPath (Join-Path $taskGame $taskMarker))) { throw '目标不是已准备的导航游戏目录。' }
if ((Get-FileHash -LiteralPath (Join-Path $taskGame 'Cube.exe') -Algorithm SHA256).Hash -ne '84A7A132A84D4282338E7EA45A1940D32066D64E39CBAFED8F2418D5A6DC30BF') { throw '游戏版本不符。' }
if ((Get-FileHash -LiteralPath (Join-Path $taskGame 'cube_mod.dll') -Algorithm SHA256).Hash -ne 'D729D26FF4F91EED453160FBE4D24D9430043AE6A7165CF3A3AF8A92C24B7A31') { throw '请先安装已验证的 0.2.0 导航框架，停止覆盖未知版本。' }
function Assert-TargetStopped {
    foreach ($taskProcess in @(Get-CimInstance Win32_Process -Filter "Name='Cube.exe' OR Name='NavigationLauncher.exe' OR Name='Server.exe'" -ErrorAction Stop)) {
        if (-not $taskProcess.ExecutablePath) { throw '无法核实运行中的游戏路径，停止安装。' }
        if ([System.IO.Path]::GetDirectoryName($taskProcess.ExecutablePath).Equals($taskGame,[System.StringComparison]::OrdinalIgnoreCase)) { throw '目标游戏仍在运行。请正常退出后再安装城镇标签。' }
    }
}
Assert-TargetStopped
$taskDestination=Join-Path $taskGame 'mods\waypoint.dll'
$taskOldHash=(Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash
if ($taskOldHash -eq $taskReleaseInfo.waypointSha256) { Write-Output '已安装同一份 0.3.8 模组，无须重复覆盖。'; return }
if ($taskOldHash -notin @('D939BA6BBEDC3E31ECCC4C9D457318D03CFFCAD3DD06BF0EE4C3B5CFC346D8D0','B08699D1B0758532E46018F29F0BFF17435C6391E46ED957012AAE60CFDF630C','416A64B9BC3A4B3ADC8FD23E04890C38B7AE310CC8F7C85050A8863F0D23AADA','884CA22535E304CF95EFC25D768E33FCDFEEBA83D61CF546428C5BDC34CEEC7D','548FE05086D4D0067F0FBFA7C42DC6C2CAC070AEEB792EDB318B91E0069687F5','EDCEBB90553E16B83CF29328700C414275A771774DA7B5166F08C1029B94039F','58BFB004EC964326A09F2DD140C4A7B0BAA75B891916AECA45F2055A7454F65C','2FC234E28F95AE834EDC3DE8BD5EF10AC808FFA9A2FB1B813E8ADF1DB3C495A2','6EDF338D0B5A71C4A403E647490A75136B173363A46D2F2B36CED5E316872B15')) { throw '现有导航模组有未知改动，停止覆盖。' }
$taskPreserve=@((Join-Path $taskGame 'Cube.exe'),(Join-Path $taskGame 'options.cfg'),(Join-Path $taskGame 'cube_mod.dll'),(Join-Path $taskGame 'NavigationLauncher.exe')) + @(Get-ChildItem -LiteralPath (Join-Path $taskGame 'Save') -Recurse -File | Select-Object -ExpandProperty FullName)
# Preserve other modules and all shared enable/strike keys when updating only the map.
$taskPreserve+=@(Get-ChildItem -LiteralPath (Join-Path $taskGame 'mods') -File | Where-Object FullName -ne $taskDestination | Select-Object -ExpandProperty FullName)
$taskModsConfig=Join-Path $taskGame 'mods.ini'
if (Test-Path -LiteralPath $taskModsConfig) { $taskPreserve+=$taskModsConfig }
if (Test-Path -LiteralPath (Join-Path $taskGame 'cube_mod.ini')) { $taskPreserve+=(Join-Path $taskGame 'cube_mod.ini') }
foreach ($taskDirectory in @('data','config')) {
    $taskDirectoryPath=Join-Path $taskGame $taskDirectory
    if (Test-Path -LiteralPath $taskDirectoryPath) { $taskPreserve+=@(Get-ChildItem -LiteralPath $taskDirectoryPath -Recurse -File | Select-Object -ExpandProperty FullName) }
}
$taskBefore=@($taskPreserve | ForEach-Object { @{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash} })
$taskBackup=Join-Path $taskRoot ('backups\town-before-0.3.8-'+$Target.ToLower()+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $taskBackup | Out-Null
Copy-Item -LiteralPath $taskDestination -Destination (Join-Path $taskBackup 'waypoint.dll')
Copy-Item -LiteralPath (Join-Path $taskGame $taskMarker) -Destination $taskBackup
$taskModData=Join-Path $taskGame 'data\waypoint'
if (Test-Path -LiteralPath $taskModData) { Copy-Item -LiteralPath $taskModData -Destination (Join-Path $taskBackup 'waypoint-data') -Recurse }
if ((Get-FileHash -LiteralPath (Join-Path $taskBackup 'waypoint.dll') -Algorithm SHA256).Hash -ne $taskOldHash) { throw '旧模组备份校验失败。' }
# Only reached after the target game/launcher has completely exited.
Copy-Item -LiteralPath (Join-Path $taskGame 'Save') -Destination (Join-Path $taskBackup 'Save') -Recurse
foreach ($taskFile in $taskBefore) {
    if ($taskFile.path.StartsWith((Join-Path $taskGame 'Save')+'\',[System.StringComparison]::OrdinalIgnoreCase)) {
        $taskSaved=Join-Path $taskBackup $taskFile.path.Substring($taskGame.Length+1)
        if ((Get-FileHash -LiteralPath $taskSaved -Algorithm SHA256).Hash -ne $taskFile.sha256) { throw '完整存档备份校验失败，停止更新。' }
    }
}
Assert-TargetStopped
foreach ($taskFile in $taskBefore) {
    if ((Get-FileHash -LiteralPath $taskFile.path -Algorithm SHA256).Hash -ne $taskFile.sha256) { throw "备份期间受保护文件发生变化，停止更新：$($taskFile.path)" }
}
Copy-Item -LiteralPath $taskSource -Destination $taskDestination
if ((Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash -ne $taskReleaseInfo.waypointSha256) { throw '候选模组复制校验失败。' }
$taskRecord=@{version='0.3.8';installed=(Get-Date).ToString('o');target=$taskGame;backup=$taskBackup;
    beforeSha256=$taskOldHash;afterSha256=$taskReleaseInfo.waypointSha256;preserved=$taskBefore;gameLaunched=$false}
if ($Target -eq 'Formal') {
    $taskManifest=Get-Content -LiteralPath (Join-Path $taskGame $taskMarker) -Raw | ConvertFrom-Json
    $taskManifest.version='0.3.8'
    ($taskManifest.installedFiles | Where-Object path -eq 'mods\waypoint.dll').sha256=$taskReleaseInfo.waypointSha256
    $taskManifest | Add-Member -NotePropertyName townLabelsUpdate -NotePropertyValue $taskRecord -Force
    $taskManifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $taskGame $taskMarker) -Encoding utf8
}
foreach ($taskFile in $taskBefore) {
    if ((Get-FileHash -LiteralPath $taskFile.path -Algorithm SHA256).Hash -ne $taskFile.sha256) { throw "原游戏、框架、配置或存档发生变化：$($taskFile.path)" }
}
$taskRecord | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $taskRoot ('research\evidence\town-0.3.8-'+$Target.ToLower()+'-install.json')) -Encoding utf8
Write-Output "城镇标签候选版已安装到 $taskGame。原游戏、框架、配置和存档未改；尚未启动游戏。"
Write-Output "旧模组与标点备份：$taskBackup"

