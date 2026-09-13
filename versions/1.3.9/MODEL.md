# SmoothWheelScroll for REAPER 1.3.9

**面向用户的设置面板正式发布**（1.3.6 曾移除设置入口，本版按用户要求做回来）。
DLL md5 `0f9e39452ca26f1bd71894d3e2062ce1`（含下方各项修复）。

## 面板

- **5 个滑杆**，与代码顶部 `TUNING` 块的**感受分组**一一对应：
  起步力度 / 加速堆量 / 缓动时长 / 高速收力 / 高速滑行。
- **总开关**（默认**开**）：关闭后插件**完全放行**——不接管、不缓动，
  **参数原样**转给 REAPER 接口（`OnAction` 提前 `return false`）。
- **REAPER 自带风格**：配色取自主题（`GetThemeColor`），窗口不写死系统灰。
- **浅暗自动跟随**：由主题背景色的**亮度**（Rec.601）判定，不调用 dark-mode API。
  这样做的原因：REAPER 的暗色查询接口在版本间时有时无（7.80rc2 就没有），
  用主题背景亮度可以**在所有版本上都成立**。取不到主题色时退回系统色。
- 入口：Extensions 菜单 + Actions 列表（`Smooth Wheel Scroll: settings...`）。

## 参数范围（中心 = 已验收的手感）

| # | 参数 | 默认(=中心) | 范围 | 说明 |
|---|---|---|---|---|
| 1 | Start | 15.0 % | 5 – 45 | 第一格行程 |
| 2 | Accel | 3.5 % | 0 – 10 | 每格增量 |
| 3 | Release | 150 ms | 60 – 300 | 缓动时长 |
| 4 | High-speed hold | **1.0 x** | 0 – 2 | 乘在 `kTempoRefMs`（↑）与 `kSoftStart`（↓）；**0 = 关闭** |
| 5 | High-speed coast | **1.0 x** | 0 – 2 | 乘在 `relGain`；**0 = 关闭** |

- 组 4 用一个倍数带两个常量：倍数**同时**抬高"放松刹车"的门槛、降低"增益衰减"的拐点，
  所以是"更强的收力"而不是两个互不相干的数。0 时两者都落到各自的**关闭值**，两端良定义。
- 组 5 只缩放刹车减小的幅度；死区 `relFloor` 与参考速度 `relRef` 属**形状**，固定不动。
- **第 6 组（过渡曲线）只标记、不做**，见下文。

## 参数只对作者/协作者开放的旧约定（1.3.8）→ 本版放开

1.3.8 时参数仅供作者改源码。本版按用户要求做成**用户可见面板**。以下仍然保持：

- **模型形状不可调、未改**：`kFrictionPow` / `kOnsetRatio` / `kBurstGapMs` 与 1.3.8 逐字节相同，
  且在 `TUNING` 块**之外**单独注明，防止被当成第 6 个参数调掉。
- 第 6 组（过渡曲线）**只标记不做**。

## 持久化：用 REAPER 官方 extstate（**已获用户批准**）

保存在 `reaper-extstate.ini` 的插件自有段 `[SmoothWheelScroll]`：

```
[SmoothWheelScroll]
start / accel / release / hold / coast / glide
```

- 这是 REAPER 为扩展提供的**官方状态机制**（`SetExtState(..., persist=true)`），
  **不改 REAPER 的任何偏好设置**，也不写插件自己的文件。
- 每个值一个键：手工改坏一个键只影响那一个值。
- **所有读入值都过同一处钳位**（`RefreshDerived` → `Clamp`），
  所以 store 里的越界/坏值**不可能**进到模型——"最大最小都不能出错"由此成立。

> ⚠️ 这与**铁律第五节**（"不动 `D:\REAPER` 里任何东西"）的冲突是**用户明确批准**的；
> 第五节也写明"若要突破必须先申请批准"。批准记录在会话中，本条是凭据。

## 与 MIDI 编辑器两个特殊情况的关系（未变）

`Delivery` 枚举与两条特殊轴的映射**完全不变**：

| 动作 | delivery |
|---|---|
| 40432 / **40661** 竖直滚动 | `kStepUnits`（整单位递进） |
| 40430 / **40663** 竖直缩放 | `kImmediate`（整格一次、直接停） |
| 其余（水平轴、arrange…） | `kStream` |

