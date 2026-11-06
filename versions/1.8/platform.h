// ---------------------------------------------------------------------------
// platform.h -- THE ONE PLACE THIS PLUGIN TOUCHES THE OPERATING SYSTEM.
//
// WHAT THIS FILE IS FOR. The motion model (model.h, anim3_core.h, anim161_core.h, device.h,
// macro.h) has no platform code at all and never will: it is arithmetic on doubles. Everything
// below that model -- noticing a wheel, asking what is under the pointer, running a frame clock,
// showing the settings panel -- is operating-system work, and that is what this header declares.
//
// WHY IT EXISTS AT ALL. The Windows version grew up as one translation unit, so Win32 types were
// spelled out wherever they happened to be needed (HWND in a function signature, DWORD in a
// timestamp, InterlockedCompareExchange in a guard). That is fine for one platform and fatal for
// two: the moment a second platform arrives, every one of those spelling choices becomes a porting
// decision scattered across 6800 lines. Concentrating them here means the Mac build has ONE file to
// disagree with, not three hundred.
//
// THE SHAPE OF THE PORT, deliberately shallow:
//
//   * OPAQUE TYPES, NOT WINDOWS TYPES. `PlatWindow` and `PlatApp` stand where HWND and HINSTANCE
//     used to. On Windows they ARE those handles, defined as such in platform/win32/; on Mac they
//     are Objective-C object pointers. Nothing above this header knows which.
//
//   * THE WHEEL IS AN EVENT, NOT A MESSAGE. The Windows hook reads WM_MOUSEWHEEL; the Mac tap reads
//     a CGEvent. Both arrive here as a PlatWheelEvent carrying the two numbers the rest of the code
//     actually uses: how far the wheel turned, and whether the sender was a continuous surface. The
//     units are normalised in the platform layer -- see kWheelUnit below.
//
//   * THE CLOCK IS A CALLBACK, NOT A TIMER ID. Each platform schedules it its own way
//     (timeSetEvent on Windows, a GCD dispatch source on Mac) and calls back onto the UI thread.
//
//   * THE PANEL IS OPTIONAL. `PlatPanelPresent` is false when a build has no settings UI at all.
//     That is a FIRST-CLASS configuration, not a degraded one: the plugin reads its settings from
//     REAPER's extstate either way, so a build with no panel still honours whatever was saved, and
//     a Mac build can ship before the panel has been written.
//
// WHAT IS *NOT* HERE. The model, the delivery grids, the top-up, the macro parsing, the routing
// table -- none of it. They are platform-free today and this header must not tempt anyone to give
// them a window handle they do not need.
// ---------------------------------------------------------------------------
#ifndef SWS_PLATFORM_H
#define SWS_PLATFORM_H

#include <cstddef>

// ---------------------------------------------------------------------------
// Opaque handles.
//
// `PlatWindow` is "a window somewhere in the host application": REAPER's main window, a mixer
// panel, a MIDI editor. The plugin never creates one and never inspects one; it passes them back to
// the host (as the action's target window) and compares them for equality.
//
// `PlatApp` is the host module handle. Only the entry point and the panel need it.
// ---------------------------------------------------------------------------
#ifdef _WIN32
struct HWND__;
struct HINSTANCE__;
typedef HWND__ *PlatWindow;
typedef HINSTANCE__ *PlatApp;
#else
struct PlatWindowOpaque;
typedef PlatWindowOpaque *PlatWindow;
struct PlatAppOpaque;
typedef PlatAppOpaque *PlatApp;
#endif

// ---------------------------------------------------------------------------
// Wheel units.
//
// ★ THE UNIT IS FIXED AT "ONE NOTCH = kWheelUnit", AND IT IS A PLUGIN DECISION, NOT A PLATFORM ONE.
//
// Every number upstream of the model is expressed in wheel deltas with one notch = 120, because
// that is what WM_MOUSEWHEEL reports and what the whole codebase (Travel(), the row-height gain, the
// delivery grids) is calibrated against. A platform that reports something else converts AT THE
// EDGE, in its own event handler, so that one rule holds everywhere:
//
//   * Windows: delta is already 120 per notch. No conversion.
//   * macOS:   a legacy mouse reports whole "lines" (NSScrollWheel: 1, 2, 3...), and a trackpad
//              reports a fractional pixel-ish amount. The Mac handler multiplies a line-based
//              device by 120 so a notched mouse still means "one notch = 120"; a CONTINUOUS device
//              never reaches the model at all (it is passed through, see PlatWheelEvent::continuous
//              and IsAnimatableWheel), so its raw units do not need to match anything.
//
// Keeping this in the header, rather than in whichever handler happens to be nearest, is what stops
// the Mac build from quietly redefining "notch" and making every tuned value wrong at once.
// ---------------------------------------------------------------------------
static const double kWheelUnit = 120.0;

// One wheel turn as the plugin sees it.
struct PlatWheelEvent
{
  double delta = 0.0;          // signed, in kWheelUnit (120) units: +120 = one notch "up"
  bool horizontal = false;     // a tilt wheel or a thumb wheel (Windows: WM_MOUSEHWHEEL)
  bool continuous = false;     // ★ a touchpad or a free-spinning surface: ALREADY SMOOTH, never
                               //   fed to the model (see IsAnimatableWheel). This is the field that
                               //   carries the platform's own verdict, because only the platform can
                               //   tell: Windows reads it from the mouse's signature in extraInfo,
                               //   macOS from kCGScrollWheelEventIsContinuous.
  bool hasExtraInfo = false;   // whether `extraInfo` is meaningful (Windows only; macOS leaves false)
  unsigned long long extraInfo = 0;
};

