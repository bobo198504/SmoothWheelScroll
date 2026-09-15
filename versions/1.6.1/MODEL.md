# SmoothWheelScroll for REAPER 1.6.1

**修复版**。相对 **1.6.0**，本版修掉一个**与第三方 Darkmode 插件冲突**的问题，
并把设置面板的控件描述**表格化**（便于后续加功能）。**动画模型零改动。**

DLL md5 `01eaea81e8858af7a5b50f03e02c41c6`
`anim_core.h` md5 `d785d9371295448461a033bb1efd179e`（与 1.6.0 **逐字节相同**）
`smooth_wheel_scroll.cpp` md5 `9bd33762adaf82cf386111950af7da69`

## 本版相对于 1.6.0 的改动

| # | 改动 | 性质 |
|---|---|---|
| 1 | 总开关改为**面板自绘**，不再用 `Button` 子控件 | 修复 |
| 2 | 面板控件描述**表格化**（加一个控件 = 加一行表） | 重构 |

---

## 1. 修复：与第三方 Darkmode 插件冲突，导致总开关的勾不显示

### 现象

装了第三方 REAPER 暗色插件后，设置面板里 "Enable smooth scrolling" 那一条**勾不出来**
（功能本身正常，只是看不见勾）。

### 根因（已定位到对方代码，不在本插件）

第三方暗色插件（如 `ReaperDarkMode`，其原理是把亮色面板按算法改暗）用
`SetWindowSubclass` **按"窗口类名 + 按钮样式"分派**。对 `Button` 类窗口，它这样判断：

```c
DWORD typeStyle = GetWindowLong(hwnd, GWL_STYLE) & BS_TYPEMASK;
if (!isStandardButton && !isCheckBox && !isRadio && !isGroupBox) {
    // 它认为"不是标准样式的 Button" = REAPER 用来假扮 SysLink 的控件
    SetWindowSubclass(hwnd, FakeSysLinkSubclassProc, 10101, 0);
}
```

本插件的总开关是 `BS_OWNERDRAW`，**不在它列举的四种之内** → 被判为"假 SysLink"，
挂上那个过程函数，而它**完全接管 `WM_PAINT`**（且不调用 `DefSubclassProc`）：

```c
if (uMsg == WM_PAINT) { FillRect(父窗口底色); SetTextColor(RGB(0,150,255)); DrawTextW(...); return 0; }
```

于是按钮自己的 `WM_PAINT` 不再执行 → **`WM_DRAWITEM` 永不触发** → 本插件画的方框与勾
一次都没被调用，只剩对方画的那行文字。**勾就是这样消失的。**

**为什么只有这一条中招**：对方认识的类名是 `Button` / `Static` / `#32770` / `REAPER*` 前缀。
本插件的面板、推子、旋钮都用**自己的类名**（`SmoothWheelScroll*`），对方不认、不碰；
只有总开关用了系统的 `Button` 类。

### 修法：总开关改为**面板自绘**

不再创建任何 `Button` 子控件：开关的方框、勾、文字、焦点框全部由面板在 `WM_PAINT` 里画，
点击由 `WM_LBUTTONDOWN` 在它的矩形内命中判定。状态只存在 `g_glideOn`
（存档与运行开关本来就用它）。

**结果：本插件不再创建任何 `Button` 类窗口**，因此**不会被"按类名分派"的工具接管**。

**顺带修掉两个旧麻烦**：

- **不透明色带**：标准/主题化复选框会把整个矩形刷成自己的面色，此前只能靠"缩到文字宽度"绕开；
  现在矩形就是面板色。
- **`BS_OWNERDRAW` 不保存勾选状态**（`BM_SETCHECK` 对它无效）：现在连"问控件"这一步都没有。

---

## 2. 重构：面板控件表格化

**目的**：后续加功能时，"加一个控件"不该变成"改好几处、漏一处还不报错"。

- **控件 ID 改为按索引计算**（`SliderIdOf` / `LabelIdOf` / `KnobIdOf`），
  删掉原先 15 行手写枚举 —— 那是最容易忘、且必须与控件表同序的一处。
- **`SliderSpec` 一行描述一个控件的全部**：名称、值、范围、单位、两端标签、默认值、颜色，
  以及它的旋钮（值/范围/默认）。
- **删掉三个平行数组**（`kSliderHue` / `kDefaultPerSlider` / `kDefaultPerKnob`），并入该行。
  它们原是"第二处真相"：改一处忘一处会导致"复位到别的参数"或颜色错位，且不报错。

**以后加一个控件只要三步**：`kNumSliders` 加一 → 加 TUNING 常量 → `BuildSliderSpecs` 表里加一行。

（本项为纯重构，**行为与外观应完全一致**：新旧表已逐行核对，行序、范围、默认值、颜色、旋钮绑定全部相同。）

---

## 验证

- 默认构建与 `--no-settings-ui` 都通过；`-Wall -Wextra` 零告警。
- `test/check_curve_model.sh`：单格 **1.890**、单峰 9/9、封顶 5/5、连滚累积、与步长无关 —— 全过。
- `test/check_classify.sh`：差异 3 处、全是 `one page` —— 与 1.6.0 相同。
- `anim_core.h` 与 1.6.0 **逐字节相同**（模型零改动）。
- DLL 163455 → **161652** 字节。

> **待用户实测**：装第三方 Darkmode 时总开关是否正常（点选、勾显示、空格/焦点）。
> 本地无法复现该插件的影响。
