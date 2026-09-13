# SmoothWheelScroll for REAPER 1.3.6

**第一个公开发布版。** 功能 = 1.4.1（含轨道面板两侧的分割条），本版只改"对外形态"：
去掉设置入口、定版号、补开源与文档。DLL md5 `fa5964c32d5c525bf9cde0a24cf45ee6`。

## 与 1.4.1 的差异（只有这些）

1. **设置入口从发布版移除**（用户要求）。
   作者的调参窗口（三个滑杆：Start / Accel / Release + Reset/Close）和它的 Extensions 菜单项
   整段包进 `#ifdef SWS_TUNING_UI`，**默认不编译**。
   * 发布 DLL 里**没有**任何设置界面：动作列表无条目、Extensions 菜单无条目、无窗口。
     （实测：`SWS_SCROLL_TUNE` / `settings...` / `AddExtensionsMainMenu` 在该 DLL 中出现 **0 次**。）
   * 自己调参时：`./build.sh --tuning-ui`（可叠加 `--debug-log`）即可恢复。
   * 构建开关：默认（发布） / `--tuning-ui` / `--debug-log`；`build.sh` 对未知参数报错退出。
2. **版本号 1.3.6**，写进 `ext_name`（REAPER 扩展列表显示 `Smooth Wheel Scroll 1.3.6`）。
3. README 重写为对外两份（`README.md` 中文 / `README.en.md` 英文）+ `LICENSE`(MIT)。

**模型与投递零改动**：`anim_core.h` 仍与 1.0.0 验收版逐字节相同。
单格对拍 **1.89 / 峰值 17.94**，与 1.0.0 一致。

## 发布信息

- GitHub：https://github.com/bobo198504/SmoothWheelScroll-REAPER
- Release：`v1.3.6`，附件 `reaper_smoothwheelscroll-x64.dll`
- 许可：MIT
