$ErrorActionPreference='Stop'
$taskCmake='C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $taskCmake -S $PSScriptRoot -B (Join-Path $PSScriptRoot 'build') -G 'Visual Studio 17 2022' -A Win32 -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
if($LASTEXITCODE -ne 0){throw '配置失败。'}
& $taskCmake --build (Join-Path $PSScriptRoot 'build') --config Release --target batch_actions batch_checks batch_module_checks batch_runtime_checks --parallel 1 -- /nodeReuse:false
if($LASTEXITCODE -ne 0){throw '批量补给编译失败。'}
& (Join-Path $PSScriptRoot 'build\bin\batch_checks.exe')
if($LASTEXITCODE -ne 0){throw '批量购买制作检查失败。'}
foreach($taskMode in @('normal','unsupported','partial','conflict','overlay','events')) {
    & (Join-Path $PSScriptRoot 'build\bin\batch_module_checks.exe') (Join-Path $PSScriptRoot 'build\bin\batch_actions.dll') $taskMode
    if($LASTEXITCODE -ne 0){throw "批量补给模块检查失败：$taskMode"}
}
& (Join-Path $PSScriptRoot 'build\bin\batch_runtime_checks.exe')
if($LASTEXITCODE -ne 0){throw '批量补给数量窗口及原生调用路径检查失败。'}