- 关掉总开关时，这两条轴也**一并放行**（`OnAction` 在匹配后、Kick 前提前返回），
  即关闭 = 全面原生，无一例外。
- 面板改动**不触碰** `Delivery`、不改判定逻辑、不改动作表。

## 修复：三处回归（滚动条暗色 / Dock 可逆 / 关闭后可再开）

用户报三条，全在**面板生命周期与窗口主题**，**与模型无关**：`anim_core.h` 仍与 1.3.8
逐字节相同，单格仍 **1.89 / 17.94**。

1. **滚动条不随暗色主题**：窗口自带的 `WS_VSCROLL` 由**窗口主题**绘制，不受我们的画笔控制。
   用 `SetWindowTheme(h, L"DarkMode_Explorer", nullptr)`（亮色传 `nullptr` 还原，**双向**）；
   `uxtheme.dll` 动态加载，**不加静态导入**。滚动条在**非客户区**，故切主题后
   `RedrawWindow(RDW_FRAME|RDW_INVALIDATE)`；滚动条出现/消失时补刷（`g_barThemed`），
   `WM_THEMECHANGED` / `WM_SYSCOLORCHANGE` 一并重建画笔并令子控件重绘。
2. **Dock 后无法回到浮动**：根因是每次开窗都无条件 `DockWindowAddEx(identstr)`，
   而 REAPER 按 identstr 记忆位置 → 每次都被拉回 docker。改为**持久化偏好 `g_dockOn`**
   （extstate key `dock`，默认 **false = 浮动**），**只有它为真才 AddEx**；
   窗口**右键菜单** `Dock in Docker / Undock`，`ToggleDocking()` **销毁后重建**（同 SWS 做法）；
   推子转发 `WM_CONTEXTMENU` 给父窗口，保证菜单点得到。
3. **关闭后再也打不开**：`ShowConfigWindow` 三路处理已存在窗口 —— **在 docker 内 → `DockWindowActivate()`**；
   **不可见 → `ShowWindow(SW_SHOW)`**（docker 折叠时是隐藏而非销毁，前台化隐藏窗口等于没反应）；
   否则前台化。发现窗口已脱离 docker 而 `g_dockOn` 仍为真则就地改回并保存。
   另把 `DockWindowRemove` 从 `WM_DESTROY` 移到 **`WM_CLOSE`**（`ToggleDocking` 自己先 remove）。

## 修复：Accel 上限收到 10%

**用户要求**：`Accel`（加速堆量）上限从 12% 收到 **10%**——"这个值太大堆起来有点可怕"。

- 只改 `kAccelMaxPct`：`12.0 → 10.0`。**默认值 3.5% 不动**，模型与全部机制不变
  （`anim_core.h` 仍与 1.3.8 逐字节相同，单格仍 1.89 / 17.94）。
- 因为范围是**单一来源**且读入过 `RefreshDerived` → `Clamp`，store 里若存着 >10 的旧值，
  下次载入会被**自动钳到 10**，不会越界。

## 修复：设置动作改为开关

**用户要求**：设置动作要能**反复开关**（可绑到快捷键），而不是只能开。

- `g_cmdTune` 分支由 `ShowConfigWindow()` 改为 `ToggleConfigWindow()`：
  **在屏 → `WM_CLOSE`；不在屏 → 打开**。
- "在屏"判定包含 **docked**（折叠的 docker 会把子窗口隐藏，但它仍是用户要的那个面板，
  不能当成"关着"，否则第一次按像没反应、第二次会开出第二个窗口）。
- 菜单项文案随状态切换（`settings (close)` / `settings...`）。

## 修复：窗口位置遵守 REAPER 的定位规则

**用户要求**："不管是窗口模式还是 Dock 模式，窗口的位置要遵循 REAPER 的定位规则，
不要自己跑来跑去，或每次都出现在左上角"。

改之前浮动窗口是用 `CW_USEDEFAULT` 创建的，所以**每次重建都落在系统默认位置**（左上角一带）。

- **Dock 模式**：位置**完全由 REAPER 决定** —— `DockWindowAddEx` 恢复 REAPER 自己记在
  `reaper.ini` 里的停靠位置，插件**不传任何几何**。
- **浮动模式**：位置由插件记（extstate key `win` = `x y w h`）。
  - 有记录 → 还原到该位置（尺寸也还原），并先过 `EnsureOnScreen()`：
    显示器布局变化后旧坐标可能落在屏幕外，此时**只挪位置、不改尺寸**。
  - 无记录（首次）→ `PlaceCenteredOnMain()`：**居中于 REAPER 主窗口**（略偏上），
    与 REAPER 自己放对话框的方式一致，而不是丢到系统默认的左上角。
