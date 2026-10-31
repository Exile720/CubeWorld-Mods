# 旅行驿站 0.1.0 实验包

游戏和启动器完全退出后，运行根目录 `Install-FastTravel.ps1` 安装实验副本。原模组管理器有独立“旅行驿站”开关；其他Mod和原开关保留。

沿用 Trader Fast Travel 的流程：城镇商店旁站稳自动记录，F8 → 返回最近城镇或选驿站 → 确认传送。野外可手动登记。完整64位坐标通过原框架保护写入；旧启动器和旧DLL未安装。每个角色与世界独立记录，最多32个点。只支持单人、下坐骑后使用，没有新增建筑或NPC，不收费。真实远距加载、宠物跟随和全部组合仍待游戏内验收。

构建后必须更新 `release.json` 的 DLL 散列，不能只替换DLL。完整说明见 `../../fast_travel/README.md`，验证边界见 `../../research/evidence/fast-travel-0.1.0-validation.md`。
