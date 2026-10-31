$ErrorActionPreference='Stop'
$taskRoot=$PSScriptRoot
$taskCmake='C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $taskCmake -S $taskRoot -B (Join-Path $taskRoot 'build') -G 'Visual Studio 17 2022' -A Win32 -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
if ($LASTEXITCODE -ne 0) {throw '配置失败。'}
& $taskCmake --build (Join-Path $taskRoot 'build') --config Release --target region_explorer explorer_checks explorer_module_checks waypoint town_checks --parallel 1 -- /nodeReuse:false
if ($LASTEXITCODE -ne 0) {throw '编译失败。'}
& (Join-Path $taskRoot 'build\bin\explorer_checks.exe')
if ($LASTEXITCODE -ne 0) {throw '区域探索检查失败。'}
& (Join-Path $taskRoot 'build\bin\town_checks.exe')
if ($LASTEXITCODE -ne 0) {throw '地图回归检查失败。'}
& (Join-Path $taskRoot 'build\bin\explorer_module_checks.exe') (Join-Path $taskRoot 'build\bin\waypoint.dll') (Join-Path $taskRoot 'build\bin\region_explorer.dll')
if ($LASTEXITCODE -ne 0) {throw '模组交接检查失败。'}
