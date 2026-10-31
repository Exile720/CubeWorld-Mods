# 构建与当前限制

## 环境

- Windows，Visual Studio 2022，安装“使用 C++ 的桌面开发”、MSVC x86 编译工具及 Windows SDK。
- CMake 3.20 或更高。项目使用 Win32；不支持把游戏 Mod 编译成 x64。
- 管理器使用 Windows 自带 .NET Framework 4.x 的 C# 编译器，不使用 npm。
- 管理器当前优先使用 PowerShell 7；找不到时回退 Windows PowerShell 5.1 只读查询并锁定写操作。

依赖源码已随仓库放在 `external/Qube-Loader/`。本仓库使用普通文件保存固定版本的构建依赖，不需要运行子模块下载命令；请保留该目录中的许可证和 `LOCAL_CHANGES` 记录。

## C++ 构建

在仓库根目录、能够找到 CMake 的 PowerShell 中运行：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32 -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
cmake --build build --config Release --parallel 1 -- /nodeReuse:false
ctest --test-dir build -C Release --output-on-failure -E "^inventory_module_checks$"
```

产物在 `build/bin/`。可通过 `--target waypoint`、`--target potion_shop` 等分别构建单个 Mod。公共框架和启动器目标为 `cube_mod`、`NavigationLauncher`。

`inventory_module_checks` 的正常模式会读取维护者本机采集的四份背包二进制样本。这些样本未公开，因此上述命令明确排除这一项现场回放；其源码仍保留，其他不需要样本的背包异常模式照常运行。不要把这项排除描述为该回放已经通过。

仓库中的 `Build-*.ps1` 保留原构建流程，CMake 路径已替换为 Visual Studio 常见安装目录示例。其他电脑优先使用上面的标准 CMake 命令，不要直接照搬示例绝对路径。

## 管理器构建

```powershell
.\Build-ModManager.ps1
```

生成 `build/bin/ModManager.exe` 与仓库内的本机快捷方式。构建不会启动游戏。

## 安装流程尚未通用化

公开副本中，管理与安装脚本的游戏目录统一为示例 `C:\Games\CubeWorld`，项目路径示例为 `C:\Projects\CubeWorld-Mods`，均不代表维护者真实目录。实验副本为仓库下 `playtest/`；实验标记的来源路径仍会严格检查。原安装器读取 `releases/` 中固定版本的 DLL 及散列，而源码仓库只包含文字说明与必要元数据，没有这些预编译文件。

自行重新编译的加载器或启动器未必与现有写死的 SHA-256 相同，管理器可能据此锁定。**不要删除散列检查、忽略冲突或开启版本不匹配开关来运行。** 对外安装包需要另行完成路径配置、可信构建清单和安装验收，再发布匹配的二进制文件。

因此，这次源码发布供阅读、研究、构建及离线检查。不是“下载后在任意电脑双击即可安装”的成品承诺，也不要求将游戏放到维护者相同的磁盘路径。

模块说明保留必要的技术历史，具体个人游玩与安装记录已移除。历史包的示例路径和本地验证记录不代表公开仓库里已有对应附件；安装状态以实际文件和安全检查为准。
