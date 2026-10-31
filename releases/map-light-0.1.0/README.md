# 地图照明 0.1.0

让按 **M** 打开的大地图使用固定光照，方便夜间查看地形、城镇和挑战地点。关闭大地图后，下一帧继续使用原版夜景。初版亮度尚待游戏内验收。

这是独立模组 `mods/map_light.dll`，管理 ID `MapLight`，模块 ID `local.cubeworld.map_light`。不依赖地图导航、区域探索或药水商店，可以分别启停。共用已有 Qube Loader 和启动器；安装不替换它们。

## 使用

正常退出实验游戏和启动器后，在原来的“魔方世界模组管理器”中选择“实验副本”，打开“地图照明”。由玩家点击“启动游戏”，进入角色和世界后按 M。关闭该开关可在下次启动时恢复原版地图光照。

模组没有游戏内面板或新快捷键，不改地图操作、时间、灯笼、物品、怪物、存档及其他模组数据，也没有永久修改游戏程序。它只在打开大地图时，让原版渲染函数读取本模组的固定颜色参数。小地图和正常走路画面继续读取原版参数。

## 构建、安装与卸下

```powershell
.\Build-MapLight.ps1
.\Install-MapLight.ps1 -ValidateOnly
.\Install-MapLight.ps1
.\Manage-Mod.ps1 -Mod MapLight -Action Disable
.\Manage-Mod.ps1 -Mod MapLight -Action Enable
.\Manage-Mod.ps1 -Mod MapLight -Action Uninstall
```

默认仅操作项目 `playtest` 实验副本，安装前验证游戏和加载器、确认已退出、备份完整 Save 并核对其他文件。安装只增加本模组 DLL 和安装记录，保留 `mods.ini` 原字节；现有 `map_light.enabled=0` 也保留。独立开关键是 `map_light.enabled`，缺失时按加载器规则默认为启用；不重置错误计数。没有新增运行时数据文件。未来或既有的 `config/map_light.ini`、`data/map_light/` 由本模组保留。

停用保留 DLL；卸下将本 DLL 移入项目备份并仅停用本开关，保留其他模组、配置和存档。正式目录另需用户明确授权。禁止运行中替换、停用或卸下，也不会自动启动游戏。

## 实现与验证边界

仅支持已核实的 2013 Alpha 客户端。加载时核对 PE 标识、原生光照函数入口、栈清理和实际调用点；发现未知版本或入口已被其他钩子更改时拒绝接入。SDK 需要 RawMem、RawHooks、Writes 权限，Writes 仅用于临时改传入指针，没有直接写游戏内存。固定光照来自原版大地图地标绘制参数；证据见 [SOURCES.md](SOURCES.md)。

离线检查执行实际 DLL 并注入合成 SDK，验证开图/关图、独立标识、参数权限、异常指针、未知版本、停用清理及无直接内存写入；没有运行原生游戏或 GPU 绘制。管理器、安装及拆装检查另有隔离目录。真实夜间亮度、关图后夜景、同其他模组一起运行的效果仍需玩家验收，记录见 验证记录（本地验证记录未随源码发布）。

本项目使用 Qube Loader SDK，许可证随发布包保留为 GPL-3.0；未分发原游戏资源或程序。
