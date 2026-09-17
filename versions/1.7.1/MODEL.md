# SmoothWheelScroll for REAPER 1.7.1

**自定义动作（宏）缓动版。** 相对 **1.7.0**，本版让**由滚轮族动作组成的 `Custom:` 动作**
也能平滑滚动/缩放。模型、投递层、设置面板**均未改动**。

DLL md5 `8e3bf8ffaac8206698e4883838473d4f`
DEV（滚轮记录版）DLL md5 `ee7ae6ae4feb55543c1d41c85b959145`

| 文件 | md5 |
|---|---|
| `smooth_wheel_scroll.cpp` | `86cddfb15f2be51fa8c80a046bc6a15a` |
| `macro.h` | `d3a3b17ab48c0e580de522ec5f40dcae`（**本版新增**）|
| `anim3_core.h` | `8d2231cef1e3d70860ea30290198d40e`（与 1.7.0 相同）|
| `anim161_core.h` | `d785d9371295448461a033bb1efd179e`（与 1.6.1 逐字节相同）|
| `model.h` | `ff6fecfe0105c0f338ada982412866c5` |
| `routing.h` | `24150782d55af10b66b752c108c653e2`（与 1.7.0 相同）|
| `device.h` | `6ebb98b9a5675d2515f47ffb4bb76618` |
| `wheel_log.h` | `1f9df7f2cdec697913ed1be6ac964b24`（DEV 版用）|

---

## 1. 自定义动作（`Custom:`）缓动

REAPER 可以把几条动作组合成一条宏。宏原生**一次性整批执行**，所以滚起来一格一格跳。
本版**接管**这条宏：读它的成分，**用一条平滑动画同时驱动其中的每一条**。

### 成分从哪来（SDK 不提供）

- 命令号 → `ReverseNamedCommandLookup` → 名字串（`_<guid>`）；
- 用该 guid 在 **`reaper-kb.ini`** 里找 `ACT` 行——**只读**（同 plugin 已在读 `reaper.ini` 的做法）；
- 行格式实测：`ACT 0 0 "<guid>" "Custom: 名字" <子命令> <子命令> …`，子命令可为**数字号**或 `_名字串`。

### 全过才接管（安全底线）

宏里**每一条**子命令都必须通过**与普通动作同一个判据**（`LookupAction || ClassifyName`），
且必须是可摊开的投递粒度；**只要一条不过，整条宏放行给 REAPER**。

- **为什么复用同一个判据、而不是另建名单**：翻页（`one page`）、`snap to theme`、脚本、嵌套宏等
  排除条件**自动生效**，不存在"第二份名单漂移"的风险。
- **为什么必须全过**：宏原生**每条只执行一次**，而缓动会把动作**重放很多次**。若宏里含
  "选择下一轨"这类动作，动画化就等于**狂选几十次**。这是硬性安全条件。

### 子命令可以跨轴（本版修正）

**横竖各一条**（"一次滚轮同时横竖缩放"）是最常见的宏形态，**本版支持**：
轴只决定**用哪个 glide 跑这次手势**（一次滚轮 = 一份行程，所有子命令共享），
每个子命令带着自己的 `section`+`command` 去重放，自己的投递粒度也各自解析。
（初版曾以"一个手势只能喂一根轴"为由拒绝跨轴宏 —— 那是**当时的实现限制**，不是设计约束。）

### 实测证据（`check_macro.sh`，含真实 ACT 行）

| 场景 | 结果 |
|---|---|
| `990 + 1000`（滚轮族，横竖缩放） | 每条子命令**各收到完整一格**（12 次发送 / 总 15.000）✅ |
| `989 + 989`（两条竖直滚动） | 同上 ✅ |
| 含 `one page` / 脚本 / 嵌套宏 | **整条拒绝**，放行给 REAPER ✅ |

实机（`--debug-log`）另证：三条子命令 `classified=1`、`smooth ... macro=3` 被接管；
而**同号 991 在不同宏里**（`Adjust vertical zoom (MIDI CC/OSC only)`）被正确拒绝 ——
说明判据按**当前动作的名字**判，不是写死命令号。

## 2. ★ 边界：`(MIDI CC/OSC only)` 那一族**不接管**

REAPER 的动作有两族与滚轮相关：

| 族 | 例子 | 本插件 |
|---|---|---|
| `(MIDI CC relative/mousewheel)` | `View: Zoom horizontally (…)` | **接管**（滚轮族）|
| `(MIDI CC/OSC only)` | `View: Adjust horizontal zoom (MIDI CC/OSC only)` | **不接管**（REAPER 标注只给 MIDI CC / OSC）|

**这是实测确认的边界，也是必须对外说明的一点**：论坛反馈者那条宏
（`Custom: Mega zoom` = `998 + 991`，两条都是 `(MIDI CC/OSC only)`）**不在接管范围**，
所以**光是升级插件解决不了他那个宏** —— 他需要用**滚轮族**动作重建这条宏。
（本次实测：`macro child ... name="View: Adjust horizontal zoom (MIDI CC/OSC only)" classified=0`
→ `pass through`。）

## 3. 未改动

- **模型**（3.0 速度预算 / 1.6.1 曲线模型）、**投递层**（`routing.h`/`FilterFor`）、
  **设置面板**：一行未动。
- `.data` 由 0x3f0 → **0x570**，增量 **0x180 = 2 × 8 × 24 字节**，即两个 `ActionSpec` 数组
  （有意放 `.data`，以保住 `Integrator` 与模型数组留在 `.bss`），**有据可查、非搬移事故**。

## 4. 门（发布时全过）

`check_anim3` / `check_conservation` / `check_travel` / `check_device` / `check_routes` /
`check_classify` / `check_filter` / `check_wheel_log` / **`check_macro`（新，含 3 个探针）**
—— **9 门全过**。三种构建通过（默认 / `--no-settings-ui` / `--wheel-log`）。

---

## 相对 1.7.0 的改动一览

| # | 改动 | 性质 |
|---|---|---|
| 1 | `Custom:` 宏：读成分并**整体缓动**（跨轴亦支持） | 核心 |
| 2 | 宏的每条子命令过**同一个判据**，一条不过则整条放行 | 安全 |
| 3 | 新增 `src/macro.h`（`reaper-kb.ini` 的 `ACT` 行解析，无 REAPER 依赖） | 结构 |
| 4 | 新增 `test/check_macro.sh`（解析 / 判据 / 端到端链，共 3 个探针） | 门 |
