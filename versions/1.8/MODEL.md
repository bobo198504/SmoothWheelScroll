# 1.8

## 更新内容

```
* 多平台重构：Windows x64 / Windows ARM64 / macOS，共用同一套模型与投递层
* 支持 macOS（测试版）：滚轮缓动在 Mac 上可用
* 支持 Windows ARM64
+ 启动时写入诊断日志，便于反馈问题
# 触控板反向对水平缩放不生效（1.7.4 引入，已修复）
```

## What's changed

```
* Multi-platform refactor: Windows x64 / Windows ARM64 / macOS share one model and delivery layer
* macOS support (test build): wheel easing works on Mac
* Windows ARM64 support
+ A diagnostic log is written at startup, to make reports easier
# Touchpad reverse had no effect on horizontal zoom (regression since 1.7.4, fixed)
```

---

## 说明（不写进 release 说明，只留档）

### 一、`# 触控板反向对水平缩放不生效（1.7.4 引入，已修复）`

**版本归属已核实**（逐个 tag 对比 `LastWheelWasTouchpad` 的实现）：

| 版本 | 写法 | 结果 |
|---|---|---|
| 1.7.0～1.7.3 | `LastWheelDevice()`（不带参数） | ✅ 正常 |
| **1.7.4** | `LastWheelDevice(true)` | ❌ **从这里开始坏** |
| 1.7.5 / 1.7.6 / 1.7.7 | 同上 | ❌ 一直坏 |
| 1.8 | 读两条流里较新的一条 | ✅ 修好 |

**1.7.4 的提交**：`cc33664 Add: horizontal wheel + row-height compensation + top-up direction fix`
—— 那次加入水平滚轮支持时把函数改成了只读水平流，之后没人发现。

**根因**：`LastWheelWasTouchpad()` 只读水平滚轮那条记录流，而**触控板双指上下滑产生的是普通垂直滚轮消息**，
记录写在垂直流里。读不到 → 判定"不是触控板" → 反向被跳过。

**修法**：读两条流里**较新的那条**（比较用有符号差值，`GetTickCount` 每 49.7 天回绕也安全）。

**实测**：日志出现 72 次 `touchpad reverse`，符号正确翻转（`val=125 → val=3`，−2.25 → +2.25）。

**⚠️ 教训**：加入"水平滚轮"这类新概念时，凡是"读哪个轴"的选择都要问一句
"**会不会有设备从另一个轴产生同样的结果**"。当时把"水平缩放 ⇒ 水平滚轮"当成了必然，
而实际最常见的触发方式是触控板上下滑。

### 二、`* 支持 macOS（测试版）`

**⚠️ 这一条是测试版，不是正式发布。**

| 项 | 状态 |
|---|---|
| 主文件为 macOS 编译 | ✅ 通过（Zig clang，错误 206 → 0） |
| 5 个 Mac 源文件 | 4 个编译通过；`wheel_mac.mm` 需要 `ApplicationServices`，本机无 SDK，**未编译** |
| 链接成 `.dylib` | ❌ **没做过** |
| 在真 Mac 上运行 | ❌ **没跑过** |

**Windows 侧零影响**：14 扇门全过，导出表只有 `ReaperPluginEntry`，`.text` +1488 字节
（平台转发层 + 启动诊断）。

**Mac 版没有设置面板**：SWELL 在 macOS 上不提供 `CreateWindowEx` / `RegisterClass`，
且 `SWELL_CreateXBridgeWindow` 在 OSX 下被 `#ifndef` 排除。面板要重写成 Cocoa，留待下一轮。
设置仍从 extstate 读取，存过的值生效。

**细节见 `MACOS-PORT.md`。**

### 三、`* 支持 Windows ARM64`

**能编译，导出已验证，但没在 ARM 机器上跑过。**

Zig `-target aarch64-windows` 编译主文件，0 错误。

**导出表已核实**（`zig objdump --exports`）：

```
Ord  Hint    RVA   Name
  1     0   105c | ReaperPluginEntry
```

⚠️ **验证时踩过的一个坑**：w64devkit 的 `objdump` **不认 ARM64 格式**，对 ARM64 DLL
会显示空导出表，看起来像"没导出"。**别用它查 ARM64，用 `zig objdump --exports`。**

DLL 大小 256512 字节（比 x64 大，因为 Zig 链接时未做 `-static` 那套裁剪）。

**是否真能被 ARM64 版 REAPER 加载，未经证实。**

### 四、`+ 启动时写入诊断日志`

Windows 版仍默认关闭（`--debug-log` 才开）。**macOS 版默认开启** —— 因为测试者拿不到别的构建，
日志是唯一的诊断通道。

**日志位置**：`$TMPDIR/SmoothWheelScroll.log`（Mac）、`%TEMP%\SmoothWheelScroll.log`（Windows）。

启动时打印：日志路径、构建平台、REAPER 版本、滚轮钩子是否安装、总开关状态。

---

## 四个产物

| 平台 | 文件 | 状态 |
|---|---|---|
| **Windows x64** | `reaper_smoothwheelscroll-x64.dll` | **正式，已验证** |
| **Windows ARM64** | `reaper_smoothwheelscroll-arm64.dll` | DEV，仅编译 |
| **macOS arm64** | `reaper_smoothwheelscroll-mac-arm64.dylib` | DEV，需在 Mac 上编译 |
| **macOS x86_64** | `reaper_smoothwheelscroll-mac-x86_64.dylib` | DEV，需在 Mac 上编译 |

**macOS 两个产物无法在此生成**（无 macOS SDK）。用户需在 Mac 上运行 `./build.sh --mac`。
