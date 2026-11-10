# 1.8.1

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
| REAPER-on-macOS 实机行为 | 由用户验证 |

### 附件

| 平台 | 文件 | 状态 |
|---|---|---|
| Windows x64 | `reaper_smoothwheelscroll-x64.dll` | 正式 |
| Windows ARM64 | `reaper_smoothwheelscroll-arm64.dll` | 测试 |
| macOS arm64 | `reaper_smoothwheelscroll-mac-arm64.dylib` | 测试 |
| macOS x86_64 | `reaper_smoothwheelscroll-mac-x86_64.dylib` | 测试 |
