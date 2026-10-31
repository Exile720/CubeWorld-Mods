# CubeWorld-Mods

《魔方世界》2013 Alpha 独立模组和 Windows 管理器源码，包含公共启动器。

**当前为开发版源码快照。** 功能可以分别构建和管理，管理器为各模组提供独立开关。公开副本中的安装路径已统一为示例目录，仍保留严格的文件散列校验，尚未改为面向所有玩家的通用安装工具。当前不提供通用安装包。

## 功能

| Mod | 当前源码版本 | 功能 | DLL |
| --- | --- | --- | --- |
| [地图导航](navigation/README.md) | 0.3.8 | 地图标点、方向距离、城镇店名与店铺指南 | `waypoint.dll` |
| [药水商店](potion_shop/README.md) | 0.2.0 | 随角色已解锁配方调整出售的生命药水档位 | `potion_shop.dll` |
| [区域探索](region_explorer/README.md) | 0.1.6 | 周边已加载城镇、地标、挑战及有限首领职业信息 | `region_explorer.dll` |
| [地图照明](map_light/README.md) | 0.1.1 | 夜间完整大地图补光 | `map_light.dll` |
| [背包分类](inventory_sort/README.md) | 0.1.7 | 装备分类显示、中文分区与空位保留 | `inventory_sort.dll` |
| [旅行驿站](fast_travel/README.md) | 0.1.4 | 到访记录、地点名称与备注，F8 选择落脚点 | `fast_travel.dll` |
| [批量补给](batch_actions/README.md) | 0.1.2 | 空瓶购买、水瓶及生命药水制作数量选择 | `batch_actions.dll` |
| [首领抽奖](boss_draw/README.md) | 0.1.7 | 保留原掉落，参与首领击杀后额外抽奖，F9 手动开奖 | `boss_draw.dll` |

[模组管理器](mod_manager/README.md)使用 C# Windows Forms，负责查看已登记模组、独立启停、备份入口及调用启动器。它本身不注入 DLL，也不自动识别任意社区 Mod。

## 架构与兼容范围

公共部分使用 [Qube-Loader](https://github.com/qad3n/Qube-Loader) 及本项目的 `NavigationLauncher.exe`。功能各自为独立 DLL；其中一个停用不应成为其他功能的必要条件。Qube-Loader、Dear ImGui 和 MinHook 的固定版本源码已随仓库保留，含原许可及本地改动记录。

只针对已核实的 Windows 32 位 **2013 Alpha** 客户端开发。实际客户端 SHA-256：

```text
84A7A132A84D4282338E7EA45A1940D32066D64E39CBAFED8F2418D5A6DC30BF
```

不适用于 2019 Steam 版或 Omega。未验证的客户端、加载器和函数冲突会被拒绝。没有承诺经典 Mod Launcher 的 DLL 或其他社区 Mod 可以直接放入本框架。

## 构建与使用

见 [构建说明与当前限制](BUILDING.md)。需要 Windows、Visual Studio 2022 C++ 桌面开发工具和 CMake；管理器使用 Windows 自带的 .NET Framework C# 编译器。

源码仓库不包含游戏本体、游戏资源、字体、存档、玩家地点、现场内存数据、私有日志或预编译 DLL/EXE。模块在运行时读取玩家已安装的游戏资源或系统字体。编译产物应另放 GitHub Releases，并与对应源码版本匹配。

首领抽奖 0.1.7 的真实新击杀登记、领奖与保存重开，以及全部模组的真实组合运行仍有待验收；离线检查通过不等于这些项目已经完成。各模块说明保留必要的技术历史，个人角色、世界、补偿和安装记录不公开；目录仅为示例。

## 目录

```text
navigation/ potion_shop/ region_explorer/ map_light/
inventory_sort/ fast_travel/ batch_actions/ boss_draw/
mod_manager/             Windows 管理器
external/Qube-Loader/    构建所需的固定版本依赖及许可
research/               预览、检查工具和必要研究说明
releases/               历史版本文字说明、安装校验元数据与许可
Build-*.ps1             维护者现有构建入口
Install-*.ps1           维护者现有安装入口，依赖本机目录及发布包
Manage-Mod.ps1          单模块启停、备份与卸下入口
SOURCE_SNAPSHOT.json    本次工作区快照及依赖版本
```

## 许可与来源

本仓库自有程序源码按 [GNU GPLv3](LICENSE) 发布；第三方组件保留各自许可和署名，见 [第三方来源](THIRD_PARTY.md)。游戏本体、资源及商标属于其各自权利人，不在本仓库的许可范围内。本项目为非官方社区项目。
