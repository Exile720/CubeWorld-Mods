$ErrorActionPreference='Stop'
$taskCmake='C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $taskCmake -S $PSScriptRoot -B (Join-Path $PSScriptRoot 'build') -G 'Visual Studio 17 2022' -A Win32 -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
if($LASTEXITCODE -ne 0){throw '配置失败。'}
& $taskCmake --build (Join-Path $PSScriptRoot 'build') --config Release --target map_light map_light_checks --parallel 1 -- /nodeReuse:false
if($LASTEXITCODE -ne 0){throw '编译失败。'}
foreach($taskMode in @('','unsupported')){
    $taskArguments=@((Join-Path $PSScriptRoot 'build\bin\map_light.dll'))
    if($taskMode){$taskArguments+=$taskMode}
    & (Join-Path $PSScriptRoot 'build\bin\map_light_checks.exe') @taskArguments
    if($LASTEXITCODE -ne 0){throw '地图照明检查失败。'}
}