// ---------------------------------------------------------------------------
// The platform interface. Implemented once per platform in platform/<name>/.
//
// DELIBERATELY SMALL. Every entry here is something the plugin genuinely cannot do for itself. If a
// candidate function could be written in portable C++, it belongs above this line, not below it.
// ---------------------------------------------------------------------------
namespace plat
{
// The frame clock. `tick` is called on the UI thread every few milliseconds while the animation
// runs; `start`/`stop` are idempotent so the caller may be careless about double calls.
//
// WHY A CALLBACK AND NOT A TIMER OBJECT: the two platforms schedule differently (Windows'
// timeSetEvent runs on its own thread and must PostMessage to reach the UI thread; a GCD source on
// Mac already has a queue choice), and the caller above only ever wants "run my frame now, on the
// thread that may call the host".
typedef void (*TickFn)(void *user);
void TimeInit();     // once, on the UI thread, before any timer starts
void TimeShutdown(); // on unload
void TimerStart(int periodMs, TickFn tick, void *user);
void TimerStop();
bool TimerRunning();

// Wall clock and monotonic time, both in milliseconds. Now() is the motion clock (monotonic,
// unaffected by the user changing the system time); WallMs() is for log timestamps and age tests.
double Now();
unsigned long WallMs();

// ---------------------------------------------------------------------------
// THE WHEEL HOOK.
//
// The platform layer notices wheel activity and, when it can, decides whether the message belongs
// to the plugin at all. It does NOT decide routing -- that needs the host's action table, which
// lives above this line. The split is:
//
//   platform:  "a wheel turned, here it is, and here is what is under the pointer"
//   plugin:    "is that action one of ours, and what should it receive instead"
//
// `install` returns false when the platform refuses (macOS: the user has not granted Accessibility
// permission), which is a state the plugin must handle by doing nothing rather than by spinning.
// ---------------------------------------------------------------------------
bool WheelHookInstall();
void WheelHookRemove();

// What is under the pointer, and is it a surface the plugin would drive if the host routed a wheel
// there. The platform answers with an opaque window and, when it matters, a CLASS NAME it is willing
// to vouch for -- but the routing decision itself stays in the plugin (see ClassifyName).
//
// `PlatHit` is returned by value and is cheap: two pointers and a flag.
struct PlatHit
{
  PlatWindow window = nullptr; // the window under the pointer, or nullptr
  bool valid = false;          // false = the platform could not tell; caller must not guess
};

PlatHit HitTest();

// ---------------------------------------------------------------------------
// WHAT THE MOST RECENT WHEEL WAS, for the action path.
//
// On Windows the hook already records this per axis (see the device records in the main file); on
// macOS the event tap does, because it is the only thing that can see the wheel at all. Both are
// consumed the same way: when the host reports the resolved action, the plugin asks "was the wheel
// behind this a touchpad" and either animates it or passes it through.
//
// Returning a zeroed event (delta 0, continuous false) means "no recent wheel", which the caller
// treats as a notched device -- the conservative choice, since animating a wheel that was really a
// touchpad would double-smooth something already smooth.
// ---------------------------------------------------------------------------
PlatWheelEvent LastWheel();
bool WheelHookDenied();

// ---------------------------------------------------------------------------
// PANEL. Optional: a build may have none (PlatPanelPresent() == false), in which case the settings
// come from extstate alone and no UI ever appears. That is a supported shape, not a stub.
// ---------------------------------------------------------------------------
bool PlatPanelPresent();
void PanelShow();     // create if needed, bring to front
void PanelToggle();   // show if hidden, hide if shown
bool PanelVisible();
void PanelRefresh();  // re-read the store and repaint (used when settings arrive from elsewhere)

// ---------------------------------------------------------------------------
// WHERE EACH PLATFORM GETS THESE FROM, because neither implements all of them directly.
//
// Windows: the panel, the wheel hook and the hit test are STILL in smooth_wheel_scroll.cpp -- they
// are entangled with the action table, the integrator state and the dock, and moving them wholesale
// would be a rewrite rather than a relocation. The Win32 platform files therefore FORWARD to them
// (see the "platform adaptors" block at the end of that file). The reason forwarding is acceptable
// here and not a smell: it is one indirection each, it is visible in one place, and it lets the Mac
// build supply real implementations without the caller knowing which shape it got.
//
// macOS: everything is implemented here directly, except hit testing, which deliberately reports
// "cannot tell" (there are no window class names to match) and the panel, which does not exist yet.
//
// The contract the caller relies on is only this: after PlatPanelPresent() returns true, the other
// four do something sensible; after it returns false, they are harmless no-ops. Nothing above the
// line may assume more.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// STARTUP.
//
// The REAPER-facing entry function is the plugin's own (ReaperPluginEntry in smooth_wheel_scroll.cpp);
// it already loads the API table, reads rec->hwnd_main, registers the hooks and handles unload. The
// Mac entry point must therefore NOT do that work again -- it only marshals the Objective-C side and
// calls the same function. Two entries, one implementation, is the whole point: the registration
// code is written once and is platform-free.
//
// These exist so the platform layer can be told when the UI thread exists and when it is going away.
// On Windows the module entry does it; on macOS the entry point does, because there is no DllMain.
//
// (Declared once, up with the rest of the clock: TimeInit/TimeShutdown are clock lifecycle, and
// repeating the declarations here is how a header grows two copies of the same contract that then
// drift apart.)
// ---------------------------------------------------------------------------

} // namespace plat

#endif // SWS_PLATFORM_H
