# macOS port — status and design

Read this before touching `src/platform/`. It separates what is built and source-checked from what
still requires a real Mac running REAPER.

## 1. Current state

| Piece | State |
|---|---|
| Shared model / routing | verified by the Windows regression gates; unchanged by the platform work |
| `smooth_wheel_scroll.cpp` for macOS | compiles for x86_64 and arm64 with Zig clang |
| `src/platform/mac/wheel_mac.mm` | process-local NSEvent monitor; latest change awaits the macOS runner |
| `src/platform/mac/panel_mac.mm` | native Cocoa settings panel; latest change awaits the macOS runner |
| `src/platform/mac/time_mac.mm` | GCD timer; both architectures compile locally with Zig |
| `src/platform/mac/entry_mac.mm` | REAPER entry/unload wrapper; x86_64 compiles locally with Zig |
| Full `.dylib` link | must be run by `.github/workflows/macos.yml` after this change |
| REAPER on a physical Mac | **not verified here** |

`test/check_macos.sh` guards the source contract. It is not a substitute for loading the dylib in
REAPER.

## 2. The boundary

The animation model is platform-free. `model.h`, `anim3_core.h`, `anim161_core.h`, `device.h`,
`macro.h` and `routing.h` are reused, not reimplemented.

`platform.h` is the OS seam: process-local wheel observation, hit test, frame clock, monotonic time
and the optional settings panel. Anything expressible in portable C++ belongs above that seam.

## 3. SWELL

`reaper_plugin.h` includes WDL/SWELL on non-Windows builds. The plugin relies on SWELL for REAPER's
Win32-shaped action and window APIs, but not for input observation or window creation.

Not available or unsuitable on macOS:

| API | Consequence |
|---|---|
| `SetWindowsHookEx` / `CallNextHookEx` | replaced by an AppKit process-local monitor |
| `CreateWindowEx` / `RegisterClass` | the Cocoa settings panel is still future work |
| `timeSetEvent` / `timeKillEvent` | replaced by a GCD dispatch source |

SWELL names calls without the Win32 `A` suffix; `win32_compat.h` supplies the narrow mapping.

## 4. Settings panel

The shared Win32 panel stays compiled out on macOS (`SWS_NO_SETTINGS_UI`). A native Cocoa window in
`panel_mac.mm` provides the four feel sliders, master switch and touchpad-reverse switch. It is
registered as the normal `Smooth Wheel Scroll: settings...` action.

The Cocoa panel does not own settings. A narrow callback fills a `PlatPanelSettings` snapshot or
writes it back to the shared globals; the write runs through the same clamp and `SaveSettings()` path
as Windows. There is no second model or settings store.

`LoadSettings()` reads REAPER ExtState section `SmoothWheelScroll`:

| Key | Meaning |
|---|---|
| `window` | glide length in milliseconds |
| `startd` | slow-step travel |
| `budget` | speed ramp-up |
| `speedmul` | maximum speed multiplier |
| `sspd0`…`sspd7` | per-surface speed factors |
| `glide` | master switch |
| `tprevzoom` | reverse touchpad horizontal zoom |

The Mac panel is intentionally compact and not dockable. A visual redesign should be evaluated on a
physical Mac before changing it.

## 5. Wheel path

### 5.1 Process-local observer

The plugin already runs inside REAPER, so `wheel_mac.mm` uses:

```objc
[NSEvent addLocalMonitorForEventsMatchingMask:... handler:...]
```

This observes only events delivered to REAPER. It does **not** require Accessibility permission,
does not see other applications, and needs no helper process.

The monitor returns the event unchanged. REAPER still resolves the wheel into an action, and
`hookcommand2` decides whether that action is animated or passed through.

### 5.2 Wheel-to-action pairing

The monitor records:

| Field | Purpose |
|---|---|
| `valid` | false after a key/mouse click or application deactivation |
| `continuous` | precise deltas or a gesture/momentum phase means native touchpad family |
| `horizontal` | axis of the latest event |
| `timestampMs` | monotonic age check |
| `sequence` | prevents the same observation being consumed repeatedly |

The shared action path copies the latest event into the existing per-axis device and latch records.
Continuous input becomes `Device::kTouchpad` and is passed to REAPER; notched input enters the normal
model. Keyboard and mouse input clears both axes, preventing a recent wheel from being attributed to
an unrelated command.

The Cocoa panel, local monitor and notification observer are removed unconditionally on unload. This
matters: leaving any of them behind would point into an unloaded dylib after plugin reload.

### 5.3 Coverage

Everything REAPER resolves through the action table is covered. Surfaces handled only by native
controls without an action-table callback may retain their native behaviour.

## 6. Frame clock

`time_mac.mm` deliberately uses `15.6 ms` to match the measured Windows `SetTimer` cadence.

The model is rate-independent, so changing the Mac interval changes visible smoothness but not total
travel. A faster interval may be preferable on modern Macs, but this is a product-feel decision:
test it on a Mac before changing `kCoalesceMs`.

The GCD source is created on the main queue, so ticks can call REAPER directly. Shutdown clears its
handler, cancels and releases it before unloading.

## 7. Diagnostics

The macOS build always writes:

```sh
open -e "$TMPDIR/SmoothWheelScroll.log"
```

Startup records the log path, build, REAPER version, observer state and master switch. Missing wheel
input lines point to the local monitor or action pairing; do not start by changing the model.

## 8. Verification

| Claim | Evidence |
|---|---|
| Windows behaviour unchanged | nine model/routing gates plus Windows build and export check |
| shared core parses for both Mac architectures | Zig clang x86_64 and arm64 object builds |
| monitor contract / unload / build flags | `test/check_macos.sh` |
| SDK link and `ReaperPluginEntry` export | GitHub `macos-latest` workflow |
| wheel feels correct in REAPER | **not yet verified here; requires a physical Mac** |

## 9. Build and install

On a Mac:

```sh
bash build.sh --mac
```

Outputs:

```text
build/reaper_smoothwheelscroll-mac-arm64.dylib
build/reaper_smoothwheelscroll-mac-x86_64.dylib
```

Deployment targets are macOS 11 for arm64 and macOS 10.13 for x86_64. The dylibs are ad-hoc signed,
not Developer ID signed or notarised.

Copy the file matching `uname -m` to:

```text
~/Library/Application Support/REAPER/UserPlugins/
```

No Accessibility permission is required.

## 10. Remaining work

1. Confirm the GitHub macOS build and exported entry point.
2. Load the dylib in REAPER on a physical Mac and inspect the startup log.
3. Check notched mouse, free-spinning mouse, touchpad, horizontal wheel, custom actions and reload.
4. Decide whether Mac should keep the 15.6 ms cadence or use a faster interval.
5. Exercise the Cocoa settings panel, persistence and live updates on a physical Mac.
