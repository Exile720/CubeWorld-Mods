# Mod 的独立安装与拆装

2026-10-30：首领抽奖0.1.7已独立备份更新实验副本，按原程序调用链修正死亡World接收对象校验，解决0.1.6仍拒绝真实首领的问题；沿用原管理器条目和独立开关，其他Mod、框架、存档与记录保留，见 修复记录 (local evidence omitted from source publication)。

2026-10-30：首领抽奖0.1.6已沿用原条目和开关独立备份更新实验副本，修复跨线程死亡观察漏记前提。存档、抽奖记录、其他Mod和公共框架保留，真实击杀待验收，见 记录 (local evidence omitted from source publication)。

2026-10-29：五项字体更新已沿用各自安装器、DLL与开关安装实验副本，不增加依赖或新条目；原字体文件、加载器、原数据和未选中的Mod保留，见 记录 (local evidence omitted from source publication)。

2026-10-29：首领抽奖0.1.5已沿用原开关更新实验副本，统一窗口与常规字重，仅影响本模组；原数据和存档保留，见 记录 (local evidence omitted from source publication)。

2026-10-29：首领抽奖 0.1.4 已沿用第八项开关更新实验副本，增加品质卡面、持续旋转和固定四周信息；已有抽奖数据与存档保留，管理器无需重建，见 记录 (local evidence omitted from source publication)。

2026-10-29：首领抽奖 0.1.3 已沿用第八项开关更新实验副本，只增加轻量翻牌展示；配置、已有抽奖数据及 CWBOSS01 格式保留，管理器无需重建。见 动画记录 (local evidence omitted from source publication)。

2026-10-29 新增独立功能：[首领抽奖](boss_draw/README.md)。`boss_draw.dll`、`boss_draw.enabled`、管理 ID `BossDraw`；F9 手动开奖，原版掉落保留。0.1.1 已修复重定位误判并备份更新实验副本，用户确认 F9 窗口可打开；真实新击杀登记和发奖仍待验收，旧版漏记没有自动补回。新增功能与其他七项各自启停，默认保留 `data/boss_draw/`；地图照明仍按原要求卸下。见 本次验证记录 (local evidence omitted from source publication)。

目标是让地图、药水、区域探索、地图照明、背包分类、旅行驿站、批量补给和首领抽奖可以独立使用、组合使用或全部停用。它们共用加载器，彼此没有必需依赖。

| 部分 | 文件 | 职责与边界 |
| --- | --- | --- |
| 公共框架 | `cube_mod.dll`、`NavigationLauncher.exe` | 加载与启动入口；拆掉单个功能时保留。启动器名称沿用已有入口。 |
| 首领抽奖 | `mods/boss_draw.dll` | 有效首领击杀额外一抽，F9 开奖，40/40/20 基础卡池和第四次职业小保底；单人使用，独立保存，详见本模块说明。 |
| 批量补给 | `mods/batch_actions.dll` | 点击空瓶或制作水瓶/生命药水后选择数量，A / D 每次减 / 加 10，Enter 确认；逐份按原价扣款、按原配方耗材。开关键 `batch_actions.enabled`，不新增数据文件。 |
| 旅行驿站 | `mods/fast_travel.dll` | 0.1.3 附近Boss在移动、交战时也自动观察并保留名字、职业，停稳后登记传送落点；F8传送，可手动登记、改名和补备注。仅单人，落点异常尝试返回。开关键 `fast_travel.enabled`，数据在 `data/fast_travel/`，不生成建筑。 |
| 地图导航 | `mods/waypoint.dll` | 标点、导航、城镇标签和店铺指南；自身数据在 `data/waypoint/`。 |
| 药水商店 | `mods/potion_shop.dll` | 出售最高可制作生命药水的上一档，随成长更新商品及价格，不依赖地图。 |
| 区域探索 | `mods/region_explorer.dll` | 0.1.5 在挑战页显示可靠关联的首领职业、有限生成预测或未知；保留巨树/巨岩译名、补字、难度色、留存状态和选点导航，不等于全世界预测或完整历史。自身目标在 `data/region_explorer/`。 |
| 地图照明 | `mods/map_light.dll` | 0.1.1 仅在夜间给完整 M 地图补光，白天保留原版；开关键 `map_light.enabled`，不改时间或生成运行数据。当前实验仍是 0.1.0，升级包待退出后安装。 |
| 背包分类 | `mods/inventory_sort.dll` | 胸甲、手套等独立分区并显示中文标题，标题避让物品详情与装备对比，取放可用且卖出后保留空位；保留原版格子尺寸、每行容量及自动换行。开关键 `inventory_sort.enabled`，不重排实际物品格。 |

