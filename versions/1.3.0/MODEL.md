# SmoothWheelScroll for REAPER 1.3.0

**范围（本版收窄，按用户决定）：只做视图滚动 / 视图缩放。**
**列表控件不做缓动**——原因见下，是实测结论，不是省事。

## 范围

### 做

| 表面 | 驱动 |
|---|---|
| 主视图（arrange）滚动 / 缩放 | 交给 **REAPER 动作**，按动画曲线重放相对值（`hookcommand2` → `KBD_OnMainActionEx`） |
| MIDI 编辑器（滚动 / 缩放 / 琴键） | 同上，走 `section->onAction` |
| 所有名字带 `mousewheel` 的 View 动作 | 同上（判定看动作名，不看快捷键） |
| 轨道面板 TCP / 混音台 MCP 本体 | 走动作 989（竖直滚动），不自己移动视图 |
| TCP 边界的分隔条 | 用 TCP 矩形几何识别 |

### 不做（本版移除）

- **列表控件**（媒体浏览器、轨道管理器、动作列表、工程湾、FX 浏览器、区域/标记管理器…）**一律放行给 REAPER，不做缓动**。

**实测原因**（探针 `_diag/listprobe.cpp` 直接测量所得）：

1. **它们是 `OWNERDATA`（虚拟化）列表**，`nPos` 是"可见起始项索引"，**滚动的最小单位就是一整项**——动画无法在项内插入中间位置，所以只能一跳一跳，读起来"卡卡的"。
2. **重放滚轮消息对它们无效**（实测 `WM_MOUSEWHEEL` 后 `nPos` 0 → 0，完全不动）。
3. **REAPER 没有列表的滚动接口**（全部滚轮上下文 `MM_CTX_*MOUSEWHEEL` 只有 arrange / TCP / MCP / 推子，无 list）。

三条叠加 = 想做好要么过度改造、要么收益极低，故整体移除，保持插件只做视图滚动/缩放。

## 模型（未变，仍为 1.1.0 定稿模型）

- 冲量 `v_k = dir·unit·(Start%+(k-1)Accel%)·Q/T`
- **每格独立**平滑渐入 `S(u)=3u²-2u³`（不是共用一条斜坡）
- 幂律摩擦 `dv/dt = -c·v^p`，p=0.8，`c` 由第一格标定
- 刹车随节奏放松 `relax = clamp(tempoRefMs/gap, 1, relaxMax)`
- 固定积分网格，与计时器无关

| 参数 | 值 |
|---|---|
| Start | 15 % |
| Accel | 3.5 % |
| Release | 150 ms（同时 = 渐起窗口） |
| frictionPow | 0.8 |
| movingOnsetMs | 30 |
| tempoRefMs / relaxMax | 120 / 4 |

**对拍**（`test/check_v1_baseline.sh`）：单格 1.89，与 1.0.0 逐位一致。

## DLL

`reaper_smoothwheelscroll-x64.dll`，md5 `d293678f254466bd6bc628377477b0ad`。
