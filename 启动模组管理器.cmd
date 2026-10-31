@echo off
setlocal
chcp 65001 >nul
if not exist "%~dp0build\bin\ModManager.exe" (
    echo 尚未找到模组管理器，请先运行项目中的 Build-ModManager.ps1 完成构建。
    pause
    exit /b 1
)
start "" /D "%~dp0." "%~dp0build\bin\ModManager.exe" --root "%~dp0."
exit /b 0
