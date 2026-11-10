# 1.8.1

**Windows x64：成熟稳定版。**

**macOS：开发阶段，不是正式版。** 需要真实 Mac 用户测试并反馈；欢迎开发者 fork 一起完善。

## 更新内容

```
* macOS 使用进程内滚轮监视，不再需要辅助功能权限
+ macOS 增加原生设置面板
# 修复 macOS 触控板被当作有格鼠标处理
# 修复 macOS 插件重载后滚轮监听器残留
```

## What's changed

```
* Use process-local wheel monitoring on macOS, with no Accessibility permission
+ Add a native macOS settings panel
# Fix macOS treating a touchpad as a notched mouse
# Fix the macOS wheel monitor surviving plugin reloads
```

---

## 技术档案

### macOS 输入

- 由会话级 `CGEventTap` 改为进程内 `NSEvent` 本地监视器。
- 只观察 REAPER 自己的滚轮、键盘按下和鼠标按下事件，不接收其它应用输入。
- 滚轮快照携带有效位、单调时间戳和序号，由共享动作路径一次性接入现有双轴设备记录。
- 精确滚动、手势阶段或惯性阶段判定为连续设备，直接交回 REAPER。

### macOS 设置面板

- 原生 Cocoa 面板提供四项核心滑杆、总开关和触控板水平缩放反向开关。
- 面板通过 `PlatPanelSettingsFn` 读写共享设置，没有第二套模型或设置存储。
- 写入继续经过 `RefreshDerived()` 和 `SaveSettings()`。

### 生命周期

- 卸载时先关闭 Cocoa 面板，再移除 NSEvent monitor 和 App 失活通知观察者。
- GCD 动画 timer 卸载时清除 handler、取消并释放 source。

### 构建

- macOS 构建启用 Blocks 与 ARC。
- arm64 最低 macOS 11.0，x86_64 最低 macOS 10.13。
- 继续生成独立的 arm64 和 x86_64 dylib。
- macOS 构建不再链接 `ApplicationServices`。

### 验证

| 项目 | 结果 |
|---|---|
| 原有 9 扇门 | 通过 |
| `test/check_macos.sh` | 通过 |
| Windows x64 正式版构建与导出 | 通过 |
| Windows x64 无面板版构建与导出 | 通过 |
| macOS 共享核心 x86_64 / arm64 编译 | 通过 |
| macOS GCD timer 与入口 x86_64 / arm64 编译 | 通过 |
| macOS AppKit 编译、dylib 链接与入口导出 | GitHub Actions 通过 |
| REAPER-on-macOS 实机行为 | 尚未验证，需要用户反馈 |

### 附件

| 平台 | 文件 | 状态 |
|---|---|---|
| Windows x64 | `reaper_smoothwheelscroll-x64.dll` | 成熟稳定 |
| Windows ARM64 | `reaper_smoothwheelscroll-arm64.dll` | 测试 |
| macOS arm64 | `reaper_smoothwheelscroll-mac-arm64.dylib` | 开发 |
| macOS x86_64 | `reaper_smoothwheelscroll-mac-x86_64.dylib` | 开发 |

### 修复：启动诊断与 macOS 开发状态

- macOS 启动日志明确标记为开发构建，不是正式版。
- 日志逐项报告启动入口、平台架构、REAPER 版本、启动结果、滚轮监视器、设置动作、设置面板和总开关。
- macOS 日志区分 CI 已验证的编译/链接/导出与仍需真机确认的 REAPER 运行状态。
- 日志提供鼠标滚轮、触控板、设置面板和重载测试项，并提供 issue 与 fork 入口。
- 修正 Windows 启动日志中的陈旧版本号。

macOS CI：`42731748567`，AppKit 编译、两架构链接、入口检查和 artifact 上传通过。

### SHA-256

| 文件 | 字节 | SHA-256 |
|---|---:|---|
| `reaper_smoothwheelscroll-x64.dll` | 183119 | `2E9BC2EAC62F779D0EACCB66F2F003CE6E0B9AC450768799B3410D3872F6701E` |
| `reaper_smoothwheelscroll-arm64.dll` | 256512 | `BF363C20C500F9EADB3D5C4F1ABEBEA0D67758253DAA10DCB9BF9EAFE63B6307` |
| `reaper_smoothwheelscroll-mac-arm64.dylib` | 155248 | `A605880A8F7358CD9FB3D9900FC1720D113AB8EE7750C5C13DD92696D587B903` |
| `reaper_smoothwheelscroll-mac-x86_64.dylib` | 149616 | `A621B0A7B2A217A0E2EB307CC392182313941ACBF06368751EC452EC5D153365` |