八个功能分别构建为独立 DLL，拥有不同模块 ID。区域探索仅读取已生成的地图记录，不写游戏或揭示标志，不安装游戏逻辑钩子；与地图 0.3.6 通过可选消息共用导航目标，单独启用时提供自己的方向提示。地图兼容更新由 `Install-TownLabels.ps1` 处理，探索安装由 `Install-RegionExplorer.ps1` 处理，不互相捆绑覆盖。药水处理商店库存复制及价格；与其他改商店、改价格的 Mod 一起使用时仍需验证。地图照明由 `Install-MapLight.ps1` 安装，0.1.1 仅在夜间 M 打开时临时替换原生光照函数的传入参数，白天直接放行，不直接写游戏内存，不捆绑更新公共框架或其他 DLL；详情见 [地图照明](map_light/README.md)。背包分类由 `Install-InventorySort.ps1` 独立安装，只改变玩家背包显示和鼠标索引映射，不更改真实物品格、价格或存档；见 [背包分类](inventory_sort/README.md)。

旅行驿站由 `Install-FastTravel.ps1` 独立安装，到访自动记录，F8 回城或选择落脚点，Boss名字和职业仅保存可现场核实的信息；个人备注独立保留，只支持单人世界；见 [使用说明](fast_travel/README.md)。可以使用 `Manage-Mod.ps1 -Mod FastTravel -Action Disable/Enable/Uninstall` 管理，准备新副本也支持 `-Mods FastTravel`，原默认组件不变。

批量补给由 `Install-BatchActions.ps1` 独立安装，`Build-BatchActions.ps1` 构建检查。点击空瓶或制作水瓶/生命药水后弹出数量窗口，A / D 每次减 / 加 10，Enter 确认、Esc 关闭；显示原生 50 个携带上限，价格、材料和药水成长规则沿用原版。可在管理器切换“批量补给”，或使用 `Manage-Mod.ps1 -Mod BatchActions -Action Disable/Enable/Uninstall`，新副本支持 `-Mods BatchActions`，原默认选择不变。见 [使用与兼容说明](batch_actions/README.md)。

## 用窗口管理开关

双击项目根目录的 **魔方世界模组管理器.lnk**，或 **启动模组管理器.cmd**。

- 窗口可拖动边缘调整大小，标题栏可最大化或还原。F11 或右上角“全屏”切换全屏，Esc 返回；模组列表独立滚动，宽窗口自动分两列，文字与开关不随模组数量缩小。
- 默认显示实验副本；需要操作正式副本时，在窗口中明确选择“正式游玩”。
- 地图、药水、区域探索、地图照明、背包分类、旅行驿站、批量补给、首领抽奖各有独立的“开 / 关”。点击后直接保存，并在下次启动游戏时生效。
- 游戏或启动器尚未退出、目录不匹配、文件缺失或无法核实进程时，相关开关锁定并说明原因。
- 关闭功能保留 DLL、标点、独立配置和存档；更改前自动备份。“查看备份”可以打开备份目录。
- 调好开关后，点击右下角“启动游戏”，通过当前所选副本的公共启动器进入游戏。运行中或无法核实状态时锁定，避免重复启动；打开管理器不会自动启动游戏。
- 窗口不执行卸下、重装或更新公共框架。启动按钮沿用已安装且通过校验的启动器，不顺带替换它。

界面使用紧凑的深蓝列表、细线控件和原金发图标。仅主标题可读取本机已有游戏字体，列表和按钮使用系统中文字体；说明不再重复弹出悬停提示。没有把游戏资源打包进管理器。