- 记忆时机：`WM_EXITSIZEMOVE`（拖/缩放结束后存一次，不必每帧写 store）
  + `WM_MOVE`/`WM_SIZE` 只更新内存；**`WM_CLOSE` 里再存一次**（关窗是最后能拿到有效矩形的时刻）。
  `g_rectTracking` 只在窗口摆好之后才打开，避免创建/摆放过程中的临时尺寸被误记为用户意图。
- 抽了三个小工具：`WindowInDock()`（"是否在 docker 里"只问一处）、
  `MinWindowSize()`（`WM_GETMINMAXINFO` 与尺寸还原共用同一组下限）、`CaptureFloatGeom()`。
- ⚠️ 顺手避了个坑：最初用 `sscanf` 解析 `win`，mingw 会链进整套格式化输入引擎，
  `.text` 从 0xc3f0 暴涨到 **0x11ec0（+23KB）**。已改为**手写 `ParseLong`**，`.text` 回到 0xcab0。
  改动后请留意这个体积特征，别无意中把大块 libc 拖进来。

## 修复：Release 上限收到 300ms（用户要求）

**用户要求**："Rel 上限改到 300ms，其它不变"。

- 只改 `kReleaseMaxMs`：`400.0 → 300.0`。**默认仍 150ms**，模型/机制/其它 4 个参数全不动
  （`anim_core.h` 仍与 1.3.8 逐字节相同，单格 1.89 / 17.94）。
- 同样过 `RefreshDerived` → `Clamp`：store 里若残留 >300 的旧值，载入自动钳到 300。

## 修复：Extensions 菜单项缩为 `SmoothScroll...`

**用户要求**：菜单里 `Smooth Wheel Scroll settings...` 太长，缩到与 `ReaPack` 差不多的长度；
**命令（动作名）不变**。

- Extensions 菜单标签 = **`SmoothScroll...`**（`kMenuLabel`）。
- **动作名仍是 `Smooth Wheel Scroll: settings...`** —— Actions 窗口靠它搜索，
  不动；README/AGENTS 也仍按此名描述。
- 标签不再写 open/close，**开关状态改为勾选**（`MFS_CHECKED`）。菜单是展开时重建的，
  每次都会按实时状态重算。

## 修复：面板有焦点时快捷键也能用（accelerator 回传）

**用户反馈**："焦点在弹出的设置面板，快捷键没效果，不能马上按回去，要点到 REAPER 再按，
才会生效"。即：面板有焦点 → 按键进不了 REAPER → 开面板的那个快捷键失灵。

**根因**：面板是真正的顶层窗口。它一旦拿到焦点，键盘就归它，按键只进我们的 WndProc，
**到不了 REAPER 的快捷键表**。这是任何扩展窗口的默认行为。

**官方解法**（REAPER 为此提供 `accelerator` 注册）：注册 `accelerator_register_t`，
在键盘队列里拿到键后有选择地**把键推回主窗口的动作表**。

- `PanelKeyHandler`：**仅当焦点在面板或其子控件上**才介入（否则 `return 0` = 不是我的窗口）。
- WM_KEYDOWN / WM_SYSKEYDOWN 时：
  - 焦点在**推子**且是 `←/→/Home/End` → `return -1`（**放行给窗口**，让推子自己处理）。
    ⚠️ **不能返回 1**：1 是"吃掉按键"，那样推子就收不到自己的 `WM_KEYDOWN`，方向键会失灵。
  - 焦点在**复选框**且是空格 → `return -1`（空格切换勾选）。
  - **其余一律 `return -666`** —— SDK 注释原文 "force it to the main window's accel table"。
    于是绑定的快捷键在面板有焦点时照常生效，**再按一次即关闭面板**。
- 注册时机：随 `custom_action` 一起注册；`RemoveAll()` 里 `-accelerator` 注销。
- 一个连带修正：`ToggleConfigWindow()` 关闭面板由 `SendMessage` 改为 **`PostMessage`**。
  因为它现在可能**从 REAPER 的快捷键处理里被调用**，在那里同步销毁窗口会拆掉 REAPER
  正在派发的窗口。改为投递后，关闭发生在消息循环里，安全。
