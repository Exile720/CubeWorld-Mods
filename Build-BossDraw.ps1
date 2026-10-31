$ErrorActionPreference='Stop'
$taskCmake='C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $taskCmake -S $PSScriptRoot -B (Join-Path $PSScriptRoot 'build') -G 'Visual Studio 17 2022' -A Win32 -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
if($LASTEXITCODE -ne 0){throw '配置失败。'}
& $taskCmake --build (Join-Path $PSScriptRoot 'build') --config Release --target boss_draw boss_draw_checks boss_draw_module_checks boss_draw_runtime_checks --parallel 1 -- /nodeReuse:false
if($LASTEXITCODE -ne 0){throw '首领抽奖编译失败。'}
& (Join-Path $PSScriptRoot 'build\bin\boss_draw_checks.exe')
if($LASTEXITCODE -ne 0){throw '首领抽奖卡池及保存检查失败。'}
foreach($taskMode in @('normal','unsupported','menu','event','hook','storage','conflict','relocated_low','relocated_high','bad_handler')){
    & (Join-Path $PSScriptRoot 'build\bin\boss_draw_module_checks.exe') (Join-Path $PSScriptRoot 'build\bin\boss_draw.dll') $taskMode
    if($LASTEXITCODE -ne 0){throw "首领抽奖模块检查失败：$taskMode"}
}
& (Join-Path $PSScriptRoot 'build\bin\boss_draw_runtime_checks.exe')
if($LASTEXITCODE -ne 0){throw '首领抽奖领奖路径检查失败。'}
