# SmoothWheelScroll for REAPER 1.7.3

**混合宏版。** 相对 **1.7.2**，本版让**自定义动作（Custom:）里混有非滚轮动作时也能平滑滚动** ——
典型形态是"设模式 → 缩放 → 还原模式"。模型、投递层、设置面板**均未改动**。

DLL md5 `69a515c47a6495368058fe348f7680d6`

**本版不产出 DEV 版** —— DEV（滚轮记录版）只属于 1.7，见 `AGENTS.md` §0。

| 文件 | md5 |
|---|---|
| `smooth_wheel_scroll.cpp` | `4ba661576f6d2270441971814acdffc4`（**本版唯一改动**）|
| `macro.h` | `d3a3b17ab48c0e580de522ec5f40dcae`（与 1.7.2 相同）|
| `anim3_core.h` | `8d2231cef1e3d70860ea30290198d40e`（与 1.7.2 相同）|
| `anim161_core.h` | `d785d9371295448461a033bb1efd179e`（与 1.7.2 相同）|
| `model.h` | `ff6fecfe0105c0f338ada982412866c5`（与 1.7.2 相同）|
| `routing.h` | `24150782d55af10b66b752c108c653e2`（与 1.7.2 相同）|
| `device.h` | `6ebb98b9a5675d2515f47ffb4bb76618`（与 1.7.2 相同）|
| `wheel_log.h` | `1f9df7f2cdec697913ed1be6ac964b24`（与 1.7.2 相同）|

---

## 1. 新能力：混合宏

**报告**（论坛用户 **ferropop**）：这样一条自定义动作**不触发平滑** ——
```
SWS/wol: Options - Set "Horizontal zoom center" to "Mouse cursor"
View: Zoom horizontally (MIDI CC relative/mousewheel)
SWS/wol: Options - Set "Horizontal zoom center" to "Edit cursor or play cursor (default)"
```
他要的是"缩放期间临时把缩放中心设为鼠标光标，松手后还原"。

**旧行为**：`MacroChildren` 要求**每个**子动作都通过 `ClassifyCommand`（名字须含 `View` 且带
`mousewheel` 标记）。上面两条 SWS 动作不是视图滚轮动作 → **整条宏被放弃、交回 REAPER** → 没有平滑。

### 为什么可以放宽（安全论证）

旧规则的理由是：**宏原生每条只执行一次，而插件要重放很多次**；
若宏里有"选择下一轨"，动画化就等于**狂选几十次**。

**关键**：危险来自**重放很多次**；而**"只跑一次"正是 REAPER 原生的行为**。
于是子动作分三类，**只对"本来就该被重复"的那一类施加重复**：

| 类别 | 判定 | 处理 |
|---|---|---|
| **DRIVABLE** | 判得出来 + 可重放 + 可摊开 | **动画化**（与原来完全相同）|
| **PLAIN** | 判不出来（SWS 开关 / 脚本 / 嵌套宏 / `one page` / "选择下一轨"）| **只执行一次** |
| **REFUSE** | 判得出来但 `Delivery::kImmediate`（MIDI 竖直缩放：一格一次）| **整条宏仍放弃** |

**一条可驱动的都没有 → 仍整条放行**给 REAPER（没有手势可跑）。

**摆放位置**：第一条可驱动子动作**之前**的普通子动作 → **手势开始时**跑一次；
**之后**的 → **手势结束时**跑一次。正好是"设模式 → 缩放 → 还原"的写法；
连滚十格**不会**把设置重跑十次。

### 做法（1 个文件）

`Route` 加 `plain[]`；`Integrator` 加 `macroPlain[]` / `nMacroTail` / `macroTail[]`（全零初值，
仍在 `.bss`）；`MacroChildren` 把判不出来的子动作标为 plain 而不是拒绝；
`Kick` 在**新手势**时执行头部一次、记录尾部；`Tick` 在**手势结束时**执行尾部一次；
`DeliverTravel` / `allStep` / 补发**三处循环均跳过 plain**（否则 SWS 那两条会被动画化重放）。

## 2. 未改动

- **模型**：上表 7 个头与 `v1.7.2` **逐字节相同** → **不需要同步 Apex**。
- **投递层 / 设置面板 / 动作表 / 分类规则**：一行未动。

## 3. 门

`check_anim3` / `check_classify` / `check_conservation` / `check_device` / `check_filter` /
**`check_macro`（已改到新规则，含混合宏用例）** / `check_routes` / `check_travel` /
`check_wheel_log` —— **9 门全过**。

`check_macro` 的两个探针本次**同步改到新规则**（不改的话门会"绿着"却守着已废除的旧规则）：
`macro_gate_probe` 改为三分法判定；`macro_chain_probe` 加入本条宏的真实形状
（设中心 + 缩放 + 还原）与"缩放放最前"的变体，断言可驱动子动作拿到完整 `15.000`、
**普通子动作从未被投递任何行程**、头/尾位置正确。

---

## 相对 1.7.2 的改动一览

| # | 改动 | 性质 |
|---|---|---|
| 1 | 含**非滚轮子动作**的自定义动作也能平滑滚动：普通子动作只执行一次，滚轮子动作照旧动画化 | **新能力** |
| 2 | `check_macro` 的两个探针改到新规则，并加入混合宏用例 | 门 |
| 3 | `ext_name` → `Smooth Wheel Scroll 1.7.3`；README 中英同步 | 版本 |