- 做法与 **SWS 的可停靠窗口一致**（其 `keyHandler` 注释："force it to main reaper wnd
  (passthrough) so that main wnd actions work!"），也是这个 API 的参考实现。

## 修复：`one page` 动作不再被驱动（论坛反馈，A 方案）

**论坛反馈**：`View: Scroll view vertically one page (MIDI CC relative/mousewheel)` 及其
`reversed`，**一格滚轮把 100 条轨道直接冲到头/底**。

**根因**：这两条名字里同时有 `one page` 和 `mousewheel`。原规则把 `one page` 的排除只写在
**非 mousewheel 家族**里，于是它们进了 mousewheel 家族、**绕过排除**被驱动。而插件按设备流
投递（单格约 28 次小步），每次调用翻一整页 → **约 28 次翻页**。

**修复**：`one page` **从两个家族里都排除**（判定放在 `View` 检查之后、家族分支之前），
一律放行给 REAPER —— 一格一页，恢复原生行为。**只动这一处，其它绑定规则不变。**

**这是对"参数传达原则（五之二）"的一处有界例外**：该动作**无视传入的值**，是**离散动作**；
五之二约束的是"连续动作收下了但处理不好"，而把离散动作当连续动作喂属于**分类错误**。
**边界：只排除 `one page` 这一串**；`snap to theme` / `height` 的 mousewheel 版本仍照绑。

**回归门**：`test/check_classify.sh` —— 把改动前后的规则各跑一遍并 diff，
要求"差异只有 one page"。当前：差异 3 处、全是 one page。

### 另外两条论坛反馈：不改代码

- **Ctrl+滚轮 竖直缩放**：反馈者称"硬编码、插件看不到"，但他同一段里又写"改绑别的快捷键
  就能生效"——自相矛盾。实查本机 `reaper-kb.ini` 第 23 行：
  `Ctrl+Mousewheel → 1000 View: Zoom vertically`，**就是插件动作表里的一条**。
  故插件能看到它，**不需要改代码**。
- **MIDI 编辑器竖直缩放"没变化"**：该轴是 `kImmediate`（整格一次、直接停），
  因它是固定 2px 刻度、单格约 0.24px，**缓动无处落脚**。**这是预期行为，不是回归。**

## 修复：悬浮面板不能拖拽调整大小

- 窗口样式原来是 `WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU`，**缺 `WS_THICKFRAME`**，
  所以没有可拖拽的边框，**浮动面板完全不能改尺寸**。
- 修法：样式收成单一来源 `PanelWindowStyle()`，**仅浮动时**带 `WS_THICKFRAME`
  （停靠时 REAPER 拥有外框，加厚框会在 docker 内部画出多余边框）。
  三处 `AdjustWindowRectEx` 与 `CreateWindowEx` 全部改用它，避免"外框尺寸按没有的样式算"。

## 修复：反复开关悬浮面板，高度会越来越高

- **根因**：记住的是**整窗矩形**。恢复时把这个**整窗尺寸当成客户区尺寸**再喂给
  `AdjustWindowRectEx`，于是标题栏和边框**被算了第二遍**；而增大的值又被**存回去**，
  于是一次比一次高（累积）。
- **改法**：**只记忆位置，不记忆尺寸**。面板尺寸是设计值——每次打开都按“标题栏不换行 +
  无滚动条”重新算出（`MinPanelWidth` / `FitWindowToContent`）；位置有记录就还原，没有就
  居中于主窗口。这样高宽永远符合设计，不会漂。
- 存储键：`pos` = `"x y"`。原先的 `win` = `"x y w h"` **已废弃**：加载时不再读，
  保存时会被清空，免得 ini 里留一条会误导人的旧值。
- **实测复现**（真实 Win32 边距，headless）：旧逻辑每次重开 **+39px**
  （500→539→578→617…）；新逻辑**恒定 539px**。

## 验收

- 单格基准 **1.89 / 峰值 17.94**，与 1.0.0 一致 ✅
- 默认值（5 参数全为默认）下，各档数值与 1.3.8 **逐位相同** ✅
- 极端值 0/2 的组合均有限（无 NaN/Inf）✅
- 关掉开关：日志出现 `glide off: native pass-through`，无 `kick`（未缓存动）✅

## 构建

- 默认 = 含设置面板。
- `./build.sh --no-settings-ui` = 不编译面板（headless DLL，无动作/无菜单项/无窗口）。
