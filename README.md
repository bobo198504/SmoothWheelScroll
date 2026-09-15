# Smooth Wheel Scroll for REAPER

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
![Platform: Windows x64](https://img.shields.io/badge/platform-Windows%20x64-blue)

一个原生 REAPER 扩展：让普通鼠标滚轮驱动的滚动/缩放变得平滑，**不改变滚轮原本做什么**。

[English](README.en.md)

<p align="center">
  <img src="test/demo.gif" alt="Smooth Wheel Scroll 效果演示" width="880">
</p>

REAPER 的滚动/缩放大多已有对应的滚轮动作（动作名含 `(MIDI CC relative/mousewheel)`），
这些动作本身就接受平滑的相对量。本插件截住普通鼠标滚轮的一格，把它转换成一段动画，
再**分小份、按时间交给同一条 REAPER 动作**。

插件不自己移动视图、不修改 REAPER 状态。缩放锚点、滚动范围、步进大小、用户自定义规则，
全部仍由 REAPER 决定。

---

## 会做缓动的

| 表面 | 方式 |
|---|---|
| 主视图（arrange）：滚动 + 缩放 | REAPER 自己的动作 |
| 主视图两条滚动条 | 竖直条滚动；`Alt`+滚轮竖直缩放。水平条 `Alt`+滚轮水平缩放（不带 `Alt` 的翻页式快移不接管） |
| MIDI 编辑器：滚动 + 缩放 | MIDI 编辑器自己的 section 动作 |
| 轨道面板（TCP） | 遵从鼠标修饰键：默认 `Scroll TCP` → 竖直滚动；`Adjust vertical zoom` → 竖直缩放 |
| 轨道面板两侧的宽度拖拽条 | 靠面板的内半侧 → 轨道滚动；外侧放行 |
| MIDI 编辑器琴键 | 竖直滚动 |
| 调音台（MCP） | 横向滚动 |
| 名字带 `mousewheel` 的动作 | 按**动作名**匹配，因此自定义或重新绑定的快捷键同样生效 |

滚动条按几何识别；轨道面板与调音台按**鼠标修饰键**（`Scroll TCP` / `Scroll MCP`）判断。
把那些组合改成别的（例如 `Passthrough`），插件就放行。

## 不做缓动的

* **参数类滚轮。** 推子、旋钮、速度、发送量、MIDI 音符力度、下拉框等一律原样透传，仍一格一格精确改值。
* **列表控件。** 整行移动，原生响应已是瞬时，加缓动只会增加延迟。
* **触控板、触摸、高分辨率/无级滚轮、触控笔。** 只处理普通有格鼠标滚轮（整倍数 `WHEEL_DELTA`、非触摸注入）。

---

## 安装

Windows x64，REAPER 7。

1. 从 [Releases](../../releases) 下载 `reaper_smoothwheelscroll-x64.dll`。
2. 放进 `UserPlugins`：便携版 `<REAPER>/UserPlugins/`，普通安装 `%APPDATA%\REAPER\UserPlugins\`。
3. 重启 REAPER。

加载后，扩展列表与启动日志中显示为 `Smooth Wheel Scroll 1.6.0`。

### 设置面板

两种打开方式：

1. **Extensions 菜单** → `SmoothScroll...`
2. **Actions 窗口**搜 `Smooth Wheel Scroll`，用 `Smooth Wheel Scroll: settings...`；可绑定快捷键，再按一次关闭。

面板包含缓动总开关、5 个滑杆，以及每个滑杆对应的一个旋钮。改动即时生效、自动保存，
默认值取各自范围的中点。

* **滑杆**调手感：起步力度、加速堆量、缓动时长、速度上限（Hold）、下冲（Coast）。
* **旋钮**调细节：Start 的起步时长（20–150 ms），其余四段的曲率。
* 底部为速度响应曲线（横轴时间 `t`、纵轴速度 `v`），随参数实时变化；五段各有颜色，与对应滑杆一致。
* 面板跟随 REAPER 的浅色/深色：标题栏、面板底色、文字、滚动条都会随之切换。
* 关掉总开关即完全放行，滚轮回到 REAPER 原生行为。

<p align="center">
  <img src="test/settings.png" alt="Smooth Wheel Scroll 设置面板" width="330">
</p>

### 卸载

删掉 DLL，重启 REAPER。除面板参数外不写任何配置。

---

## 从源码构建

一个翻译单元加一个头文件，用 C++17 编译器对着仓库内的 REAPER SDK（`third_party/`）编译。
参考构建使用便携版 MinGW-w64。

```sh
./build.sh        # release DLL -> build/reaper_smoothwheelscroll-x64.dll
./deploy.sh       # 可选：复制进 REAPER 的 UserPlugins
```

构建参数：

| 参数 | 作用 |
|---|---|
| *（无）* | 含设置面板（默认） |
| `--no-settings-ui` | 不编译设置面板 |
| `--debug-log` | 附加诊断日志（`%TEMP%\SmoothWheelScroll.log`） |

回归门（脱离 REAPER 运行）：

```sh
./test/check_curve_model.sh   # 模型：单格行程 / 单峰 / 封顶 / 连滚累积 / 与步长无关
./test/check_classify.sh      # 分类规则改动前后对比
```

---

## 代码结构

| 文件 | 内容 |
|---|---|
| `src/anim_core.h` | 动画模型（纯数学，不依赖 REAPER / Windows） |
| `src/smooth_wheel_scroll.cpp` | REAPER 扩展：分类、喂入、投递 |
| `test/` | 回归门 |
| `versions/<ver>/` | 各版本冻结快照 |
| `third_party/` | REAPER 扩展 SDK |

---

## 限制

* 仅 Windows x64；macOS / Linux 需要各自的窗口钩子实现。
* 仅普通有格鼠标滚轮。设备本身已带惯性的情况下可能感到双重缓动。

---

## 许可证

MIT —— 见 [LICENSE](LICENSE)。`third_party/` 内的 REAPER 扩展 SDK 适用其自带许可。
