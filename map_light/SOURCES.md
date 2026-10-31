# 大地图光照的本地证据

依据是本机已核实的 2013 Alpha `playtest/Cube.exe` 与本项目保存的 [CubeWorld-Reversal](https://github.com/qad3n/CubeWorld-Reversal) 代码。恢复名称属于分析辅助，偏移与调用约定以实际客户端字节核对；恢复代码不能当作可编译的完整原版源码。

- 客户端 SHA256：`84A7A132A84D4282338E7EA45A1940D32066D64E39CBAFED8F2418D5A6DC30BF`。
- `external/CubeWorld-Reversal/cube/control/GameController.cpp` 中，原生帧绘制入口为 `0x4AC260`；原版以控制器 `+0x800440` 的时间计算日夜系数，再调用光照设置。大地图标志为 `+0x8006E4`，主光照对象指针为 `+0x800580`。
- 主调用点 `0x4AD957` 调用 `0x448170`，四个指针分别为漫反射、环境色、方向、填充色；`thiscall`，被调用者 `ret 0x10`。原函数读取参数、在本地栈归一化方向，再由原生 Direct3D 设备上传常量。
- M 打开时，帧绘制进入 `0x5FC1B0` 的世界地图绘制后返回；恢复文件误将这一函数命名为 `GameController_processNetworkTick`。原生小地图也会调用该函数，因此模组另核对完整 M 地图标志和主光照对象。
- 原生大地图地标绘制一段（约 `0x5FFBB0` 附近；恢复源码约 91435 行）传入漫反射 `{1,1,1,1}`、环境色 `{.2,.3,.4,1}`、方向 `{0,.5,-1}`、填充色 `{.4,.4,.4,1}`。模组复用这组参数；“地形会更亮”是依据渲染路径的工程判断，仍需实际画面验收。
- 运行时校验 PE 时间戳 `0x51EA955E`、SizeOfImage `0x3BC000`、入口 RVA `0x28E1E0`，并校验 `0x448170`、`0x448274`、`0x4AD931` 和 `0x4AD957` 的原始字节。构建期反汇编保存在本机 `build/map-light-native-disassembly.txt`、`build/map-light-main-call.txt`。
- 现有药水商店钩子在 `0x44AD30`、`0x4C76E0`；导航与区域探索不修改该光照函数。这只证明已知挂接点不重叠，不能代替游戏内兼容验收。

0.1.1 沿用当前加载器 `external/Qube-Loader/modloader/src/game/offsets.h` 的 `kDayStartHour=6`、`kNightStartHour=20`，读取同一个 `+0x800440` 时钟判定日夜。该日夜区间属于加载器的约定，不是从原版光照曲线中推导出的官方日出日落定义。完整大地图打开时，只有夜间才读取 DLL 自有的固定数组；白天直接保留传入指针，连颜色、强度和方向都不覆盖。时钟失败或不在 `[0,86400000)` 时保持原版。

实现没有修改 `+0x800440`，没有改游戏保存内容，也不调用时间设置接口。关图后不再替换参数。不修改像素着色器、天空物件或太阳/月亮。

走路卡顿排查另发现：`external/Qube-Loader/modloader/src/game/session.cpp` 的 `readSession()` 会调用 `countPlayers()`，后者在 `game/creatures.cpp` 遍历驻留实体。0.1.0 在判断地图打开前调用这个接口，可能产生多余开销。0.1.1 已将地图/着色器和日夜判断前置，关闭地图时仅两次定长内存读取；没有关闭安全检查或改动加载器。是否为实际卡顿主因仍需同场景对照及帧时间证据。
