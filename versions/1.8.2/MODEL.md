# 1.8.2

**Windows x64：成熟稳定版。**

**macOS：开发阶段，不是正式版。** 需要真实 Mac 用户测试并反馈；欢迎开发者 fork 一起完善。

## 更新内容

```
+ macOS 启动日志增加开发状态、组件自检、真机测试项和反馈入口
# Windows 启动日志中的版本号
```

## What's changed

```
+ The macOS startup log now reports development status, component checks, physical-Mac test steps and feedback links
# The version shown in the Windows startup log
```

---

## 技术档案

### 状态声明

- Windows x64 已经过多次实际使用和迭代，本轮不改变其模型、路由或行为。
- macOS 仅完成 macOS CI 编译、链接和导出验证；REAPER-on-macOS 运行仍需真机证据。
- macOS 附件继续标记为 `DEV (macOS development build)`。

### 启动自检

日志现在逐项报告：

- 插件是否进入共享启动入口；
- 构建平台、架构和版本；
- REAPER 版本；
- 启动是否成功；
- 滚轮监视器是否安装；
- 设置动作是否注册；
- 设置面板是否可用；
- 总开关和当前核心参数。

macOS 还会额外报告：

- 这是开发构建，不是正式版；
- CI 已验证构建、链接和导出；
- REAPER 内运行证据来自当前启动报告；
- 不请求辅助功能权限；
- 鼠标滚轮、触控板、设置面板和重载的测试步骤；
- issue 反馈入口和 fork 贡献说明。

### 验证

| 项目 | 结果 |
|---|---|
| 原有 9 扇门 | 通过 |
| `test/check_macos.sh` | 通过 |
| Windows x64 构建与导出 | 通过 |
| Windows ARM64 构建与导出 | 通过 |
| macOS 共享核心 x86_64 / arm64 编译 | 通过 |
| macOS AppKit 编译、dylib 链接与入口导出 | GitHub Actions 通过 |
| REAPER-on-macOS 真机测试 | 仍需用户反馈 |

### 附件

| 平台 | 文件 | 状态 |
|---|---|---|
| Windows x64 | `reaper_smoothwheelscroll-x64.dll` | 成熟稳定 |
| Windows ARM64 | `reaper_smoothwheelscroll-arm64.dll` | 测试 |
| macOS arm64 | `reaper_smoothwheelscroll-mac-arm64.dylib` | 开发 |
| macOS x86_64 | `reaper_smoothwheelscroll-mac-x86_64.dylib` | 开发 |
