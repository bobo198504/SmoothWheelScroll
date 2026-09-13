# SmoothWheelScroll 模型 1.0.0（基准版）

**状态：用户已验收（"舒服极了"）。此为手感基准，后续只允许"顺着它往上优化"。**

## 冻结的文件

| 文件 | 说明 |
|---|---|
| `anim_core.h` | 模型本体（禁止改结构） |
| `smooth_wheel_scroll.cpp` | 插件主体（分类/投递） |
| `reaper_smoothwheelscroll-x64.dll` | 已部署的二进制，md5 `56bf3f75d14ce14fb677b3b6cf7eb8e8` |

## 模型三要素（缺一不可）

1. **冲量**：一格给速度 `v_k = dir · unit · (Start% + (k-1)·Accel%) · Q/T`，累加到 `injTotal_`
2. **单一共享平滑渐入**：`vel += injTotal_·S(t/W) − injDone_`，`S(u)=3u²−2u³`，`W=OnsetSec()`（0.8×RELEASE，钳 30–260ms）
3. **幂律摩擦 + 刹车随节奏放松**：
   - `powC_base = vRef^(1-p)/((1-p)T)`，`vRef = unit·Start%·Q/T`
   - `relax = clamp(tempoRefMs/gap, 1, relaxMax)`，`powC_ = powC_base / relax`
   - `dv/dt = -c·v^p`，闭式积分 `Δs = (u0^Q − u1^Q)/(c(1-p)Q)`，`Q=(2-p)/(1-p)`

## 冻结参数

| 名称 | 值 |
|---|---|
| Start | 15 % |
| Accel | 3.6 % |
| Release | 100 ms |
| frictionPow | 0.8 |
| onsetRatio | 0.8 |
| tempoRefMs | 120 |
| relaxMax | 4 |
| burstGapMs | 250 |
| 输出 | 整数单位投递；普通 SetTimer |

## 基准数值（tick=15.6ms，单位=wheel units，一格=15）

| 操作 | 总行程 | 峰值速度 |
|---|---|---|
| 单格 | 1.89 | 17.94 |
| 10 格 @250ms | 47.85 | 201.14 |
| 10 格 @120ms | 55.78 | 220.14 |
| 10 格 @60ms | 121.50 | 417.27 |
| 10 格 @33ms | 239.93 | 1021.88 |

**能量随滚速增长（47→56→122→240），这是本模型的灵魂。**

## 优化规则

- ✅ 允许：调 Start/Accel/Release 等**参数**；改**输出投递**（频率、单次精度）；修 bug。
- ❌ 禁止：改上表三要素的结构；删 `relax`；换摩擦律；换渐入方式。
- 任何改动后，必须跑 `test/anim_sim.cpp` 并与上表**对拍**，确认总量/峰值一致或按预期变化。
