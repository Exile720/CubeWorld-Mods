# 区域探索的来源与可核验边界

核验时间：2026-10-24。仅支持本机已验证的 2013 Alpha：`Cube.exe` SHA-256 `84A7A132A84D4282338E7EA45A1940D32066D64E39CBAFED8F2418D5A6DC30BF`。

- [RegionReveal](https://github.com/Jovinull/RegionReveal/tree/8633915f10b6ad565a31fc653d4f7e3f3b2c2484)，固定提交 `8633915f10b6ad565a31fc653d4f7e3f3b2c2484`。参考其 `docs/MAP_LABELS.md`、`docs/REVERSE_ENGINEERING.md`、`docs/BEHAVIOUR.md`、`src/game/cube_world.hpp`、`src/game/world.cpp` 和 `src/region_reveal/marks.cpp` 所整理的本版本地图布局、原生标签分类及任务状态。保留原 MIT 许可于 `RegionReveal-LICENSE.txt`。本模块自行实现 Qube SDK 只读扫描、列表、目标存储和可选导航消息；没有安装该项目的 dinput8 代理或原生补丁，也未调用它的区域揭示函数。
- [CubeWorld-Reversal](https://github.com/qad3n/CubeWorld-Reversal)：本地 `external/CubeWorld-Reversal/cube/control/GameController.cpp` 的 `NameGen_initFirstNameTables`（原地址 0x5a0ed0）及区域生成调用；本地 `cube/_library/crt_stl.cpp` 的地图序列化函数（原地址 0x605420）。地图保存结构与任务实时数据不同，不能把存档中缺失任务字段理解成没有 Boss。
- [Qube-Loader](https://github.com/qad3n/Qube-Loader)：沿用项目现有 SDK、ImGui 和公共加载器，不改变公共框架。其 GPL-3.0 许可随项目保留。
- 本机只读证据：`research/evidence/explorer-name-layout.json` 核对实际可执行文件中的 40 段 UTF-16 名字音节；`explorer-saved-records.json` 核对两份实际地图、761 条地标及其中 24 条城镇。测试记录只重建存档已具有的字段，不补造实时任务状态。

WorldMap 为控制器内嵌对象 `gc+0x800d44`，稀疏块表为 `+0xb0`；每块地标区偏移 0x34000，共 64 条、每条 0x68 字节。城镇类别为 1；任务 ID 在记录 +0x34，状态字节在 +0x41（0 未完成、1 进行中、2 完成）。类别 0/10 不参与原生地标标签；尚未完整核实的其他类别只称“地标”。

名字采用原生默认种族索引算法，保留 uint32 溢出：`(seed/7 + seed*13)%20`、`(seed*7 + seed/10)%20`。源码只保存本版本中字符串的 RVA，运行时读取用户原程序，不分发游戏字体、词表或地图资源。未实现汉化专名转换。

存储块是固定大小的技术分块；有名字的地理区域边界由其他噪声/分区规则决定，不能将存储块边缘当作沙漠或雪地区域边界。本版未绘制此类边界，也未实现任意种子离线世界预测。

## 0.1.2 的原版地区难度色

已通过 GitHub 连接读取上述固定提交的 `docs/MAP_LABELS.md`，本地参考副本在 `research/upstream/RegionReveal/MAP_LABELS.md`；该文档明确区分地区名称颜色和任务完成标记。再对本机准确 SHA256 的原客户端逐字节反汇编核对，未直接执行原游戏函数。

- 原版地图标签在 `0x4CAC69..0x4CADB3` 读取地区记录 `+0x24` 的等级与本地角色 `+0x190` 的等级，分别调用 `0x43CA60`。
- `0x43CA60` 的单精度曲线为 `C(L)=1-1/((L-1)*0.05+1)`。原程序常量 `0x745DC0` 是 `1.0f`，`0x6FCDB4` 是 `0.05f`；阈值 `0x6FCB7C` 为 `0.1f`。
- 地区曲线值 `<= C(玩家)-0.1f` 保持白色；高于这一界线且 `<= C(玩家)+0.1f` 为青色 RGB `(0,1,1)`；更高为红色 `(1,0.2,0.2)`。城市分支直接保留白色。两侧的等号与浮点计算顺序沿用原程序。
- 这是地区等级颜色，使用 `+0x24`，没有用任务怪物等级 `+0x38` 或任务完成状态替代它，也不计算装备战力。距离不参与颜色判定。
- 本模块自行实现这段纯计算，不调用或钩住原版着色函数，不写游戏状态；只在地图打开时增加一次有明确成功/失败结果的 4 字节角色等级读取，扫描频率和范围不变。

常量与布局核对见 `research/evidence/explorer-difficulty-native-rule.json`，地图分支与曲线反汇编分别保存在 `explorer-difficulty-map-color-disassembly.txt`、`explorer-difficulty-native-curve-disassembly.txt`。
