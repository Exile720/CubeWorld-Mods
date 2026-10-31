# 区域探索的来源与可核验边界

核验时间：2026-10-24。仅支持本机已验证的 2013 Alpha：`Cube.exe` SHA-256 `84A7A132A84D4282338E7EA45A1940D32066D64E39CBAFED8F2418D5A6DC30BF`。

- [RegionReveal](https://github.com/Jovinull/RegionReveal/tree/8633915f10b6ad565a31fc653d4f7e3f3b2c2484)，固定提交 `8633915f10b6ad565a31fc653d4f7e3f3b2c2484`。参考其 `docs/MAP_LABELS.md`、`docs/REVERSE_ENGINEERING.md`、`docs/BEHAVIOUR.md`、`src/game/cube_world.hpp`、`src/game/world.cpp` 和 `src/region_reveal/marks.cpp` 所整理的本版本地图布局、原生标签分类及任务状态。保留原 MIT 许可于 `RegionReveal-LICENSE.txt`。本模块自行实现 Qube SDK 只读扫描、列表、目标存储和可选导航消息；没有安装该项目的 dinput8 代理或原生补丁，也未调用它的区域揭示函数。
- [CubeWorld-Reversal](https://github.com/qad3n/CubeWorld-Reversal)：本地 `external/CubeWorld-Reversal/cube/control/GameController.cpp` 的 `NameGen_initFirstNameTables`（原地址 0x5a0ed0）及区域生成调用；本地 `cube/_library/crt_stl.cpp` 的地图序列化函数（原地址 0x605420）。地图保存结构与任务实时数据不同，不能把存档中缺失任务字段理解成没有 Boss。
- [Qube-Loader](https://github.com/qad3n/Qube-Loader)：沿用项目现有 SDK、ImGui 和公共加载器，不改变公共框架。其 GPL-3.0 许可随项目保留。
- 本机只读证据：`research/evidence/explorer-name-layout.json` 核对实际可执行文件中的 40 段 UTF-16 名字音节；`explorer-saved-records.json` 核对两份实际地图、761 条地标及其中 24 条城镇。测试记录只重建存档已具有的字段，不补造实时任务状态。

WorldMap 为控制器内嵌对象 `gc+0x800d44`，稀疏块表为 `+0xb0`；每块地标区偏移 0x34000，共 64 条、每条 0x68 字节。城镇类别为 1；任务类型/编号在记录 +0x34，状态字节在 +0x41（0 未完成、1 进行中、2 完成）。类别 0/10 不参与原生地标标签；0.1.3 的类型核对见下文，未核实的组合仍只称“地标”。

名字采用原生默认种族索引算法，保留 uint32 溢出：`(seed/7 + seed*13)%20`、`(seed*7 + seed/10)%20`。源码只保存本版本中字符串的 RVA，运行时读取用户原程序，不分发游戏字体、词表或地图资源。未实现汉化专名转换。

存储块是固定大小的技术分块；有名字的地理区域边界由其他噪声/分区规则决定，不能将存储块边缘当作沙漠或雪地区域边界。本版未绘制此类边界，也未实现任意种子离线世界预测。

## 0.1.2 的原版地区难度色

已通过 GitHub 连接读取上述固定提交的 `docs/MAP_LABELS.md`，本地参考副本在 `research/upstream/RegionReveal/MAP_LABELS.md`；该文档明确区分地区名称颜色和任务完成标记。再对本机准确 SHA256 的原客户端逐字节反汇编核对，未直接执行原游戏函数。

- 原版地图标签在 `0x4CAC69..0x4CADB3` 读取地区记录 `+0x24` 的等级与本地角色 `+0x190` 的等级，分别调用 `0x43CA60`。
- `0x43CA60` 的单精度曲线为 `C(L)=1-1/((L-1)*0.05+1)`。原程序常量 `0x745DC0` 是 `1.0f`，`0x6FCDB4` 是 `0.05f`；阈值 `0x6FCB7C` 为 `0.1f`。
- 地区曲线值 `<= C(玩家)-0.1f` 保持白色；高于这一界线且 `<= C(玩家)+0.1f` 为青色 RGB `(0,1,1)`；更高为红色 `(1,0.2,0.2)`。城市分支直接保留白色。两侧的等号与浮点计算顺序沿用原程序。
- 这是地区等级颜色，使用 `+0x24`，没有用任务怪物等级 `+0x3C` 或任务完成状态替代它，也不计算装备战力。`+0x38` 为任务物种字段，原说明将其写作等级不准确，现已修正；颜色实现一直只读 `+0x24`。距离不参与颜色判定。
- 本模块自行实现这段纯计算，不调用或钩住原版着色函数，不写游戏状态；只在地图打开时增加一次有明确成功/失败结果的 4 字节角色等级读取，扫描频率和范围不变。

常量与布局核对见 `research/evidence/explorer-difficulty-native-rule.json`，地图分支与曲线反汇编分别保存在 `explorer-difficulty-map-color-disassembly.txt`、`explorer-difficulty-native-curve-disassembly.txt`。

## 0.1.3 的地点类型与列表缺项

2026-10-25 对同一 SHA256 客户端只读核对。`GameController_format_region_singular @0x4E5590` 实际用记录 `+0x18` **和 `+0x1C`** 调用 `WorldMap_lookupTileValue @0x5A5240`，再以返回的键查找本地化 `singular` 模板。后者按二元键查询 World 的 `+0x800124` 红黑树（World 内嵌于 `gc+0x2E4`），节点键在 `+0x10/+0x14`，名称键为节点 `+0x18` 的原版 UTF16 字符串。`research/inspect_place_catalog.py` 通过准确进程路径、二进制 SHA 和模块基址验证，仅用 VM_READ/QUERY 权限遍历这个有界词表，没有执行游戏函数。

现场完整读取 123 个键，保存于 `research/evidence/explorer-place-catalog-live.json`。类别 1/2/3/4 对应 Village/Mountain/Forest/Lake；5 的子类 0..3 是 Ruins、4 是 Gravesite；6/7/8 是 Canyon/Valley/Crater；9 子类 0 是 Cave；11/12/13 是 Rock/Tree/Peak；15 是 Island。类别 **14 的子类 0..5 分别是 Castle、Ruins、Catacombs、Palace、Temple、Pyramid**，不能仅凭类别 14 给所有建筑叫同一种名字。词表中普通地形类别的子类范围是 0..9；无对应项时不猜测类型。中文类型为这些原版名称键的直译，原地点专名仍由原游戏的运行时音节生成。沙漠/雪地是另一层群系数据，不由本表推断。

原先参考的 RegionReveal `src/region_reveal/marks.cpp` 的确只在 `mission!=0 && state==2` 时标 BossDefeated；参考副本已存于 `research/upstream/RegionReveal/marks.cpp`。本次现场 489 条地点有 6 条当前任务（其中 2 条状态为 2），另有 4 条 `mission=0,state=2`、1 条 `mission=0,state=1`。旧筛选因此只列 6 条。新筛选保留这些原生非零状态，显示“留存完成/留存进度”，不追认它们为当前任务、永久历史或所有 Boss 击杀证明；也不修改原游戏的状态、图标或任务。该现场的列表应为 11 条、6 条完成状态，其中 5 条是无当前任务编号的留存记录。状态可能随游戏运行变化。

## 0.1.4 的译名与缺字复核

类型再次对照上述原版 123 项词表和逆向 `World.cpp` 的类型名字键；具体形态参考社区资料中明确标注 Alpha 的 [Giant Tree](https://cubeworld.fandom.com/wiki/Giant_Tree)、[Giant Rock](https://cubeworld.fandom.com/wiki/Giant_Rock)、[Missions](https://cubeworld.fandom.com/wiki/Missions)。原键 `Tree/Rock` 在这一版是巨型地标，中文改为“巨树/巨岩”。`Crater` 原键不说明成因；“环形坑”是保守翻译判断，不能据此宣称是陨石坑或火山口。`Gravesite/Peak/Catacombs` 保留“墓地/山峰/地下墓穴”，其余类型未发现意义错误。社区 Wiki 混有 2019 内容，此处未采用 Steam 的 Mana Tree 或其他机制。

本机 `cube_cn.dll` SHA256 为 `F52B099FC23DB290316622DCFB211302BD5FE37E67FC141A3318737E016FD5B9`。按原版 40 个音节键只读提取 UTF8 嵌入词条，40/40 均唯一且严格解码通过，42 个不同汉字中没有字面 `?` 或 U+FFFD。`Lugo = 露芶[IS_END]` 在字节偏移 1689274（0x19C6BA），`lon = 龙`；seed 1863 对应 Lugolon，已有只读位置与截图 6143m 距离吻合。原字体 `resource1.dat` 实测缺少“芶”U+82B6，Windows `msyh.ttc` 能提供。证据见 `research/evidence/explorer-0.1.4-translation-audit.json`，包含每条原键、原译文、码点及偏移。没有将专名擅自改成近似汉字。

补字使用当前 SDK 的 ImGui 1.92 动态字体接口。源码 `imgui.cpp::UpdateFontsNewFrame` 只在后端不支持 `RendererHasTextures` 时锁定图集；现有 `imgui_impl_dx9.cpp` 支持动态纹理更新。模块在受支持的未锁定图集中建立自有字体，借用原字体数据作为第一来源，再将系统字库作为缺字来源，绘制探索时临时选择并恢复。没有合并到共享默认字体、替换公共加载器或重写游戏词表。图集持有字体资源至其自身销毁；原字体数据不重复接管，不持有会在添加字体时失效的配置向量指针。无可用后端/字体时不强改锁定图集，明确记录回退不可用。

## 0.1.5 的首领职业依据与边界

本机 Cube.exe SHA256 仍为 `84A7A132A84D4282338E7EA45A1940D32066D64E39CBAFED8F2418D5A6DC30BF`。可行性报告见 `research/evidence/boss-class-feasibility-20261025.md`。原版生成分支与字段来自本地 CubeWorld-Reversal 的 `game_misc.cpp`、`control/GameController.cpp`、`world/Spawn.cpp` 以及 Qube SDK 的 `offsets.h/creatures.cpp`；没有复制或运行 cuwo 生成器，也没有引入新依赖。

- `GameController.cpp` 原生 `0x60FCFE..0x60FD4F` 明确判断任务类型5与 Spawn 的 `+0x7A & 0x1000`，用记录 `+0x30 + Spawn ID低32位` 调用 srand，随即传入该 Spawn 调用 `0x5C7730`。任务怪物等级在 `+0x3C`，物种在 `+0x38`；二者在代码中分开读取，不用于替换地区难度色。
- `0x5C7730` 的常规分支首个 rand `%4+1` 写到 Spawn `+0x30`；Vampire物种97固定改为4。预测只接受已核对的常规人形物种、原class字节0..4，排除 `flags & 0x58` 的提前返回分支；不将特殊物种、能力代码或未知生成分支硬套为玩家职业。
- Cube.exe 导入当前游戏目录的 `MSVCR110.dll`，其 SHA256 为 `B30160E759115E24425B9BCDF606EF6EBCE4657487525EDE7F1AC40B90FF7E49`。只读反汇编其 rand `RVA 0xA492A` 与 srand `0xA494D`，确认32位状态 `state*214013+2531011`，取 `(state>>16)&0x7FFF`。模块自行计算这一轮，不执行或更改游戏的 RNG。安装器新增该库的散列核对，不替换它。
- 来源分支在巨大人形首领的 Spawn 同时设置 `0x1000` 与 `0x200`。两位必须同时存在，普通任务怪物、只靠SDK boss物种布尔值或距离最近都不能作为匹配依据。详细区块在 World（gc+0x2E4）的 `+0xBC` 区域网格、Region `+0x10018` 指针表中；Spawn 指针向量位于 Zone `+0x18/+0x1C`。原ID为 `((uint64(zoneY)<<16)+zoneX)*256+spawnIndex`，与 Spawn `+0x48` 的64位编号必须完全一致，出生位置与区块的所属粗地标也必须一致。
- 当前 World 原地标记录在 Region `+0x14018`，需与地图快照的类别、子类、专名种子、任务类型、任务seed、物种和等级一致，并且未完成。重新生成或读不到当前任务时保留未知，不复用过去的任务seed。
- 用完整 Spawn ID 查询 SDK 已核实的实体红黑树（gc+0x2E8，节点键+0x10、实体指针+0x18），有界查找，不扫描整个实体列表。实际实体的种类、等级、阶级和存活状态需与原Spawn一致，然后读取实体 `+0x140`。特殊物种显示“特殊首领”；多个候选时不随意挑一个。实际职业优先于预测；发现不一致后本次世界会话停止预测。

查询只在挑战页开图且两秒刷新时发生，限定周边7×7详细区块、每块最多4096条、总计最多8192条；超限时不输出首领结论。此边界意味着远处没有详细生成记录时无法显示职业，不能等同于全世界种子预测。原始现场489条粗地图快照不含这些Spawn和实体，不能用它声称已真实对照过职业；本版游戏内对照仍待进行。

新增二进制证据为 `explorer-0.1.5-task-reseed-disassembly.txt`、`explorer-0.1.5-native-rand-disassembly.txt`；已有 `boss-class-generator-disassembly.txt` 核对常规分支。此轮重新打开[官方补丁索引](https://wollay.com/patch-notes/)，最新仍列2026-10-24 v0.1.12，索引没有确认可复用于当前Alpha的对应功能；公开检索未核实到可直接装用的成品，不据此断言不存在其他实现。采用现有探索增量实现，没有切换版本或安装外部工具。
## 本次字体微调

保留原游戏字体，正常笔画浓度、像素对齐和私有字体采样；不改变共享默认字体、物品数据或交互。独立编译更新，见 验证记录（本地验证记录未随源码发布）。
