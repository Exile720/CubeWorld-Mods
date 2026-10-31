param(
    [string]$OriginalGame = 'C:\Games\CubeWorld',
    [ValidateSet('Navigation','PotionShop','RegionExplorer','MapLight','InventorySort','FastTravel','BatchActions','BossDraw')][string[]]$Mods = @('Navigation')
)
$ErrorActionPreference = 'Stop'
$taskRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$taskSource = (Resolve-Path -LiteralPath $OriginalGame).Path
$taskTarget = [System.IO.Path]::GetFullPath((Join-Path $taskRoot 'playtest'))
if (-not $taskTarget.StartsWith($taskRoot + '\', [System.StringComparison]::OrdinalIgnoreCase)) { throw '实验目录必须位于当前项目内。' }
if ($taskSource -eq $taskTarget) { throw '实验目录不能是原游戏目录。' }
if (Test-Path -LiteralPath $taskTarget) { throw 'playtest 已存在。为保留其中的实验存档，本脚本不会覆盖它。' }
$taskBin = Join-Path $taskRoot 'build\bin'
$taskModFiles = @()
if ($Mods -contains 'Navigation') { $taskModFiles += 'waypoint.dll' }
if ($Mods -contains 'PotionShop') { $taskModFiles += 'potion_shop.dll' }
if ($Mods -contains 'RegionExplorer') { $taskModFiles += 'region_explorer.dll' }
if ($Mods -contains 'MapLight') { $taskModFiles += 'map_light.dll' }
if ($Mods -contains 'InventorySort') { $taskModFiles += 'inventory_sort.dll' }
if ($Mods -contains 'FastTravel') { $taskModFiles += 'fast_travel.dll' }
if ($Mods -contains 'BatchActions') { $taskModFiles += 'batch_actions.dll' }
if ($Mods -contains 'BossDraw') { $taskModFiles += 'boss_draw.dll' }
foreach ($taskName in (@('cube_mod.dll', 'NavigationLauncher.exe') + $taskModFiles)) {
    if (-not (Test-Path -LiteralPath (Join-Path $taskBin $taskName))) { throw "请先编译：缺少 $taskName" }
}
New-Item -ItemType Directory -Path $taskTarget | Out-Null
New-Item -ItemType Directory -Path (Join-Path $taskTarget 'mods'),(Join-Path $taskTarget 'Save') | Out-Null
$taskManifest = @()
foreach ($taskFile in Get-ChildItem -LiteralPath $taskSource -File) {
    if ($taskFile.Name -eq 'Cube.exe' -or $taskFile.Name -eq 'logo.bmp' -or
        $taskFile.Extension.ToLowerInvariant() -in @('.dll','.db','.dat','.plx')) {
        $taskDestination = Join-Path $taskTarget $taskFile.Name
        Copy-Item -LiteralPath $taskFile.FullName -Destination $taskDestination
        $taskManifest += @{ name=$taskFile.Name; sha256=(Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash }
    }
}
# The player's active Save is intentionally not read or copied. Tests start with a new character/world.
$taskOptions = Join-Path $taskSource 'options.cfg'
$taskText = if (Test-Path -LiteralPath $taskOptions) { [System.IO.File]::ReadAllText($taskOptions) } else { "fullscreen 0`nresolutionX 1280`nresolutionY 800`n" }
$taskText = [regex]::Replace($taskText, '(?m)^fullscreen\s+\d+', 'fullscreen 0')
[System.IO.File]::WriteAllText((Join-Path $taskTarget 'options.cfg'), $taskText)
Copy-Item -LiteralPath (Join-Path $taskBin 'cube_mod.dll') -Destination $taskTarget
Copy-Item -LiteralPath (Join-Path $taskBin 'NavigationLauncher.exe') -Destination $taskTarget
foreach ($taskName in $taskModFiles) {
    Copy-Item -LiteralPath (Join-Path $taskBin $taskName) -Destination (Join-Path $taskTarget ('mods\' + $taskName))
}
@'
log_level = info
console = 0
capture_game_log = 0
fault_isolation = 1
overlay = 1
allow_build_mismatch = 0
'@ | Set-Content -LiteralPath (Join-Path $taskTarget 'cube_mod.ini') -Encoding ascii
@{ created=(Get-Date).ToString('o'); original=$taskSource; saves='new and isolated; original saves excluded'; runtimeFiles=$taskManifest } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskTarget '.navigation-sandbox.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $taskRoot 'external\Qube-Loader\LICENSE') -Destination (Join-Path $taskTarget 'Qube-Loader-LICENSE.txt')
Write-Output "已准备独立实验副本：$taskTarget。原存档没有复制，游戏没有启动。"
