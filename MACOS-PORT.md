# macOS port — status and design

**Read this before touching `src/platform/`.** It records what has been decided, what has been
*verified*, and — the important part — what has **not**.

---

## 1. Where it stands

| Piece | State | Verified how |
|---|---|---|
| `src/platform.h` | **done** | both platforms build against it |
| `src/platform/win32/time_win32.cpp` | **done** | compiles; Windows behaviour unchanged |
| `src/platform/win32/wheel_win32.cpp` | **done** | compiles |
| Win32 adaptors (end of `smooth_wheel_scroll.cpp`) | **done** | 14 gates pass; export table unchanged |
| Entry split (`SwsEntryImpl` + `SwsSharedEntry` + per-platform `ReaperPluginEntry`) | **done** | export table identical to 1.7.7 |
| `src/platform/mac/time_mac.mm` | **compiled for macOS** ✅ | Zig clang, `-target x86_64-macos` |
| `src/platform/mac/panel_mac.mm` | **compiled for macOS** ✅ | deliberate stub (no panel) |
| `src/platform/mac/entry_mac.mm` | **compiled for macOS** ✅ | Zig clang |
| `src/platform/mac/win32_compat.{h,mm}` | **compiled for macOS** ✅ | Zig clang |
| `src/platform/mac/wheel_mac.mm` | **written, CANNOT be compiled here** ⚠️ | needs `ApplicationServices` |
| `smooth_wheel_scroll.cpp` for macOS | **compiles** ✅ | 206 errors → 0, via Zig clang |
| Full `.dylib` link | **never done** | no macOS SDK on this machine |
| `build.sh --mac` | **written, never run** | — |

**Nothing has run on a Mac.** "Compiled for macOS" means Zig's clang produced a Mach-O object for
the right target on a Windows machine — a real parse and typecheck, not a link, and certainly not an
execution.

---

## 2. The one architectural decision that matters

**The model is already platform-free.** `model.h`, `anim3_core.h`, `anim161_core.h`, `device.h`,
`macro.h` — about 1900 lines — contain no operating-system code. They are the *feel* of the plugin
and are reused verbatim on Mac. That was always true and is why a port is possible at all.

**`platform.h` is the boundary**, and it is deliberately narrow: wheel hook, frame clock, hit test,
optional panel, monotonic clock. The rule for anything wanting to join it: *if it could be written
in portable C++, it belongs above the line, not below it.*

---

## 3. ★ SWELL: what it gives, and what it does not

`reaper_plugin.h` includes `../WDL/swell/swell.h` on non-Windows builds. **SWELL is Cockos' Win32
emulation layer** and it is what makes the port tractable — but it is **not complete**, and the gaps
decided the shape of this work. `third_party/reaper-sdk-git/WDL/` now ships those headers (zlib-style
licence, redistribution permitted).

**HAVE:** `HWND`, `WM_*`, `SetTimer`, `KillTimer`, `GetDlgItem`, `SetWindowPos`, `InvalidateRect`,
`GetWindowLong`, `SetWindowLong`, `SendMessage`, `PostMessage`, `GetParent`, `GetClassName`,
`DefWindowProc`, `FindWindowEx`, `LoadLibrary`, `GetSystemMetrics`, `SetBkMode`, `GetSysColor`,
`SCROLLINFO`, `SWELL_DrawText`, `SWELL_FillRect`, `dispatch` and `mach_time` headers, libc.

**DO NOT HAVE — and these are the ones that matter:**

| Missing | Consequence |
|---|---|
| `SetWindowsHookEx` / `CallNextHookEx` | **The Windows wheel hook cannot exist on Mac.** Replaced by a CGEventTap. |
| `CreateWindowEx` / `RegisterClass` | **The settings panel cannot be created.** See §4. |
| `timeSetEvent` / `timeKillEvent` | No multimedia timer. Replaced by a GCD dispatch source. |
| `TextOut` / `FrameRect` / `GetTextExtentPoint32` | Panel drawing would need `SWELL_*` rewrites. |

Note also: **`SWELL_CreateXBridgeWindow` is inside `#ifndef SWELL_TARGET_OSX`** — the one API that
looks like it would create a custom window is compiled OUT on macOS. That is the decisive fact for §4.

SWELL names its functions **without the A suffix** (`GetClassName`, not `GetClassNameA`), which is
why `win32_compat.h` is mostly a wall of `#define`s rather than implementations.

---

## 4. The panel is not in the Mac build — a decision, not a stub

The Windows panel is ~3000 lines: four hand-drawn controls (faders, knobs, motion chart), theme
detection, docking, window placement, keyboard forwarding. All of it rests on `RegisterClass` +
`CreateWindowEx`, **which SWELL does not provide on macOS**. A Cocoa panel is therefore a
**from-scratch UI**, not a translation.

So the Mac build defines `SWS_NO_SETTINGS_UI` and ships with `PlatPanelPresent() == false`.

**This degrades less than it sounds:** `LoadSettings()` is compiled unconditionally and reads every
setting from REAPER's extstate (section `SmoothWheelScroll`), so a saved configuration is honoured
with no UI at all. The keys that matter for feel are `window`, `startd`, `budget`, `speedmul`, then
`sspd0`…`sspd7`.

