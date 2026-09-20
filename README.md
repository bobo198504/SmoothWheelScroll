# Smooth Wheel Scroll for REAPER

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
![Platform: Windows x64](https://img.shields.io/badge/platform-Windows%20x64-blue)

一个原生 REAPER 扩展：把普通鼠标滚轮驱动的滚动/缩放变成平滑动画，**不改变滚轮原本做什么**。

[English](README.en.md)

<p align="center">
  <img src="test/demo.gif" alt="Smooth Wheel Scroll 效果演示" width="880">
</p>

插件截住滚轮的一格，转成一段动画，再**分小份、按时间交给同一条 REAPER 动作**。
不自己移动视图、不修改 REAPER 状态：缩放锚点、滚动范围、步进大小、自定义规则全部仍由 REAPER 决定。

---

## 缓动事件支持

| 事件 | 方式 |
|---|---|
| 名字带 `mousewheel` 的动作 | 按**动作名**匹配，含自定义 / 重绑的快捷键（**特殊动作除外，如翻页**）|
| 自定义动作 | 由上述动作组成的宏，整体缓动 |

## 原生放行事件

* **参数类滚轮** —— 推子、旋钮、速度、发送量、力度、下拉框等原样透传。
* **列表控件** —— 整行移动，原生已是瞬时。
* **触控板、触摸、触控笔** —— 原样交给 REAPER。

---

## 安装

Windows x64，REAPER 7。

1. 从 [Releases](../../releases) 下载 `reaper_smoothwheelscroll-x64.dll`。
2. 放进 `UserPlugins`：便携版 `<REAPER>/UserPlugins/`，普通安装 `%APPDATA%\REAPER\UserPlugins\`。
3. 重启 REAPER。

加载后显示为 `Smooth Wheel Scroll 1.7.2`。

### 设置面板

**Extensions 菜单** → `SmoothScroll...`；或在 **Actions 窗口**用 `Smooth Wheel Scroll: settings...`
（可绑快捷键，再按一次关闭）。改动即时生效、自动保存。

* **Glide length** — 一格动画的时长（100–300 ms，默认 200）
* **Slow step** — 慢轮一格走多少（1–10 delta，默认 5）
* **Ramp-up** — 转多少才涨满到整格（60–2000 delta，默认 1000）
* **Top speed** — 最快时超过自身速度的倍数（1.0–2.0×，默认 1.5）
* **总开关** —— 关掉即完全放行，滚轮回到原生行为

滑杆下方是**运动轨迹图**：每收到一个滚轮消息就有一颗球沿路径跑，直观显示当前设置的效果。

<p align="center">
  <img src="test/settings-dark.png" alt="设置面板（深色）" width="330">
  &nbsp;&nbsp;
  <img src="test/settings-light.png" alt="设置面板（浅色）" width="330">
</p>

### 卸载

删掉 DLL，重启 REAPER。

---

## 从源码构建

C++17 编译器，对着仓库内的 REAPER SDK（`third_party/`）编译。参考构建使用便携版 MinGW-w64。

```sh
./build.sh        # -> build/reaper_smoothwheelscroll-x64.dll
./deploy.sh       # 可选：复制进 UserPlugins
```

| 参数 | 作用 |
|---|---|
| *（无）* | 含设置面板（默认） |
| `--no-settings-ui` | 不编译设置面板 |
| `--debug-log` | 附加诊断日志（`%TEMP%\SmoothWheelScroll.log`） |
| `--wheel-log` | DEV 版：把最近的滚轮消息记到插件同目录，用于设备排查 |

回归门（脱离 REAPER 运行）：

```sh
./test/check_anim3.sh          ./test/check_conservation.sh   ./test/check_travel.sh
./test/check_device.sh         ./test/check_routes.sh         ./test/check_classify.sh
./test/check_filter.sh         ./test/check_wheel_log.sh      ./test/check_macro.sh
```

---

## 代码结构

| 文件 | 内容 |
|---|---|
| `src/anim3_core.h` | 动画模型（纯数学，不依赖 REAPER / Windows） |
| `src/anim161_core.h` | 竖直缩放用的曲线模型 |
| `src/model.h` | 模型接缝：唯一对外的模型入口 |
| `src/routing.h` | 投递路由：哪条动作、什么粒度 |
| `src/device.h` | 设备分类（有格 / 无级 / 触控板） |
| `src/macro.h` | 自定义动作成分解析 |
| `src/smooth_wheel_scroll.cpp` | REAPER 扩展：分类、喂入、投递、设置面板 |
| `test/` | 回归门 |
| `versions/<ver>/` | 各版本冻结快照 |
| `third_party/` | REAPER 扩展 SDK |

---

## 限制

* 仅 Windows x64；macOS / Linux 需要各自的窗口钩子实现。
* 无级滚轮的支持未经实机验证。
* 设备本身已带惯性时，可能感到双重缓动。

---

## 许可证

MIT —— 见 [LICENSE](LICENSE)。`third_party/` 内的 REAPER 扩展 SDK 适用其自带许可。
