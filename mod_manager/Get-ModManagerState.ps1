[CmdletBinding()]
param([ValidateSet('Experiment','Formal')][string]$Target='Experiment')
$ErrorActionPreference='Stop'

# Read-only companion to Manage-Mod.ps1. No game or Mod binary is ever loaded.
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$taskGame=if ($Target -eq 'Formal') {'C:\Games\CubeWorld'} else {Join-Path $taskRoot 'playtest'}
$taskGame=[IO.Path]::GetFullPath($taskGame).TrimEnd('\')
$taskMarkerName=if ($Target -eq 'Formal') {'.navigation-installed.json'} else {'.navigation-sandbox.json'}
$taskUtf8=[Text.UTF8Encoding]::new($false,$true)

function Assert-PlainPath([string]$Path,[switch]$Required,[switch]$File) {
    $absolute=[IO.Path]::GetFullPath($Path)
    $probe=$absolute
    while ($probe) {
        if (Test-Path -LiteralPath $probe) {
            $item=Get-Item -LiteralPath $probe -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
                ($item.PSObject.Properties.Name -contains 'LinkType' -and $item.LinkType)) {
                throw "拒绝链接路径：$probe"
            }
        }
        $parent=[IO.Path]::GetDirectoryName($probe.TrimEnd('\'))
        if (-not $parent -or $parent -eq $probe) {break}
        $probe=$parent
    }
    if ($Required -and -not (Test-Path -LiteralPath $absolute)) {throw "缺少必要路径：$absolute"}
    if (Test-Path -LiteralPath $absolute) {
        $item=Get-Item -LiteralPath $absolute -Force
        if ($File -and $item.PSIsContainer) {throw "预期普通文件，实际是目录：$absolute"}
        if (-not $File -and -not $item.PSIsContainer) {throw "预期目录，实际是文件：$absolute"}
    }
}

function Read-StrictUtf8([string]$Path,[switch]$AllowBom) {
    [byte[]]$bytes=[IO.File]::ReadAllBytes($Path)
    $start=0
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xef -and $bytes[1] -eq 0xbb -and $bytes[2] -eq 0xbf) {
        if (-not $AllowBom) {throw 'mods.ini 含 BOM；仅支持无 BOM 的 UTF-8/ASCII，请先人工核对。'}
        $start=3
    }
    if ($bytes.Length -ge 2 -and (($bytes[0] -eq 0xff -and $bytes[1] -eq 0xfe) -or
                                 ($bytes[0] -eq 0xfe -and $bytes[1] -eq 0xff))) {
        throw '配置使用 UTF-16；请先人工核对编码。'
    }
    try {$text=$taskUtf8.GetString($bytes,$start,$bytes.Length-$start)}
    catch {throw '配置不是有效的 UTF-8；请先人工核对编码。'}
    if ($text.Contains([char]0)) {throw '配置包含 NUL 字节或不支持的编码；请先人工核对。'}
    return $text
}

$taskState=[ordered]@{
    target=$Target
    targetLabel=$(if ($Target -eq 'Formal') {'正式游戏'} else {'实验副本'})
    gameDirectory=$taskGame
    directoryExists=[bool](Test-Path -LiteralPath $taskGame -PathType Container)
    canManage=$false
    blockReason=''
    gameRunning=$false
    processCheckOk=$false
    canLaunch=$false
    launchReason=''
    mods=@()
}
$taskSafetyError=''
$taskConfigError=''
$taskProcessError=''
$taskConfigText=''
$taskSafePaths=$false

try {
    Assert-PlainPath $taskRoot -Required
    Assert-PlainPath $taskGame -Required
    $taskExe=Join-Path $taskGame 'Cube.exe'
    $taskLoader=Join-Path $taskGame 'cube_mod.dll'
    $taskMarker=Join-Path $taskGame $taskMarkerName
    $taskConfig=Join-Path $taskGame 'mods.ini'
    $taskMods=Join-Path $taskGame 'mods'
    foreach ($path in @($taskExe,$taskLoader,$taskMarker)) {Assert-PlainPath $path -Required -File}
    Assert-PlainPath $taskMods -Required
    Assert-PlainPath $taskConfig -File
    Assert-PlainPath (Join-Path $taskRoot 'backups')
    foreach ($stem in @('waypoint','potion_shop','region_explorer','map_light','inventory_sort','fast_travel','batch_actions','boss_draw')) {Assert-PlainPath (Join-Path $taskMods ($stem+'.dll')) -File}
    $taskSafePaths=$true
    if ((Get-FileHash -LiteralPath $taskLoader -Algorithm SHA256).Hash -ne 'D729D26FF4F91EED453160FBE4D24D9430043AE6A7165CF3A3AF8A92C24B7A31') {
        throw '公共加载器不是当前已验证的兼容版本。'
    }
    $record=Read-StrictUtf8 $taskMarker -AllowBom | ConvertFrom-Json
    if ($Target -eq 'Formal') {
        if (-not $record.directory -or -not ([string]$record.directory).Equals($taskGame,[StringComparison]::OrdinalIgnoreCase)) {
            throw '正式安装标记中的目录不匹配。'
        }
        $gameHash=[string]$record.gameSha256
        $loaderEntries=@($record.installedFiles | Where-Object {$_.path -eq 'cube_mod.dll'})
        if ($loaderEntries.Count -ne 1 -or
            (Get-FileHash -LiteralPath $taskLoader -Algorithm SHA256).Hash -ne $loaderEntries[0].sha256) {
            throw '公共加载器与正式安装标记不符。'
        }
    } else {
        if (-not $record.original -or -not ([string]$record.original).Equals('C:\Games\CubeWorld',[StringComparison]::OrdinalIgnoreCase)) {
            throw '实验副本标记中的来源不匹配。'
        }
        $gameEntries=@($record.runtimeFiles | Where-Object {$_.name -eq 'Cube.exe'})
        if ($gameEntries.Count -ne 1) {throw '实验副本标记缺少唯一游戏记录。'}
        $gameHash=[string]$gameEntries[0].sha256
    }
    if ($gameHash -notmatch '^[0-9a-fA-F]{64}$' -or
        (Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash -ne $gameHash) {
        throw '游戏文件与安全标记不符。'
    }
} catch {$taskSafetyError='安全检查未通过：'+$_.Exception.Message}

# An inaccessible or pathless candidate is unknown, not proof that the game stopped.
try {
    foreach ($process in @(Get-CimInstance Win32_Process -Filter "Name='Cube.exe' OR Name='NavigationLauncher.exe' OR Name='Server.exe'" -ErrorAction Stop)) {
        if (-not $process.ExecutablePath) {throw '无法核实正在运行的游戏、服务器或启动器路径。'}
        $directory=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($process.ExecutablePath)).TrimEnd('\')
        if ($directory.Equals($taskGame,[StringComparison]::OrdinalIgnoreCase)) {$taskState.gameRunning=$true}
    }
    $taskState.processCheckOk=$true
    if ($taskState.gameRunning) {$taskProcessError='目标游戏、服务器或启动器仍在运行，请完全退出后再管理 Mod。'}
} catch {$taskProcessError='无法查询或核实游戏进程，已锁定开关：'+$_.Exception.Message}

if ($taskSafePaths) {
    try {
        if (Test-Path -LiteralPath $taskConfig -PathType Leaf) {$taskConfigText=Read-StrictUtf8 $taskConfig}
    } catch {$taskConfigError=$_.Exception.Message}
}
$taskState.blockReason=if ($taskSafetyError) {$taskSafetyError} elseif ($taskProcessError) {$taskProcessError} else {$taskConfigError}
$taskState.canManage=(-not $taskState.blockReason)

$taskDefinitions=@(
    @{id='Navigation';stem='waypoint';name='地图导航';description='地图标点、方向导航、城镇标签和店铺指南。'},
    @{id='PotionShop';stem='potion_shop';name='药水商店';description='出售比当前可制作药水低一档的生命药水，随成长更新。'},
    @{id='RegionExplorer';stem='region_explorer';name='区域探索';description='提前查看已生成的周边城镇、挑战和地标，选择地点导航。'},
    @{id='MapLight';stem='map_light';name='地图照明';description='仅夜间给大地图补光，白天保留原版光照；关闭地图恢复原夜景。'},
    @{id='InventorySort';stem='inventory_sort';name='背包分类';description='胸甲、手套等中文分区，拿起可放回；卖出留空不补位。'},
    @{id='FastTravel';stem='fast_travel';name='旅行驿站';description='附近 Boss 名字和职业在交战、移动时也记录；停稳登记传送落点，可改名补备注，F8 传送，仅限单人。'},
    @{id='BatchActions';stem='batch_actions';name='批量补给';description='点击空瓶、水瓶或生命药水后弹出数量窗口，A / D 每次减 / 加 10，Enter 确认；显示携带上限，单人使用。'},
    @{id='BossDraw';stem='boss_draw';name='首领抽奖';description='每次有效首领击杀额外一抽；F9 开奖，本职业小保底，原版掉落保留，仅限单人。'}
)
foreach ($definition in $taskDefinitions) {
    $installed=$false
    $enabled=$null
    $reason=''
    if ($taskSafePaths) {$installed=[bool](Test-Path -LiteralPath (Join-Path $taskMods ($definition.stem+'.dll')) -PathType Leaf)}
    if (-not $taskSafePaths) {$reason=$taskSafetyError}
    elseif ($taskConfigError) {$reason=$taskConfigError}
    else {
        # ini::read ignores comment/section lines, trims ASCII whitespace, folds
        # key case, and accepts the booleans below. Missing keys default to true
        # (loader/core/modregistry.cpp). Ambiguity is deliberately not guessed.
        $values=@()
        foreach ($line in ($taskConfigText -split "`n")) {
            $trimmed=$line.Trim([char[]]" `t`r`n")
            if (-not $trimmed -or $trimmed[0] -eq '#' -or $trimmed[0] -eq ';' -or $trimmed[0] -eq '[') {continue}
            $equals=$trimmed.IndexOf('=')
            if ($equals -lt 0) {continue}
            $key=$trimmed.Substring(0,$equals).Trim([char[]]" `t`r`n")
            if ($key.Equals($definition.stem+'.enabled',[StringComparison]::OrdinalIgnoreCase)) {
                $values+=,$trimmed.Substring($equals+1).Trim([char[]]" `t`r`n")
            }
        }
        if ($values.Count -gt 1) {$reason='mods.ini 中此 Mod 的 enabled 键重复，请先人工核对。'}
        elseif ($values.Count -eq 0) {$enabled=$true}
        elseif ($values[0] -match '^(?i:1|true|yes|on)$') {$enabled=$true}
        elseif ($values[0] -match '^(?i:0|false|no|off)$') {$enabled=$false}
        else {$reason='mods.ini 中此 Mod 的 enabled 值无效，请先人工核对。'}
    }
    $canToggle=[bool]($taskState.canManage -and $installed -and $null -ne $enabled)
    $statusLabel=if (-not $taskSafePaths) {'状态未知'} elseif (-not $installed) {'未安装'} elseif ($null -eq $enabled) {'状态未知'} elseif ($enabled) {'已启用'} else {'已停用'}
    if (-not $reason) {
        if ($taskState.blockReason) {$reason=$taskState.blockReason}
        elseif (-not $installed) {$reason='缺少此 Mod 的 DLL，请先安装后再启用。'}
    }
    $taskState.mods+=,[pscustomobject][ordered]@{
        id=$definition.id;stem=$definition.stem;name=$definition.name;description=$definition.description
        installed=$installed;enabled=$enabled;canToggle=$canToggle;statusLabel=$statusLabel;reason=$reason
    }
}
# Launch readiness shares the same target/process checks, but never changes
# the installed shared launcher as a side effect of starting the game.
try {
    if (-not $taskState.canManage) {throw $taskState.blockReason}
    foreach ($mod in $taskState.mods) {
        if ($mod.installed -and $null -eq $mod.enabled) {throw $mod.reason}
    }
    $taskLauncher=Join-Path $taskGame 'NavigationLauncher.exe'
    Assert-PlainPath $taskLauncher -Required -File
    $launcherHash=(Get-FileHash -LiteralPath $taskLauncher -Algorithm SHA256).Hash
    # Formal's 740 repair is a separately built, recorded legacy version.
    # Both legacy builds still require waypoint.dll, even when disabled.
    $legacyHashes=@(
        'A6D8E4CAC9E7F53EA000EE1538D77CF524C81B93EF8F59E10D93AF6938F96723',
        '6971B37D292907F069658AE733078BFFD3099D8713C2C47B30EF2936C8176CEB'
    )
    $independentHash='B57534B00C72A100743BA36799B39FA0C742246BBE2512CC562F85AD9FCBBE7C'
    if ($launcherHash -notin $legacyHashes -and $launcherHash -ne $independentHash) {
        throw '启动器不是当前已验证的版本，请先核对公共启动器。'
    }
    if ($launcherHash -in $legacyHashes -and -not (Test-Path -LiteralPath (Join-Path $taskMods 'waypoint.dll') -PathType Leaf)) {
        throw '当前旧启动器仍要求地图 DLL；请先单独升级公共启动器。'
    }
    $taskState.canLaunch=$true
} catch {$taskState.launchReason=$_.Exception.Message}
# Exactly one JSON object on stdout, including safe failures. Do not print status prose.
$taskState | ConvertTo-Json -Depth 6 -Compress