**The order of work is deliberate:** get the wheel animating, then build a panel where it can be
seen. A panel for a plugin whose wheel path does not work is decoration.

---

## 5. The wheel path on macOS

### 5.1 The tap is an OBSERVER

On Windows the hook decides "is this wheel mine" and re-drives the action itself. That relies on the
hook running inside REAPER's own thread. On macOS the equivalent would have to know *which action*
the wheel resolves to, and that resolution happens inside REAPER's AppKit handling, which a tap
cannot see.

So the Mac design is the portable one the plugin **already uses on Windows for the arrange area**:
let the host resolve the wheel, and hook the action table via `hookcommand2` (which REAPER calls on
macOS too). The tap's only job is to remember the most recent wheel — how big, and whether it came
from a continuous surface (`kCGScrollWheelEventIsContinuous`).

**What a tester loses:** surfaces the plugin reaches only through the Windows hook (the TCP panel's
own wheel, the divider strips) are not animated on macOS. Everything driven through the action table
is.

### 5.2 Accessibility permission is mandatory

`CGEventTapCreate` needs **System Settings → Privacy & Security → Accessibility** to include REAPER.
Without it the tap is not created, `WheelHookInstall()` returns false, and the plugin sees no wheel
at all — it looks completely dead.

**The startup log says this in plain words**, because otherwise a tester has nothing to go on.

### 5.3 The cadence matches Windows, on purpose

Windows asks `SetTimer` for 5 ms and gets **~15.6 ms** (clamped by the system; AGENTS 10.1 measured
15.58 ms). The model is rate-independent — one notch delivers the same total at 50/64/240/1000 Hz —
but the number of visible steps follows the frame rate.

A GCD timer honouring 5 ms would fire three times as often and feel denser than Windows. That is a
real difference, so `time_mac.mm` targets the **measured Windows cadence** (`kCoalesceMs = 15.6`).

**If a tester finds the Mac build too coarse or too smooth, change that one constant.** It should be
a deliberate choice followed by a feel test, not a "fix".

---

## 6. The log is the only diagnostic channel

**On macOS the debug log is ON BY DEFAULT** (`kDebugLog` is true for any non-Windows build). A Mac
tester cannot be asked to install a special debug build — the build they have is the only one that
can exist — and the questions that matter are exactly the ones only a log answers.

It lands in `$TMPDIR/SmoothWheelScroll.log`:

```
open -e "$TMPDIR/SmoothWheelScroll.log"
```

The first lines are a startup report: **where the log is, which build and REAPER version, whether
the wheel hook installed, and whether the glide master switch is on.** Those four facts decide
whether a report is diagnosable at all.

---

## 7. How the Mac sources were checked without a Mac

**Zig 0.17.0** (in `D:\Projects\Code\_tools\zig`) was used as a cross-compiler:

```
zig c++ -target x86_64-macos -std=c++17 -fno-rtti -c -DSWS_NO_SETTINGS_UI \
    -I src -I third_party/reaper-sdk-git/sdk -I third_party/reaper-sdk-git \
    src/smooth_wheel_scroll.cpp -o main.o
```

Zig carries its own libc and **`dispatch` / `mach_time` headers**, so the clock and the entry point
compile for real. It does **not** carry `CoreFoundation`, `ApplicationServices` or `AppKit`, which is
why `wheel_mac.mm` is the one file that cannot be checked here.

**The 206 → 0 path**, recorded so a future port does not repeat it:

| Step | Errors |
|---|---|
| Wrapped Win32 includes, added SWELL + compat header | 206 |
| Added the 11 missing types (`SIZE`, `SYSTEMTIME`, `LARGE_INTEGER`, …) | 181 |
| A-suffix #define map (`GetClassNameA` → `GetClassName`, …) | 134 |
| `SCROLLINFO` was SWELL's already; `QueryPerformance*` via `plat::Now()` | 109 |
| Constants (`WHEEL_DELTA`, `HWND_MESSAGE`, `HC_ACTION`, …) | 104 |
| Excluded the hook and the animation window on non-Windows | 22 |
| `__declspec` / `DllMain` guarded | 5 |
| Fixed an unbalanced `#ifdef` | **0** |

**Windows did not change:** 14 gates pass, export table is `ReaperPluginEntry` only, `.text` grew
`0x15030` → `0x15600` (+1488 bytes: platform forwarders plus the startup diagnostics).

---

## 8. What is left, in order

1. **Build the `.dylib` on a Mac** (`./build.sh --mac`). Expect the first failure to be an include
   path or an SDK-version difference, not a logic error.
2. **Install to `~/Library/Application Support/REAPER/UserPlugins/`** and reload REAPER.
3. **Grant Accessibility**, then check whether a wheel animates.
4. **Read the log** before theorising: `open -e "$TMPDIR/SmoothWheelScroll.log"`.
5. **Then** the cadence constant, and hit testing via `GetThingFromPoint`.
6. **Then** the panel, as its own piece of work.

**Do not write the Mac panel before step 3 succeeds.**
