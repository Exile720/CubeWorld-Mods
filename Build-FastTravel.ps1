$ErrorActionPreference='Stop'
$taskCmake='C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $taskCmake -S $PSScriptRoot -B (Join-Path $PSScriptRoot 'build') -G 'Visual Studio 17 2022' -A Win32 -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
if($LASTEXITCODE -ne 0){throw '配置失败。'}
& $taskCmake --build (Join-Path $PSScriptRoot 'build') --config Release --target fast_travel travel_checks --parallel 1 -- /nodeReuse:false
if($LASTEXITCODE -ne 0){throw '编译失败。'}
& (Join-Path $PSScriptRoot 'build\bin\travel_checks.exe')
if($LASTEXITCODE -ne 0){throw '旅行驿站检查失败。'}
& (Join-Path $PSScriptRoot 'build\bin\travel_checks.exe') unsupported
if($LASTEXITCODE -ne 0){throw '旅行驿站版本保护检查失败。'}

foreach($taskMode in @('events','overlay')) {
    & (Join-Path $PSScriptRoot 'build\bin\travel_checks.exe') $taskMode
    if($LASTEXITCODE -ne 0){throw '旅行驿站初始化保护检查失败。'}
}
