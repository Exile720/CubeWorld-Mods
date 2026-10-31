$ErrorActionPreference = 'Stop'
$taskRoot = [System.IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$taskSourceGame = 'C:\Games\CubeWorld'
$taskSource = Join-Path $taskSourceGame 'Save'
$taskExperiment = Join-Path $taskRoot 'playtest'
$taskDestination = Join-Path $taskExperiment 'Save'
foreach ($taskProcess in @(Get-CimInstance Win32_Process -Filter "Name='Cube.exe' OR Name='Server.exe' OR Name='NavigationLauncher.exe'")) {
    if (-not $taskProcess.ExecutablePath) { throw '无法确认正在运行的程序路径，停止复制。' }
    $taskProcessDirectory=[System.IO.Path]::GetDirectoryName($taskProcess.ExecutablePath)
    if ($taskProcessDirectory.Equals($taskSourceGame,[System.StringComparison]::OrdinalIgnoreCase) -or
        $taskProcessDirectory.Equals($taskExperiment,[System.StringComparison]::OrdinalIgnoreCase)) {
        throw '正式版或实验版仍在运行，请正常退出后再复制。'
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $taskExperiment '.navigation-sandbox.json'))) { throw '缺少独立实验副本标记。' }
if (-not (Test-Path -LiteralPath $taskDestination -PathType Container)) { throw '未找到实验版 Save，停止以避免误操作。' }
if (@(Get-ChildItem -LiteralPath $taskSource -Recurse -File | Where-Object Name -Match '-(wal|shm|journal)$').Count) {
    throw '原存档仍存在数据库事务侧文件，请先确认游戏已完全正常退出。'
}
$taskBackup = Join-Path $taskRoot ('backups\formal-save-import-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$taskStage = Join-Path $taskBackup 'incoming-Save'
$taskOldSave = Join-Path $taskBackup 'previous-playtest-Save'
# Verify absolute paths before moving either directory. No delete operation is used.
foreach ($taskPath in @($taskDestination,$taskStage,$taskOldSave)) {
    $taskResolved=[System.IO.Path]::GetFullPath($taskPath)
    if (-not $taskResolved.StartsWith($taskRoot+'\',[System.StringComparison]::OrdinalIgnoreCase)) { throw "目标超出工作区：$taskResolved" }
}
if (Test-Path -LiteralPath $taskBackup) { throw '备份目录已存在，拒绝覆盖。' }
New-Item -ItemType Directory -Path $taskBackup | Out-Null
Copy-Item -LiteralPath $taskSource -Destination $taskStage -Recurse
$taskCheckScript = Join-Path $taskRoot 'research\check_save_copy.py'
$taskCheck = & python $taskCheckScript $taskSource $taskStage
if ($LASTEXITCODE -ne 0) { throw '暂存存档校验失败，实验版原存档保持原位。' }
$taskCheck | Set-Content -LiteralPath (Join-Path $taskBackup 'source-snapshot-validation.json') -Encoding utf8
# Keep a separately recoverable copy of the old experimental marker files too.
$taskOldMarkers = Join-Path $taskExperiment 'data\waypoint'
if (Test-Path -LiteralPath $taskOldMarkers) { Copy-Item -LiteralPath $taskOldMarkers -Destination (Join-Path $taskBackup 'previous-playtest-waypoint') -Recurse }
Move-Item -LiteralPath $taskDestination -Destination $taskOldSave
try {
    Move-Item -LiteralPath $taskStage -Destination $taskDestination
} catch {
    if (-not (Test-Path -LiteralPath $taskDestination)) { Move-Item -LiteralPath $taskOldSave -Destination $taskDestination }
    throw
}
$taskFinal = & python $taskCheckScript $taskSource $taskDestination
if ($LASTEXITCODE -ne 0) { throw "最终校验失败。此前实验存档保存在 $taskOldSave" }
$taskRecord = @{copied=(Get-Date).ToString('o');source=$taskSource;destination=$taskDestination;backup=$taskBackup;
    originalSaveWritten=$false;gameLaunched=$false;validation=($taskFinal -join "`n" | ConvertFrom-Json)}
$taskRecord | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskBackup 'copy-result.json') -Encoding utf8
$taskRecord | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $taskRoot 'research\evidence\formal-save-to-playtest.json') -Encoding utf8
Write-Output '正式角色及世界存档已完整复制到实验版，逐文件内容一致，数据库检查通过。'
Write-Output "旧实验存档备份：$taskOldSave"
Write-Output '正式存档未写入，未启动游戏。'
