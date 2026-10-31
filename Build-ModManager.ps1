$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
$taskSource = Join-Path $taskRoot 'mod_manager\ModManager.cs'
$taskIcon = Join-Path $taskRoot 'mod_manager\assets\launcher.ico'
$taskOutputDir = Join-Path $taskRoot 'build\bin'
$taskOutput = Join-Path $taskOutputDir 'ModManager.exe'

if (-not (Test-Path -LiteralPath $taskSource -PathType Leaf)) {
    throw '未找到 mod_manager\ModManager.cs，请先准备模组管理器源码。'
}
if (-not (Test-Path -LiteralPath $taskIcon -PathType Leaf)) {
    throw '未找到模组管理器图标 mod_manager\assets\launcher.ico。'
}

$taskCompiler = @(
    (Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'),
    (Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe')
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $taskCompiler) {
    throw '未找到 Windows 的 .NET Framework 4.x C# 编译器。'
}

New-Item -ItemType Directory -Path $taskOutputDir -Force | Out-Null
$taskCompilerArguments = @(
    '/nologo',
    '/target:winexe',
    '/platform:anycpu',
    '/optimize+',
    '/codepage:65001',
    ('/win32icon:' + $taskIcon),
    ('/resource:' + $taskIcon + ',CubeWorldModManager.LauncherIcon'),
    '/reference:System.dll',
    '/reference:System.Core.dll',
    '/reference:System.Windows.Forms.dll',
    '/reference:System.Drawing.dll',
    '/reference:System.Web.Extensions.dll',
    ('/out:' + $taskOutput),
    $taskSource
)
& $taskCompiler @taskCompilerArguments
if ($LASTEXITCODE -ne 0) { throw '模组管理器编译失败。' }
if (-not (Test-Path -LiteralPath $taskOutput -PathType Leaf)) {
    throw '编译器未生成模组管理器。'
}

$taskShortcutPath = Join-Path $taskRoot '魔方世界模组管理器.lnk'
$taskShell = New-Object -ComObject WScript.Shell
$taskShortcut = $taskShell.CreateShortcut($taskShortcutPath)
$taskShortcut.TargetPath = $taskOutput
$taskShortcut.Arguments = '--root "' + $taskRoot + '"'
$taskShortcut.WorkingDirectory = $taskRoot
$taskShortcut.Description = '独立启用或停用魔方世界 Mod，并启动所选游戏副本'
$taskShortcut.IconLocation = $taskOutput + ',0'
$taskShortcut.Save()

Write-Host ('编译完成：' + $taskOutput)
Write-Host ('双击入口：' + $taskShortcutPath)
