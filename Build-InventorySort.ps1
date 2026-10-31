$ErrorActionPreference='Stop'
$taskCmake='C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $taskCmake -S $PSScriptRoot -B (Join-Path $PSScriptRoot 'build') -G 'Visual Studio 17 2022' -A Win32 -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
if($LASTEXITCODE -ne 0){throw '配置失败。'}
& $taskCmake --build (Join-Path $PSScriptRoot 'build') --config Release --target inventory_sort inventory_module_checks --parallel 1 -- /nodeReuse:false
if($LASTEXITCODE -ne 0){throw '编译失败。'}
foreach($taskMode in @('','unsupported','partial','overlay')){
    $taskArguments=@((Join-Path $PSScriptRoot 'build\bin\inventory_sort.dll'))
    if($taskMode){$taskArguments+=$taskMode}
    & (Join-Path $PSScriptRoot 'build\bin\inventory_module_checks.exe') @taskArguments
    if($LASTEXITCODE -ne 0){throw '背包分类检查失败。'}
}