维护者修改源码后运行 `Build-ModManager.ps1` 重新生成程序与本机快捷方式。程序在 `build/bin/ModManager.exe`；本机快捷方式和编译产物不纳入 Git。

## 新增 Mod 的默认交付要求

在这个项目的任意开发对话中，新增独立 Mod 都必须同时接入现有模组管理器。用户只需说明想要的功能，不需要再补充“接入管理器”。这项要求属于开发与交付流程，由负责该功能的 Agent 一起完成。

每个新 Mod 的交付至少包括：

1. 独立的 DLL、唯一标识、配置与数据边界，以及必要的构建和安装入口。
2. 管理器中的中文名称、简短说明、安装状态和独立开关。未安装、游戏运行中或校验失败时明确锁定，不把未知状态显示成已关闭。
3. 接通只读状态接口与现有单模块管理流程，保留其他 Mod 的配置键、文件和存档；更新副本准备的选项时保留原默认行为。
4. 重新构建管理器，确保用户原来的双击入口能管理新增功能，不让用户自己改配置、执行登记或另开一个管理工具。
5. 必要的独立启停、与现有 Mod 同用、备份和数据保留检查，以及使用说明；游戏内尚未验收的部分明确列为待验证。

未接入管理器时，只能报告实现进度，不能把新增 Mod 宣称为交付完成。只有用户明确要求仅研究、仅做原型或暂不接入时，才按该范围处理。更新已有 Mod 应沿用现有条目并核对兼容，不重复增加同一个功能的开关。

当前管理器仍按已登记的 Mod 列表工作；这项项目约定不代表程序会自动识别任意外来 DLL。新 Mod 的开发者负责在交付时完成登记、接线和重新构建，用户继续使用同一个管理器即可。实际安装到实验或正式游戏目录仍遵守对应目标的授权与备份要求。

## 准备新的实验副本

`Prepare-Playtest.ps1` 默认仍准备带地图的副本，也可以通过 `-Mods` 选择组件：

```powershell
# 只准备药水商店及公共框架，不要求地图 DLL
.\Prepare-Playtest.ps1 -Mods PotionShop

# 同时准备两个功能
.\Prepare-Playtest.ps1 -Mods Navigation,PotionShop

# 只准备区域探索，或在列表中追加它
.\Prepare-Playtest.ps1 -Mods RegionExplorer

# 只准备地图照明及公共框架
.\Prepare-Playtest.ps1 -Mods MapLight

# 只准备背包分类及公共框架
.\Prepare-Playtest.ps1 -Mods InventorySort

# 只准备公共框架，之后再安装需要的功能
.\Prepare-Playtest.ps1 -Mods @()
```

这只适用于尚未创建的 `playtest`。已有目录仍会拒绝覆盖，不能拿它重新准备正在使用的实验副本。

## 单个 Mod 的操作

以下命令只针对项目内的实验副本；执行前先正常退出该副本的游戏和启动器。可以在命令末尾加 `-WhatIf`，只查看拟进行的操作。

```powershell
# 停用地图，保留地图 DLL、标点与其他 Mod
.\Manage-Mod.ps1 -Mod Navigation -Action Disable

# 重新启用地图（要求地图 DLL 仍在）
.\Manage-Mod.ps1 -Mod Navigation -Action Enable

# 单独停用或重新启用药水
.\Manage-Mod.ps1 -Mod PotionShop -Action Disable
.\Manage-Mod.ps1 -Mod PotionShop -Action Enable

# 单独管理区域探索；停用保留目标和其他 Mod
.\Manage-Mod.ps1 -Mod RegionExplorer -Action Disable
.\Manage-Mod.ps1 -Mod RegionExplorer -Action Enable
.\Manage-Mod.ps1 -Mod RegionExplorer -Action Uninstall

# 单独管理地图照明；卸下只移动照明 DLL
.\Manage-Mod.ps1 -Mod MapLight -Action Disable
.\Manage-Mod.ps1 -Mod MapLight -Action Enable
.\Manage-Mod.ps1 -Mod MapLight -Action Uninstall

# 卸下药水：将它的 DLL 移入备份，并停用它的启动项
.\Manage-Mod.ps1 -Mod PotionShop -Action Uninstall

# 单独管理背包分类；卸下保留实际背包及全部存档
.\Manage-Mod.ps1 -Mod InventorySort -Action Disable
.\Manage-Mod.ps1 -Mod InventorySort -Action Enable
.\Manage-Mod.ps1 -Mod InventorySort -Action Uninstall

# 卸下地图前，必须先具备支持独立 Mod 的公共启动器
.\Manage-Mod.ps1 -Mod Navigation -Action Uninstall
```

