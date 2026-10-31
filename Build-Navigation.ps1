$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
$taskCmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $taskCmake)) { throw '未找到本机 CMake，请修改脚本中的工具路径。' }
& $taskCmake -S $taskRoot -B (Join-Path $taskRoot 'build') -G 'Visual Studio 17 2022' -A Win32 -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
if ($LASTEXITCODE -ne 0) { throw '配置失败。' }
& $taskCmake --build (Join-Path $taskRoot 'build') --config Release --target cube_mod waypoint NavigationLauncher navigation_checks town_checks --parallel 1 -- /nodeReuse:false
if ($LASTEXITCODE -ne 0) { throw '编译失败。' }
& (Join-Path $taskRoot 'build\bin\navigation_checks.exe')
if ($LASTEXITCODE -ne 0) { throw '导航检查失败。' }
& (Join-Path $taskRoot 'build\bin\town_checks.exe')
if ($LASTEXITCODE -ne 0) { throw '城镇标签检查失败。' }
