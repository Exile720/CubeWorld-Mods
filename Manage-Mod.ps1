[CmdletBinding(SupportsShouldProcess=$true, ConfirmImpact='Medium')]
param(
    [Parameter(Mandatory=$true)][ValidateSet('Navigation','PotionShop','RegionExplorer','MapLight','InventorySort','FastTravel','BatchActions','BossDraw')][string]$Mod,
    [Parameter(Mandatory=$true)][ValidateSet('Enable','Disable','Uninstall')][string]$Action,
    [ValidateSet('Experiment','Formal')][string]$Target='Experiment'
)
$ErrorActionPreference='Stop'

# Only these two locations and the registered module names may be managed.
$taskRoot=[IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$taskGame=if ($Target -eq 'Formal') {'C:\Games\CubeWorld'} else {Join-Path $taskRoot 'playtest'}
$taskGame=[IO.Path]::GetFullPath($taskGame).TrimEnd('\')
$taskStem=switch ($Mod) {'Navigation' {'waypoint'} 'PotionShop' {'potion_shop'} 'RegionExplorer' {'region_explorer'} 'MapLight' {'map_light'} 'InventorySort' {'inventory_sort'} 'FastTravel' {'fast_travel'} 'BatchActions' {'batch_actions'} 'BossDraw' {'boss_draw'}}
$taskMarkerName=if ($Target -eq 'Formal') {'.navigation-installed.json'} else {'.navigation-sandbox.json'}

function Assert-PlainPath([string]$Path, [switch]$Required, [switch]$File) {
    $absolute=[IO.Path]::GetFullPath($Path)
    $probe=$absolute
    while ($probe) {
        if (Test-Path -LiteralPath $probe) {
            $item=Get-Item -LiteralPath $probe -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
                ($item.PSObject.Properties.Name -contains 'LinkType' -and $item.LinkType)) {
                throw "拒绝链接路径，未进行修改：$probe"
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

function Assert-GameStopped {
    foreach ($process in @(Get-CimInstance Win32_Process -Filter "Name='Cube.exe' OR Name='NavigationLauncher.exe' OR Name='Server.exe'")) {
        if (-not $process.ExecutablePath) {throw '无法核实正在运行的游戏或启动器路径，未进行修改。'}
        $directory=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($process.ExecutablePath)).TrimEnd('\')
        if ($directory.Equals($taskGame,[StringComparison]::OrdinalIgnoreCase)) {
            throw '目标游戏、服务器或启动器仍在运行，请完全退出后再管理 Mod。'
        }
    }
}

Assert-PlainPath $taskRoot -Required
if ($Target -eq 'Experiment' -and -not $taskGame.Equals((Join-Path $taskRoot 'playtest'),[StringComparison]::OrdinalIgnoreCase)) {
    throw '实验目标必须是当前项目的 playtest 目录。'
}
Assert-PlainPath $taskGame -Required
$taskExe=Join-Path $taskGame 'Cube.exe'
$taskLoader=Join-Path $taskGame 'cube_mod.dll'
$taskMarker=Join-Path $taskGame $taskMarkerName
$taskMods=Join-Path $taskGame 'mods'
$taskDll=Join-Path $taskMods ($taskStem+'.dll')
$taskConfig=Join-Path $taskGame 'mods.ini'
$taskBackupRoot=Join-Path $taskRoot 'backups'
foreach ($path in @($taskExe,$taskLoader,$taskMarker)) {Assert-PlainPath $path -Required -File}
Assert-PlainPath $taskMods -Required
foreach ($path in @($taskDll,$taskConfig)) {Assert-PlainPath $path -File}
Assert-PlainPath $taskBackupRoot
Assert-GameStopped
if ((Get-FileHash -LiteralPath $taskLoader -Algorithm SHA256).Hash -ne 'D729D26FF4F91EED453160FBE4D24D9430043AE6A7165CF3A3AF8A92C24B7A31') {
    throw '公共加载器不是当前已验证的兼容版本，未进行修改。'
}
if ($Action -eq 'Uninstall' -and $Mod -eq 'Navigation') {
    $taskLauncher=Join-Path $taskGame 'NavigationLauncher.exe'
    Assert-PlainPath $taskLauncher -Required -File
    if ((Get-FileHash -LiteralPath $taskLauncher -Algorithm SHA256).Hash -ne 'B57534B00C72A100743BA36799B39FA0C742246BBE2512CC562F85AD9FCBBE7C') {
        throw '当前启动器仍要求地图 DLL 或版本未知。请先更新共享启动器，或先使用 Disable；未进行修改。'
    }
}

$taskRecord=Get-Content -LiteralPath $taskMarker -Raw -Encoding UTF8 | ConvertFrom-Json
if ($Target -eq 'Formal') {
    if (-not $taskRecord.directory -or -not ([string]$taskRecord.directory).Equals($taskGame,[StringComparison]::OrdinalIgnoreCase)) {
        throw '正式安装标记中的目录不匹配，未进行修改。'
    }
    $taskGameHash=[string]$taskRecord.gameSha256
    $taskLoaderEntries=@($taskRecord.installedFiles | Where-Object {$_.path -eq 'cube_mod.dll'})
    if ($taskLoaderEntries.Count -ne 1 -or
        (Get-FileHash -LiteralPath $taskLoader -Algorithm SHA256).Hash -ne $taskLoaderEntries[0].sha256) {
        throw '公共加载器与正式安装标记不符，未进行修改。'
    }
} else {
    if (-not $taskRecord.original -or -not ([string]$taskRecord.original).Equals('C:\Games\CubeWorld',[StringComparison]::OrdinalIgnoreCase)) {
        throw '实验副本标记中的来源不匹配，未进行修改。'
    }
    $taskGameEntries=@($taskRecord.runtimeFiles | Where-Object {$_.name -eq 'Cube.exe'})
    if ($taskGameEntries.Count -ne 1) {throw '实验副本标记缺少唯一游戏记录，未进行修改。'}
    $taskGameHash=[string]$taskGameEntries[0].sha256
}
if ($taskGameHash -notmatch '^[0-9a-fA-F]{64}$' -or
    (Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash -ne $taskGameHash) {
    throw '游戏文件与安全标记不符，未进行修改。'
}

$taskDllExists=Test-Path -LiteralPath $taskDll -PathType Leaf
if ($Action -eq 'Enable' -and -not $taskDllExists) {throw "缺少 $taskStem.dll，不能启用。请先安装该 Mod。"}
$taskConfigExisted=Test-Path -LiteralPath $taskConfig -PathType Leaf
[byte[]]$taskBeforeBytes=@()
if ($taskConfigExisted) {$taskBeforeBytes=[IO.File]::ReadAllBytes($taskConfig)}
$taskEncoding=[Text.UTF8Encoding]::new($false,$true)
# The loader reads narrow UTF-8/ASCII lines. BOMs, UTF-16 and legacy code pages
# cannot be round-tripped safely here, so leave such files completely untouched.
if (($taskBeforeBytes.Length -ge 3 -and $taskBeforeBytes[0] -eq 0xef -and $taskBeforeBytes[1] -eq 0xbb -and $taskBeforeBytes[2] -eq 0xbf) -or
    ($taskBeforeBytes.Length -ge 2 -and (($taskBeforeBytes[0] -eq 0xff -and $taskBeforeBytes[1] -eq 0xfe) -or
                                      ($taskBeforeBytes[0] -eq 0xfe -and $taskBeforeBytes[1] -eq 0xff)))) {
    throw 'mods.ini 含 BOM 或使用 UTF-16；仅支持无 BOM 的 UTF-8/ASCII，原文件未修改。'
}
try {$taskText=$taskEncoding.GetString($taskBeforeBytes)} catch {throw 'mods.ini 不是有效的无 BOM UTF-8，原文件未修改。'}
if ($taskText.Contains([char]0)) {throw 'mods.ini 包含 NUL 字节或不支持的编码，原文件未修改。'}
$taskEnabled=if ($Action -eq 'Enable') {'1'} else {'0'}
$taskPattern='(?im)^(?<prefix>[ \t]*'+[regex]::Escape($taskStem)+ '\.enabled[ \t]*=[ \t]*)(?<value>[^\r\n]*?)(?<tail>[ \t]*)(?=\r?$)'
$taskMatches=[regex]::Matches($taskText,$taskPattern)
if ($taskMatches.Count -gt 1) {throw 'mods.ini 中所选 Mod 的 enabled 键重复，请先人工核对；原文件未修改。'}
if ($taskMatches.Count -eq 1) {
    $taskMatch=$taskMatches[0]
    $taskOldValue=$taskMatch.Groups['value'].Value
    if ($taskOldValue -notmatch '^(?i:0|1|true|false|yes|no|on|off)$') {
        throw '所选 Mod 的 enabled 值无法被加载器识别，原文件未修改。'
    }
    $taskWasEnabled=$taskOldValue -match '^(?i:1|true|yes|on)$'
    if ($taskWasEnabled -eq ($taskEnabled -eq '1')) {$taskNewText=$taskText}
    else {
        $taskValueGroup=$taskMatch.Groups['value']
        $taskNewText=$taskText.Substring(0,$taskValueGroup.Index)+$taskEnabled+$taskText.Substring($taskValueGroup.Index+$taskValueGroup.Length)
    }
} else {
    $taskNewline=if ($taskText.Contains("`r`n")) {"`r`n"} else {"`n"}
    $taskSeparator=if ($taskText.Length -and -not $taskText.EndsWith("`n")) {$taskNewline} else {''}
    $taskNewText=$taskText+$taskSeparator+$taskStem+'.enabled='+$taskEnabled+$taskNewline
}
$taskConfigChanged=$taskNewText -cne $taskText
$taskMoveDll=$Action -eq 'Uninstall' -and $taskDllExists
if (-not $taskConfigChanged -and -not $taskMoveDll) {
    Write-Output "$Mod 已处于所需状态；未修改文件。"
    return
}
$taskBackup=Join-Path $taskBackupRoot ('mod-'+$taskStem+'-'+$Action.ToLower()+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))
if (-not $taskBackup.StartsWith($taskBackupRoot+'\',[StringComparison]::OrdinalIgnoreCase)) {throw '备份路径超出项目。'}
Assert-PlainPath $taskBackup
if (Test-Path -LiteralPath $taskBackup) {throw '备份目录已存在，拒绝覆盖。'}
$taskDescription="保存独立备份并将 $taskStem.enabled 设为 $taskEnabled"
if ($taskMoveDll) {$taskDescription+='，将该 DLL 移到备份目录'}
if (-not $PSCmdlet.ShouldProcess($taskGame,$taskDescription)) {return}

# No target file is written before ShouldProcess. Recheck paths and processes
# immediately before committing the prepared, single-module change.
Assert-GameStopped
foreach ($path in @($taskConfig,$taskDll)) {Assert-PlainPath $path -File}
Assert-PlainPath $taskBackupRoot
if ((Test-Path -LiteralPath $taskConfig -PathType Leaf) -ne $taskConfigExisted) {throw 'mods.ini 在检查后发生变化，请重新执行。'}
if ($taskConfigExisted -and [Convert]::ToBase64String([IO.File]::ReadAllBytes($taskConfig)) -cne [Convert]::ToBase64String($taskBeforeBytes)) {
    throw 'mods.ini 在检查后发生变化，请重新执行。'
}
New-Item -ItemType Directory -Path $taskBackup | Out-Null
if ($taskConfigExisted) {
    $taskConfigBackup=Join-Path $taskBackup 'mods.ini'
    Copy-Item -LiteralPath $taskConfig -Destination $taskConfigBackup
    if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($taskConfigBackup)) -cne [Convert]::ToBase64String($taskBeforeBytes)) {
        throw 'mods.ini 备份校验失败，目标未修改。'
    }
}
$taskBackupRecord=@{mod=$Mod;action=$Action;target=$taskGame;modsIniExisted=$taskConfigExisted;created=(Get-Date).ToString('o');dataConfigAndSavesRetained=$true}
$taskBackupRecord | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskBackup 'management.json') -Encoding UTF8
Assert-GameStopped
if ($taskConfigChanged) {
    $taskStaged=Join-Path $taskBackup 'mods.ini.next'
    [IO.File]::WriteAllBytes($taskStaged,$taskEncoding.GetBytes($taskNewText))
    if ($taskConfigExisted) {[IO.File]::Replace($taskStaged,$taskConfig,[NullString]::Value)}
    else {[IO.File]::Move($taskStaged,$taskConfig)}
}
if ($taskMoveDll) {
    Assert-PlainPath $taskDll -Required -File
    Move-Item -LiteralPath $taskDll -Destination (Join-Path $taskBackup ($taskStem+'.dll'))
}
Write-Output "$Mod：$Action 完成。备份：$taskBackup"
Write-Output '仅管理所选 Mod；公共加载器、其他 Mod、data、各 Mod 的独立配置和角色存档均保留。重启游戏后生效。'