停用只修改选定模块的 `mods.ini` 开关键；卸下再将它自己的 DLL 移入项目 `backups/`。其他 Mod 的开关、错误计数、DLL 及数据不随之更改。默认保留选定 Mod 的配置、数据，以及全部角色和世界存档。

如果模块已经卸下，重新使用时应先通过对应安装流程恢复 DLL，再启用；`Enable` 不会从未知备份自动恢复代码。

脚本也提供显式的 `-Target Formal`，但只有用户明确要求操作正式副本时才能使用。它不会自动启动游戏。

## 新旧启动器的过渡

旧版 `NavigationLauncher.exe` 会检查 `waypoint.dll` 是否存在。新源码已解除这个要求，药水安装脚本也不再要求地图 DLL 必须存在。

本次新启动器仅编译至 `build/bin/NavigationLauncher.exe`，没有替换实验或正式目录中的程序。因此，现有副本可以先通过 `Disable` 单独停用地图；在公共启动器升级并核对前，管理脚本会拒绝直接卸下地图，避免破坏药水的启动入口。

升级公共启动器与安装功能 Mod 是两个操作。不能因为更新药水就替换公共启动器或地图 DLL，也不能删除公共框架来卸下某个功能。

## 兼容与验收

- 背包分类只临时替换玩家背包的显示视图，其他 Mod 的挂接点不同。五项功能的 32 种开关组合做离线状态核对；真实背包中的悬停、取放、装备、连续卖出、保存重开及各组合仍需游戏内验收。见 [背包分类](inventory_sort/README.md)。
- 拆成不同 DLL 能减少耦合，不能保证任意 Mod 都没有冲突；尤其是两个 Mod 都改变商店价格或同一批商品时，必须核对规则如何组合。
- 管理脚本在隔离的小型目录中回归验证配置修改、备份和拆装保护；这不代表已经验证所有游戏内行为。
- 游戏内仍需分别验收：仅地图、仅药水、两者同时、全部停用，以及卸下一项后其余功能仍能启动。
- 区域探索另需验收独立运行、与地图共用箭头、与药水同时运行及停用后其他功能保留。八种配置组合已做离线状态检查，不能替代真实游戏内组合验收。
- 地图照明另需在夜间验收开 M 地图更亮、关图后夜景与时间正常，及独立运行、同其他功能一起运行、停用和卸下后的表现。四个功能的十六种开关配置做离线状态核对；原生光照钩子与现有药水钩子地址不同，实际渲染兼容仍待验收。
- 药水使用原版物品类型。停用或卸下商店功能不是回滚此前的交易；已购买物品能否继续正确使用，仍需在实验副本确认。
- Git 保存代码版本；不同对话或 Git Worktree 的开发隔离，不能代替游戏运行时的 Mod 兼容设计。

2026-10-23 离线验证：启动器 Release 编译、脚本语法检查、5 种副本准备场景及 41 项单模块管理回归通过。管理回归仅在项目 `build/` 下的隔离目录执行；受当前沙箱的 CIM 访问限制，进程分支使用模拟清单覆盖，另用实际进程列表确认隔离副本没有运行游戏。没有对真实实验副本或正式目录执行拆装，也没有完成游戏内组合验收。

首领抽奖由 `Install-BossDraw.ps1` 独立安装，`Build-BossDraw.ps1` 构建检查。`Manage-Mod.ps1 -Mod BossDraw -Action Disable/Enable/Uninstall` 管理单项；准备新副本可用 `-Mods BossDraw`，不覆盖已有实验目录。卸下只移动本 DLL 到备份，抽奖记录和已正常获得的装备保留。
