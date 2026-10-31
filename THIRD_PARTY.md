# 第三方来源与许可

本次源码发布整理日期：2026-10-31。固定版本以 `SOURCE_SNAPSHOT.json` 为准。

| 组件 | 上游与固定版本 | 随附许可 / 处理 |
| --- | --- | --- |
| Qube-Loader | [qad3n/Qube-Loader](https://github.com/qad3n/Qube-Loader)，`4c3dad5a4665ffcc40117aef7fe6da6fdea61ca9` | [GNU GPLv3](external/Qube-Loader/LICENSE)。保留构建所需源码，两处已有本地改动已包含，见 [修改记录](external/Qube-Loader/LOCAL_CHANGES.md)。 |
| Dear ImGui | [ocornut/imgui](https://github.com/ocornut/imgui)，`8936b58fe26e8c3da834b8f60b06511d537b4c63` | [MIT](external/Qube-Loader/modloader/sdk/imgui/LICENSE.txt)。保留 core 及 Win32/DX9 后端，不分发示例字体。 |
| MinHook | [TsudaKageyu/minhook](https://github.com/TsudaKageyu/minhook)，`c3fcafdc10146beb5919319d0683e44e3c30d537` | [原 BSD 风格许可及附带署名](external/Qube-Loader/modloader/include/minhook/LICENSE.txt)。 |
| RegionReveal | [Jovinull/RegionReveal](https://github.com/Jovinull/RegionReveal/tree/8633915f10b6ad565a31fc653d4f7e3f3b2c2484) | 参考地图布局与分析资料，保留 [MIT 许可](region_explorer/RegionReveal-LICENSE.txt)，细节见 `region_explorer/SOURCES.md`。未安装其 dinput8 代理或揭示补丁。 |

## 仅供研究参考的上游

- [CubeWorld-Reversal](https://github.com/qad3n/CubeWorld-Reversal)：用于辅助理解布局、调用与偏移，并与本地客户端指令交叉核对。未将该恢复源码项目整体复制进公开仓库或作为构建依赖。
- [Synap5e / Trader Fast Travel](https://github.com/synap5e/cubeworld-trader-fast-travel)，研究提交 `27e57f113b0aa1bbb4c78c219a5ab6fac8c6a12c`：参考旅行流程与坐标处理。当地开发记录没有确认上游开源许可，因此不随仓库复制或重许可该项目原始源码/二进制。当前实现边界见 `fast_travel/SOURCES.md`，不要把其原始研究副本加入发布范围。

## 图标、字体与游戏资源

- 管理器头像是本项目生成的体素冒险者图标，保留生成说明、参考来源与 PNG/ICO；见 [图标说明](mod_manager/assets/README.md)。生成图像不作为第三方程序代码重新许可。
- Picroma 原始参考图片未随源码发布。游戏本体、资源包、词表、系统字体均不打包。代码按需从用户本机读取资源。
- 模组说明中的游戏名称、商标和相关权利归其各自权利人；本项目不代表 Picroma。
