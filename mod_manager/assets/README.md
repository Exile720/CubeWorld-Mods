# 启动器图标

`launcher-icon.png` 是透明背景的生成原图；`launcher.ico` 包含16、20、24、32、40、48、64、128、256像素的32位图层，供 Windows 程序、任务栏与快捷方式使用。

2026-10-24 使用内置 imagegen 工具生成，先查阅并实际查看 Picroma 官网参考：

- [游戏官网](https://picroma.com/)
- [角色造型参考](https://picroma.com/media/warrior.jpg)：金发、蓝眼、大头短身的体素冒险者，深色护肩与背剑。
- [标志配色参考](https://picroma.com/media/cube-world-logo.png)：橙金色像素高光。

参考图片仅存于被忽略的 `build/icon-reference/`，不作为程序资源分发。首次草地方块方案未采用。新图标只用于本项目的模组管理器；不替换原游戏或公共启动器。

已按用户要求恢复金发冒险者原图。ICO 由 `Build-Icon.ps1` 使用 .NET System.Drawing 保留透明通道缩放，再封装为 32 位 DIB 图层，避免 .NET Framework 缩放绘制 PNG 图层时出现花屏。原始 PNG 图样保持不变。更新图标时先运行本目录 `Build-Icon.ps1`，再执行根目录 `Build-ModManager.ps1`；构建会嵌入同一 ICO 用于程序与窗口，并更新本机快捷方式。

生成提示词如下：

```text
Use case: logo-brand. Asset: one finished transparent Windows desktop app icon for a personal CUBE WORLD mod launcher. Reference image 1 (official-warrior.jpg) is the key visual-style and character-proportion reference, NOT an edit target: study its actual Picroma Cube World character, oversized cubic head, small voxel subdivisions, blonde chunky hair, large bright blue eyes, peach block face, tiny dark shoulder armor, and sword. Reference image 2 (official-logo.png) provides the warm orange/gold accent palette only; do not draw its lettering. Create a NEW tightly composed, charming three-quarter FRONT VIEW HEAD-AND-SHOULDERS PORTRAIT emblem of a blonde blue-eyed Cube World fantasy adventurer, matching the distinctive cute proportions and detailed small-voxel construction of reference 1. Big readable friendly face and blocky swept blonde hair are the focal point. Compact navy/charcoal shoulder plates, a small warm orange-gold square clasp at the chest, a single simple steel sword hilt and short visible blade silhouette emerging diagonally behind one shoulder. Pose is confident and friendly, gaze toward viewer. Head is large and readable; very short neck, compact shoulders, no elongated realistic torso. Hard cubic contours, simple solid shaded voxel faces and tiny stepped hair silhouette, bright playful classic 2013 Cube World RPG inventory-portrait aesthetic. A restrained dark contour helps against light Windows desktops. NOT a Minecraft character or Minecraft terrain icon: no grass/dirt block, no floating landscape, no pickaxe, no Steve, no generic isometric cube. Isolated centered bust, fills about 80% of square canvas, even transparent padding, no background tile or medallion, no scenery. True transparent RGBA outside the portrait. Very strong clean silhouette at 32x32, 48x48; limit fine detail. No lettering, logos, watermark, UI screenshot, variants sheet, glossy clay render, soft round shapes, thick cartoon linework, sparkle effects, or photorealism. One icon only.
```
