[CmdletBinding()]
param([ValidateSet('Experiment','Formal')][string]$Target='Experiment')
$ErrorActionPreference='Stop'

# Re-read state on every click: the GUI's previous result may be stale.
# The existing launcher owns guarded game creation and duplicate prevention.
$taskState=& (Join-Path $PSScriptRoot 'Get-ModManagerState.ps1') -Target $Target | ConvertFrom-Json
if ($taskState.target -ne $Target -or -not $taskState.canLaunch) {
    $reason=if ($taskState.launchReason) {$taskState.launchReason} else {'当前不能启动游戏，请刷新状态后核对。'}
    throw $reason
}
$taskLauncher=Join-Path $taskState.gameDirectory 'NavigationLauncher.exe'
$taskProcess=Start-Process -FilePath $taskLauncher -WorkingDirectory $taskState.gameDirectory -PassThru -ErrorAction Stop
if (-not $taskProcess -or $taskProcess.Id -le 0) {throw '未能交给游戏启动器，请刷新后重试。'}
# Dispatch success is not proof that the game reached its main menu. The
# launcher's own error window remains visible if game startup later fails.
[ordered]@{requested=$true;target=$Target;processId=$taskProcess.Id} | ConvertTo-Json -Compress
