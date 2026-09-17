// SmoothWheelScroll for REAPER
//
// Port of the WPF SmoothWheelScrollBehavior module to a native REAPER extension.
//
// The plugin does ONE thing: turn a wheel notch into an elegant, animated
// parameter and hand that to the receiver. It never moves a view itself and never
// changes REAPER state -- see AGENTS.md for the binding rules of this project.
//
// Because bindings are fully user-customisable, we never guess from modifiers.
// Instead REAPER resolves the wheel to an action and reports it through
// hookcommand2; we then classify that action:
//
//   - view scroll / zoom -> consume it and re-invoke THE SAME ACTION over time
//     with the animated relative value
//   - anything else (parameters, tempo, transport, track height, custom, ...)
//     -> do nothing, let REAPER run it untouched, with the original value
//
// This is the "unified port": hookcommand2 covers every key section (main,
// MIDI editor, ...), so any view whose wheel operation is an action is covered.
//
// Driving: action replay only. The action receives a relative value (a wheel
// notch is kNotchUnits units) and does all the moving, so zoom centre, range
// limits and any user customisation stay exactly the action's business.
//   - main section  : KBD_OnMainActionEx
//   - other sections: the section's own onAction callback (see ReplayAction)
//
// List/tree controls have no action bound to their wheel, so they are handled
// separately through the OS control interface (WM_VSCROLL) -- still no REAPER
// state touched.
//
// The motion itself (the animation) lives behind model.h and is pure math,
// independent of REAPER; test/anim_sim.cpp exercises it standalone.

#include <windows.h>
#include <windowsx.h> // GET_X_LPARAM / GET_Y_LPARAM for the right-click menu
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm")

// ---------------------------------------------------------------------------
// REAPER API surface (minimal set)
// ---------------------------------------------------------------------------
#define REAPERAPI_IMPLEMENT
#define REAPERAPI_MINIMAL
#define REAPERAPI_WANT_plugin_register
#define REAPERAPI_WANT_GetMainHwnd
#define REAPERAPI_WANT_kbd_getTextFromCmd
#define REAPERAPI_WANT_SectionFromUniqueID
#define REAPERAPI_WANT_KBD_OnMainActionEx
#define REAPERAPI_WANT_kbd_RunCommandThroughHooks
#define REAPERAPI_WANT_MIDIEditor_GetActive
#define REAPERAPI_WANT_GetThingFromPoint
#define REAPERAPI_WANT_GetSetMediaTrackInfo_String
// Settings storage is REAPER's own extended state: the plugin keeps no file of its own
// and does not touch REAPER's preferences. See SaveSettings/LoadSettings.
#define REAPERAPI_WANT_GetExtState
#define REAPERAPI_WANT_SetExtState
// Theme colours: the panel's materials (background, card, frame, groove, handle) are the THEME's
// own colours, so the panel matches the theme the user is running. See ResolveTheme.
#define REAPERAPI_WANT_GetThemeColor
// REAPER-native colour to real RGB. GetThemeColor returns REAPER's own packing, not Win32's.
#define REAPERAPI_WANT_ColorFromNative
// REAPER's own switch, READ (never written), for the ONE yes/no the panel cannot get from a colour:
// whether the UI is in its dark mode, which decides the text colours and the direction the derived
// colours lean. Two routes, because get_config_var was measured not to answer for this key while
// reaper.ini does. See ReadAppDarkFlag.
#define REAPERAPI_WANT_get_config_var
#define REAPERAPI_WANT_get_ini_file
// Read what the user has assigned to a Mouse Modifier context (track panel / mixer panel
// wheel). This is how the panel rules avoid hijacking a gesture the user has rebound:
// see TcpWheelIsPlainScroll.
#define REAPERAPI_WANT_GetMouseModifier
// The DEV wheel log (SWS_WHEEL_LOG) stamps the test machine's REAPER version into the file, so a
// report can be read in context. Nothing else uses it.
#ifdef SWS_WHEEL_LOG
#define REAPERAPI_WANT_GetAppVersion
#endif
// The mixer needs NO API. Its only official interface, SetMixerScroll, takes a TRACK -- so it
// cannot express anything finer than a whole track, and using it produced exactly that
// visible stepping. The animated travel is instead handed to REAPER's own mixer window as a
// small wheel message, the same way a device delivers it, and REAPER's own handler decides
// how far that moves (it already scrolls the mixer smoothly for a trackpad). See DRIVE_MCP_WHEEL.
// Docking, so the settings window can live in REAPER's dock. See ShowConfigWindow.
#define REAPERAPI_WANT_DockWindowAddEx
#define REAPERAPI_WANT_DockWindowActivate
#define REAPERAPI_WANT_DockWindowRemove
#define REAPERAPI_WANT_DockIsChildOfDock
#ifndef SWS_NO_SETTINGS_UI
#define REAPERAPI_WANT_AddExtensionsMainMenu
#endif
#include "reaper_plugin.h"
#include "reaper_plugin_functions.h"

// The animation itself lives in a REAPER-free header so it can be exercised
// standalone (test/anim_sim.cpp). Everything below only classifies, feeds in and
// delivers -- it holds no motion math of its own.
//
// The plugin reaches the model ONLY through model.h (the seam): this file never includes the
// model's own header directly and never writes `model::`, so a new model is adopted by editing
// model.h alone.
#include "model.h"

// ---------------------------------------------------------------------------
// Master switch (g_glideOn, defined with the tuning block below)
//
// on  = glide on: wheel-driven view scroll/zoom is smoothed (see below).
// off = glide off: no animation and no interception -- the original wheel goes to
//       REAPER untouched, parameters included.
//
// This is a RUNNING switch, not a compile-time one, so it can be toggled from the
// settings window without a restart. The message hook therefore stays installed
// either way (it forwards immediately when off); only the interception is skipped.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Tunables -- runtime values, adjustable from the config window (see below).
//
// The motion is a simple physical model: every wheel notch gives the view a
// velocity (an impulse), and friction bleeds it off. Position integrates that
// velocity, so a notch starts from rest, accelerates, coasts and settles -- the
// natural feel -- and repeated notches add their impulses before the previous one
// has decayed, so a sustained roll builds speed.
//
//   1. START    length of the FIRST notch's travel, in % of one wheel notch
//   2. ACCEL    extra travel added to each further notch's impulse, same unit
//   3. RELEASE  the base coast-out time (the brake) after a slow roll
//
// Friction is derived from RELEASE (velocity falls by e^-6 over that time).
//
// BRAKE BY RHYTHM: RELEASE is the brake when you roll unhurried. Rolling fast
// shortens the gap between notches, and a fast rhythm loosens the brake -- the
// motion decays more slowly, so it coasts further. The brake chosen by the last
// rhythm is kept through the coast-out, so a quick flick keeps sliding even after
// you stop. See kTempoRefMs / kBrakeRelaxMax.
// ---------------------------------------------------------------------------
// THE MODEL'S PARAMETERS
//
//  1. WINDOW (ms)  -- pure timing: a received amount is handed over across this many ms. Nothing here
//                     changes the DISTANCE, only how it is spread out. See anim3_core.h.
//  2. START (deltas)   -- how far a single, slow notch moves, 1..10. This is the fine,
//                     touchpad-like
//                     step at a slow deliberate turn. It is in wheel DELTAS, so it means the same
//                     thing for a notched mouse (120/message) and a free-spinner (small/message).
//  3. BUDGET (deltas)  -- how much turning it takes for a notch to reach its full size. The budget
//                     follows the current speed (it decays), so a steady hand speed gives a steady
//                     glide at whatever speed the user holds -- see model::SpeedBudget.
//
// The travel a notch goes is model::Travel(): `start` at a standstill, rising to the message's own
// size (120 for a notched mouse, its small share for a free-spinner) once the budget is full.
// ---------------------------------------------------------------------------
static double g_windowMs = 200.0;
static const double kWindowMinMs = 100.0;
static const double kWindowMaxMs = 300.0; // 400 was unfcomfortable, so the top is 300
static const double kDefaultWindowMs = 200.0;

static double g_startDeltas = 5.0;
static const double kStartMin = 1.0;
static const double kStartMax = 10.0; // upper limit set by the user: a slow step of at most 10 deltas
static const double kDefaultStart = 5.0;

static double g_budgetDeltas = 1000.0;
static const double kBudgetMin = 60.0;
static const double kBudgetMax = 2000.0;
static const double kDefaultBudget = 1000.0;

// SPEED MULTIPLIER (1..2): how far past the wheel's OWN speed the fastest rolls may climb. The ramp
// reaches the wheel's own speed at u = 1; this raises the ceiling to u = speedMul, so past that point
// the travel keeps climbing on the same line -- up to `speedMul` times the wheel's own size. 1.0 is
// the baseline (stop at the wheel's own speed), which is the previous behaviour exactly.
//
// It changes NOTHING below the baseline: slow rolls, mid rolls and the whole ramp-up are untouched
// (see model::Travel). It only engages once the wheel is already at full speed -- "only at the
// fastest speed, not during the climb".
static double g_speedMul = 1.5;
static const double kSpeedMulMin = 1.0;
static const double kSpeedMulMax = 2.0;
static const double kDefaultSpeedMul = 1.5;

// The four defaults were retuned by the user on 2026-09-16, together with the panel's motion chart:
// the chart maps each parameter to ONE visual channel (Glide = width, Slow step = the knee's height,
// Ramp-up = the slope, Top speed = the height), and these are the values that make all four read
// clearly at once. See the motion chart's note, and AGENTS.md 114.
//
// A store written under the OLD defaults is upgraded once, in LoadSettings: a value that is still
// exactly an old default is replaced by the new one, so a setting the user had actually TUNED is
// never overwritten. Anything else keeps what it had (double-click a fader to take the new default).
static const double kOldWindowMs = 150.0, kOldStart = 1.0;
static const double kOldBudget = 600.0, kOldSpeedMul = 1.0;

// Kept as an alias for the release-too-short check below; the window IS the timing now.

// (The extstate keys "eatrate", "accelv" and "slowmove" are no longer read; their old values are
// simply ignored, and cleared when settings are saved.)

// Master switch: off = the plugin adds no animation at all and every wheel goes to REAPER
// untouched (see GetMsgProc / OnAction / InstallHook).
static bool g_glideOn = true;
// Second switch, on by default: reverse the touchpad's direction for the horizontal ZOOM actions
// (main view and MIDI editor). Independent of the master switch -- it is about a wheel that is
// passed through, not about the animation.
static bool g_touchpadReverse = true;
// Whether the settings window should come up inside REAPER's docker. Kept as our own preference
// because REAPER also remembers a placement per ident string, and the two together are what caused
// the window to be dragged back into the dock on every open (so it could never be restored to a
// normal floating window).
static bool g_dockOn = false;
// Which revision of the four DEFAULTS this install is on. The defaults were retuned on 2026-09-16
// (see kDefaultWindowMs); a store still on revision 1 (or unset, meaning older than this bookkeeping)
// has its untouched values upgraded once. Zero here is "no revision recorded yet", which is exactly
// the state every store is in before this build.
static int g_defaultsRev = 0;
// Where the FLOATING window was last seen, remembered across opens and restarts so the panel
// reappears where the user left it. Without this the window was recreated at the OS default position
// on every open and always came up near the top-left corner.
//
// Only the POSITION is remembered, never the size. The panel has a designed size (the narrowest
// width that keeps its top line intact, and the height at which nothing needs scrolling), and it
// opens at that size every time.
//
// Docked geometry is deliberately NOT kept here either: in a docker REAPER owns the position and
// restores it through DockWindowAddEx (reaper.ini), so duplicating that would only be a second,
// competing memory.
static POINT g_floatPos = {0, 0};
static bool g_floatPosValid = false;

// ---------------------------------------------------------------------------
// OUTPUT-DENSITY TOGGLE
//
// Two independent switches. They used to be one, which is why "finer values" and
// "many more calls" could not be tried separately.
//
//   kFineValues : deliver 1/256-unit steps instead of whole units. This is what
//                 fixes slow-roll precision (whole units are 1/15 notch = 6.7% of
//                 a notch, a visible step; 1/256 unit = 0.026%).
//   kFastTimer  : drive the glide with a 4 ms multimedia timer instead of the
//                 plain thread timer (~15.6 ms here). Only this raises the CALL
//                 RATE; it is the part that previously overloaded REAPER's UI, so
//                 it stays off until it is known to be safe.
//
// The delivered step size is (travel per interval), so precision and call rate are
// inseparable: fine values at the plain 15.6 ms timer give ~8 calls per slow notch
// (max step 3.4% of a notch) with NO increase in call rate beyond that.
// ---------------------------------------------------------------------------
static const bool kFineValues = true;   // deliver the travel in fine steps (see Delivery)
// OFF -- the accepted state (1.6.1 / 2.0 / the unified build are ALL false).
//
// This is the CALL-RATE switch, not a quality switch: ON drives the glide from a 1 ms multimedia
// timer, which is up to ~1000 calls/s into REAPER instead of ~64 (the plain timer runs at ~15.6 ms
// here). That call rate is what overloaded REAPER's UI, and it is why this stays OFF.
//
// It was turned ON during the 3.0 work (that model needed short windows resolved finely) and then
// NOT turned back when 3.0 was abandoned -- and the symptom was exactly the old one: heavy actions
// (View: Scroll horizontally / Zoom vertically) stuttered while lighter ones did not. Restored to
// OFF, which is the version the user accepted. See AGENTS.md 10.1: the multimedia timer path exists
// and is verified, but it has never been the shipping state.
static const bool kFastTimer = false;

// Animation tick for the plain thread timer, used only as the fallback when the multimedia timer is
// unavailable. Windows runs this at about 15.6 ms here regardless of the requested value.
static UINT kAnimTimerMs = 5;
// Multimedia timer period. 1 ms is what the window maths assumes: at 50..400 ms that is 50..400
// parts per amount. This is also the ceiling on how often the plugin can call REAPER, so it is the
// number to raise first if a very fast roll ever costs too much (see AGENTS.md on call rate).
static UINT kFastTimerMs = 1;

// Wheel messages count in WHEEL_DELTA units: one notch is 120. The model works in 7-bit units
// (one notch is kNotchUnits = 15), so travel is converted into wheel deltas when it is handed to
// the mixer, and the resistance slider (deltas/ms) converts the other way for the model. Declared
// here, before the first use, so every consumer sees the same two numbers.
static const double kNotchUnits = 15.0;
static const double kDeltasPerNotch = 120.0;
static const double kDeltasPerUnit = kDeltasPerNotch / kNotchUnits;

// THE MAIN VIEW'S VERTICAL ZOOM uses the 1.6.1 CURVE MODEL, not the window model (AGENTS.md 111).
// Its parameters are the 1.6.1 defaults, copied here so the zoom's feel is exactly the accepted one:
// measured, the window model's travel sliders made a sustained zoom move 5-8x too little.
//
// These are NOT on the panel: they are the tuned 1.6.1 values, and the panel's four sliders keep
// controlling the (scroll) window model. Keeping them as named constants is what makes the zoom's
// behaviour reproducible rather than "whatever the panel happened to be set to".
static const double kZoom161StartPct = 15.0; // the user's own 1.6.1 setting
static const double kZoom161AccelPct = 13.61; // the user's own 1.6.1 setting (default was 7.0)
static const double kZoom161ReleaseMs = 200.0;
static const double kZoom161Hold = 1.0;
static const double kZoom161Coast = 1.0;
static const double kZoom161OnsetMs = 85.0; // (kOnsetMinMs + kOnsetMaxMs) * 0.5
static const double kZoom161CeilingUnit = 1605.6;
static const double kZoom161BurstGapMs = 250.0;
static const double kZoom161HoldBend = 1.625; // (kHoldBendMin + kHoldBendMax) * 0.5

static model161::Params Zoom161Params()
{
  model161::Params P;
  P.startPct = kZoom161StartPct;
  P.accelPct = kZoom161AccelPct;
  P.releaseMs = kZoom161ReleaseMs;
  P.hold = kZoom161Hold;
  P.coast = kZoom161Coast;
  P.onsetMs = kZoom161OnsetMs;
  P.ceilingUnit = kZoom161CeilingUnit;
  P.burstGapMs = kZoom161BurstGapMs;
  for (int i = 0; i < model161::kSegments; ++i)
    P.bend[i] = 0.0;
  P.bend[2] = kZoom161HoldBend; // [0] unused; [2] is the Hold bend (1.6.1's non-zero default)
  return P;
}

// The model's parameters for one axis: the window (timing), the start travel and the budget (both in
// wheel deltas), plus the payout shape for THIS amount. This is the only bridge between the panel and
// the model.
//
// `gapMs` is the time since the previous wheel message on this axis (0 for the first one). It only
// decides the payout SHAPE: an amount whose gap is shorter than the window will overlap its
// neighbours, and only there does easing the payout remove a real ripple (see model::PayoutEaseFor).
// Callers that are not feeding an amount (the animation tick) pass 0: the ease is remembered per
// window at Feed time, so it is not looked at again.
static model::Params AnimParams(double gapMs)
{
  model::Params P;
  P.windowMs = g_windowMs;
  P.payoutEase = model::PayoutEaseFor(gapMs, g_windowMs);
  return P;
}

// START is measured in WHEEL NOTCHES, not pixels: one notch of the wheel is the
// unit. The whole point of the eased start is to move LESS than the stock single
// notch (smaller = more elegant), so the slider is a percentage and its maximum
// is 100% = exactly one stock notch, never more. Each drive applies the same
// fraction to its own notion of one notch (scroll step, zoom step, list line
// group), so "30%" is 0.3 of a notch everywhere.
//
// ACCEL is likewise a percentage of one notch, added to the impulse of every
// notch after the first. Together with the natural impulse build-up (a new notch
// arrives before the last has decayed) this is what makes a sustained roll speed
// up; there is no ceiling, and the physics cannot run away.

// Windows timers cannot fire much faster than one frame (~10-16 ms) on the main
// thread, and the scroll APIs must be called on that thread. So a release at or
// below this is delivered as one immediate step instead of a fake animation
// (which would still take a whole frame anyway). With the minimum g_releaseMs
// (50 ms) the animated path is used.
static const double kSyncReleaseMs = 5.0;

// Relative value scale. A wheel notch reaches an action as 15 units in REAPER's
// 7-bit relative form (measured: the wheel reports val=15, valhw=-1). The 7-bit
// integer part alone is coarse -- the smallest step would be 1/15 of a notch --
// but the relative form carries a FRACTIONAL part as well, so the animated value
// can be transmitted far more finely than that.
//
// The exact encoding is REAPER's own, take it from the SDK rather than guessing:
// the cSurf OSC code (reaper_csurf/csurf_osc.cpp) ships `encode_relmode1_extended`,
// marked "copied from kbd.cpp":
//
//   value[-64..+63] = val7bit + (val7bit < 0 ? z/256 : val7bit > 0 ? -z/256 : 0)
//   val = val7bit & 0x7f;   valhw = -1 - z          (z = 0..255)
//
// So the integer part is val7bit (7-bit, signed: 1..63 up, 65..127 down) and the
// fractional part is 256 sub-steps carried in valhw as a NEGATIVE number. One
// 7-bit unit is 1/15 notch, so the finest step is 1/(256*15) = 1/3840 of a notch.
// (The `valhw >= 0` form documented for KBD_OnMainActionEx is the ABSOLUTE pitch
// encoding, used with relmode=0 -- not this one.)
static const double kRelSubPerUnit = 256.0; // fractional sub-steps per 7-bit unit
// How a whole-unit receiver's travel is delivered: in whole 7-bit units.
//
// This number has been moved twice, in both directions, and the measurements say where it
// belongs. It must be 1.0 (whole units):
//   - At the finest grid (1/3840 of a notch) the axis JITTERED -- the receiver cannot act on a
//     step that small, so consecutive values cancelled against each other.
//   - At 1/8 unit the axis came back LURCHING, and the log shows why: the receiver was sent
//     1.125 and 1.25 unit pieces, i.e. it rounded each one UP to its own step, so a stream of
//     slightly-different sub-unit values arrived as a stream of oversized jumps. It wants whole
//     units and reads anything between them as the next unit up.
//   - At whole units the pieces are exactly what it acts on, so nothing is lost or rounded.
//
// A gentle notch's travel (measured, about 0.6 units at Start 5%) is then smaller than one
// piece, and the fraction accumulates across frames until it reaches a whole unit -- which is
// the "gather then hand over" behaviour this receiver requires, not a defect.
static const double kVertStepsPerUnit = 1.0;
static const int kRelIntMax = 63;       // max |integer part| in the relative form

// How much travel counts as ONE device notch for the mixer, in 7-bit units.
//
// The mixer's own wheel moves one track per notch (measured), and the plugin's job is to change only
// the TIMING, never the distance. MODEL 3.0 hands a notch over in full, so one input notch is one
// device notch and the conversion is the plain unit->delta ratio: 120 deltas / 15 units = 8 deltas
// per unit. (Under the old model a notch moved only 1.89 units -- friction ate most of a slow turn
// -- so this was 1.89.) Kept as a named constant rather than inlined, so the mixer's scale stays
// visible and is not confused with the model's travel.
static const double kMixerUnitsPerNotch = kNotchUnits;

static const DWORD kWheelFlagMs = 250;   // wheel->action latch validity window

// ---------------------------------------------------------------------------
// Debug logging (compile-time gate)
//
// Kept SEPARATE from the DEV wheel log on purpose. The wheel log's own build must not drag in the
// per-wheel verbose logging (it is heavy and would fill %TEMP% for no reason); the two switches are
// independent and can be combined if someone wants both.
// ---------------------------------------------------------------------------
#ifdef SWS_DEBUG_LOG
static const bool kDebugLog = true;
#else
static const bool kDebugLog = false;
#endif

// Diagnostics, defined with the entry point; declared here so the wheel hook can call it.
static void DumpMouseModifiers(const char *why);

// What is assigned to a panel's wheel Mouse Modifier context for this exact modifier
// combination? The context is one of the MM_CTX_* names ("Track control panel / Mouse
// wheel" is MM_CTX_TCP_MOUSEWHEEL); the answer is a mouse modifier id formatted as it is
// written in reaper-mouse.ini ("1 m" = the built-in Scroll TCP).
//
// The modifier flag is a bit field: +1 shift, +2 control, +4 alt, +8 win (from the SDK).
static void PanelWheelAssignment(const char *context, bool shift, bool ctrl, bool alt,
                                char *out, int outSize)
{
  if (out && outSize > 0)
    out[0] = 0;
  if (!GetMouseModifier || !out || outSize <= 0)
    return;
  const bool win = (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
                   (GetKeyState(VK_RWIN) & 0x8000) != 0;
  const int flag = (shift ? 1 : 0) | (ctrl ? 2 : 0) | (alt ? 4 : 0) | (win ? 8 : 0);
  GetMouseModifier(context, flag, out, outSize);
}

static void Log(const char *fmt, ...)
{
  if (!kDebugLog)
    return;
  char path[MAX_PATH];
  DWORD n = GetTempPathA(MAX_PATH, path);
  if (n == 0 || n + 32 >= MAX_PATH)
    return;
  strcat(path, "SmoothWheelScroll.log");
  FILE *f = fopen(path, "a");
  if (!f)
    return;
  SYSTEMTIME st;
  GetLocalTime(&st);
  fprintf(f, "%02d:%02d:%02d.%03d ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fputc('\n', f);
  fclose(f);
}

static bool ArrangeScreenRect(RECT *out); // defined further down (surface section)

static double Now()
{
  static double freq = 0.0;
  if (freq == 0.0)
  {
    LARGE_INTEGER f;
    if (QueryPerformanceFrequency(&f))
      freq = (double)f.QuadPart;
    else
      freq = -1.0;
  }
  if (freq > 0.0)
  {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / freq;
  }
  return (double)GetTickCount64() / 1000.0;
}

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static HINSTANCE g_hInst = nullptr;
static HWND g_main = nullptr;
static UINT_PTR g_timer = 0;
static bool g_timerOn = false;
// The animation clock. SetTimer/CreateTimerQueueTimer are tied to the thread
// scheduler and measured at ~15.6 ms here regardless of the requested interval,
// which leaves only ~6 samples in a 100 ms notch -- the coarse "steppy" motion.
// A multimedia timer (timeSetEvent) does reach 1 ms on the same machine, so it is
// the animation's clock. Its callback runs on a timer thread, where REAPER calls
// must NOT be made, so it only posts this message to a small window the plugin
// owns on the UI thread; the WndProc there does the actual tick.
#define WM_APP_GLIDE_TICK (WM_APP + 42)
static HWND g_animWnd = nullptr;      // plugin-owned message window (UI thread)
static HANDLE g_mmTimer = nullptr;    // timeSetEvent handle
static UINT g_mmTimerMs = kFastTimerMs; // period requested for the multimedia timer
static volatile LONG g_tickPosted = 0; // coalesce: one posted tick at a time
static HHOOK g_msgHook = nullptr;
static DWORD g_uiThreadId = 0;
static bool g_shuttingDown = false;
static volatile LONG g_wheelTick = 0;   // GetTickCount() of last arrange wheel; 0=none
// WHAT the most recent wheel was: its Device verdict and the tick it arrived. Set for EVERY message
// by the hook. Two things read it:
//   * the reverse feature needs "was it a touchpad";
//   * the action path needs "did the hook pass this one through" (a touchpad OR an unidentified
//     one), so that nothing the hook refused to animate is animated a moment later by OnAction.
static volatile LONG g_lastDevTick = 0; // GetTickCount of the last wheel; 0 = none yet
static volatile LONG g_lastDevKind = 0; // that wheel's Device, as an int
static HWND g_wheelHwnd = nullptr;      // window the wheel was over
static POINT g_wheelPt = {0, 0};        // screen point of that wheel
// Screen point of the last wheel that landed on the MIXER. A mixer gesture animates over many
// frames, so by the time it is delivered the cursor may have moved elsewhere; REAPER resolves
// its own "Scroll MCP" context from the point we hand it, so it must stay the point that was
// over the mixer. Updated only by mixer wheels, never by the generic g_wheelPt above.
static POINT g_mcpWheelPt = {0, 0};
// Set while the plugin is handing its own wheel message to REAPER's mixer, so the message
// hook cannot mistake our synthetic wheel for a new user gesture. See ApplyMcpWheel.
static bool g_sendingSynthWheel = false;
// Probe support: every wheel gets a sequence number, and the tick it happened on. The
// action hook then reports which wheel it belongs to, so a log can be read as a table of
// "this exact modifier combination, on this surface, produced this action". Without the
// pairing a reader has to guess from timestamps.
static volatile LONG g_wheelSeq = 0;
static DWORD g_wheelSeqTick = 0;
static char g_wheelMods[4] = "---";     // last wheel's modifiers, for the action line
static bool g_replaying = false;        // bypass flag for our own replay calls
static bool g_timerPeriodRaised = false; // whether timeBeginPeriod(1) is active

// ---------------------------------------------------------------------------
// Classification: which actions are view scroll / zoom, and how to drive them
//
// The enums (Drive / Axis / Kind / Delivery), the known-action table and the two rules that used
// to live here now live in ONE place, src/routing.h, together with the single filter. They are
// pure data and string rules with no REAPER or Windows dependency, so the real code can be
// compiled and diffed outside REAPER -- see test/check_routes.sh, which proves the rule is
// unchanged against the frozen 1.6.1 behaviour in test/expected/routes_before.txt.
// ---------------------------------------------------------------------------
#include "routing.h"
// Which DEVICE sent a wheel (notched / free-spinning / touchpad). Pure arithmetic, no REAPER.
#include "device.h"

#ifndef SWS_NO_SETTINGS_UI
// ---------------------------------------------------------------------------
// THE MOTION CHART -- a BALL RUNNING A TRACK, and a track shaped by the four sliders.
//
// The panel used to show a table of the last few deltas in and out. A table answers "which numbers
// moved"; it does not answer "what does this FEEL like", which is what the sliders are for. So the
// block became a motion chart, and then a ball on that chart -- a ball is read without thinking: it
// goes, it climbs, it arrives, and a second one follows.
//
// EACH SLIDER OWNS ONE VISUAL CHANNEL. That is the whole design, and it is why the chart is a
// SCHEMATIC rather than a run of the model: a model run's SHAPE depends on how the mechanisms happen
// to interact, so two sliders could look like they do the same thing (or nothing). Here each slider
// moves one obvious property of the picture, and no two share one:
//
//   * Glide length -> the WIDTH of the whole drawing (how long the motion takes);
//   * Slow step    -> the HEIGHT of the first segment's end (the knee), in that slider's colour;
//   * Ramp-up      -> the SLOPE of the climb from the knee to the native height, in its colour;
//   * Top speed    -> how far PAST the native height the climb continues, in its colour.
//
// The GREY STAIRCASE is the reference: what the wheel itself would do with the same input. It always
// tops out at the NATIVE height, and the ball HOPS up it (square ball). A notched mouse lands whole
// notches (a coarse staircase); a free-spinning wheel works in fine pieces (a fine one) -- the
// "coarse grid / fine grid" difference, visible as the size of the hops.
//
// The SMOOTH path (round ball) is the shaped one, and its peak is what Top speed scales: at 1.0 the
// curve tops out exactly AT the native line, at 2.0 at TWICE it. Because the native line is drawn
// where the staircase tops out, pulling Top speed up visibly sinks that reference -- "twice as far"
// is read directly off the picture, no numbers needed.
//
// WHEN BALLS ARE BORN: one per received wheel message (a roll therefore shows several in flight at
// once, capped at kAnimBalls, oldest discarded first), one when a fader is RELEASED after dragging
// (the user's "let go of the lever and one runs"), and one on a reset / a switch flip.
//
// COST: one small pass over the path (a few hundred points) when a slider moves or a wheel arrives,
// plus a ~40 fps repaint of ONE SMALL RECTANGLE while a ball is in flight. Both are trivial; and the
// repaint is bounded to this block, because repainting the whole panel per wheel was measured to be
// expensive (AGENTS.md 69).
//
// All state defaults to zero, so it stays in .bss.
// ---------------------------------------------------------------------------
static const int kAnimNotches = 6;     // notches the scripted roll carries
static const int kAnimN = 240;         // points along the path
static const DWORD kAnimTickMs = 25;   // ball frame (~40 fps)
static const UINT_PTR kAnimTimerId = 0xA161;
static const int kAnimBalls = 6;       // balls in flight at once (the user's cap)
static const int kAnimBallPx = 5;      // ball radius / square half-side

// Which row of the slider table owns which channel. By NAME-derived index, so the mapping is stated
// once here rather than implied by `g_sliders[2]`-style numbers scattered through the drawing code.
static const int kRowGlide = 0, kRowSlow = 1, kRowRamp = 2, kRowTop = 3;

// The channel ranges. Each is the "how much of the drawing" question for one slider, and each is
// clamped so the drawing can never leave its box at any slider setting.
static const double kChartKneeMin = 0.02;      // the knee never quite reaches the floor, so it is drawn
static const double kChartKneeX = 0.20;        // where the first segment ends
static const double kChartRampX0 = 0.34, kChartRampX1 = 0.88; // where the climb reaches the peak
static const double kChartCorner = 0.10;       // how much of the span a rounded corner may take
// The animation curve's stroke, in pixels. Heavier than a hairline: it is the subject of the picture,
// and at 1px it read as a thin guide rather than as the thing being shown. (Doubled on the user's
// call, 2026-09-16: "动画曲线加粗一倍".)
static const int kChartCurvePx = 4;
static const int kChartMaxTicksX = 8;          // at most this many X ticks (ms)
static const int kChartMaxTicksY = 8;          // at most this many Y ticks (deltas)
static const double kChartTickFont = 0.72;     // tick numbers, as a fraction of the dialog font

// BOTH AXES ARE DYNAMIC: the drawing FILLS its box, and the two scales are what the sliders move.
// This is the 1.6.1 behaviour (AGENTS.md 19.23: "两轴均动态"), asked for again on 2026-09-16 after a
// version that pinned the axes and made the drawing end short of the right edge:
//
//   * Glide length -> the HORIZONTAL scale: the same picture stays full width, but it stands for a
//     longer or shorter motion, so the ball crosses it slower or faster;
//   * Top speed   -> the VERTICAL scale: the flat top stays the highest line, and the wheel's own
//     height (the native line) sinks as Top rises, which is what "twice as high" then means.
//
// The empty right-hand part, the vertical end marker that went with it, and the fixed native line are
// all gone with the fixed axes.
static const double kAnimPace = 0.45; // the ball's travel time as a fraction of the drawn span

struct PanelAnim
{
  float on[kAnimN];  // the shaped path, 0..1 of the box (1 = the top of the box)
  float off[kAnimN]; // the wheel's own landing: a staircase up to the native line
  double kneeX = 0.0, reachX = 0.0; // where the rise ends, and where the climb reaches the top
  double kneeY = 0.0, peakY = 0.0;  // their heights: Slow step, and Top speed
  long spanMs = 0;       // how long a ball takes to cross it (Glide length, as a time)
  double yTopDeltas = 0.0; // what the top of the box is worth in deltas (Top speed, as travel)
  DWORD start[kAnimBalls] = {0};    // each ball's start tick; 0 = the slot is free
  bool smooth[kAnimBalls] = {false}; // that ball's mode when it was born
  bool have = false;                 // the paths are computed
};
static PanelAnim g_anim;
static HWND g_monWnd = nullptr; // the panel; the chart repaints through it (kept name)
static RECT g_monBlk = {0, 0, 0, 0};

// A slider's value as 0..1 of its range -- the unit every channel is expressed in.
static double ChartNorm(double v, double lo, double hi)
{
  if (hi <= lo)
    return 0.0;
  double t = (v - lo) / (hi - lo);
  return (t < 0.0) ? 0.0 : ((t > 1.0) ? 1.0 : t);
}

// A "nice" tick step (1, 2 or 5 times a power of ten) that divides `range` into AT MOST `maxTicks`
// parts -- and, among those, the FINEST one that fits.
//
// Finest-that-fits matters: the obvious "round range/maxTicks up to the next 1/2/5" quantises UP, so
// asking for more ticks could land on the same step and give fewer of them. This scans from the
// finest candidate instead, so a bigger maxTicks really does mean a denser grid. (Same rule the old
// response curve settled on, AGENTS.md 19.22.3.)
static double NiceStep(double range, int maxTicks)
{
  if (range <= 0.0 || maxTicks < 1)
    return 0.0;
  const double raw = range / (double)maxTicks;
  double mag = pow(10.0, floor(log10(raw)));
  for (int k = 0; k < 30; ++k)
  {
    const double mult[3] = {1.0, 2.0, 5.0};
    for (int i = 0; i < 3; ++i)
    {
      const double step = mag * mult[i];
      if (range / step <= (double)maxTicks + 1e-9)
        return step;
    }
    mag *= 10.0;
  }
  return range;
}

// The step the WHEEL itself works in, in deltas: a notched mouse delivers a whole notch per message;
// a free-spinning wheel delivers a small regular share of one, more often. Unknown/touchpad falls in
// with the notched case -- a touchpad is passed through, never drawn here.
static Device LastWheelDevice(); // defined with the wheel hook, further down
static double AnimOffQuantum()
{
  return (LastWheelDevice() == Device::kFreeSpin) ? (kDeltasPerNotch / 8.0) : kDeltasPerNotch;
}

// A straight piece from (x0,y0) to (x1,y1), evaluated at x.
static double ChartLineY(double x, double x0, double y0, double x1, double y1)
{
  return (x1 > x0) ? (y0 + (y1 - y0) * ((x - x0) / (x1 - x0))) : y1;
}

// A quadratic through (xc-half, y0) - (xc, yc) - (xc+half, y2), evaluated at x, where the control
// point IS the sharp corner: the curve bends smoothly through where the kink used to be. Only the Y is
// interpolated this way; the X is the linear map t = (x-xc+half)/(2*half), which is exactly the
// quadratic's own x-component for symmetric ends, so the curve is parameterised by x.
static double ChartFilletY(double x, double y0, double xc, double yc, double y2, double half)
{
  if (half <= 0.0)
    return yc;
  double t = (x - xc + half) / (2.0 * half);
  if (t < 0.0) t = 0.0;
  if (t > 1.0) t = 1.0;
  const double u = 1.0 - t;
  return u * u * y0 + 2.0 * t * u * yc + t * t * y2;
}

// The shaped path's height (0..1 of the box) at x (0..1 of the drawing). Three pieces, so the picture
// is the parameters rather than a simulation of them:
//
//   0 .. kneeX    the RISE   -- ends at the knee, whose height is Slow step;
//   kneeX..reachX the CLIMB  -- reaches the top; how early is Ramp-up (earlier = steeper);
//   reachX..1.0   the TOP    -- a HORIZONTAL line at the top, whose height is Top speed.
//
// BOTH JOINTS ARE ROUNDED. Drawn with sharp kinks the picture was three straight lines meeting at two
// corners, which reads as something mechanical; the motion it stands for has no kink at all (the
// model is a smooth glide), so each corner is a short quadratic curve instead (the user asked for
// exactly this, 2026-09-16: "线段两个拐角给个倒角，让线段看起来平滑些，如实际体验").
static double ChartPathY(double x, double kneeX, double reachX, double kneeY, double peakY)
{
  // The corner's half-width, limited by what the neighbouring pieces can spare, so two fillets can
  // never overlap or run off either end of the drawing.
  double d = kChartCorner;
  const double riseLen = kneeX;
  const double climbLen = reachX - kneeX;
  const double topLen = 1.0 - reachX;
  if (d > riseLen * 0.5) d = riseLen * 0.5;
  if (d > climbLen * 0.4) d = climbLen * 0.4; // both corners eat into this piece
  if (d > topLen * 0.5) d = topLen * 0.5;
  if (d < 1e-6)
    d = 0.0;

  if (x < kneeX - d)
    return ChartLineY(x, 0.0, 0.0, kneeX, kneeY);
  if (x < kneeX + d)
    return ChartFilletY(x, ChartLineY(kneeX - d, 0.0, 0.0, kneeX, kneeY), kneeX, kneeY,
                        ChartLineY(kneeX + d, kneeX, kneeY, reachX, peakY), d);
  if (x < reachX - d)
    return ChartLineY(x, kneeX, kneeY, reachX, peakY);
  if (x < reachX + d)
    return ChartFilletY(x, ChartLineY(reachX - d, kneeX, kneeY, reachX, peakY), reachX, peakY, peakY,
                        d);
  return peakY; // the flat top: the highest line, and the only horizontal one
}

static void BuildAnimCurves()
{
  // Top speed is the VERTICAL SCALE. The drawing fills its box, so the flat top always reaches the
  // roof and the wheel's OWN height is drawn at 1/top of the box: raising Top therefore sinks the
  // native line, and at Top = 1.0 the flat top sits exactly ON it.
  const double top = (g_speedMul < 1.0) ? 1.0 : g_speedMul;
  const double natTop = 1.0 / top;

  // SLOW STEP is measured against the wheel's OWN message size, for the device actually in use -- NOT
  // against an arbitrary fraction of the picture. A slow step of 10 deltas is 10/120 of a notched
  // mouse's notch, but 10/15 of a free-spinner's much smaller message, so the same slider value is a
  // tall knee on one device and a low one on the other. That is the truth of the parameter, and it is
  // what makes the knee readable against the native line. (Drawing it as a fixed fraction of the
  // picture made a notched mouse's knee look like a free-spinner's -- the user spotted exactly that,
  // 2026-09-16: "如果示意是无级鼠标，那就对，但我用的是普通鼠标，120".)
  const double msg = AnimOffQuantum();
  double kneeFrac = (msg > 0.0) ? (g_startDeltas / msg) : 1.0;
  if (kneeFrac > 1.0)
    kneeFrac = 1.0;
  if (kneeFrac < kChartKneeMin)
    kneeFrac = kChartKneeMin; // never flat on the floor: the segment still has a colour to show

  const double rampN = ChartNorm(g_budgetDeltas, kBudgetMin, kBudgetMax);

  // Glide length is the HORIZONTAL SCALE: the drawing always spans the full box, and Glide decides
  // how long the motion it stands for takes -- so the ball crosses the same width slower or faster.
  // That time IS the X axis, and the ticks are read off it (see the tick drawing), which is why the
  // grid changes when Glide moves.
  g_anim.spanMs = (long)((150.0 + 2.5 * g_windowMs) * kAnimPace);

  // Top speed is also the VERTICAL SCALE's range: the box is as tall as `top` times the wheel's own
  // total travel, so this is what the top of the box is worth in deltas. The Y ticks read this, so
  // the grid changes when Top moves.
  g_anim.yTopDeltas = top * (double)kAnimNotches * kDeltasPerNotch;

  g_anim.kneeX = kChartKneeX;
  g_anim.reachX = kChartRampX0 + (kChartRampX1 - kChartRampX0) * rampN;
  g_anim.kneeY = kneeFrac * natTop;
  g_anim.peakY = 1.0; // the drawing fills the box, so the flat top is the top

  // The shaped path.
  for (int i = 0; i < kAnimN; ++i)
  {
    const double x = (double)i / (double)(kAnimN - 1);
    const double y = ChartPathY(x, g_anim.kneeX, g_anim.reachX, g_anim.kneeY, g_anim.peakY);
    g_anim.on[i] = (float)(y < 0.0 ? 0.0 : (y > 1.0 ? 1.0 : y));
  }

  // The wheel's own path: a staircase up to the NATIVE line, in as many steps as the device sends
  // messages. Whole steps, so the ball hops rather than climbs.
  const double q = AnimOffQuantum();
  const int nStep = (kAnimNotches * (int)(kDeltasPerNotch / q + 0.5) > 0)
                        ? kAnimNotches * (int)(kDeltasPerNotch / q + 0.5)
                        : kAnimNotches;
  for (int i = 0; i < kAnimN; ++i)
  {
    // How many messages have landed by this point of the span (they are spread evenly across it).
    const int landed = (int)((double)i * (double)nStep / (double)(kAnimN - 1) + 1e-9);
    g_anim.off[i] = (float)((double)landed / (double)nStep * natTop);
  }
  g_anim.have = true;
}

// A ball is born: the first free slot.
//
// A ball is NEVER recycled mid-flight: it was drawn to arrive, and cutting one short so a new one can
// start reads as a glitch (the user's "跑一半就没了"). So when all kAnimBalls are busy the new ball is
// simply not sent -- the roll still reads as continuous motion, because the balls that ARE out there
// are short (kAnimPace) and keep arriving.
//
// The path is rebuilt here rather than cached: the DEVICE can change between rolls (notched mouse
// one moment, free-spinner the next), and a stale path would show the wrong step size.
static void AnimSpawnBall()
{
  if (!g_monWnd || !IsWindow(g_monWnd))
    return;
  BuildAnimCurves();
  int slot = -1;
  for (int i = 0; i < kAnimBalls; ++i)
    if (g_anim.start[i] == 0)
    {
      slot = i;
      break;
    }
  if (slot < 0)
    return; // all busy: let them finish rather than cut one short
  DWORD now = GetTickCount();
  if (now == 0)
    now = 1; // 0 means "free slot"
  g_anim.start[slot] = now;
  g_anim.smooth[slot] = g_glideOn; // the mode at birth, so a later toggle cannot teleport it
  SetTimer(g_monWnd, kAnimTimerId, kAnimTickMs, nullptr);
  InvalidateRect(g_monWnd, &g_monBlk, FALSE);
}

// The path changed (a slider moved): recompute and repaint, but do NOT launch a ball -- a drag
// would otherwise leave a crowd of balls behind, and one is enough once the lever is let go.
static void AnimRebuild()
{
  if (!g_monWnd || !IsWindow(g_monWnd))
    return;
  BuildAnimCurves();
  InvalidateRect(g_monWnd, &g_monBlk, FALSE);
}
#else
// Headless build: the track lives on the settings panel, so there is nothing to draw. The two entry
// points stay, as no-ops, so the call sites below need no guard of their own.
static void AnimSpawnBall() {}
static void AnimRebuild() {}
#endif // SWS_NO_SETTINGS_UI

#ifndef SWS_NO_SETTINGS_UI
// Settings window (defined further down); its action id is handled in OnAction.
static void ShowConfigWindow();
static void ToggleConfigWindow();
static int g_cmdTune;
#endif

// Clamp every runtime value into its slider range. The sliders and any loaded setting
// both go through here, so no value outside the range can ever reach the model --
// that is what makes "min and max cannot go wrong" hold at both ends.
static double Clamp(double v, double lo, double hi)
{
  return v < lo ? lo : (v > hi ? hi : v);
}

// Same for a colour component, so deriving a border colour from the theme background
// cannot wrap around at black or white.
static int ClampInt(int v, int lo, int hi)
{
  return v < lo ? lo : (v > hi ? hi : v);
}

static void RefreshDerived()
{
  // The model's parameters are clamped at this single source, so a bad value in the store can never
  // reach the model.
  g_windowMs = Clamp(g_windowMs, kWindowMinMs, kWindowMaxMs);
  g_startDeltas = Clamp(g_startDeltas, kStartMin, kStartMax);
  g_budgetDeltas = Clamp(g_budgetDeltas, kBudgetMin, kBudgetMax);
  g_speedMul = Clamp(g_speedMul, kSpeedMulMin, kSpeedMulMax);
}

// ---------------------------------------------------------------------------
// Settings storage
//
// Saved through REAPER's own extended state (SetExtState with persist=true), under
// the plugin's own section. That is REAPER's supported mechanism for extension state:
// the plugin writes no file of its own and never touches REAPER's preferences.
//
// Each value is its own key, so a stale or hand-edited entry can only affect that one
// value, and every loaded value goes through RefreshDerived -> Clamp, so a bad number
// in the store can never reach the model.
// ---------------------------------------------------------------------------
static const char *kStateSection = "SmoothWheelScroll";

static void SaveSettings()
{
  if (!SetExtState)
    return;
  char buf[64];
  // The model's parameters: the window (duration), the start travel and the speed budget.
  _snprintf(buf, sizeof(buf), "%.6f", g_windowMs);
  SetExtState(kStateSection, "window", buf, true);
  _snprintf(buf, sizeof(buf), "%.6f", g_startDeltas);
  SetExtState(kStateSection, "startd", buf, true);
  _snprintf(buf, sizeof(buf), "%.6f", g_budgetDeltas);
  SetExtState(kStateSection, "budget", buf, true);
  _snprintf(buf, sizeof(buf), "%.6f", g_speedMul);
  SetExtState(kStateSection, "speedmul", buf, true);
  SetExtState(kStateSection, "glide", g_glideOn ? "1" : "0", true);
  SetExtState(kStateSection, "tprevzoom", g_touchpadReverse ? "1" : "0", true);
  SetExtState(kStateSection, "dock", g_dockOn ? "1" : "0", true);
  // Clear the keys the OLD model's controls used. They are no longer read; leaving them behind would
  // be a puzzling thing to find in the store, and a stale "release" in particular looks like a live
  // setting. Cleared once per save (cheap, and idempotent).
  SetExtState(kStateSection, "precision", "", true);
  SetExtState(kStateSection, "slowmove", "", true);
  SetExtState(kStateSection, "start", "", true);
  SetExtState(kStateSection, "accel", "", true);
  SetExtState(kStateSection, "eatrate", "", true);
  SetExtState(kStateSection, "accelv", "", true);
  SetExtState(kStateSection, "release", "", true);
  SetExtState(kStateSection, "hold", "", true);
  SetExtState(kStateSection, "coast", "", true);
  SetExtState(kStateSection, "rise", "", true);
  SetExtState(kStateSection, "bend0", "", true);
  SetExtState(kStateSection, "bend1", "", true);
  SetExtState(kStateSection, "bend2", "", true);
  SetExtState(kStateSection, "bend3", "", true);
  SetExtState(kStateSection, "bend4", "", true);
  // Floating window POSITION as "x y". Written only once the window has actually been
  // placed, so an install that has never moved it does not pin a position it never had.
  if (g_floatPosValid)
  {
    _snprintf(buf, sizeof(buf), "%ld %ld", (long)g_floatPos.x, (long)g_floatPos.y);
    SetExtState(kStateSection, "pos", buf, true);
  }
  // Clear the key an earlier build used for the whole window rect: it is no longer read, and a
  // stale value would only be a puzzle later.
  SetExtState(kStateSection, "win", "", true);
  // The defaults revision (see LoadSettings): written so the one-time default upgrade runs once.
  _snprintf(buf, sizeof(buf), "%d", g_defaultsRev);
  SetExtState(kStateSection, "defrev", buf, true);
  Log("settings saved: window=%.0fms start=%.1fd budget=%.0fd mul=%.2f glide=%d dock=%d", g_windowMs,
      g_startDeltas, g_budgetDeltas, g_speedMul, g_glideOn ? 1 : 0, g_dockOn ? 1 : 0);
}

// Read a key back as a double; returns false when absent or unparseable, so the
// caller keeps its current (default) value.
static bool LoadDouble(const char *key, double *out)
{
  if (!GetExtState || !out)
    return false;
  const char *s = GetExtState(kStateSection, key);
  if (!s || !*s)
    return false;
  char *end = nullptr;
  const double v = strtod(s, &end);
  if (end == s)   // nothing parsed
    return false;
  *out = v;
  return true;
}

// Read one long from a string and advance past it. Hand-rolled rather than sscanf: the C
// library's formatted-input engine is large (linking it cost ~23 KB of code) and this is
// only ever used for the window rectangle below.
static bool ParseLong(const char **p, long *out)
{
  const char *s = *p;
  while (*s == ' ' || *s == '\t')
    ++s;
  bool neg = false;
  if (*s == '-' || *s == '+')
  {
    neg = (*s == '-');
    ++s;
  }
  if (*s < '0' || *s > '9')
    return false;
  long v = 0;
  while (*s >= '0' && *s <= '9')
    v = v * 10 + (*s++ - '0');
  *out = neg ? -v : v;
  *p = s;
  return true;
}

static void LoadSettings()
{
  // Start from the compiled-in defaults, then overlay whatever was saved, so a partial
  // or corrupt store simply keeps the defaults for the missing keys.
  g_windowMs = kDefaultWindowMs;
  g_startDeltas = kDefaultStart;
  g_budgetDeltas = kDefaultBudget;
  g_speedMul = kDefaultSpeedMul;
  g_glideOn = true;
  g_touchpadReverse = true;

  double v = 0.0;
  if (LoadDouble("window", &v)) g_windowMs = v;
  if (LoadDouble("startd", &v)) g_startDeltas = v;
  if (LoadDouble("budget", &v)) g_budgetDeltas = v;
  if (LoadDouble("speedmul", &v)) g_speedMul = v;

  // THE DEFAULTS WERE RETUNED (2026-09-16, see kDefaultWindowMs). A store written under the old
  // defaults would otherwise keep showing them, and "the default changed" would look ignored. So the
  // four values are upgraded ONCE -- and only where the stored value is still EXACTLY an old default,
  // which is what a setting the user never touched looks like. A value that WAS tuned is left alone.
  {
    const char *rev = GetExtState ? GetExtState(kStateSection, "defrev") : nullptr;
    if (!rev || *rev != '2')
    {
      const double eps = 1e-9;
      if (fabs(g_windowMs - kOldWindowMs) < eps) g_windowMs = kDefaultWindowMs;
      if (fabs(g_startDeltas - kOldStart) < eps) g_startDeltas = kDefaultStart;
      if (fabs(g_budgetDeltas - kOldBudget) < eps) g_budgetDeltas = kDefaultBudget;
      if (fabs(g_speedMul - kOldSpeedMul) < eps) g_speedMul = kDefaultSpeedMul;
      g_defaultsRev = 2;
    }
  }
  if (GetExtState)
  {
    const char *g = GetExtState(kStateSection, "glide");
    if (g && *g)
      g_glideOn = (g[0] != '0');
    const char *tr = GetExtState(kStateSection, "tprevzoom");
    if (tr && *tr)
      g_touchpadReverse = (tr[0] != '0');
    const char *d = GetExtState(kStateSection, "dock");
    if (d && *d)
      g_dockOn = (d[0] != '0');
    // Floating window position, if one was ever recorded. A malformed value is simply
    // ignored, which leaves the window to open at its default (centred) placement. The
    // size is not read -- it is not remembered (see g_floatPos).
    const char *p = GetExtState(kStateSection, "pos");
    long px = 0, py = 0;
    const char *pp = p;
    if (pp && ParseLong(&pp, &px) && ParseLong(&pp, &py))
    {
      g_floatPos.x = (LONG)px;
      g_floatPos.y = (LONG)py;
      g_floatPosValid = true;
    }
  }
  RefreshDerived(); // clamps everything into range
  Log("settings loaded: window=%.0fms start=%.1fd budget=%.0fd mul=%.2f glide=%d dock=%d", g_windowMs,
      g_startDeltas, g_budgetDeltas, g_speedMul, g_glideOn ? 1 : 0, g_dockOn ? 1 : 0);
}

// (There is no "reset everything" function any more: a RESET button was replaced by
//  double-clicking a fader, which restores just that one parameter -- the gesture
//  REAPER uses on its own faders and knobs. Restoring everything at once has no
//  natural gesture and no longer has a control, so the code is gone rather than left
//  unreachable.)

// Name-based classification: the thin REAPER-facing shell around routing.h.
//
// The rule itself (which names are admitted, and the axis/kind they resolve to) lives in
// ClassifyName() in src/routing.h -- pure string logic with no REAPER dependency, so it can be
// compiled and diffed outside REAPER. This function only fetches the name from REAPER and hands it
// over, which is the one part that cannot leave this file.
static bool ClassifyByName(KbdSectionInfo *sec, int command, ActionSpec &out)
{
  if (!sec || !kbd_getTextFromCmd)
    return false;
  const char *nm = kbd_getTextFromCmd(command, sec);
  if (!ClassifyName(sec->uniqueID, nm, out))
    return false;
  out.command = command; // the name rule does not carry the id; the caller has it
  return true;
}

// ---------------------------------------------------------------------------
// Integrator -- one per axis. Holds only the delivery plumbing (which drive,
// which action, which window); the motion itself lives in model::Axis.
// ---------------------------------------------------------------------------
struct Integrator
{
  Drive drive = DRIVE_NONE;
  int section = 0;
  int command = 0;
  HWND replayHwnd = nullptr; // context window to replay the action on
  double accum = 0.0;        // fractional carry for REPLAY (units)
  double last = 0.0;         // seconds; 0 = needs priming (tick dt bookkeeping)
  Delivery delivery = Delivery::kStream; // how this action's travel must be handed over
  // Whether this gesture has delivered anything yet. A whole-unit receiver (kStepUnits) moves
  // in units, so a gesture whose travel never reaches one unit would do nothing at all; when
  // such a gesture ends, one unit is sent (see Tick), which is the smallest move it can make.
  bool sentThisBurst = false;
  // THE SPEED BUDGET needs the time since the previous message on THIS axis, to decay itself and to
  // know how fast the wheel is being turned. Both default to ZERO, which is what keeps this struct
  // (and the model's 2048-entry arrays) in .bss -- a non-zero default would move it into .data (see
  // AGENTS.md).
  bool hasLast = false;
  double lastMsg = 0.0; // seconds; the previous wheel message on this axis
  model::SpeedBudget budget; // "how much the wheel has been turning lately", in deltas (see model.h)
  // Identity of the current operation, kept so a run of notches on one action is recognised as one
  // gesture and its glide is not reset between them.
  Drive lastDrive = DRIVE_NONE;
  int lastSection = 0, lastCommand = 0;
  model::Axis glide;         // the actual motion
  // The MAIN view's VERTICAL ZOOM runs on the 1.6.1 curve model instead (AGENTS.md 111), so each
  // axis needs that motion state too.
  //
  // It is held as a POINTER into a separate global (see glide161For), NOT as a member, and that is
  // deliberate: the 1.6.1 Glide has non-zero member initialisers, so a member would make this whole
  // struct non-zero-initialisable and move it -- and `glide`'s two 2048-entry arrays, ~49 KB each --
  // out of .bss into .data (measured: .data 0x2d0 -> 0x184d0, .bss 0x11270 -> 0x11b0). The pointer
  // stays null (= zero), so the struct keeps its place in .bss. AGENTS.md's .bss rule.
  model161::Axis *glide161 = nullptr;
  bool zoom161 = false; // this gesture runs on the 1.6.1 curve model (vertical zoom)
};

// The 1.6.1 motion state, one per axis, kept as file-scope globals so `Integrator` stays zero-
// initialisable (see the note on glide161). Only the vertical axis ever uses one (the zoom actions
// are main-section-vertical); the horizontal pair exists so the lookup is uniform.
static model161::Axis g_vert161;
static model161::Axis g_horz161;

static bool AxisActive(const Integrator &g)
{
  return g.zoom161 ? (g.glide161 && g.glide161->Active()) : g.glide.Active();
}
static void AxisReset(Integrator &g)
{
  g.glide.Reset();
  if (g.glide161)
    g.glide161->Reset();
}

static Integrator g_vert;
static Integrator g_horz;

// ---------------------------------------------------------------------------
// One notch's travel, in each drive's own unit. The animation treats that unit
// as "1 notch"; everything below deals only in it.
// ---------------------------------------------------------------------------
static double OneNotchUnit()
{
  return kNotchUnits; // one wheel notch, in the relative units the action uses
}

// ---------------------------------------------------------------------------
// List controls (media explorer, track manager, FX browser, ...). These are not
// action-driven -- no REAPER command is bound to their wheel -- so hookcommand2
// never sees them. They are, however, all standard common-control listviews or
// tree views, which gives us one class-based port instead of per-list work:
// swallow the wheel over such a control and step it with WM_VSCROLL.
// ---------------------------------------------------------------------------
// Encode a signed relative amount (in 7-bit units, fractional allowed) into
// REAPER's relative form. This is a faithful port of REAPER's own
// encode_relmode1_extended (reaper_csurf/csurf_osc.cpp, itself "copied from
// kbd.cpp"), so the value is decoded exactly as REAPER encodes it internally --
// no guessing at the format.
// The inverse of EncodeRel1: recover the signed value REAPER will see from a (val, valhw) pair.
// Used by the touchpad direction-reverse, which must negate exactly what REAPER was going to use.
// The form is documented in the encoder above: value = val + (val<0 ? z/256 : val>0 ? -z/256 : 0),
// with valhw = -1-z.
static double DecodeRel1(int val, int valhw)
{
  const int v = (val & 0x40) ? (val - 128) : val; // sign-extend the 7-bit field
  const int z = -1 - valhw;
  if (v < 0)
    return v + z / 256.0;
  if (v > 0)
    return v - z / 256.0;
  return 0.0;
}

static void EncodeRel1(double d, int *val, int *valhw)
{
  int val7bit = 0, z = 0;
  if (d < 0.0)
  {
    const double w = floor(d);
    if (w >= -64.0)
    {
      val7bit = (int)w;
      z = (int)((d - w) * 256.0 + 0.5);
    }
    else
      val7bit = -64;
  }
  else if (d > 0.0)
  {
    const double w = ceil(d);
    if (w <= 63.0)
    {
      val7bit = (int)w;
      z = (int)((w - d) * 256.0 + 0.5);
    }
    else
      val7bit = 63;
  }
  *val = val7bit & 0x7f;
  *valhw = -1 - (z > 255 ? 255 : z);
}

// Re-invoke the classified action with a signed relative amount.
//
// `unitsSigned` is in 7-bit units (fractional; one wheel notch is kNotchUnits) and
// is encoded with REAPER's own relative encoding (see EncodeRel1), so the animated
// amount arrives with 256x more resolution in its fractional part than the plain
// integer form -- the action still does all the moving, only the precision of the
// value changes.
//
// Dispatch differs by section (all measured against REAPER):
//   - main (0/100): KBD_OnMainActionEx, the documented main-action entry.
//   - other sections (MIDI editor, ...): the section's own onAction callback.
//     kbd_RunCommandThroughHooks only runs the hook chain and does NOT execute
//     the action, and MIDIEditor_OnCommand takes no relative value, so neither
//     can reproduce a relative zoom/scroll; section->onAction does.
// hwnd is the context window the original action was dispatched to -- replaying
// on the wrong window (e.g. a child under the mouse instead of the editor itself)
// makes REAPER drop the action.
static void SendRelative(int section, int command, HWND hwnd, int val, int valhw)
{
  if (!hwnd)
    hwnd = g_main;
  g_replaying = true;
  if (kDebugLog)
    Log("  replay sec=%d cmd=%d val=%d valhw=%d", section, command, val, valhw);
  if (section == 0 || section == 100)
  {
    if (KBD_OnMainActionEx)
      KBD_OnMainActionEx(command, val, valhw, 1, hwnd, nullptr);
  }
  else if (SectionFromUniqueID)
  {
    KbdSectionInfo *sec = SectionFromUniqueID(section);
    if (sec && sec->onAction)
      sec->onAction(command, val, valhw, 1, hwnd);
  }
  g_replaying = false;
}

static void ReplayAction(int section, int command, HWND hwnd, double unitsSigned)
{
  if (unitsSigned == 0.0)
    return;
  // Whole units only, encoded in the plain 7-bit relative form.
  // as before the precision work.
  if (!kFineValues)
  {
    int mag = (int)(fabs(unitsSigned) + 0.5);
    if (mag < 1)
      mag = 1;
    const int sign = (unitsSigned < 0.0) ? -1 : 1;
    while (mag > 0)
    {
      const int chunk = mag > 63 ? 63 : mag;
      SendRelative(section, command, hwnd, (sign < 0) ? (128 - chunk) : chunk, -1);
      mag -= chunk;
    }
    return;
  }
  double remain = unitsSigned;
  // The relative form tops out at |integer part| = 63..64, so split anything
  // larger into calls instead of dropping it.
  const double chunk = (double)kRelIntMax;
  while (fabs(remain) >= 1.0 / (kRelSubPerUnit * 2.0))
  {
    double d = remain;
    if (d > chunk)
      d = chunk;
    else if (d < -chunk)
      d = -chunk;
    int val = 0, valhw = 0;
    EncodeRel1(d, &val, &valhw);
    SendRelative(section, command, hwnd, val, valhw);
    remain -= d;
  }
}

// Apply one notch's travel immediately, in the drive's own unit, for the
// synchronous case (release shorter than the timer can express).
//   DRIVE_REPLAY    : raw wheel units
//   DRIVE_MCP_WHEEL : whole wheel deltas, sent to REAPER's mixer window
static void ApplyMcpWheel(HWND mcp, int deltaSigned); // defined below DeliverTravel

static void ApplyTravelNow(Integrator &g, double signedTravel)
{
  const double mag = fabs(signedTravel);
  if (mag <= 0.0)
    return;

  switch (g.drive)
  {
  case DRIVE_NONE:
    return;
  case DRIVE_MCP_WHEEL:
  {
    // Release at or below the sync threshold: no glide, so the whole travel goes out at once.
    // It is still measured in whole wheel deltas (converted the same way as the animated
    // path), and REAPER's own mixer handler does the moving -- just without spreading it out.
    const double deltas = signedTravel * (kDeltasPerNotch / kMixerUnitsPerNotch);
    const double whole = (deltas < 0.0) ? -floor(-deltas) : floor(deltas);
    if (whole != 0.0)
      ApplyMcpWheel(g.replayHwnd, (int)whole);
    return;
  }
  case DRIVE_REPLAY:
  {
    // Reached only when Release is at or below kSyncReleaseMs (an immediate step,
    // no animation). The value is still sent at full 14-bit resolution.
    ReplayAction(g.section, g.command, g.replayHwnd, signedTravel);
    return;
  }
  }
}

// WHERE a gesture is going, and how to reach it. This is the whole interface a caller needs: it
// hands over a Route and nothing else -- the FILTER is resolved inside Kick from the route, so no
// call site can pick a different one, and a caller cannot even name a delivery.
struct Route
{
  ActionSpec spec;              // section / command / axis / kind / drive
  HWND hwnd = nullptr;          // context window to replay the action on
};

// units: the amount the wheel reported, in 7-bit units (fractional ok). MODEL 3.0 does no shaping
// of its own -- it only decides WHEN the amount arrives (one window of `precisionMs`, split into
// equal parts) -- so this just names the operation and feeds it the amount, signed.
static void Kick(Integrator &g, const Route &route, double wheelSign, double units)
{
  if (units <= 0.0)
    return;

  // ONE BALL PER WHEEL MESSAGE, launched here so it covers EVERY surface that drives the model -- the
  // action path, the arrange scrollbars, the track panel and the mixer all end up in this function.
  // The glide-OFF case does not reach here (it returns before the model), so it launches its own ball
  // in OnAction; between the two, every wheel gets exactly one ball.
  //
  // The ball picks the glide/stepped path from g_glideOn at birth (see AnimSpawnBall), so a wheel
  // arriving just after the switch was flipped is drawn in the mode that flip selected.
  AnimSpawnBall();

  // The filter: chosen from the route, in ONE place (see FilterFor in routing.h). Nothing below
  // or at any call site decides it.
  const Drive drive = route.spec.drive;
  const int section = route.spec.section;
  const int command = route.spec.command;
  const HWND replayHwnd = route.hwnd;

  const bool wasActive = AxisActive(g);
  // Same operation = same drive/action/target.
  const bool sameOp = g.lastDrive == drive && g.lastSection == section &&
                      g.lastCommand == command;

  g.drive = drive;
  g.section = section;
  g.command = command;
  g.replayHwnd = replayHwnd;
  g.delivery = FilterFor(route.spec);
  g.lastDrive = drive;
  g.lastSection = section;
  g.lastCommand = command;

  const double sign = wheelSign;
  const double unit = OneNotchUnit();
  const double recvDeltas = fabs(units) * kDeltasPerNotch; // this message, in wheel deltas

  // ---- HOW FAR THIS NOTCH GOES: the speed budget ------------------------------------------------
  //
  // A notch moves `g_startDeltas` at a standstill, rising to THIS message's own size (120 for a notched
  // mouse, its small share for a free-spinner) once the budget is full. See model::Travel.
  //
  // The budget follows the CURRENT speed: each message adds its deltas, and the total decays over the
  // time since the previous message (model::SpeedBudget). So a steady hand speed settles at a steady
  // travel -- any speed the user holds gives an even glide -- and slowing down lets the travel fall
  // back. Counting DELTAS (not messages) is what keeps a notched mouse and a free-spinner alike.
  //
  // The first message of a gesture has no previous one, so it contributes nothing to the decay and
  // starts from just its own deltas (a small budget -> a small first step, as expected).
  const double now = Now();
  if (!sameOp)
    g.budget.Reset(); // a new operation measures its speed from scratch
  const double gapMs = g.hasLast ? (now - g.lastMsg) * 1000.0 : 0.0;
  g.hasLast = true;
  g.lastMsg = now;
  const double budget = g.budget.Add(recvDeltas, gapMs);
  const double notchDeltas =
      model::Travel(recvDeltas, budget, g_budgetDeltas, g_startDeltas, g_speedMul);

  // THE MAIN VIEW'S VERTICAL ZOOM runs on the 1.6.1 CURVE MODEL (AGENTS.md 111). It is a SUSTAINED
  // gesture, not a per-notch amount: each notch STACKS a claim and the speed climbs toward a ceiling
  // over the rise time. That is why the whole message goes in as ONE notch of travel (`unit*units`,
  // as 1.6.1 fed it) instead of being pre-scaled -- the build-up is the model's job, not ours.
  const bool zoom161 = FullTravelFor(route.spec);
  g.zoom161 = zoom161;

  if (zoom161)
  {
    // Bind this axis to its 1.6.1 motion state (the vertical pair; see the globals above).
    g.glide161 = (&g == &g_horz) ? &g_horz161 : &g_vert161;
    if (wasActive && !g.glide161->Active())
      g.accum = 0.0; // previous gesture drained: drop any stale carry
    g.glide161->Kick(unit * units, sign, section * 100000 + command, now, Zoom161Params());
    if (!sameOp)
      g.accum = 0.0; // new operation: no carry leaks across
    if (kDebugLog)
      Log("kick161 drive=%d cmd=%d units=%.2f (1.6.1 curve model) in-flight=%d", (int)drive, command,
          units, g.glide161->Active() ? 1 : 0);
    return;
  }

  const double scale = (recvDeltas > 0.0) ? (notchDeltas / recvDeltas) : 1.0;
  const double amount = sign * unit * units * scale; // the amount actually handed over

  if (!sameOp)
  {
    // New operation: drop any half-finished carry, so no motion from the previous operation leaks
    // into this one.
    g.accum = 0.0;
    g.glide.Reset();
  }
  if (!sameOp || !wasActive)
    g.last = Now(); // starting from rest: the tick clock restarts here
  if (!wasActive)
    g.sentThisBurst = false; // a gesture beginning from rest can be topped up at its end

  // The whole model: this amount opens one window of `g_windowMs`, handed over across it. Overlapping
  // windows (a roll) simply add up, which is what keeps the motion flowing; the gap decides whether
  // this window should ease its payout (see AnimParams).
  g.glide.Feed(amount, AnimParams(gapMs));

  if (kDebugLog)
    Log("kick drive=%d cmd=%d units=%.2f amount=%.4f (notch=%.1fd budget=%.0fd) window=%.0fms in-flight=%d",
        (int)drive, command, units, amount, notchDeltas, budget, g_windowMs, g.glide.InFlight());

  // Release shorter than the timer can express, or a receiver where a coast has nowhere to land:
  // hand the whole amount over at once and skip the window.
  if (g.delivery == Delivery::kImmediate || g_windowMs <= kSyncReleaseMs)
  {
    ApplyTravelNow(g, amount);
    g.glide.Reset();
  }
}

static void StopTimer()
{
  if (g_mmTimer)
  {
    timeKillEvent((UINT)(UINT_PTR)g_mmTimer);
    g_mmTimer = nullptr;
  }
  if (g_timer) // fallback timer, if the multimedia one was unavailable
  {
    KillTimer(nullptr, g_timer);
    g_timer = 0;
  }
  g_timerOn = false;
  InterlockedExchange(&g_tickPosted, 0);
}

// Hand the mixer SMALL WHEEL MESSAGES, so REAPER's own mixer code moves it.
//
// The obvious-looking interface, SetMixerScroll, takes a TRACK -- one whole track is the
// smallest move it can express. Driving it from the animation produced exactly that visible
// one-track-at-a-time stepping, because the plugin was doing the quantising itself and could
// never ask for less than a track. A trackpad scrolls the same mixer smoothly (measured by
// the user), which shows the mixer's OWN handler can do finer work; the plugin was simply not
// using it.
//
// So the animated travel is converted into whole wheel deltas (one notch = WHEEL_DELTA = 120,
// the unit a real device reports in) and sent to REAPER's mixer window as WM_MOUSEWHEEL, with
// the fraction carried so no travel is lost. REAPER then applies its own "Scroll MCP" rule to
// it -- the same code path a physical wheel or trackpad takes -- so the distance per notch and
// the smoothness are REAPER's, not ours, and the plugin only decides the timing.
//
// Sent with SendMessage, not PostMessage, deliberately: SendMessage goes straight to the
// window procedure, so this plugin's own wheel hook (which runs on queued messages) never sees
// our synthetic wheel -- otherwise the plugin would try to animate its own output. Both run on
// the UI thread, so this is a direct call, not a cross-thread one.
//
// deltaSigned is a whole device delta; positive = wheel up, matching a real WM_MOUSEWHEEL.
static void ApplyMcpWheel(HWND mcp, int deltaSigned)
{
  if (!mcp || deltaSigned == 0)
    return;
  double remain = (double)deltaSigned;
  while (fabs(remain) >= 1.0)
  {
    // At most one notch per message: a larger single delta would be pointless (REAPER would
    // read it as several notches at once) and this keeps each message in the range a real
    // device sends.
    double d = remain;
    if (d > WHEEL_DELTA) d = WHEEL_DELTA;
    else if (d < -WHEEL_DELTA) d = -WHEEL_DELTA;
    // WM_MOUSEWHEEL carries SCREEN coordinates in lParam and the signed delta in the high
    // word of wParam (the low word is the key state, 0 here since we send no modifiers).
    // The point is the position of the wheel that started this gesture, which was over the
    // mixer -- so REAPER resolves its own "Scroll MCP" context for it, not some other one.
    const WPARAM wp = MAKEWPARAM(0, (WORD)(short)d);
    const LPARAM lp = MAKELPARAM(g_mcpWheelPt.x, g_mcpWheelPt.y);
    g_sendingSynthWheel = true;
    SendMessage(mcp, WM_MOUSEWHEEL, wp, lp);
    g_sendingSynthWheel = false;
    remain -= d;
  }
}


static void DeliverTravel(Integrator &g, double step)
{
  if (step == 0.0)
    return;

  if (g.drive == DRIVE_MCP_WHEEL)
  {
    g.accum += step;
    // The glide's travel is in 7-bit units (one notch is kNotchUnits). One input notch is
    // mapped to one device notch of WHEEL_DELTA deltas, which is the same distance REAPER's
    // own wheel moves the mixer (measured: one notch = one track), so the glide changes only
    // the timing, not the distance. Whole deltas are sent; the fraction is carried.
    const double deltas = g.accum * (kDeltasPerNotch / kMixerUnitsPerNotch);
    const double whole = (deltas < 0.0) ? -floor(-deltas) : floor(deltas);
    if (whole != 0.0)
    {
      // Device sign, not track-index sign: a real wheel-up sends a POSITIVE delta, and that
      // is what REAPER's own handler expects to see. (The old SetMixerScroll code negated
      // because it was naming a track index instead; that no longer applies.)
      const int sendDelta = (int)whole;
      ApplyMcpWheel(g.replayHwnd, sendDelta);
      // Remove exactly what was sent, converted back into the glide's units.
      g.accum -= whole * (kMixerUnitsPerNotch / kDeltasPerNotch);
      if (kDebugLog)
        Log("MCP send delta=%d (%.3f notch); accum left %.4f units", sendDelta,
            sendDelta / kDeltasPerNotch, g.accum);
    }
    return;
  }


  if (g.drive == DRIVE_REPLAY)
  {
    g.accum += step;
    if (g.delivery == Delivery::kStepUnits)
    {
      // A receiver that moves in whole increments still gets its travel in PIECES -- it just
      // gets pieces no finer than that receiver can act on.
      //
      // Whole-unit-only was wrong. A gentle notch travels well UNDER one 7-bit unit (measured:
      // 0.6 units at Start 5%), so flooring to whole units collapsed a smooth gesture into a
      // single 1-unit step -- larger than the travel itself. That is the "sudden speed-up
      // while creeping" that was reported. The grid is now kVertStepsPerUnit pieces per unit:
      // fine enough that a slow gesture is still a stream of small pieces, coarse enough to
      // stay clearly above the finest step that used to make this axis jitter.
      const double grid = 1.0 / kVertStepsPerUnit;
      const double m = floor(fabs(g.accum) / grid + 0.5);
      if (m >= 1.0)
      {
        const double send = m * grid;
        const double signed_send = (g.accum < 0.0) ? -send : send;
        g.accum -= signed_send;
        g.sentThisBurst = true;
        ReplayAction(g.section, g.command, g.replayHwnd, signed_send);
      }
      return;
    }
    if (kFineValues)
    {
      // Device-stream output: the relative form's finest step is 1/kRelSubPerUnit
      // of a 7-bit unit, so accumulate on that grid and carry the remainder -- a
      // step too small to send this frame is added to the next one.
      const double grid = 1.0 / kRelSubPerUnit;
      const double m = floor(fabs(g.accum) / grid + 0.5);
      if (m >= 1.0)
      {
        const double send = m * grid;
        const double signed_send = (g.accum < 0.0) ? -send : send;
        g.accum -= signed_send;
        ReplayAction(g.section, g.command, g.replayHwnd, signed_send);
      }
    }
    else
    {
      // Baseline: whole relative units only, rounded to nearest (never truncated),
      // with the fraction carried. This is exactly the pre-precision behaviour.
      const double m = floor(fabs(g.accum) + 0.5);
      if (m >= 1.0)
      {
        const int whole = (int)m;
        const double signed_send = (g.accum < 0.0) ? -(double)whole : (double)whole;
        g.accum -= signed_send;
        ReplayAction(g.section, g.command, g.replayHwnd, signed_send);
      }
    }
    return;
  }
}

// ---------------------------------------------------------------------------
// One animation step: ask the glide for this frame's travel and deliver it.
// The glide is exact over the whole dt, so the motion is frame-rate independent.
// ---------------------------------------------------------------------------
static void TickIntegrator(Integrator &g, double dt)
{
  if (dt <= 0.0)
    return;
  double step = 0.0;
  if (g.zoom161)
  {
    if (!g.glide161 || !g.glide161->Active())
      return;
    step = g.glide161->Tick(dt, Zoom161Params());
  }
  else
  {
    if (!g.glide.Active())
      return;
    step = g.glide.Tick(dt, AnimParams(0.0)); // the ease lives per window, not here
  }
  // Defensive: a non-finite step would poison every receiver. Drop the glide
  // rather than forward it.
  if (!(step > -1e30 && step < 1e30))
  {
    AxisReset(g);
    return;
  }
  if (step != 0.0)
    DeliverTravel(g, step);
}

static void Tick()
{
  const double t = Now();
  Integrator *list[2] = {&g_vert, &g_horz};
  for (int i = 0; i < 2; ++i)
  {
    Integrator &g = *list[i];
    if (!AxisActive(g))
      continue;
    double dt;
    if (g.last == 0.0)
    {
      // Safety net; Kick normally primes this already.
      g.last = t;
      dt = kAnimTimerMs / 1000.0;
    }
    else
    {
      dt = t - g.last;
      g.last = t;
      if (dt < 0.0)
        dt = 0.0;
      if (dt > 0.25)
        dt = 0.25; // a stalled frame must not fling the view
    }
    const bool wasActive = AxisActive(g);
    TickIntegrator(g, dt);

    // A whole-unit receiver (kStepUnits) moves in whole units, and a gesture whose travel
    // never reaches one would be delivered nothing at all -- the axis simply would not
    // respond. When such a gesture ends, send one unit in the direction it was going, which
    // is the smallest move this receiver can make. A gesture that did reach a unit keeps its
    // own remainder for the next one, so ordinary use is unchanged.
    if (wasActive && !AxisActive(g) && g.delivery == Delivery::kStepUnits &&
        !g.sentThisBurst && g.accum != 0.0)
    {
      const double send = (g.accum < 0.0) ? -1.0 : 1.0;
      g.accum = 0.0;
      ReplayAction(g.section, g.command, g.replayHwnd, send);
      if (kDebugLog)
        Log("top-up sec=%d cmd=%d -> one unit (gesture never reached a whole unit)",
            g.section, g.command);
    }
  }
  if (!AxisActive(g_vert) && !AxisActive(g_horz))
  {
    StopTimer();
  }
}

// Fallback timer callback, used only if the multimedia timer is unavailable.
static void CALLBACK AnimProc(HWND, UINT, UINT_PTR, DWORD)
{
  Tick();
}

// Runs on the multimedia timer's own thread: post a tick to the UI thread and
// return immediately. REAPER's scroll/zoom calls must stay on the UI thread, so
// nothing else may happen here. At most one tick is outstanding at a time, so a
// busy UI thread cannot build up a backlog of them.
static void CALLBACK MmTimerProc(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR)
{
  if (InterlockedExchange(&g_tickPosted, 1) == 0)
    PostMessage(g_animWnd, WM_APP_GLIDE_TICK, 0, 0);
}

static LRESULT CALLBACK AnimWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  if (msg == WM_APP_GLIDE_TICK)
  {
    InterlockedExchange(&g_tickPosted, 0);
    Tick();
    return 0;
  }
  return DefWindowProc(h, msg, wp, lp);
}

// Create the plugin's own message window. This is a window WE own and register,
// never a REAPER window, so nothing about REAPER is touched.
static bool EnsureAnimWindow()
{
  if (g_animWnd && IsWindow(g_animWnd))
    return true;
  static bool registered = false;
  if (!registered)
  {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = AnimWndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = "SmoothWheelScrollAnimWnd";
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
      return false;
    registered = true;
  }
  g_animWnd = CreateWindowExA(0, "SmoothWheelScrollAnimWnd", "", 0, 0, 0, 0, 0,
                              HWND_MESSAGE, nullptr, g_hInst, nullptr);
  return g_animWnd != nullptr;
}

static void StartTimer()
{
  if (g_timerOn)
    return;
  // Baseline: the plain thread timer, exactly as before the precision work.
  if (!kFastTimer)
  {
    g_timer = SetTimer(nullptr, 0, kAnimTimerMs, AnimProc);
    g_timerOn = (g_timer != 0);
    return;
  }
  if (!EnsureAnimWindow())
  {
    // No window: fall back to the coarse thread timer so the glide still runs.
    g_timer = SetTimer(nullptr, 0, kAnimTimerMs, AnimProc);
    g_timerOn = (g_timer != 0);
    return;
  }
  g_mmTimer = (HANDLE)(UINT_PTR)timeSetEvent(g_mmTimerMs, 1, MmTimerProc,
                                             (DWORD_PTR)g_animWnd, TIME_PERIODIC);
  if (g_mmTimer)
  {
    g_timerOn = true;
    return;
  }
  g_timer = SetTimer(nullptr, 0, kAnimTimerMs, AnimProc);
  g_timerOn = (g_timer != 0);
}

// ---------------------------------------------------------------------------
// hookcommand2 -- REAPER reports the action each wheel resolved to
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// THE TOUCHPAD DIRECTION REVERSE -- its OWN feature, and it BYPASSES THE MODEL.
//
// A touchpad is a continuous surface: the OS already reports it smoothly, so it is never fed to the
// model (see IsAnimatableWheel -- it is passed straight through). But REAPER reads the horizontal
// ZOOM the opposite way round from what a sideways swipe on a touchpad feels like, so for those two
// actions only (the main view's and the MIDI editor's horizontal zoom) the value REAPER resolved is
// taken, INVERTED, and handed straight back to the action -- no window, no model, no animation.
//
// WHY IT LIVES IN OnAction: REAPER has already turned the wheel into one of its own actions by the
// time this runs, so the action and its value are exactly what REAPER was about to use; inverting
// that value is the least indirect way to change the direction. It is deliberately placed BEFORE the
// wheel latch: a passed-through wheel never arms the latch, so the latch would reject it here.
// Consuming the action is required -- letting REAPER also run it would cancel the flip out.
//
// Returns true when it handled the action (the caller must then consume it).
// ---------------------------------------------------------------------------
// The Device verdict of the most recent wheel, or kUnknown-with-no-record when there is none. A
// stale record (from an action with no wheel behind it) reports kNotched, so an unrelated action is
// not mistaken for a passed-through wheel.
static Device LastWheelDevice()
{
  const DWORD t = (DWORD)InterlockedCompareExchange(&g_lastDevTick, 0, 0);
  if (t == 0 || (DWORD)(GetTickCount() - t) > kWheelFlagMs)
    return Device::kNotched; // no recent wheel: treat as "not a pass-through"
  return (Device)InterlockedCompareExchange(&g_lastDevKind, 0, 0);
}

// Was the most recent wheel a TOUCHPAD? (For the direction-reverse feature, which is the one thing
// a touchpad does get.)
static bool LastWheelWasTouchpad() { return LastWheelDevice() == Device::kTouchpad; }

// Did the hook PASS THAT WHEEL THROUGH -- i.e. did it refuse to animate it? A touchpad and an
// unidentified wheel both qualify. The action path must refuse them too, or REAPER's own
// re-offering of the resolved action would animate what the hook just declined.
static bool LastWheelPassedThrough()
{
  const Device d = LastWheelDevice();
  return d == Device::kTouchpad || d == Device::kUnknown;
}

static bool TouchpadZoomReverse(const ActionSpec &spec, int section, int command, int val, int valhw,
                                HWND hwnd)
{
  if (!g_touchpadReverse)
    return false;
  if (spec.axis != Axis::kHorizontal || spec.kind != Kind::kZoom)
    return false; // only the two horizontal-zoom actions

  if (!LastWheelWasTouchpad())
    return false; // the most recent wheel was not a touchpad

  const double v = DecodeRel1(val, valhw);
  if (v == 0.0)
    return true; // nothing to flip, but still ours: REAPER must not run it either
  int nv = 0, nvw = 0;
  EncodeRel1(-v, &nv, &nvw);
  if (kDebugLog)
    Log("touchpad reverse sec=%d cmd=%d val=%d valhw=%d -> val=%d valhw=%d (v=%.4f, model bypassed)",
        section, command, val, valhw, nv, nvw, v);
  SendRelative(section, command, hwnd, nv, nvw);
  return true;
}

static bool OnAction(KbdSectionInfo *sec, int command, int val, int val2, int relmode, HWND hwnd)
{
  if (g_replaying)
  {
    if (kDebugLog)
      Log("  replayed call reached hook sec=%d cmd=%d val=%d", sec ? sec->uniqueID : -999,
          command, val);
    return false; // our own replay call; never consume it
  }

#ifndef SWS_NO_SETTINGS_UI
  // Our own registered actions are dispatched through hookcommand2 (that is the
  // documented callback for custom_action), so handle them here rather than in a
  // separate hookcommand -- which never fires for a custom_action.
  if (command == g_cmdTune)
  {
    // A TOGGLE, not just "open": the action can be bound to a key and pressed again to
    // close the panel, so the same binding both opens and closes it.
    ToggleConfigWindow();
    return true;
  }
#endif

  if (kDebugLog)
  {
    const DWORD lt = (DWORD)InterlockedCompareExchange(&g_wheelTick, 0, 0);
    // Which wheel does this action belong to? Only claim the pairing when the wheel was
    // recent, so an unrelated action (a script, a toolbar click) is not attributed to a
    // wheel that happened minutes ago.
    const long seq = (DWORD)(GetTickCount() - g_wheelSeqTick) < 200
                         ? InterlockedCompareExchange(&g_wheelSeq, 0, 0)
                         : -1;
    Log("HOOK #%ld mod=%s sec=%d cmd=%d val=%d val2=%d relmode=%d hwnd=%p latch=%d", seq,
        g_wheelMods, sec ? sec->uniqueID : -999, command, val, val2, relmode, (void *)hwnd,
        lt ? 1 : 0);
  }

  if (!sec)
    return false;

  ActionSpec spec;
  const bool matched = LookupAction(sec->uniqueID, command, spec) ||
                       ClassifyByName(sec, command, spec);
  if (!matched)
  {
    InterlockedExchange(&g_wheelTick, 0);
    if (kDebugLog && kbd_getTextFromCmd)
    {
      const char *nm = kbd_getTextFromCmd(command, sec);
      Log("pass through sec=%d cmd=%d relmode=%d val=%d \"%s\"", sec->uniqueID, command,
          relmode, val, nm ? nm : "?");
    }
    return false;
  }

  if (TouchpadZoomReverse(spec, sec->uniqueID, command, val, val2, hwnd))
    return true; // consumed, so REAPER's own pass does not undo the sign flip

  // NOTHING THE HOOK PASSED THROUGH MAY BE ANIMATED HERE.
  //
  // The message hook refuses to animate a touchpad (and an unidentified wheel) and clears the wheel
  // latch, but that latch is only demanded for NON-relative actions: the "mousewheel"-marked family
  // is admitted without it, so that a MIDI CC or OSC can drive it. A passed-through wheel resolving
  // to one of those -- the main view's horizontal zoom is exactly such an action -- was therefore
  // animated anyway. This is that missing refusal. (The reverse feature above is the one thing a
  // touchpad DOES get, and it has already returned by now.)
  if (LastWheelPassedThrough())
    return false; // leave it to REAPER

  // Admission gate. The wheel latch exists to tell a wheel-driven action from a
  // keyboard-driven one. The "(MIDI CC relative/mousewheel)" actions are also
  // driven by a MIDI CC or OSC, which send no wheel message, so for those the
  // latch would reject every CC-driven notch; they are admitted on the relative
  // dispatch instead. Everything else keeps the original latch requirement.
  if (!spec.relativeAction)
  {
    const DWORD tick = (DWORD)InterlockedCompareExchange(&g_wheelTick, 0, 0);
    if (tick == 0)
      return false; // not a mouse wheel
    if ((DWORD)(GetTickCount() - tick) > kWheelFlagMs)
    {
      InterlockedExchange(&g_wheelTick, 0);
      return false;
    }
  }
  InterlockedExchange(&g_wheelTick, 0);

  // (A ball is launched from Kick, and from the glide-off branch below -- see those notes.)

  // Glide off: leave the action entirely to REAPER (native = no slide, and the
  // original wheel parameter is what reaches it, untouched).
  if (!g_glideOn)
  {
    // ONE ball, even with the glide OFF: the user wants the chart to move for EVERY wheel, and in
    // this state the ball runs the wheel's own stepped path instead of the model's (it picks that up
    // from g_glideOn when it is born -- see AnimSpawnBall). This is the action path; the surfaces the
    // hook drives are not handled at all while the glide is off, so nothing else is launched here.
    // (2026-09-16: "只要鼠标滚轮有事件就给动画，包括Enable关掉的时候，但是这时走的是那个锯齿动画".)
    AnimSpawnBall();
    if (kDebugLog)
      Log("glide off: native pass-through sec=%d cmd=%d", sec->uniqueID, command);
    return false;
  }

  if (kDebugLog)
    Log("MATCH sec=%d cmd=%d relmode=%d val=%d hwnd=%p -> drive=%d delivery=%d",
        sec->uniqueID, command, relmode, val, (void *)hwnd, (int)spec.drive,
        (int)FilterFor(spec));

  // Decode the relative wheel value: 1..63 = +, 65..127 = -.
  int raw = val & 0x7f;
  if (raw == 0)
    return false;
  const double wheelSign = (raw > 63) ? -1.0 : 1.0;
  const double units = (raw > 63) ? (128 - raw) : raw;
  const double notches = units / kNotchUnits;

  Integrator &g = (spec.axis == Axis::kHorizontal) ? g_horz : g_vert;
  Route route;
  route.spec = spec;
  route.hwnd = hwnd;
  Kick(g, route, wheelSign, notches);
  StartTimer();
  if (kDebugLog)
    Log("smooth sec=%d cmd=%d val=%d drive=%d hwnd=%p", sec->uniqueID, command, val,
        (int)spec.drive, (void *)hwnd);
  return true; // consume: REAPER must not perform its own jump
}

// ---------------------------------------------------------------------------
// Message hook -- notice wheel activity and remember where it happened.
//
// Three jobs:
//   1. Latch that a wheel just happened so hookcommand2 can tell a wheel-driven
//      action apart from a keyboard-driven one. The message is left intact here;
//      hookcommand2 decides.
//   2. Lists have no bound action, so hookcommand2 never sees their wheel. For
//      those we intercept here, swallow the message, and drive the list.
//   3. Two more surfaces scroll natively without an action (measured): the track
//      control panel (REAPERTCPDisplay) and the MIDI editor's piano keys
//      (MIDIPianoWindow). Those are intercepted here too.
// ---------------------------------------------------------------------------
// Which surface (if any) the wheel is taken over for.
//
// Two steps, and both are needed:
//
//   1. WHICH WINDOW the wheel landed on -- by window class. There is no API that
//      names "the TCP window"; the message itself arrives addressed to a window, so
//      the class is how the surface family is identified (REAPERTCPDisplay, the MIDI
//      piano keys, a list). This is not theme guessing: these are REAPER's own
//      top-level view windows.
//
//   2. WHAT PART of that window -- by REAPER's official hit-test,
//      GetThingFromPoint(). It answers in REAPER's own terms ("tcp", "tcp.meter",
//      "tcp.volume", "mcp.pan", ...), which a class check cannot, because a panel
//      draws its body and its controls into the same window. This is the step the
//      project rule is about: use the interface, do not reverse-engineer.
//
// An EMPTY hit-test string means "not a named track thing": on a panel that is the
// body / chrome / resize edge (measured -- the TCP's width-adjust strips report "").
// Those are view surface, so they scroll. Getting this wrong is exactly what made
// the panel edges stop working.
//
// Inside a panel, only the spots where the wheel ADJUSTS A VALUE are left to REAPER
// (the project rule: parameter-type wheels are never touched); everything else
// scrolls.
static const char *kPanelClasses[] = {
    "REAPERTCPDisplay", // track control panel
    "MIDIPianoWindow",  // MIDI editor piano keys
};

static bool ClassIsPanel(const char *cls)
{
  for (size_t i = 0; i < sizeof(kPanelClasses) / sizeof(kPanelClasses[0]); ++i)
    if (strcmp(cls, kPanelClasses[i]) == 0)
      return true;
  return false;
}

// Parts of a panel where the wheel ADJUSTS A VALUE: the wheel stays REAPER's there.
// Verified by measurement (strings GetThingFromPoint returns; a trailing index such
// as "tcp.fxlist 2" is cut at the first space):
//   volume / pan / width   faders and knobs
//   recinput / recmode     dropdowns that cycle
//   fxparm                 adjusts the parameter under the cursor
//   fxlist / sendlist      the list itself scrolls
//   fxembed(+header)       embedded FX GUI: parameter territory
//   envcp fader/value/...  envelope lane controls
// Toggles (mute / solo / phase / mono / recarm / recmon / fxbyp / folder / env / io
// / notes) are NOT here: a toggle is not wheel-adjustable, so a wheel over it is not
// a "parameter" wheel and REAPER does nothing there anyway -- those scroll.
static bool ThingIsValueControl(const char *info)
{
  char tok[64] = {0};
  size_t n = 0;
  for (const char *c = info; *c && *c != ' ' && n + 1 < sizeof(tok); ++c)
    tok[n++] = *c;

  static const char *kWheelAdjustable[] = {
      "tcp.volume", "tcp.pan", "tcp.width", "tcp.recinput", "tcp.recmode",
      "tcp.fxparm", "tcp.fxlist", "tcp.sendlist", "tcp.fxembed",
      "tcp.fxembedheader",
      "mcp.volume", "mcp.pan", "mcp.width", "mcp.recinput", "mcp.recmode",
      "mcp.fxparm", "mcp.fxlist", "mcp.sendlist", "mcp.fxembed",
      "mcp.fxembedheader",
      "envcp.fader", "envcp.value", "envcp.learn", "envcp.mod", "envcp.arm",
      "envcp.bypass",
  };

  for (size_t i = 0; i < sizeof(kWheelAdjustable) / sizeof(kWheelAdjustable[0]); ++i)
    if (strcmp(tok, kWheelAdjustable[i]) == 0)
      return true;
  return false;
}

// The TCP's screen rectangle. Taken from the TCP view window itself (the class
// REAPERTCPDisplay is REAPER's own track-panel window), refreshed when it moves or
// resizes. It is needed because the panel's WIDTH-ADJUST strips are separate helper
// windows sitting on top of the panel edge: a wheel there reports a generic window
// class and an empty hit-test string, so the panel has to be recognised by geometry.
static bool TcpScreenRect(RECT *out)
{
  static RECT cached = {0, 0, 0, 0};
  static DWORD checked = 0;
  const DWORD now = GetTickCount();
  HWND tcpw = g_main ? FindWindowExA(g_main, nullptr, "REAPERTCPDisplay", nullptr) : nullptr;
  if (!tcpw)
    return false;
  // Re-read at most a few times a second (cheap, and follows panel resizes).
  if (cached.right <= cached.left || (DWORD)(now - checked) > 250)
  {
    checked = now;
    RECT r = {0};
    if (!GetWindowRect(tcpw, &r))
      return false;
    cached = r;
  }
  *out = cached;
  return out->right > out->left && out->bottom > out->top;
}

// ---------------------------------------------------------------------------
// The TCP's two resize dividers (measured, not assumed).
//
// Beside the track panel sit two narrow strips that belong to neither panel: bare
// main-window chrome between the panel and whatever is next to it (the mixer on the
// left, the arrange view on the right). They are the drag handles for the panel's
// width AND they answer the wheel.
//
// REAPER splits each strip itself. Measured across the strips (x, one pixel at a time,
// y in the panel):
//
//   left strip  x 1280..1283 -> "View: Zoom horizontally" (990);  x 1284..1286 -> NOTHING
//   right strip x 1790..1794 -> NOTHING;                          x 1795..1797 -> 990
//
// So the OUTER half already behaves exactly like the main view (a plain wheel fires
// the same horizontal zoom the arrange does, and this plugin already smooths it via
// the action path). The INNER half -- the few pixels against the panel edge -- does
// nothing at all. That half is the panel's own edge, so a wheel there should move the
// tracks exactly as a wheel on the panel body does.
//
// This function answers only for the INNER half. The outer half is deliberately not
// claimed, so the main view's own mappings (including every modifier) keep working
// there untouched.
//
// The strips are located from WINDOW GEOMETRY, not from hardcoded pixel counts: a
// strip is the run of main-window chrome between the panel edge and the next real
// window. "Chrome" is the main window itself, the "#32770" frame the mixer is drawn
// inside, and plain "Static" labels. That way the split follows the theme's actual
// border widths instead of a number guessed for one layout.
static bool ClassIsChrome(const char *cls)
{
  return strcmp(cls, "REAPERwnd") == 0 || strcmp(cls, "#32770") == 0 ||
         strcmp(cls, "Static") == 0;
}

// The arrange view's rectangle. It is REAPERTrackListWindow, a direct child of the main
// window (NOT a dialog item -- GetDlgItem(main, 1000) does not return it).
//
// Cached briefly like TcpScreenRect: this is asked on every wheel message and the answer
// only changes when a panel is resized.
static bool ArrangeScreenRect(RECT *out)
{
  static RECT cached = {0, 0, 0, 0};
  static DWORD checked = 0;
  HWND arr = g_main ? FindWindowExA(g_main, nullptr, "REAPERTrackListWindow", nullptr)
                    : nullptr;
  if (!arr)
    return false;
  const DWORD now = GetTickCount();
  if (cached.right <= cached.left || (DWORD)(now - checked) > 250)
  {
    checked = now;
    RECT r = {0};
    if (!GetWindowRect(arr, &r))
      return false;
    cached = r;
  }
  *out = cached;
  return out->right > out->left && out->bottom > out->top;
}

// Which scrollbar of the arrange view, if any, a point is on. The plugin takes the wheel
// there because REAPER handles those bars itself and never lets them reach the action
// table -- measured: 87 wheels on the bars produced 0 actions (see AGENTS.md 19.12/19.13).
//
// There is no way to recognise a bar from the window (REAPER draws it inside the arrange
// window, and even GetThingFromPoint answers "arrange" there), so this is the one place
// that keys on GEOMETRY. Two things keep that honest:
//
//   * the band width is not a magic number: it is what Windows says a scrollbar is
//     (SM_CXVSCROLL / SM_CYHSCROLL, so it follows DPI), and
//   * it is sampled ONE pixel narrower than that, so the outermost pixel is left to
//     REAPER. Erring inward can only mean "the last pixel is not smoothed"; erring
//     outward would steal a pixel of the view. Same direction as the TCP divider rule.
//
// The vertical bar also has a MINIMUM LENGTH: a scrollbar is not drawn when the content
// fits, and a couple of pixels at the end of the view are not a bar.
enum class ArrangeBar { kNone, kVertical, kHorizontal };
static ArrangeBar ArrangeBarAt(POINT pt)
{
  RECT ar;
  if (!ArrangeScreenRect(&ar))
    return ArrangeBar::kNone;
  // Must be on the arrange view at all (a wheel elsewhere must not match).
  if (pt.x < ar.left || pt.x >= ar.right || pt.y < ar.top || pt.y >= ar.bottom)
    return ArrangeBar::kNone;

  int vw = GetSystemMetrics(SM_CXVSCROLL);
  int hh = GetSystemMetrics(SM_CYHSCROLL);
  if (vw < 2) vw = 17; // a sane fallback; the metrics call cannot usefully fail
  if (hh < 2) hh = 17;
  --vw; // sample one pixel narrow (see above)
  --hh;
  const int minLen = 8; // ignore slivers: not a bar REAPER would have drawn

  const bool onV = (pt.x >= ar.right - vw) && ((ar.bottom - ar.top) >= minLen);
  const bool onH = (pt.y >= ar.bottom - hh) && ((ar.right - ar.left) >= minLen);
  // The corner belongs to the vertical bar, so the two can never both match.
  if (onV)
    return ArrangeBar::kVertical;
  if (onH)
    return ArrangeBar::kHorizontal;
  return ArrangeBar::kNone;
}

static bool TcpDividerIsInner(int x, int y, const RECT &tcp)
{
  // Walk from the panel edge outward to the first window that is not chrome. That
  // window's edge bounds the strip. Bounded walk: if no real window is found close
  // by, there is no divider on that side.
  //
  // A divider is a NARROW strip. If the chrome run is wider than this it is not a
  // divider at all (for example the bare main-window edge when the panel sits hard
  // against it with no mixer), and nothing is claimed -- REAPER keeps that area.
  const int kMaxWalk = 32;
  const int kMaxStrip = 16;

  if (x < tcp.left)
  {
    int outer = -1; // leftmost pixel of the strip
    for (int i = 1; i <= kMaxWalk; ++i)
    {
      POINT p = {tcp.left - i, y};
      HWND h = WindowFromPoint(p);
      char cls[64] = {0};
      if (!h || !GetClassNameA(h, cls, sizeof(cls)) || !ClassIsChrome(cls))
      {
        outer = tcp.left - i + 1;
        break;
      }
    }
    const int width = (outer > 0) ? (tcp.left - outer) : 0;
    if (width >= 1 && width <= kMaxStrip)
    {
      // Claim half the strip, measured from the panel edge, ROUNDED DOWN (at least
      // one pixel). Rounding down is the safe direction: the pixels this leaves go
      // back to REAPER, which on this outer part already fires the main view's
      // horizontal zoom, whereas a pixel claimed by mistake would lose it.
      int n = width / 2;
      if (n < 1)
        n = 1;
      return x >= tcp.left - n;
    }
    return false;
  }

  if (x >= tcp.right)
  {
    int outer = -1; // rightmost pixel of the strip
    for (int i = 0; i <= kMaxWalk; ++i)
    {
      POINT p = {tcp.right + i, y};
      HWND h = WindowFromPoint(p);
      char cls[64] = {0};
      if (!h || !GetClassNameA(h, cls, sizeof(cls)) || !ClassIsChrome(cls))
      {
        outer = tcp.right + i - 1;
        break;
      }
    }
    const int width = (outer >= tcp.right) ? (outer - tcp.right + 1) : 0;
    if (width >= 1 && width <= kMaxStrip)
    {
      // Same rule as the left side (see above).
      int n = width / 2;
      if (n < 1)
        n = 1;
      return x < tcp.right + n;
    }
    return false;
  }

  return false; // inside the panel: not the divider
}

// Which surface the wheel is on, or null if it is not one we take over.
static HWND SurfaceWindow(HWND under, POINT pt, char *thingOut, size_t thingSize)
{
  thingOut[0] = 0;
  if (!under)
    return nullptr;

  char cls[64] = {0};
  if (!GetClassNameA(under, cls, sizeof(cls)))
    return nullptr;

  // What REAPER says is at the point (its own terms; empty = panel body/chrome).
  if (GetThingFromPoint)
    GetThingFromPoint(pt.x, pt.y, thingOut, (int)thingSize);

  HWND surface = under;
  // A plain label/child drawn on a panel still belongs to that panel.
  if (!ClassIsPanel(cls))
  {
    if (strcmp(cls, "Static") != 0)
    {
      // Not a panel, not a label. It may be one of the TCP's resize dividers: bare
      // main-window chrome beside the panel. Only the INNER half (the few pixels
      // against the panel edge) is claimed -- there a wheel currently does nothing,
      // and it belongs to the panel. The outer half is left alone so the main view's
      // own horizontal zoom keeps working there. See TcpDividerIsInner.
      RECT tcp;
      if (TcpScreenRect(&tcp) && pt.y >= tcp.top && pt.y < tcp.bottom &&
          TcpDividerIsInner(pt.x, pt.y, tcp))
      {
        HWND tcpw = g_main ? FindWindowExA(g_main, nullptr, "REAPERTCPDisplay", nullptr) : nullptr;
        return tcpw ? tcpw : under;
      }
      return nullptr;
    }
    HWND parent = GetParent(under);
    if (!parent || !GetClassNameA(parent, cls, sizeof(cls)))
      return nullptr;
    surface = parent;
  }

  if (ClassIsPanel(cls))
  {
    // Panel body, chrome, resize edge, label -- anything that is not a value
    // control -- is a view surface we scroll.
    if (ThingIsValueControl(thingOut))
      return nullptr; // parameter wheel: leave it to REAPER
    return surface;
  }

  return nullptr;
}

// WHICH DEVICE sent this wheel. MODEL 3.0 animates the two that report a wheel (a plain notched
// mouse and a free-spinning one) and leaves a touchpad to REAPER, because a touchpad is already
// continuous in the OS -- adding a second, plugin-side easing on top would be easing something that
// is not stepped in the first place.
//
// The rule itself lives in src/device.h (pure arithmetic over the delta and the message's extra
// info, so it can be compiled and tested outside REAPER -- see _diag/device_probe.cpp). This
// function is only the REAPER-facing shell: it reads the two inputs and asks the tracker.
static DeviceTracker g_device;

static bool IsAnimatableWheel(int delta)
{
  if (delta == 0)
    return false;
  const Device kind = g_device.Feed(delta, (long)GetMessageExtraInfo());

  // Record WHAT this wheel was, for the two readers above. Done for EVERY message, so the pair
  // always describes the most recent one.
  InterlockedExchange(&g_lastDevKind, (LONG)kind);
  InterlockedExchange(&g_lastDevTick, (LONG)GetTickCount());

  // ANIMATE ONLY A POSITIVELY IDENTIFIED WHEEL.
  //   kNotched  -- a whole 120, known on the very first message;
  //   kFreeSpin -- one fixed sub-notch step, identified within a few messages.
  // Everything else goes to REAPER:
  //   kTouchpad -- a continuous surface, already smooth; a second easing on top is wrong;
  //   kUnknown  -- too little evidence. This is the important one: it is the first messages of
  //                EVERY gesture, and animating them leaked a touchpad into the model on every
  //                single gesture. Waiting a couple of messages costs a free-spinner its first
  //                fraction of a turn and costs a notched mouse NOTHING (a whole notch is known
  //                immediately), which is the right way round.
  return kind == Device::kNotched || kind == Device::kFreeSpin;
}

// ---------------------------------------------------------------------------
// DEV WHEEL LOG -- compile-time only (SWS_WHEEL_LOG, i.e. ./build.sh --wheel-log)
//
// A research build. The device classifier's thresholds in device.h are a first cut that has never
// been checked against real hardware (there is no free-spinning wheel here to check them with), so
// this build records what the wheel actually sends and lets a tester send the file back.
//
// The ring, the per-verdict tally and the file's text live in src/wheel_log.h, which has no REAPER
// in it and is exercised by _diag/wheel_log_probe.cpp -- the ring's ORDER is the whole point of the
// file, so it is the part worth testing outside the host (see that header). What is left HERE is
// the machine-specific part: where the file goes, what the clock says, and the header block.
//
// The file lands next to the DLL:
//
//     <the plugin's OWN FOLDER>\SmoothWheelScroll_wheel_log.txt
//
// It is written when a gesture ends (and at most once a second), and again when the plugin unloads,
// so it always holds the last few messages and can never grow.
//
// WHAT IT DELIBERATELY DOES NOT CONTAIN: no project paths, track names, media or REAPER
// preferences -- only wheel values, key state, window class names and the settings. That is stated
// inside the file, because the file is meant to be handed to someone else.
//
// THE RELEASE BUILD HAS NONE OF THIS: the whole block compiles out, so a shipped DLL cannot write
// a file. (Writing into the REAPER install is otherwise confined to the plugin DLL itself, see
// AGENTS.md 5 -- this is a deliberate, user-requested exception for the DEV build only.)
// ---------------------------------------------------------------------------
#ifdef SWS_WHEEL_LOG
#include "wheel_log.h"

static WheelLog g_wheelLog;
static double g_wheelLogStart;
static DWORD g_wheelLogLastFlushMs;
static bool g_wheelLogReady;

// The plugin's own folder. GetModuleFileName on OUR instance gives the DLL's path, which is the
// only reliable way to find "the folder this plugin lives in" -- the current directory is REAPER's,
// which is not the same thing.
static void WheelLogInit()
{
  if (g_wheelLogReady)
    return;
  // THE DLL'S OWN FOLDER, and nothing else. Two notes on why this is written the way it is:
  //
  //   * `g_hInst` is OUR module handle (handed to the entry point), so the directory that comes out
  //     is the one the plugin actually sits in -- identical for a portable install and an installed
  //     one, because both are just "the folder this DLL is in".
  //   * There is deliberately NO fallback when it is null. `GetModuleHandleA(nullptr)` would return
  //     REAPER'S OWN module, i.e. the log would land in the REAPER install directory rather than in
  //     UserPlugins -- exactly the write this feature is not allowed to make. If the handle is not
  //     known, the log is simply not written.
  if (!g_hInst)
    return;
  char mod[MAX_PATH] = {0};
  const DWORD n = GetModuleFileNameA(g_hInst, mod, MAX_PATH);
  if (n == 0 || n >= MAX_PATH)
    return;
  char *slash = strrchr(mod, '\\');
  if (!slash)
    return;
  *slash = 0; // now `mod` is the folder
  char path[MAX_PATH], tmp[MAX_PATH];
  _snprintf(path, sizeof(path), "%s\\SmoothWheelScroll_wheel_log.txt", mod);
  _snprintf(tmp, sizeof(tmp), "%s\\SmoothWheelScroll_wheel_log.tmp", mod);
  g_wheelLog.Init(path, tmp);
  g_wheelLogStart = Now();
  // `Ready()` is false when the folder refuses writes. That is worth SAYING rather than leaving as
  // "no file ever appears": the folder the user just dropped the DLL into is normally writable, but
  // a REAPER under Program Files may not be, and a silent nothing would read as a broken plugin.
  if (g_wheelLog.PathKnownButReadOnly())
    Log("wheel log: CANNOT WRITE %s (folder is read-only for this user)", path);
  else if (g_wheelLog.Ready())
    Log("wheel log -> %s", path);
  g_wheelLogReady = true;
}

// The file's comment block. Built at write time so the timestamp is current.
static void WheelLogHeader(char *out, int outSize)
{
  SYSTEMTIME st;
  GetLocalTime(&st);
  int off = 0;
  off += _snprintf(out + off, outSize - off,
                   "# SmoothWheelScroll wheel log (DEV build)\n"
                   "# THIS FILE IS MEANT TO BE SHARED. It records, for recent mouse-wheel messages:\n"
                   "# how long after the previous one it arrived, the raw delta the device reported,\n"
                   "# the message's key state and extra-info word, the window CLASS under the cursor,\n"
                   "# what the plugin decided the device was, and whether it animated the message.\n"
                   "# It contains NO project paths, track names, media or REAPER preferences.\n"
                   "#\n"
                   "# written  : %04d-%02d-%02d %02d:%02d:%02d\n",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  if (off < 0 || off >= outSize)
    return;
  {
    char rv[64] = {0};
    if (GetAppVersion)
    {
      const char *v = GetAppVersion();
      _snprintf(rv, sizeof(rv), "%s", v ? v : "?");
    }
    else
      _snprintf(rv, sizeof(rv), "?");
    HDC dc = GetDC(nullptr);
    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 0;
    if (dc)
      ReleaseDC(nullptr, dc);
    off += _snprintf(out + off, outSize - off, "# reaper   : %s\n# windows  : dpi=%d\n", rv, dpi);
  }
  if (off < 0 || off >= outSize)
    return;
  off += _snprintf(out + off, outSize - off,
                   "# settings : Glide length=%.0fms  Slow step=%.1fd  Ramp-up=%.0fd  Top speed=%.2fx"
                   "  smoothing=%s  touchpad-reverse=%s\n#\n",
                   g_windowMs, g_startDeltas, g_budgetDeltas, g_speedMul, g_glideOn ? "on" : "off",
                   g_touchpadReverse ? "on" : "off");
  (void)off;
}

static void WheelLogFlush()
{
  if (!g_wheelLogReady)
    return;
  char header[1024];
  WheelLogHeader(header, (int)sizeof(header));
  g_wheelLog.Flush(header);
}

static void WheelLogRecord(int delta, unsigned key, long extra, const char *wincls, Device dev,
                          bool anim)
{
  if (!g_wheelLogReady)
    return;
  WheelLogRec r;
  ZeroMemory(&r, sizeof(r));
  r.delta = delta;
  r.key = key;
  r.extra = extra;
  _snprintf(r.wincls, sizeof(r.wincls), "%s", wincls ? wincls : "");
  r.dev = (int)dev;
  r.anim = anim ? 1 : 0;

  const bool gestureEnded = g_wheelLog.Record(r, Now() - g_wheelLogStart);

  // Write at the end of a gesture, and otherwise at most once a second, so the file is never more
  // than a second behind what has been scrolled -- without writing on every message.
  const DWORD tick = GetTickCount();
  if (gestureEnded || (tick - g_wheelLogLastFlushMs) > 1000)
  {
    g_wheelLogLastFlushMs = tick;
    WheelLogFlush();
  }
}
#endif // SWS_WHEEL_LOG

static LRESULT CALLBACK GetMsgProc(int code, WPARAM wParam, LPARAM lParam)
{
  // Our own mixer wheel is sent with SendMessage, which goes straight to the window procedure
  // and is therefore never seen by this hook (WH_GETMESSAGE only sees queued messages). This
  // flag is belt-and-braces for the same reason the replay path has one: if a synthetic wheel
  // ever did arrive here, treating it as a fresh user gesture would animate our own output.
  if (g_sendingSynthWheel)
    return CallNextHookEx(g_msgHook, code, wParam, lParam);

  if (code == HC_ACTION && wParam == PM_REMOVE)
  {
    MSG *m = (MSG *)lParam;
    if (m && m->message == WM_MOUSEWHEEL)
    {
      HWND under = WindowFromPoint(m->pt); // window the wheel was over
      g_wheelHwnd = under;
      g_wheelPt = m->pt;
      const int delta = (int)(short)HIWORD(m->wParam);

      // What REAPER says is under the cursor. This is the official hit-test and the
      // value control, the window class plus this hit-test decide (see above).

      // WHICH DEVICE: the two wheels go through the model (3.0), a touchpad is left to REAPER.
      // The latch is cleared for a pass-through so the action path stays out of it too.
      const bool animatable = IsAnimatableWheel(delta);

#ifdef SWS_WHEEL_LOG
      // THE DEV LOG. Recorded here, BEFORE the pass-through return below, because a wheel that is
      // left to REAPER is exactly as interesting as one that is animated -- a touchpad's values are
      // what tells the two apart, and the verdict just computed is part of the record. Every wheel
      // the hook sees lands in this call. Guarded at the CALL SITE as well as around the
      // definitions, so a release build contains none of it (not even a call to an empty stub).
      {
        char lcls[64] = {0};
        if (under)
          GetClassNameA(under, lcls, sizeof(lcls));
        WheelLogRecord(delta, (unsigned)(m->wParam & 0xFFFF), (long)GetMessageExtraInfo(), lcls,
                       LastWheelDevice(), animatable);
      }
#endif

      if (!animatable)
      {
        InterlockedExchange(&g_wheelTick, 0);
        return CallNextHookEx(g_msgHook, code, wParam, lParam);
      }

      const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
      const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
      const bool plain = !shift && !ctrl && !alt;

      // Probe: every wheel is numbered and its exact modifier combination is recorded, so
      // the log reads as a table. "mod=--A" is Alt alone, "mod=S--" is Shift alone, etc.
      // The view's state is logged alongside it (VIEW), so the analysis can diff
      // consecutive readings and see what REAPER itself did with the gesture.
      const long wheelSeq = InterlockedIncrement(&g_wheelSeq);
      if (kDebugLog)
      {
        g_wheelMods[0] = shift ? 'S' : '-';
        g_wheelMods[1] = ctrl ? 'C' : '-';
        g_wheelMods[2] = alt ? 'A' : '-';
        g_wheelMods[3] = 0;
        g_wheelSeqTick = GetTickCount();
        char bcls[64] = {0}, bthing[128] = {0};
        if (under) GetClassNameA(under, bcls, sizeof(bcls));
        if (GetThingFromPoint)
          GetThingFromPoint(m->pt.x, m->pt.y, bthing, (int)sizeof(bthing));
        RECT ar = {0};
        const bool haveAr = ArrangeScreenRect(&ar);
        const ArrangeBar barNow = ArrangeBarAt(m->pt);
        // One line: sequence, point, exact modifiers, the window class, REAPER's own
        // hit-test string, and this plugin's geometric scrollbar classification.
        Log("WHEEL #%ld pt=(%d,%d) mod=%s class=%s thing=\"%s\" bar=%s arrange=%d",
            wheelSeq, m->pt.x, m->pt.y, g_wheelMods, under ? bcls : "(none)", bthing,
            barNow == ArrangeBar::kVertical ? "V"
            : barNow == ArrangeBar::kHorizontal ? "H" : "none",
            haveAr ? 1 : 0);
      }

      // Consume only when the cursor is on one of the surfaces we drive AND the
      // wheel is unmodified. Everything else falls through, where the latch is
      // armed so the action path (arrange scroll/zoom) can recognise the wheel.
      // The arrange view is NOT taken here: it resolves to an action, so it falls
      // through with the latch armed and the action path (hookcommand2) smooths it.
      // Here we only take the surfaces that produce no action of their own.
      const double actSign = (delta > 0) ? 1.0 : -1.0; // wheel-up = view-up
      char thing[128] = {0};
      // The arrange view's scrollbars first: they sit ON the arrange window and are
      // recognised by geometry (there is no other way -- see ArrangeBarAt). Unlike a
      // panel, EVERY modifier combination lands here, because the bars are a fixed
      // two-way choice -- see the mapping below.
      const ArrangeBar bar = g_glideOn ? ArrangeBarAt(m->pt) : ArrangeBar::kNone;
      // The surface is looked up for EVERY wheel now, not just unmodified ones: on the
      // track panel the decision is made by what the user has that modifier combination
      // assigned to (see the isTcp branch), which may be any combination. Surfaces that
      // only act on a plain wheel guard themselves below.
      HWND surface = (bar == ArrangeBar::kNone && g_glideOn)
                         ? SurfaceWindow(under, m->pt, thing, sizeof(thing))
                         : nullptr;
      if (kDebugLog)
      {
        // Unconditional: an unrecognised surface (empty thing AND no surface) is
        // exactly the case that must be visible, so never gate this on a hit.
        char ucls[64] = {0}, pcls[64] = {0};
        if (under) GetClassNameA(under, ucls, sizeof(ucls));
        HWND upar = under ? GetParent(under) : nullptr;
        if (upar) GetClassNameA(upar, pcls, sizeof(pcls));
        // Log the full geometry so an unrecognised strip can be placed exactly:
        // the point, the window under it (with its rect), the TCP window rect, and
        // the arrange view rect.
        RECT ur = {0}, kr = {0}, ar = {0};
        if (under) GetWindowRect(under, &ur);
        HWND kw = g_main ? FindWindowExA(g_main, nullptr, "REAPERTCPDisplay", nullptr) : nullptr;
        if (kw) GetWindowRect(kw, &kr);
        HWND av = g_main ? GetDlgItem(g_main, 1000) : nullptr;
        if (av) GetWindowRect(av, &ar);
        Log("wheel pt=(%d,%d) under=%p[%s %d,%d,%d,%d] parent=[%s] thing=\"%s\" tcpw=%p[%d,%d,%d,%d] arrange=[%d,%d,%d,%d] surface=%p plain=%d",
            m->pt.x, m->pt.y, (void *)under, ucls, ur.left, ur.top, ur.right, ur.bottom,
            pcls, thing, (void *)kw, kr.left, kr.top, kr.right, kr.bottom,
            ar.left, ar.top, ar.right, ar.bottom, (void *)surface, plain ? 1 : 0);
      }

      // Arrange scrollbar. REAPER moves these itself and never lets the wheel reach the
      // action table, so the plugin has to drive them or they stay native (one step per
      // notch). The mapping and the reason it is safe are in ArrangeBarAt.
      //
      // The bars have a simple two-way split, reported from use:
      //
      //   Alt held  -> ZOOM      vertical bar: vertical zoom; horizontal bar: horizontal
      //   no Alt    -> SCROLL    vertical bar: vertical scroll
      //                          horizontal bar: NOT taken (see below)
      //
      // On the horizontal bar the plain/Ctrl/Shift/Ctrl+Shift gestures are a fast pan
      // that is not the main view's Shift+wheel ("it is faster, like paging"), so they
      // are left exactly as REAPER has them rather than replaced with a different
      // gesture. Only the Alt group is uniform enough to take over. All targets are
      // REAPER's own actions -- the plugin hands over a relative value and decides
      // nothing about the view.
      if (bar != ArrangeBar::kNone)
      {
        const double notches = fabs(delta) / 120.0;
        int cmd = 0;
        if (bar == ArrangeBar::kVertical)
          cmd = alt ? 1000 : 989; // View: Zoom vertically / Scroll vertically
        else if (alt)
          cmd = 990; // View: Zoom horizontally

        if (cmd != 0)
        {
          // The vertical bar's Alt gestures are the vertical ZOOM actions; the filter is resolved
          // from the route inside Kick, exactly as it is for the action path.
          Route route;
          route.spec.section = 0;
          route.spec.command = cmd;
          route.spec.axis = (bar == ArrangeBar::kVertical) ? Axis::kVertical : Axis::kHorizontal;
          route.spec.kind = alt ? Kind::kZoom : Kind::kScroll;
          route.spec.drive = DRIVE_REPLAY;
          route.hwnd = under;
          Kick(g_vert, route, actSign, notches);
          if (kDebugLog)
            Log("TAKE #%ld bar=%s mod=%s -> cmd=%d", wheelSeq,
                bar == ArrangeBar::kVertical ? "V" : "H", g_wheelMods, cmd);
          StartTimer();
          InterlockedExchange(&g_wheelTick, 0);
          m->message = WM_NULL; // swallowed; the bar must not move natively as well
          return CallNextHookEx(g_msgHook, code, wParam, lParam);
        }
        // No command chosen (horizontal bar without Alt): fall through untouched, so
        // REAPER's own fast pan keeps working exactly as it does today.
      }

      // Mixer panel, before the generic surface handling: it is a Mouse Modifier
      // behaviour too (MM_CTX_MCP_MOUSEWHEEL -> "Scroll MCP") with NO action behind it, so
      // it has to be driven by handing its window the wheel directly (see DRIVE_MCP_WHEEL).
      //
      // The mixer is a deeper window than the track panel (REAPERwnd -> #32770 -> #32770
      // -> REAPERMCPDisplay), so its class is looked for up the parent chain, not on the
      // window under the cursor.
      //
      // The wheel is taken UNCONDITIONALLY here. An earlier version first read
      // MM_CTX_MCP_MOUSEWHEEL and only took over while it was still the built-in "Scroll MCP",
      // on the theory that a user's own choice should be respected. That gate was added from a
      // probe reading of the mouse-modifier ids without ever testing what removing it does, so
      // it has been dropped at the user's request -- the assignment is still read for the log
      // (it is useful evidence), but it no longer decides whether the mixer is driven.
      if (g_glideOn && bar == ArrangeBar::kNone && !surface)
      {
        HWND mcp = nullptr;
        for (HWND w = under; w; w = GetParent(w))
        {
          char mc[64] = {0};
          if (!GetClassNameA(w, mc, sizeof(mc)))
            break;
          if (strcmp(mc, "REAPERMCPDisplay") == 0)
          {
            mcp = w;
            break;
          }
        }
        if (mcp)
        {
          const double notches = fabs(delta) / 120.0;
          g_mcpWheelPt = m->pt; // remembered while the gesture runs; see ApplyMcpWheel
          if (kDebugLog)
          {
            char asg[64] = {0};
            PanelWheelAssignment("MM_CTX_MCP_MOUSEWHEEL", shift, ctrl, alt, asg, sizeof(asg));
            Log("TAKE #%ld mcp notches=%.2f (assignment=\"%s\", no longer gated on it)",
                wheelSeq, notches, asg);
          }
          Route mcpRoute;
          mcpRoute.spec.section = 0;
          mcpRoute.spec.command = 0;
          mcpRoute.spec.axis = Axis::kHorizontal;
          mcpRoute.spec.kind = Kind::kScroll;
          mcpRoute.spec.drive = DRIVE_MCP_WHEEL;
          mcpRoute.hwnd = mcp;
          Kick(g_horz, mcpRoute, actSign, notches);
          StartTimer();
          InterlockedExchange(&g_wheelTick, 0);
          m->message = WM_NULL; // swallowed; REAPER's own Scroll MCP must not also run
          return CallNextHookEx(g_msgHook, code, wParam, lParam);
        }
      }

      if (surface)
      {
        char cls[64] = {0};
        GetClassNameA(surface, cls, sizeof(cls));
        const bool isTcp = (strcmp(cls, "REAPERTCPDisplay") == 0);
        const bool isPiano = (strcmp(cls, "MIDIPianoWindow") == 0);
        const double notches = fabs(delta) / 120.0;

        // Which operation does this wheel drive? Only the ones below are taken;
        // anything else falls through to REAPER.
        bool took = true;
        if (isTcp)
        {
          // The track panel's wheel is a Mouse Modifier behaviour, not an action: REAPER
          // keeps it in the "Track control panel / Mouse wheel" context (reaper-mouse.ini,
          // MM_CTX_TCP_MOUSEWHEEL). Its DEFAULT is the built-in "Scroll TCP", which is why
          // the wheel never reached the action table and had to be intercepted at all.
          //
          // Rather than blindly replaying a scroll action, ask what the user has that
          // combination set to and reproduce THAT -- so a user who changes the assignment
          // (to Passthrough, or to another behaviour) is respected instead of overridden.
          //
          // The API answers with "<id> m" (a mouse modifier id, as spelled in
          // reaper-mouse.ini). The built-in ids are not documented, so they were read
          // from a running REAPER; the mapping is recorded in AGENTS.md 19.19.
          char asg[64] = {0};
          PanelWheelAssignment("MM_CTX_TCP_MOUSEWHEEL", shift, ctrl, alt, asg, sizeof(asg));
          int cmd = 0;
          if (asg[0])
          {
            if (strcmp(asg, "1 m") == 0)
              cmd = 989; // built-in "Scroll TCP"            -> View: Scroll vertically
            else if (strcmp(asg, "3 m") == 0)
              cmd = 1000; // built-in "Adjust vertical zoom"  -> View: Zoom vertically
            // Anything else (Passthrough "4 m", unassigned "0", a custom action, or a
            // future id) is left to REAPER: the plugin must not replace a behaviour it
            // cannot reproduce.
          }
          else
          {
            // No answer at all -- GetMouseModifier unavailable, or a context this REAPER
            // does not know. Keep the previous behaviour rather than silently dropping
            // panel scrolling: plain wheel scrolls, Ctrl zooms.
            if (plain)
              cmd = 989;
            else if (ctrl && !shift && !alt)
              cmd = 1000;
          }
          if (kDebugLog)
            Log("tcp wheel mod=%s assignment=\"%s\" -> cmd=%d", g_wheelMods, asg, cmd);
          if (cmd != 0)
          {
            // "Scroll TCP" scrolls; "Adjust vertical zoom" is the zoom action. The filter is
            // resolved from the route inside Kick.
            Route tcpRoute;
            tcpRoute.spec.section = 0;
            tcpRoute.spec.command = cmd;
            tcpRoute.spec.axis = Axis::kVertical;
            tcpRoute.spec.kind = (cmd == 1000) ? Kind::kZoom : Kind::kScroll;
            tcpRoute.spec.drive = DRIVE_REPLAY;
            tcpRoute.hwnd = surface;
            Kick(g_vert, tcpRoute, actSign, notches);
          }
          else
          {
            took = false;
          }
        }
        else if (isPiano && plain)
        {
          // MIDI editor piano keys: vertical scroll via the editor's own action.
          // This is the SAME vertical scroll the note area drives, so it needs the
          // same whole-unit delivery. Plain wheel only: with a modifier the editor has
          // its own meanings and the plugin stays out of the way.
          HWND editor = MIDIEditor_GetActive ? MIDIEditor_GetActive() : nullptr;
          Route pianoRoute;
          pianoRoute.spec.section = 32060;
          pianoRoute.spec.command = 40432;
          pianoRoute.spec.axis = Axis::kVertical;
          pianoRoute.spec.kind = Kind::kScroll;
          pianoRoute.spec.drive = DRIVE_REPLAY;
          pianoRoute.hwnd = editor;
          Kick(g_vert, pianoRoute, actSign, notches);
          if (kDebugLog)
            Log("wheel -> piano keys notches=%.2f", notches);
        }
        else
        {
          took = false;
        }

        if (took)
        {
          StartTimer();
          InterlockedExchange(&g_wheelTick, 0);
          m->message = WM_NULL; // swallowed; the surface must not also jump
          return CallNextHookEx(g_msgHook, code, wParam, lParam);
        }
      }

      // Not consumed here: arm the latch for the action path (arrange view).
      InterlockedExchange(&g_wheelTick, (LONG)GetTickCount());
    }
  }
  return CallNextHookEx(g_msgHook, code, wParam, lParam);
}

static void InstallHook()
{
  // The hook is installed even while the glide is off: toggling the switch at runtime
  // has to take effect without a restart, and when off the hook forwards immediately
  // without touching the message.
  if (!g_main)
    g_main = GetMainHwnd();
  if (!g_main)
    return;
  if (!g_uiThreadId)
    g_uiThreadId = GetWindowThreadProcessId(g_main, nullptr);
  if (!g_msgHook)
  {
    g_msgHook = SetWindowsHookEx(WH_GETMESSAGE, GetMsgProc, g_hInst, g_uiThreadId);
    Log("msg hook -> %p (thread %lu)", (void *)g_msgHook, g_uiThreadId);
  }
}

// ---------------------------------------------------------------------------
// TUNING UI -- the author's own debug tool, NOT shipped.
//
// The three sliders are how the model's feel was tuned, so the code is kept; but
// they are compiled OUT of the released DLL, so a user has no settings entry at all
// (in REAPER's action list or its Extensions menu). Nothing is exposed and nothing
// is persisted: the released plugin always runs the compiled-in defaults.
//
// Build with --tuning-ui (see build.sh) to get the window and its Extensions-menu
// entry back for a tuning session.
// ---------------------------------------------------------------------------
#ifndef SWS_NO_SETTINGS_UI
// --- Settings window -------------------------------------------------------
//
// Five sliders, one per feel group, plus the on/off switch. The chrome uses the STANDARD WINDOWS
// COLOURS (see ResolveTheme): the dialog face, the 3D shadow and the button text, taken from
// GetSysColor. Those are the same colours the standard controls and the window frame already draw
// from, so the panel agrees with them by construction, and whatever themes those -- Windows, or
// REAPER's own dark mode -- themes this with them. The faders, knobs and the response curve keep
// their own drawing and their per-setting hues.

// The master switch's caption. It is also the widest single line the panel has to keep on one
// line, so the minimum width is derived from it (see MinPanelWidth). Defined here, before the
// painting code, because the panel draws the switch itself (see DrawSwitch).
static const char *kEnableText = "Enable smooth scrolling";

// The SECOND switch. A touchpad is left to REAPER (see IsAnimatableWheel), but REAPER reads the
// horizontal ZOOM the opposite way round from what a sideways swipe on a touchpad feels like, so
// the same value is handed to the action with the opposite sign (see OnAction). On by default.
static const char *kTouchpadRevText = "Reverse touchpad horizontal zoom";

enum
{
  // Control ids are COMPUTED from the row index (SliderIdOf / LabelIdOf / KnobIdOf), never written
  // out one at a time. A hand-kept list of ids is another thing to forget when a control is added,
  // and it silently has to stay in the same order as the table below. The three bases are kept
  // apart so a slider, its label and its knob can never collide.
  IDC_S_BASE = 1010,
  IDC_L_BASE = 1110,
  IDC_K_BASE = 1210,
};

// How many controls the panel has. BuildSliderSpecs is the ONE place a control is described; adding
// one means raising this number and adding ONE row there (plus its TUNING constants). Ids, hues,
// defaults and the knob are all part of that row.
static const int kNumSliders = 4;

static int SliderIdOf(int i) { return IDC_S_BASE + i; }
static int LabelIdOf(int i) { return IDC_L_BASE + i; }
static int KnobIdOf(int i) { return IDC_K_BASE + i; }

// ONE ROW DESCRIBES ONE CONTROL, completely: the slider, its label, its knob, its colour and its
// default. BuildSliderSpecs is the only place any of that is written down, and the ids are computed
// from the row's index, so adding a control cannot half-miss a parallel list.
struct SliderSpec
{
  // Everything the table supplies, in this order (the initializers below must match it).
  const char *name;
  double *value;
  double min, max;      // slider positions 0..1000 map linearly to [min,max]
  const char *unit;
  const char *lomark;   // shown at the left end
  const char *himark;   // shown at the right end
  double defValue;      // double-click restores THIS (the middle of the range by convention)
  COLORREF hue;         // this row's segment colour on the curve and its own controls

  // The knob that goes with this row. A knob's value is NOT the slider's: the Start knob is the
  // rise's duration (ms) and the other four are their curve segment's bend. A row may have NO knob
  // (MODEL 3.0's single precision control has none): then hasKnob is false, the pointers below are
  // null, and no knob is ever created, laid out or handled for that row.
  bool hasKnob;
  double *knobValue;
  double knobMin, knobMax, knobDef;
  const char *knobName; // for the log only

  // Filled in from the row's index after the table is copied, so they are LAST (an aggregate
  // initializer covers the fields above and leaves these to the loop).
  int sliderId, labelId;
};

static SliderSpec g_sliders[kNumSliders];
static int g_faderPos[kNumSliders]; // 0..1000, one per fader (parallel to g_sliders)

// The knob controls, DERIVED from the rows above (never filled in separately): the panel creates
// and drives them by index, and every knob's data already lives in its row.
struct KnobSpec
{
  int id;
  double *value;
  double min, max;
  const char *name;
};
static KnobSpec g_knobs[kNumSliders];
static int g_knobPos[kNumSliders]; // 0..1000 per knob, parallel to g_knobs

// Blend between the panel background and the hue by `amount` (0..1). Amount 0 gives a colour
// barely off the background (the faint end) rather than the background itself, so the hue is
// always traceable. Defined with the theme (it needs g_theme.card).
static COLORREF SliderColor(int idx, double amount);
static double SliderAmount(int idx);

static HWND g_cfgWnd = nullptr;
#ifndef SWS_NO_SETTINGS_UI
// The monitor's rings and block live with the panel code that draws them; declared early so the
// send/receive taps can fill them (see PanelMonitor).
#endif
static bool g_cfgUpdating = false;
// The controls that need repositioning on resize. Held by handle rather than looked up
// by id, because the end-cap labels have no id of their own.
//
// THE MASTER SWITCH IS NOT A CONTROL. It is DRAWN BY THE PANEL (see DrawSwitch) and this is where it
// sits. Two reasons, both learned the hard way:
//   * it is the system "Button" class otherwise, which puts it in reach of anything that subclasses
//     windows by class name. A third-party REAPER dark-mode plugin reads BS_TYPEMASK on every
//     Button and treats any style it does not recognise -- BS_OWNERDRAW was ours -- as REAPER's
//     fake SysLink, takes over WM_PAINT, and the control's own WM_DRAWITEM never runs, so its tick
//     silently stopped being drawn;
//   * a check box is opaque, so its whole rectangle was painted in the control's own colour --
//     the band that did not match the panel.
// Drawing it on the panel removes both: the panel's window class is ours, so no class-name-based
// tool recognises it, and there is no second window to disagree about the background.
// TWO switches are drawn this way; the row count keeps the layout, the content height and the
// separator rule in step without a second constant to forget.
static const int kSwitchLines = 2;
static RECT g_switchRect = {0, 0, 0, 0};  // master switch, client coords, already scrolled
static RECT g_switch2Rect = {0, 0, 0, 0}; // touchpad direction reverse
static bool g_hasSwitch = false;
static HWND g_capLo[kNumSliders], g_capHi[kNumSliders];
// (No footer label handles: the two footer texts were removed -- they occupied space
//  without telling the user anything they could act on.)
// Scrolling state. The panel keeps its compact spacing always; when the window is too
// short for the content it scrolls instead of squeezing the rows. That is what makes
// adding more parameters later safe -- they just extend the scrollable height.
static int g_contentH = 0;   // full height the content needs
static int g_scrollY = 0;    // current scroll offset, >= 0
static int g_scrollStep = 40;
// Arms the geometry tracking in CfgProc. It stays off while the window is being created
// and placed, so the transient sizes and positions that happen during setup are not
// mistaken for where the user wants the panel.
static bool g_rectTracking = false;

static double SliderToValue(const SliderSpec &s, int pos)
{
  return s.min + (s.max - s.min) * (pos / 1000.0);
}
static int ValueToSlider(const SliderSpec &s, double v)
{
  if (s.max <= s.min)
    return 0;
  // Round first, then clamp, so the ends land exactly on min/max and nothing can step
  // outside the range even if "value" was left slightly off by an earlier edit.
  int p = (int)((v - s.min) / (s.max - s.min) * 1000.0 + 0.5);
  if (p < 0) p = 0;
  if (p > 1000) p = 1000;
  return p;
}

// The knob's 0..1000 position for the value it currently holds. The knob's value can be in
// any unit (ms for Start, a bend for the others), so it works from the value, not the slider.
static int ValueToKnob(int i)
{
  const KnobSpec &k = g_knobs[i];
  if (k.max <= k.min)
    return 0;
  int p = (int)((*k.value - k.min) / (k.max - k.min) * 1000.0 + 0.5);
  if (p < 0) p = 0;
  if (p > 1000) p = 1000;
  return p;
}

struct Theme
{
  COLORREF bg, text, sub, edit, track;
  COLORREF line;        // separator and group-outline colour, derived from bg
  COLORREF card;        // group surface fill (a touch off the background)
  COLORREF faderBg;     // fader groove fill
  COLORREF faderThumb;  // fader handle
  COLORREF grid;        // faint gridline inside the response curve (a whisper of `line`)
  COLORREF axisText;    // the curve's tick numbers: fainter than `sub`, they only need to read
  HBRUSH bgBrush;
  bool dark;
};

static Theme g_theme = {0};

// Blend between the panel background and a setting's hue by `amount` (0..1): 0 gives a colour
// only slightly off the card (the faint end), 1 the full hue. Defined here because it needs
// g_theme.card -- the curve and the faders both sit on the card, so that is what to blend with.
static COLORREF SliderColor(int idx, double amount)
{
  if (idx < 0 || idx >= kNumSliders)
    return g_theme.text;
  const double lo = 0.35, hi = 1.0; // never fully washed out, so the hue stays traceable
  if (amount < 0.0) amount = 0.0;
  if (amount > 1.0) amount = 1.0;
  const double a = lo + (hi - lo) * amount;
  const COLORREF bg = g_theme.card;
  const COLORREF c = g_sliders[idx].hue; // this row's colour, from the one table
  return RGB((int)(GetRValue(bg) + (GetRValue(c) - GetRValue(bg)) * a + 0.5),
             (int)(GetGValue(bg) + (GetGValue(c) - GetGValue(bg)) * a + 0.5),
             (int)(GetBValue(bg) + (GetBValue(c) - GetBValue(bg)) * a + 0.5));
}

// The value of slider `idx` as 0..1 across its own range.
static double SliderAmount(int idx)
{
  const SliderSpec &s = g_sliders[idx];
  if (s.max <= s.min)
    return 0.0;
  double a = (*s.value - s.min) / (s.max - s.min);
  if (a < 0.0) a = 0.0;
  if (a > 1.0) a = 1.0;
  return a;
}

// The dialog font. Without this Win32 falls back to the old bitmap "System" font, which
// is what made the panel look unlike the rest of the application: REAPER's own dialogs
// use the shell's message font. Also returns the row height implied by that font, so
// the layout is sized by text rather than by guessed pixels.
static HFONT g_uiFont = nullptr;

static HFONT UiFont(int *fontPx)
{
  if (!g_uiFont)
  {
    NONCLIENTMETRICSA ncm = {0};
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
      g_uiFont = CreateFontIndirectA(&ncm.lfMessageFont);
    if (!g_uiFont)
      g_uiFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
  }
  if (fontPx)
  {
    HDC dc = GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, g_uiFont);
    TEXTMETRICA tm = {0};
    GetTextMetricsA(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    *fontPx = tm.tmHeight;
  }
  return g_uiFont;
}

// A smaller version of the dialog font, for the chart's tick numbers. Built from the same
// NONCLIENTMETRICS face with a scaled height, so it stays in the system's typeface and scales with
// DPI. If it cannot be created the caller falls back to UiFont -- see ChartFont.
static HFONT g_chartFont = nullptr;

static HFONT ChartFont()
{
  if (!g_chartFont)
  {
    NONCLIENTMETRICSA ncm = {0};
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
    {
      const int h = (int)(ncm.lfMessageFont.lfHeight * kChartTickFont);
      ncm.lfMessageFont.lfHeight = (h != 0) ? h : ncm.lfMessageFont.lfHeight;
      g_chartFont = CreateFontIndirectA(&ncm.lfMessageFont);
    }
  }
  return g_chartFont ? g_chartFont : UiFont(nullptr);
}

static void ApplyFontToChildren(HWND parent){
  const HFONT f = UiFont(nullptr);
  HWND c = nullptr;
  while ((c = FindWindowExA(parent, c, nullptr, nullptr)) != nullptr)
    SendMessageA(c, WM_SETFONT, (WPARAM)f, TRUE);
}

// ---------------------------------------------------------------------------
// The panel's colours COME FROM THE THEME; only light-vs-dark is decided by REAPER.
//
// Two separate questions, each answered by the thing that actually knows:
//
//   WHAT the colours are -> the theme. GetThemeColor supplies the background, card, frame line,
//     fader groove and handle, so the panel matches the theme the user is really running rather
//     than a palette hard-coded here. Anything the theme does not supply is derived from the
//     background by a fixed step in the right direction.
//
//   WHICH WAY it leans (light text on dark, or the reverse) -> REAPER's own `win32_darkmode`
//     switch, via ReadAppDarkFlag. This is the one yes/no that a colour cannot answer.
//
// The split is deliberate. Deciding the DIRECTION from the background's brightness was the earlier
// approach and is wrong whenever the theme's arrange colour does not track REAPER's mode; asking
// the flag fixes that. But hard-coding the COLOURS (tried in between) was a regression in the other
// direction -- the dark panel came out as a flat self-made grey instead of the theme's own dark
// colours. Colours from the theme, direction from the flag.
//
// WHY TWO ROUTES TO THE FLAG: get_config_var was measured not to answer for this key on this
// machine (it returns a null pointer and leaves the size untouched), while reaper.ini does. Both
// are reads; the plugin writes nothing of REAPER's.
//
// READ LIVE: WM_THEMECHANGED and WM_SYSCOLORCHANGE both re-run this, and so does every open of the
// panel, so a switch is picked up rather than only at startup.
// ---------------------------------------------------------------------------

// REAPER's light/dark answer. Returns false when neither route yields it.
static bool ReadAppDarkFlag(bool *outDark)
{
  if (get_config_var)
  {
    int sz = 0;
    const void *p = get_config_var("win32_darkmode", &sz);
    if (p && sz > 0)
    {
      const unsigned char *b = (const unsigned char *)p;
      int v = 0;
      if (sz >= (int)sizeof(int)) v = *(const int *)p;
      else if (sz >= (int)sizeof(short)) v = (int)*(const short *)p;
      else v = (int)b[0];
      *outDark = (v != 0);
      return true;
    }
  }
  if (get_ini_file)
  {
    const char *ini = get_ini_file();
    if (ini && *ini)
    {
      const int v = (int)GetPrivateProfileIntA("reaper", "win32_darkmode", -1, ini);
      if (v >= 0) { *outDark = (v != 0); return true; }
    }
  }
  return false;
}

// One theme colour for `key`, decoded through ColorFromNative (GetThemeColor returns REAPER's own
// packing, not Win32's, so masking it and calling GetRValue can read the channels swapped).
static bool ThemeColorRgb(const char *key, COLORREF *out)
{
  if (!GetThemeColor)
    return false;
  const int c = GetThemeColor(key, 0);
  if (c < 0)
    return false;
  int r = -1, g = -1, b = -1;
  if (ColorFromNative)
    ColorFromNative(c, &r, &g, &b);
  else
  {
    const COLORREF k = (COLORREF)(c & 0xFFFFFF);
    r = (int)GetRValue(k); g = (int)GetGValue(k); b = (int)GetBValue(k);
  }
  *out = RGB(r, g, b);
  return true;
}

// First key in the list that the theme answers.
static bool ThemeColorAny(const char *const *keys, int n, COLORREF *out)
{
  for (int i = 0; i < n; ++i)
    if (ThemeColorRgb(keys[i], out))
      return true;
  return false;
}

static void ResolveTheme()
{
  // --- direction first: REAPER's own answer decides WHICH palette -------------------------
  bool dark = false;
  if (!ReadAppDarkFlag(&dark))
    dark = g_theme.dark; // keep the last known answer rather than inventing one
  g_theme.dark = dark;

  if (dark)
  {
    // --- DARK: the THEME's own dark colours -------------------------------------------------
    // The materials come from the theme, because that is what matches the theme the user runs.
    // Measured on this machine the theme supplies background 303030, frame 202020, groove 202020.
    //
    // THE CARD IS THE PANEL'S OWN COLOUR. Taking it from the theme (col_buttonbg) put the parameter
    // groups and the response curve on pure black, far below the 303030 panel -- a heavy block
    // rather than a raised surface. The LIGHT palette already works this way (card == background,
    // the frame doing the separating), and matching that reads better in dark too: the groups get
    // their layer from the thin frame, not from a colour step.
    static const char *kBgKeys[] = {"col_main_bg", "col_arrangebg", "col_tl_bg", "col_mixerbg"};
    static const char *kGroove[] = {"gen_volbg_horz", "col_main_bg2", "col_main_editbk"};
    static const char *kThumb[]  = {"gen_volthumb_horz", "col_buttonbg", "col_main_3dhl"};
    static const char *kLine[]   = {"col_main_3dsh", "col_tl_bg"};

    COLORREF bg = RGB(48, 48, 48);
    ThemeColorAny(kBgKeys, 4, &bg);
    g_theme.bg = bg;
    g_theme.card = bg; // same as the panel; the frame separates the groups
    if (!ThemeColorAny(kLine, 2, &g_theme.line))
      g_theme.line = RGB(32, 32, 32);
    if (!ThemeColorAny(kGroove, 3, &g_theme.faderBg))
      g_theme.faderBg = RGB(78, 78, 78);
    if (!ThemeColorAny(kThumb, 3, &g_theme.faderThumb))
      g_theme.faderThumb = RGB(150, 150, 150);

    g_theme.text = RGB(235, 235, 235);
    g_theme.sub = RGB(170, 170, 170);
    g_theme.axisText = RGB(130, 130, 130); // tick numbers: there, but not shouty
    g_theme.edit = RGB(48, 48, 48);
    g_theme.track = RGB(70, 70, 70);
    g_theme.grid = RGB(ClampInt(GetRValue(bg) + 8, 0, 255), ClampInt(GetGValue(bg) + 8, 0, 255),
                       ClampInt(GetBValue(bg) + 8, 0, 255));

    // A groove that matches the card is invisible and a handle that matches its groove vanishes.
    const int d1 = abs((int)GetRValue(g_theme.faderThumb) - (int)GetRValue(g_theme.faderBg)) +
                   abs((int)GetGValue(g_theme.faderThumb) - (int)GetGValue(g_theme.faderBg)) +
                   abs((int)GetBValue(g_theme.faderThumb) - (int)GetBValue(g_theme.faderBg));
    if (d1 < 60)
      g_theme.faderThumb = RGB(150, 150, 150);
  }
  else
  {
    // --- LIGHT: the standard Windows dialog colours -----------------------------------------
    // The standard dialog colours, deliberately NOT the theme's. The theme's arrange colour is
    // not tied to the UI's light/dark state (that is why the direction above is asked of REAPER
    // rather than derived from a colour), so painting from it is not dependable for this; the
    // standard dialog colours are what the surrounding chrome already draws in, and this is the
    // light look that was approved. Structure is a fixed step off the face so it stays visible.
    const COLORREF face = GetSysColor(COLOR_3DFACE);
    g_theme.bg = face;
    g_theme.card = face; // a group sits ON the dialog face, exactly as a group box does
    g_theme.line = GetSysColor(COLOR_3DSHADOW);
    g_theme.text = GetSysColor(COLOR_BTNTEXT);
    g_theme.sub = GetSysColor(COLOR_GRAYTEXT);
    g_theme.axisText = GetSysColor(COLOR_GRAYTEXT);
    g_theme.edit = GetSysColor(COLOR_WINDOW);
    g_theme.track = GetSysColor(COLOR_3DSHADOW);
    // The curve's gridlines: a much fainter step off the face than the frame uses. At the frame's
    // value (COLOR_3DSHADOW, 0xA0 here) the grid read as strongly as the frame itself and competed
    // with the line drawn over it; the grid is only a scale, so it is set well back from the face
    // instead. The DARK palette keeps its own value (see above) -- only light was asked to change.
    g_theme.grid = RGB(ClampInt(GetRValue(face) - 20, 0, 255),
                       ClampInt(GetGValue(face) - 20, 0, 255),
                       ClampInt(GetBValue(face) - 20, 0, 255));
    g_theme.faderBg = RGB(ClampInt(GetRValue(face) - 30, 0, 255),
                          ClampInt(GetGValue(face) - 30, 0, 255),
                          ClampInt(GetBValue(face) - 30, 0, 255));
    g_theme.faderThumb = RGB(ClampInt(GetRValue(face) - 90, 0, 255),
                             ClampInt(GetGValue(face) - 90, 0, 255),
                             ClampInt(GetBValue(face) - 90, 0, 255));
  }

  Log("theme: %s bg=%02x%02x%02x text=%02x%02x%02x card=%02x%02x%02x line=%02x%02x%02x "
      "groove=%02x%02x%02x thumb=%02x%02x%02x",
      g_theme.dark ? "DARK" : "LIGHT",
      GetRValue(g_theme.bg), GetGValue(g_theme.bg), GetBValue(g_theme.bg),
      GetRValue(g_theme.text), GetGValue(g_theme.text), GetBValue(g_theme.text),
      GetRValue(g_theme.card), GetGValue(g_theme.card), GetBValue(g_theme.card),
      GetRValue(g_theme.line), GetGValue(g_theme.line), GetBValue(g_theme.line),
      GetRValue(g_theme.faderBg), GetGValue(g_theme.faderBg), GetBValue(g_theme.faderBg),
      GetRValue(g_theme.faderThumb), GetGValue(g_theme.faderThumb), GetBValue(g_theme.faderThumb));
}

static void UpdateLabels()
{
  char buf[160];
  for (int i = 0; i < kNumSliders; ++i)
  {
    const SliderSpec &s = g_sliders[i];
    // Decimals follow the ROW'S RANGE, so a narrow row still reads to a useful resolution. A wide
    // row (Precision, 100..400 ms) needs none; a narrow one (Accel, 1.0..2.0) needs one.
    const double range = s.max - s.min;
    const int dec = (range >= 10.0) ? 0 : (range >= 1.0) ? 1 : (range > 0.1) ? 2 : 3;
    _snprintf(buf, sizeof(buf), "%s: %.*f %s", s.name, dec, *s.value, s.unit);
    SetWindowTextA(GetDlgItem(g_cfgWnd, s.labelId), buf);
  }
}

static void PushValuesToSliders()
{
  // Save/restore the guard rather than clearing it, so this can be called both while
  // the window is being built (where the outer guard must stay on) and from "Reset".
  const bool was = g_cfgUpdating;
  g_cfgUpdating = true;
  for (int i = 0; i < kNumSliders; ++i)
  {
    const SliderSpec &s = g_sliders[i];
    g_faderPos[i] = ValueToSlider(s, *s.value);
    HWND f = g_cfgWnd ? GetDlgItem(g_cfgWnd, s.sliderId) : nullptr;
    if (f)
      InvalidateRect(f, nullptr, FALSE);
    // The knobs carry their own values, so they are pushed from those.
    if (!s.hasKnob)
      continue;
    g_knobPos[i] = ValueToKnob(i);
    HWND k = g_cfgWnd ? GetDlgItem(g_cfgWnd, g_knobs[i].id) : nullptr;
    if (k)
      InvalidateRect(k, nullptr, FALSE);
  }
  g_cfgUpdating = was;
  UpdateLabels();
}

// Repaint every control with the theme colours. Called on create and on WM_CTLCOLOR*.
// Panel chrome. The group outlines are drawn in the PARENT window, not by the
// controls, and the control areas are excluded so the painting never fights what the
// child controls draw. That is what gives each group a surface (a "card") without
// needing owner-drawn controls.
static RECT g_cardRect[kNumSliders];    // each group's fill area and hit-test rect
static RECT g_groupRect = {0, 0, 0, 0}; // the whole block, drawn as ONE outer frame
static HBRUSH g_cardBrush = nullptr;
static HBRUSH g_lineBrush = nullptr;
static HBRUSH g_gridBrush = nullptr; // faint ticks inside the curve
static RECT g_headRect = {0, 0, 0, 0};
static RECT g_sepRect = {0, 0, 0, 0}; // thin rule under the master switch
static bool g_hasCards = false;
// (Re)create the three solid brushes from the current theme colours. Kept in one place
// because the brushes are rebuilt both when the window is created and whenever the
// theme changes while it is open -- ResolveTheme only updates the colours, so a rebuild
// that forgot a brush would paint the old theme's colour after a switch.
static void RebuildThemeBrushes()
{
  if (g_theme.bgBrush) DeleteObject(g_theme.bgBrush);
  g_theme.bgBrush = CreateSolidBrush(g_theme.bg);
  if (g_cardBrush) DeleteObject(g_cardBrush);
  g_cardBrush = CreateSolidBrush(g_theme.card);
  if (g_lineBrush) DeleteObject(g_lineBrush);
  g_lineBrush = CreateSolidBrush(g_theme.line);
  if (g_gridBrush) DeleteObject(g_gridBrush);
  g_gridBrush = CreateSolidBrush(g_theme.grid);
}

static LRESULT OnCtlColor(UINT msg, HDC hdc, HWND child)
{
  const bool isEdit = (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX);
  SetBkMode(hdc, TRANSPARENT);
  if (isEdit)
  {
    SetTextColor(hdc, g_theme.text);
    SetBkColor(hdc, g_theme.edit);
    return (LRESULT)g_theme.bgBrush;
  }
  // Controls that sit inside a group card are given the card's fill, so the card reads
  // as a surface rather than an empty frame around the panel background. The child
  // handle comes from the message's lParam (WM_CTLCOLOR* passes it).
  if (g_cardBrush && child && g_cfgWnd)
  {
    RECT r = {0};
    if (GetWindowRect(child, &r))
    {
      POINT p = {r.left, r.top};
      ScreenToClient(g_cfgWnd, &p);
      for (int i = 0; i < kNumSliders; ++i)
      {
        const RECT &c = g_cardRect[i];
        if (p.x >= c.left && p.x < c.right && p.y >= c.top && p.y < c.bottom)
        {
          SetTextColor(hdc, g_theme.text);
          SetBkColor(hdc, g_theme.card);
          return (LRESULT)g_cardBrush;
        }
      }
    }
  }
  SetTextColor(hdc, g_theme.text);
  SetBkColor(hdc, g_theme.bg);
  return (LRESULT)g_theme.bgBrush;
}

// ONE self-drawn check box: the glyph, the tick when checked, and the caption. Shared by both
// switches so their look cannot drift apart. `checked` comes from the flag the rest of the plugin
// and the settings store use, so there is no second copy of any state.
static void DrawCheckbox(HDC dc, const RECT &rc, const char *text, bool checked, bool focus)
{
  FillRect(dc, &rc, g_theme.bgBrush); // same colour as the panel: no band

  HGDIOBJ oldFont = SelectObject(dc, UiFont(nullptr));
  const int oldBk = SetBkMode(dc, TRANSPARENT);

  // The box, the size Windows uses for a menu check, vertically centred on the caption's line.
  const int box = GetSystemMetrics(SM_CXMENUCHECK);
  RECT g = {rc.left, rc.top + (rc.bottom - rc.top - box) / 2, rc.left + box,
            rc.top + (rc.bottom - rc.top - box) / 2 + box};
  if (g.bottom > rc.bottom - 1) g.bottom = rc.bottom - 1;
  if (g.right > rc.right - 1) g.right = rc.right - 1;
  HBRUSH edge = CreateSolidBrush(g_theme.sub);
  FrameRect(dc, &g, edge);
  DeleteObject(edge);
  if (checked)
  {
    HPEN pen = CreatePen(PS_SOLID, 2, g_theme.text);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    const int x0 = g.left + 3, x1 = g.left + (g.right - g.left) / 2;
    const int x2 = g.right - 3;
    const int y0 = g.top + (g.bottom - g.top) / 2;
    MoveToEx(dc, x0, y0, nullptr);
    LineTo(dc, x1, g.bottom - 4);
    LineTo(dc, x2, g.top + 3);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
  }

  RECT tr = {g.right + 6, rc.top, rc.right, rc.bottom};
  SetTextColor(dc, g_theme.text);
  DrawTextA(dc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

  // Focus ring, drawn the way a dialog draws it, so the switches still show the keyboard.
  if (focus)
    DrawFocusRect(dc, &rc);

  SetBkMode(dc, oldBk);
  SelectObject(dc, oldFont);
}

// The two switches, drawn by the panel itself. See g_switchRect for why they are not controls.
static void DrawSwitch(HDC dc)
{
  if (!g_hasSwitch)
    return;
  const bool focus = (GetFocus() == g_cfgWnd);
  DrawCheckbox(dc, g_switchRect, kEnableText, g_glideOn, focus);
  DrawCheckbox(dc, g_switch2Rect, kTouchpadRevText, g_touchpadReverse, focus);
}

// The MOTION TRACK. Each slider owns one visual channel (width / knee height / slope / peak), so the
// picture IS the four parameters and no two sliders share a channel -- see the note by PanelAnim.
// The hatched curve is the shaped path, in the sliders' own colours, one colour per segment; the grey
// staircase is the native reference, which the shaped curve meets at Top speed = 1.0.
static float AnimSample(const float *v, double u)
{
  if (u <= 0.0)
    return v[0];
  if (u >= 1.0)
    return v[kAnimN - 1];
  const double fi = u * (double)(kAnimN - 1);
  const int i0 = (int)fi;
  const int i1 = (i0 + 1 < kAnimN) ? (i0 + 1) : i0;
  const double f = fi - (double)i0;
  return (float)(v[i0] + ((double)v[i1] - (double)v[i0]) * f);
}

static void DrawMonitorBlock(HDC dc)
{
#ifndef SWS_NO_SETTINGS_UI
  const RECT &r = g_monBlk;
  if (r.right - r.left < 40 || r.bottom - r.top < 20)
    return;
  if (g_cardBrush)
    FillRect(dc, &r, g_cardBrush);
  if (g_lineBrush)
    FrameRect(dc, &r, g_lineBrush);

  HGDIOBJ oldFont = SelectObject(dc, UiFont(nullptr));
  const int oldBk = SetBkMode(dc, TRANSPARENT);
  const int padX = 8;
  const int x0 = r.left + padX;

  // NO CAPTION LINE. There used to be one ("Motion (blue glide, amber knee, ...)"), but it only
  // spelled out what the picture shows by itself: each segment is drawn in its own slider's colour,
  // and the fader right above it is that same colour. A legend for a colour-coded picture that sits
  // under the colour-coded controls is noise (the user's call, 2026-09-16: "那行 Motion，也不用，
  // 看得懂，去掉"). The space it took goes to the chart.

  // The drawing box: with a left band for the Y tick numbers, and above the X tick numbers. Each
  // band is measured, not guessed, so a larger font or DPI cannot make the numbers overlap the plot.
  int tickH = 0, digitW = 0;
  {
    HDC mdc = GetDC(nullptr);
    HGDIOBJ mof = SelectObject(mdc, ChartFont());
    TEXTMETRICA tm = {0};
    GetTextMetricsA(mdc, &tm);
    SelectObject(mdc, mof);
    ReleaseDC(nullptr, mdc);
    tickH = tm.tmHeight + 2;
    digitW = tm.tmAveCharWidth * 6; // room for a four-digit number plus a little slack
  }
  // The X tick band, plus a small margin. It used to be much taller because the device readout sat
  // under it; with that line gone the chart takes the space instead. The TOP margin likewise shrank
  // once the caption line was dropped -- the chart now starts just below the block's frame.
  const int belowBox = tickH + 6;
  RECT box = {x0 + digitW, r.top + 6, r.right - padX, r.bottom - belowBox};
  if (box.bottom - box.top < 20 || box.right - box.left < 40)
  {
    SetBkMode(dc, oldBk);
    SelectObject(dc, oldFont);
    return;
  }
  // One inset all round so a ball at either end is not clipped by the frame.
  const int bx0 = box.left + kAnimBallPx + 1;
  const int bx1 = box.right - kAnimBallPx - 1;
  const int by0 = box.top + kAnimBallPx + 1;
  // The floor is the zero line, so a ball at rest sits ON it (centre one radius up) rather than
  // straddling it.
  const int by1 = box.bottom - kAnimBallPx;
  const int wIn = bx1 - bx0;
  const int hIn = by1 - by0;

  // Floor and left wall.
  {
    HPEN ax = CreatePen(PS_SOLID, 1, g_theme.grid);
    HGDIOBJ op = SelectObject(dc, ax);
    MoveToEx(dc, box.left, by1, nullptr);
    LineTo(dc, box.right, by1);
    MoveToEx(dc, box.left, box.top, nullptr);
    LineTo(dc, box.left, by1);
    SelectObject(dc, op);
    DeleteObject(ax);
  }

  if (!g_anim.have)
  {
    SetBkMode(dc, oldBk);
    SelectObject(dc, oldFont);
    return;
  }

  // MEASURED TICKS: the grid is taken from the ACTUAL VALUE RANGE on each axis -- the X axis is
  // g_anim.spanMs (Glide length) and the Y axis is g_anim.yTopDeltas (Top speed) -- so moving either
  // slider rescales the grid with the picture. Fixed fractional ticks could not do that: at 1/6 and
  // 1/4 of the box they sat in the same place whatever the sliders said, which made them decoration.
  //
  // The step is a "nice" one (1/2/5 x 10^n) snapping to the real numbers, so the grid reads as a
  // scale. The NUMBERS are drawn in a smaller, fainter face: they are there to be glanced at for
  // magnitude, not measured, so they must not compete with the curve.
  {
    const double spanMs = (double)g_anim.spanMs;
    const double yRange = g_anim.yTopDeltas;
    const double stepX = NiceStep(spanMs, kChartMaxTicksX);
    const double stepY = NiceStep(yRange, kChartMaxTicksY);

    HGDIOBJ chartFont = SelectObject(dc, ChartFont());
    SetTextColor(dc, g_theme.axisText);

    // X: vertical gridlines at multiples of stepX, labelled where a label fits.
    HPEN tk = CreatePen(PS_SOLID, 1, g_theme.grid);
    HGDIOBJ op = SelectObject(dc, tk);
    int lastLabelRight = -1000;
    if (stepX > 0.0)
    {
      for (double v = stepX; v < spanMs - 1e-9; v += stepX)
      {
        const int px = bx0 + (int)((double)wIn * (v / spanMs));
        MoveToEx(dc, px, box.top, nullptr);
        LineTo(dc, px, by1);

        char lab[24];
        _snprintf(lab, sizeof(lab), "%.0f", v);
        SIZE sz = {0};
        GetTextExtentPoint32A(dc, lab, (int)strlen(lab), &sz);
        const int lx = px - sz.cx / 2;
        if (lx > lastLabelRight + 4 && lx >= box.left && lx + sz.cx <= box.right)
        {
          TextOutA(dc, lx, by1 + 2, lab, (int)strlen(lab));
          lastLabelRight = lx + sz.cx;
        }
      }
    }
    // Y: horizontal gridlines at multiples of stepY, labelled down the left edge.
    int lastLabelTop = 100000;
    if (stepY > 0.0)
    {
      for (double v = stepY; v < yRange - 1e-9; v += stepY)
      {
        const int py = by1 - (int)((double)hIn * (v / yRange));
        MoveToEx(dc, box.left, py, nullptr);
        LineTo(dc, box.right, py);

        char lab[24];
        _snprintf(lab, sizeof(lab), "%.0f", v);
        SIZE sz = {0};
        GetTextExtentPoint32A(dc, lab, (int)strlen(lab), &sz);
        // Right-aligned in the left band, so the numbers never sit on the plot.
        const int tx = box.left - 3 - sz.cx;
        const int ty = py - sz.cy / 2;
        if (ty < lastLabelTop - 4 && ty >= box.top && ty + sz.cy <= by1 && tx >= r.left + 1)
        {
          TextOutA(dc, tx, ty, lab, (int)strlen(lab));
          lastLabelTop = ty;
        }
      }
    }
    SelectObject(dc, op);
    DeleteObject(tk);
    SelectObject(dc, chartFont);
  }

  // The drawing fills the box: both axes are dynamic (see the note by kAnimPace). The Glide colour
  // marks the floor, which is the horizontal extent of the whole drawing -- there is no separate end
  // marker any more, because nothing ends short of the right-hand edge.
  {
    HPEN gp = CreatePen(PS_SOLID, 2, SliderColor(kRowGlide, SliderAmount(kRowGlide)));
    HGDIOBJ op = SelectObject(dc, gp);
    MoveToEx(dc, bx0, by1, nullptr);
    LineTo(dc, bx1, by1);
    SelectObject(dc, op);
    DeleteObject(gp);
  }

  // The native reference: the wheel's own height, at 1/top of the box because the drawing fills its
  // box and Top speed is the vertical scale. At Top = 1.0 the flat top sits exactly ON this line; at
  // 2.0 the line is half-way down, which is what "twice as high" looks like.
  const double top = (g_speedMul < 1.0) ? 1.0 : g_speedMul;
  const int natY = by1 - (int)((double)hIn / top);
  {
    HPEN np = CreatePen(PS_SOLID, 1, g_theme.sub);
    HGDIOBJ op = SelectObject(dc, np);
    MoveToEx(dc, box.left, natY, nullptr);
    LineTo(dc, box.right, natY);
    SelectObject(dc, op);
    DeleteObject(np);
  }

  // The wheel's own path: the grey staircase the square ball hops up. Drawn first, so the shaped
  // curve and its balls sit on top. It spans the same width as the shaped path (both fill the box):
  // the two are two answers to the same input, so they must be read against the same time axis.
  {
    HPEN pen = CreatePen(PS_DOT, 1, g_theme.sub);
    HGDIOBJ op = SelectObject(dc, pen);
    for (int i = 0; i < kAnimN; ++i)
    {
      const float v = g_anim.off[i];
      const int px = bx0 + (int)((long)wIn * i / (kAnimN - 1));
      int py = by1 - (int)((double)hIn * v);
      if (py < by0) py = by0;
      if (py > by1) py = by1;
      if (i == 0)
        MoveToEx(dc, px, py, nullptr);
      else
        LineTo(dc, px, py);
    }
    SelectObject(dc, op);
    DeleteObject(pen);
  }

  // The shaped path, ONE COLOUR PER SEGMENT so each slider's contribution is identifiable: the rise
  // ends at Slow step's height, the climb's steepness is Ramp-up, and the FLAT TOP is Top speed.
  // Colours come from the rows themselves, at the row's current amount, so each segment and its fader
  // match.
  //
  // The stroke follows the SAMPLED path (g_anim.on), not straight lines between the joints, so the
  // rounded corners are drawn rounded. Where the ownership changes the polyline is broken and a new
  // one starts, which is why each column is classified by its own x rather than the segments being
  // drawn as three chords.
  {
    const int segRow[3] = {kRowSlow, kRowRamp, kRowTop};
    const COLORREF segCol[3] = {SliderColor(segRow[0], SliderAmount(segRow[0])),
                                SliderColor(segRow[1], SliderAmount(segRow[1])),
                                SliderColor(segRow[2], SliderAmount(segRow[2]))};
    int prevSeg = -1;
    HPEN pen = nullptr;
    HGDIOBJ op = nullptr;
    for (int i = 0; i < kAnimN; ++i)
    {
      const double x = (double)i / (double)(kAnimN - 1);
      const int seg = (x < g_anim.kneeX) ? 0 : ((x < g_anim.reachX) ? 1 : 2);
      const int px = bx0 + (int)((long)wIn * i / (kAnimN - 1));
      int py = by1 - (int)((double)hIn * g_anim.on[i]);
      if (py < by0) py = by0;
      if (py > by1) py = by1;
      if (seg != prevSeg)
      {
        if (pen)
        {
          SelectObject(dc, op);
          DeleteObject(pen);
        }
        pen = CreatePen(PS_SOLID, kChartCurvePx, segCol[seg]);
        op = SelectObject(dc, pen);
        MoveToEx(dc, px, py, nullptr); // a fresh run: no line drawn across the colour change
        prevSeg = seg;
      }
      else
      {
        LineTo(dc, px, py);
      }
    }
    if (pen)
    {
      SelectObject(dc, op);
      DeleteObject(pen);
    }
  }

  // The balls. Each has its own birth tick; the position is the path value at its own progress, so
  // several are visible at once during a roll -- which is the point of sending one per wheel.
  {
    const DWORD now = GetTickCount();
    const long span = (g_anim.spanMs > 0) ? g_anim.spanMs : 1;
    for (int i = 0; i < kAnimBalls; ++i)
    {
      if (g_anim.start[i] == 0)
        continue;
      const DWORD el = now - g_anim.start[i];
      if (el >= (DWORD)span)
      {
        g_anim.start[i] = 0; // arrived: free the slot
        continue;
      }
      const double u = (double)el / (double)span;
      // The ball is drawn in the plain text colour, NOT a slider hue: the SHAPE says which mode it
      // is, and colouring it like a parameter would suggest it belonged to one.
      const COLORREF col = g_theme.text;
      const float val = AnimSample(g_anim.smooth[i] ? g_anim.on : g_anim.off, u);
      const int px = bx0 + (int)((double)wIn * u);
      int py = by1 - (int)((double)hIn * val);
      if (py < by0) py = by0;
      if (py > by1) py = by1;
      HBRUSH br = CreateSolidBrush(col);
      if (g_anim.smooth[i])
      {
        HGDIOBJ ob = SelectObject(dc, br);
        HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, px - kAnimBallPx, py - kAnimBallPx, px + kAnimBallPx + 1,
                py + kAnimBallPx + 1);
        SelectObject(dc, op);
        SelectObject(dc, ob);
      }
      else
      {
        // A square: the same travel, handed over in whole steps.
        RECT sq = {px - kAnimBallPx, py - kAnimBallPx, px + kAnimBallPx + 1,
                   py + kAnimBallPx + 1};
        FillRect(dc, &sq, br);
      }
      DeleteObject(br);
    }
  }


  // The device verdict that used to sit under the chart is gone. It was the last remnant of the old
  // parameter monitor (its value table was removed in AGENTS.md 113, and this one line was kept),
  // and on the chart it was only in the way -- a "device: notched (steps of 120 delta)" readout
  // below a picture that already shows the device by the SIZE of its steps. Nothing diagnostics-facing
  // belongs on a settings panel.

  SetBkMode(dc, oldBk);
  SelectObject(dc, oldFont);
#endif
}

static void PaintPanel(HWND h)
{
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(h, &ps);

  // Start from the background, then lay the chrome on top.
  RECT rc;
  GetClientRect(h, &rc);
  FillRect(dc, &rc, g_theme.bgBrush);

  if (g_hasCards)
  {
    // Group surfaces. Each is filled with the card colour and outlined with a uniform
    // 1px frame.
    //
    // FrameRect, not Rectangle: GDI's Rectangle() draws its right and bottom edges one
    // pixel OUTSIDE the given rectangle, so a 1-pixel box comes out heavier on two
    // sides than the other two. FrameRect puts the border exactly inside the rect on
    // all four sides, which is what makes every line in the panel the same weight.
    if (g_cardBrush && g_lineBrush)
    {
      // Fill every group, then outline the BLOCK rather than each card. The cards sit
      // flush against one another (no gap), so framing each one would draw a 2px line
      // where two frames meet while the outer edge stayed 1px -- visibly uneven. One
      // outer frame plus a 1px separator at each boundary keeps every line the same
      // weight and reads as a single continuous block.
      for (int i = 0; i < kNumSliders; ++i)
        FillRect(dc, &g_cardRect[i], g_cardBrush);

      FrameRect(dc, &g_groupRect, g_lineBrush);
      for (int i = 1; i < kNumSliders; ++i)
      {
        RECT sep = {g_groupRect.left, g_cardRect[i].top, g_groupRect.right,
                    g_cardRect[i].top + 1};
        FillRect(dc, &sep, g_lineBrush);
      }
      // A rule under the master switch, so the switch reads as a separate header.
      RECT sep = g_sepRect;
      FillRect(dc, &sep, g_lineBrush);
    }
  }

  // The master switch is part of the panel's own drawing (see DrawSwitch).
  DrawSwitch(dc);

  // The parameter monitor.
  DrawMonitorBlock(dc);

  EndPaint(h, &ps);
}

// ---------------------------------------------------------------------------
// The fader control
//
// A trackbar cannot be made to look like REAPER's faders: the common control has no
// owner-draw style (there is no TBS_OWNERDRAW), so its look is whatever Windows draws.
// This is therefore a small control of our own, painted with REAPER's own generic
// fader colours (gen_volbg_horz / gen_volthumb_horz -- see ResolveTheme), which is
// what REAPER's generic windows use for their horizontal faders.
//
// Because it is ours, the geometry is also exact: the groove and the thumb are laid
// out from odd/even pixel counts and drawn with FillRect, so every line in the panel
// comes out the same weight (GDI's Rectangle() would put the right/bottom border one
// pixel outside, which is what made the earlier frames look uneven).
// ---------------------------------------------------------------------------
#define SWSC_FADER_CLASS "SmoothWheelScrollFader"
#define SWSC_FADER_CHANGED (WM_APP + 17) // wParam = control id, lParam = new pos
#define SWSC_FADER_RESET (WM_APP + 18)   // wParam = control id  (double click)
#define SWSC_FADER_RELEASED (WM_APP + 21) // wParam = control id  (the drag ended)

static void FaderTrackRect(HWND h, RECT *out)
{
  RECT rc;
  GetClientRect(h, &rc);
  const int thumbW = 11;
  out->left = thumbW / 2;
  out->right = rc.right - (thumbW - thumbW / 2);
  out->top = rc.top;
  out->bottom = rc.bottom;
}

// The fader groove's thickness. It was shared with the response curve before 3.0 removed it.
static const int kBarThickness = 4;
static void FaderPaint(HWND h, int idx)
{
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(h, &ps);

  RECT rc;
  GetClientRect(h, &rc);
  // The control sits on a card, so its background is the card colour, not the window's.
  HBRUSH card = CreateSolidBrush(g_theme.card);
  FillRect(dc, &rc, card);
  DeleteObject(card);

  RECT tr;
  FaderTrackRect(h, &tr);
  const int cy = (rc.top + rc.bottom) / 2;

  // Groove: a bar down the middle of the control, in this slider's own hue so the control and the
  // curve segment it drives can be matched. The hue's saturation carries the value, so the groove
  // itself shows how much the setting is contributing. Its thickness is kBarThickness, shared with
  // the response curve so the two are the same weight by construction.
  const int grooveH = kBarThickness;
  RECT g = {tr.left, cy - grooveH / 2, tr.right, cy - grooveH / 2 + grooveH};
  HBRUSH gb = CreateSolidBrush(SliderColor(idx, SliderAmount(idx)));
  FillRect(dc, &g, gb);
  DeleteObject(gb);

  // Thumb: a vertical bar straddling the track, positioned by the value.
  const int pos = g_faderPos[idx];
  const int span = tr.right - tr.left;
  const int cx = tr.left + (span * pos + 500) / 1000;
  const int thumbW = 11;
  const int thumbH = (rc.bottom - rc.top) - 4;
  RECT t = {cx - thumbW / 2, cy - thumbH / 2, cx - thumbW / 2 + thumbW, cy - thumbH / 2 + thumbH};

  // The thumb is the hue at full strength, so the handle is unmistakable on any theme.
  HBRUSH tb = CreateSolidBrush(SliderColor(idx, 1.0));
  FillRect(dc, &t, tb);
  DeleteObject(tb);
  // A 1px outline in the line colour, so the thumb reads on any theme.
  HBRUSH lb = CreateSolidBrush(g_theme.line);
  FrameRect(dc, &t, lb);
  DeleteObject(lb);

  EndPaint(h, &ps);
}

// Turn a click x into 0..1000, clamped at both ends.
static int FaderPosFromX(HWND h, int x)
{
  RECT tr;
  FaderTrackRect(h, &tr);
  const int span = tr.right - tr.left;
  if (span <= 0)
    return 0;
  int p = (int)(((long long)(x - tr.left) * 1000 + span / 2) / span);
  if (p < 0) p = 0;
  if (p > 1000) p = 1000;
  return p;
}

static void FaderSetPos(HWND h, int idx, int pos, bool notify)
{
  if (pos < 0) pos = 0;
  if (pos > 1000) pos = 1000;
  if (g_faderPos[idx] == pos)
    return;
  g_faderPos[idx] = pos;
  InvalidateRect(h, nullptr, FALSE);
  if (notify)
    SendMessage(GetParent(h), SWSC_FADER_CHANGED, (WPARAM)GetDlgCtrlID(h), (LPARAM)pos);
}

static LRESULT CALLBACK FaderProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  const int idx = (int)(INT_PTR)GetWindowLongPtrA(h, GWLP_USERDATA);
  switch (msg)
  {
  case WM_PAINT:
    FaderPaint(h, idx);
    return 0;
  case WM_ERASEBKGND:
    return 1; // painted in WM_PAINT
  case WM_LBUTTONDOWN:
  {
    SetCapture(h);
    SetFocus(h);
    FaderSetPos(h, idx, FaderPosFromX(h, ((int)(short)LOWORD(lp))), true);
    return 0;
  }
  case WM_LBUTTONDBLCLK:
    // Double click restores this one parameter to its default -- the same gesture
    // REAPER uses on its own faders and knobs, rather than a separate Reset button.
    // Handled here (not by the parent) because only the control knows which slider it
    // is; the parent performs the actual restore and the save.
    SendMessage(GetParent(h), SWSC_FADER_RESET, (WPARAM)GetDlgCtrlID(h), 0);
    return 0;
  case WM_MOUSEMOVE:
    if (GetCapture() == h)
      FaderSetPos(h, idx, FaderPosFromX(h, ((int)(short)LOWORD(lp))), true);
    return 0;
  case WM_LBUTTONUP:
    if (GetCapture() == h)
    {
      ReleaseCapture();
      // The lever was let go: run ONE ball down the track, so the settings settled on can be
      // watched once rather than inferred from the path (the user's "调完参数，松开拉杆，跑一颗过去").
      SendMessage(GetParent(h), SWSC_FADER_RELEASED, (WPARAM)GetDlgCtrlID(h), 0);
    }
    return 0;
  case WM_MOUSEWHEEL:
  {
    // While the panel is scrolled (content taller than the window), the wheel scrolls
    // the panel instead of nudging the value -- otherwise a wheel over a fader would
    // move the value and never reach the panel, and the scrolled rows would be
    // unreachable. When everything fits, the wheel adjusts the value, the way REAPER's
    // own faders behave.
    if (g_contentH > 0)
    {
      RECT pr;
      GetClientRect(GetParent(h), &pr);
      if (g_contentH > pr.bottom)
      {
        SendMessage(GetParent(h), WM_MOUSEWHEEL, wp, lp);
        return 0;
      }
    }
    const int delta = ((short)HIWORD(wp));
    FaderSetPos(h, idx, g_faderPos[idx] + (delta > 0 ? 10 : -10), true);
    return 0;
  }
  case WM_KEYDOWN:
    if (wp == VK_LEFT)  { FaderSetPos(h, idx, g_faderPos[idx] - 10, true); return 0; }
    if (wp == VK_RIGHT) { FaderSetPos(h, idx, g_faderPos[idx] + 10, true); return 0; }
    if (wp == VK_HOME)  { FaderSetPos(h, idx, 0, true); return 0; }
    if (wp == VK_END)   { FaderSetPos(h, idx, 1000, true); return 0; }
    break;
  case WM_SETFOCUS:
    InvalidateRect(h, nullptr, FALSE);
    return 0;
  case WM_KILLFOCUS:
    InvalidateRect(h, nullptr, FALSE);
    return 0;
  case WM_CONTEXTMENU:
    // The panel's Dock / Undock menu belongs to the whole window, not to a fader. The
    // faders cover most of the card area, so without forwarding, a right-click would
    // fall on a fader and the menu would rarely be reachable.
    SendMessageA(GetParent(h), WM_CONTEXTMENU, wp, lp);
    return 0;
  }
  return DefWindowProcA(h, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// The KNOB -- one behind each slider. It turns up to 170 degrees each way from straight up,
// with the left end at the parameter's minimum and the right end at its maximum, and the
// default sits in the middle. A knob carries a value that is NOT the slider's: the Start knob
// is the rise's duration in ms, and the other four are their segment's bend (see model.h).
//
// Drawn by hand like the fader, for the same reasons: the same 1px weight as everything else
// and no dependence on a themed control that would not match the panel.
// ---------------------------------------------------------------------------
#define SWSC_KNOB_CLASS "SmoothWheelScrollKnob"
#define SWSC_KNOB_CHANGED (WM_APP + 19) // wParam = control id, lParam = new 0..1000 pos
#define SWSC_KNOB_RESET (WM_APP + 20)   // wParam = control id  (double click)

// One knob's spec: where its value lives and the range it covers. The type itself and the
// arrays it fills are declared up with the sliders, so the value mapping can use them.

// The knob's slot in the curve's joint array: [0] is the Start knob, then Accel/Hold/Coast/
// Release, i.e. the same order. Kept as a function so the two never drift apart.
static int KnobCurveSlot(int knobIdx) { return knobIdx; }

static double KnobValue(int i)
{
  const KnobSpec &k = g_knobs[i];
  const int pos = g_knobPos[i];
  return k.min + (k.max - k.min) * (pos / 1000.0);
}

static void KnobPaint(HWND h, int idx)
{
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(h, &ps);
  RECT rc;
  GetClientRect(h, &rc);
  HBRUSH card = CreateSolidBrush(g_theme.card);
  FillRect(dc, &rc, card);
  DeleteObject(card);

  const int cx = (rc.left + rc.right) / 2;
  const int cy = (rc.top + rc.bottom) / 2;
  int r = ((rc.right - rc.left) < (rc.bottom - rc.top)) ? (rc.right - rc.left)
                                                        : (rc.bottom - rc.top);
  r = r / 2 - 2;
  if (r < 4)
    r = 4;

  // The face and its ring, in the knob's own hue (the same hue as the slider and the curve
  // segment it drives, so the three read as one control).
  const COLORREF face = SliderColor(idx, 0.35);
  const COLORREF edge = g_theme.line;
  HBRUSH fb = CreateSolidBrush(face);
  HBRUSH ob = CreateSolidBrush(edge);
  Ellipse(dc, cx - r, cy - r, cx + r + 1, cy + r + 1);
  // (Ellipse uses the brush for the fill and the pen for the outline; set both.)
  HGDIOBJ oldBr = SelectObject(dc, fb);
  HGDIOBJ oldPen = SelectObject(dc, CreatePen(PS_SOLID, 1, edge));
  Ellipse(dc, cx - r, cy - r, cx + r + 1, cy + r + 1);
  DeleteObject(SelectObject(dc, oldPen));
  SelectObject(dc, oldBr);
  DeleteObject(fb);
  DeleteObject(ob);

  // The pointer: 170 degrees each way from straight up. 0 = min is -170, 1000 = max is +170.
  const double ang = (-170.0 + 340.0 * (g_knobPos[idx] / 1000.0)) * 3.14159265358979 / 180.0;
  const int px = cx + (int)(sin(ang) * (r - 2) + 0.5);
  const int py = cy - (int)(cos(ang) * (r - 2) + 0.5);
  HPEN pen = CreatePen(PS_SOLID, 2, SliderColor(idx, 1.0));
  HGDIOBJ op = SelectObject(dc, pen);
  MoveToEx(dc, cx, cy, nullptr);
  LineTo(dc, px, py);
  SelectObject(dc, op);
  DeleteObject(pen);

  // A tick at straight up (the default's position), so "centred" is obvious.
  HPEN tp = CreatePen(PS_SOLID, 1, g_theme.grid);
  op = SelectObject(dc, tp);
  MoveToEx(dc, cx, cy - r - 2, nullptr);
  LineTo(dc, cx, cy - r + 1);
  SelectObject(dc, op);
  DeleteObject(tp);

  if (GetFocus() == h)
  {
    HBRUSH fr = CreateSolidBrush(g_theme.text);
    FrameRect(dc, &rc, fr);
    DeleteObject(fr);
  }
  EndPaint(h, &ps);
}

// A knob is dragged UP and DOWN: up raises the value, down lowers it. That is the gesture
// asked for, and it also suits this size -- a rotary drag on a 24px knob is fiddly, while a
// vertical drag has room to be precise. The drag is RELATIVE: the value moves by how far the
// pointer has travelled since the button went down, so the knob never jumps to the pointer.
static void KnobSetPos(HWND h, int idx, int pos, bool notify)
{
  if (pos < 0) pos = 0;
  if (pos > 1000) pos = 1000;
  if (g_knobPos[idx] == pos)
    return;
  g_knobPos[idx] = pos;
  InvalidateRect(h, nullptr, FALSE);
  if (notify)
    SendMessage(GetParent(h), SWSC_KNOB_CHANGED, (WPARAM)GetDlgCtrlID(h), (LPARAM)pos);
}

static LRESULT CALLBACK KnobProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  const int idx = (int)(INT_PTR)GetWindowLongPtrA(h, GWLP_USERDATA);
  const int my = (int)(short)HIWORD(lp);
  // Where the drag started and the value it started from: the drag is relative to these, so
  // the knob follows the pointer instead of jumping to it.
  static int dragY = 0, dragPos = 0;
  switch (msg)
  {
  case WM_PAINT:
    KnobPaint(h, idx);
    return 0;
  case WM_ERASEBKGND:
    return 1;
  case WM_LBUTTONDOWN:
    SetCapture(h);
    SetFocus(h);
    dragY = my;
    dragPos = g_knobPos[idx];
    return 0;
  case WM_LBUTTONDBLCLK:
    // Double click restores this one knob to its default, exactly like the faders.
    SendMessage(GetParent(h), SWSC_KNOB_RESET, (WPARAM)GetDlgCtrlID(h), 0);
    return 0;
  case WM_MOUSEMOVE:
    if (GetCapture() == h)
    {
      // 150 px of travel covers the whole range: enough to set a value precisely, short
      // enough not to be a chore. Up (smaller y) raises the value.
      const int span = 150;
      const int delta = dragY - my;
      KnobSetPos(h, idx, dragPos + (int)((long long)delta * 1000 / span), true);
    }
    return 0;
  case WM_LBUTTONUP:
    if (GetCapture() == h)
      ReleaseCapture();
    return 0;
  case WM_MOUSEWHEEL:
    // Same rule as the faders: when the panel is scrolled, the wheel belongs to the panel.
    if (g_contentH > 0)
    {
      RECT pr;
      GetClientRect(GetParent(h), &pr);
      if (g_contentH > pr.bottom)
      {
        SendMessage(GetParent(h), WM_MOUSEWHEEL, wp, lp);
        return 0;
      }
    }
    KnobSetPos(h, idx, g_knobPos[idx] + ((short)HIWORD(wp) > 0 ? 10 : -10), true);
    return 0;
  case WM_KEYDOWN:
    if (wp == VK_LEFT)  { KnobSetPos(h, idx, g_knobPos[idx] - 10, true); return 0; }
    if (wp == VK_UP)    { KnobSetPos(h, idx, g_knobPos[idx] + 10, true); return 0; }
    if (wp == VK_DOWN)  { KnobSetPos(h, idx, g_knobPos[idx] - 10, true); return 0; }
    if (wp == VK_RIGHT) { KnobSetPos(h, idx, g_knobPos[idx] + 10, true); return 0; }
    if (wp == VK_HOME)  { KnobSetPos(h, idx, 0, true); return 0; }
    if (wp == VK_END)   { KnobSetPos(h, idx, 1000, true); return 0; }
    break;
  case WM_SETFOCUS:
  case WM_KILLFOCUS:
    InvalidateRect(h, nullptr, FALSE);
    return 0;
  case WM_CONTEXTMENU:
    SendMessageA(GetParent(h), WM_CONTEXTMENU, wp, lp);
    return 0;
  }
  return DefWindowProcA(h, msg, wp, lp);
}

static void RegisterFaderClass()
{
  static bool done = false;
  if (done)
    return;
  WNDCLASSA wc = {0};
  wc.lpfnWndProc = FaderProc;
  wc.hInstance = g_hInst;
  wc.hCursor = LoadCursor(nullptr, IDC_HAND);
  wc.hbrBackground = nullptr;
  // Without CS_DBLCLKS the control never receives WM_LBUTTONDBLCLK, which is what
  // double-click-to-reset needs.
  wc.style = CS_DBLCLKS;
  wc.lpszClassName = SWSC_FADER_CLASS;
  RegisterClassA(&wc);

  // Same treatment for the knobs (their own class, so their window proc differs).
  WNDCLASSA kc = {0};
  kc.lpfnWndProc = KnobProc;
  kc.hInstance = g_hInst;
  kc.hCursor = LoadCursor(nullptr, IDC_HAND);
  kc.hbrBackground = nullptr;
  kc.style = CS_DBLCLKS;
  kc.lpszClassName = SWSC_KNOB_CLASS;
  RegisterClassA(&kc);

  done = true;
}

// Width of a string in the panel font, measured rather than assumed.
static int MeasureTextWidth(const char *text)
{
  if (!text || !*text)
    return 0;
  HDC dc = GetDC(nullptr);
  HGDIOBJ oldFont = SelectObject(dc, UiFont(nullptr));
  SIZE sz = {0};
  GetTextExtentPoint32A(dc, text, (int)strlen(text), &sz);
  SelectObject(dc, oldFont);
  ReleaseDC(nullptr, dc);
  return sz.cx;
}

// --- Layout -----------------------------------------------------------------
//
// All panel geometry in one place, recomputed on demand rather than cached at build
// time. That is what lets the panel re-flow when its size changes -- and its size DOES
// change: REAPER's docker resizes the window when it is docked or undocked, and the
// user can resize it either way.
struct PanelMetrics
{
  int fontH, pad, groupH, barH, rowH, headH, hintH, btnH, capW, cardPad, gapY;
  int monH, monGap; // the motion chart block under the rows (balls running a track)
};

// 1.6.1's opening size, measured on this machine from 1.6.1's own metrics (client 377 x 545).
//
// 1.6.1 opened at its own minimum width with a height that carried FIVE slider rows plus the
// response curve. 3.0 has one row and no curve, so recomputing the size from the current content
// made the window visibly smaller. The user asked for 1.6.1's size back, so it is stated here as a
// SIZE rather than derived: these are measurements, not a formula, and they should be re-measured on
// a machine with a different font or DPI.
//
// This only sets how big the window OPENS. The layout still comes from the metrics above, and a
// window smaller than the content simply scrolls.
// WIDER than 1.6.1 on purpose: the user asked for a longer, easier-to-grab fader. 1.6.1 was 377
// (and its faders were further shortened by a knob reservation that 3.0 does not need), which left
// a track too short to place a value comfortably.
static const int kDesignClientW = 360; // the user's requested width, and the minimum (see MinPanelWidth)
// The user first asked for 450; the motion chart was then doubled (2026-09-16, "动画面板高度再增高一倍"),
// which adds exactly one more chart block of height (77 px at this font, so 450 -> 527 with the same
// slack). Opening at 450 would therefore open WITH a scrollbar, which is not what "the default
// height" should look like.
static const int kDesignClientH = 527; // the user's requested height, plus the doubled chart

static PanelMetrics PanelMetricsNow()
{
  PanelMetrics m;
  m.fontH = 16;
  UiFont(&m.fontH);       // also yields the row height the font needs
  m.pad = 16;             // window edge padding
  m.groupH = m.fontH + 6; // title line
  m.barH = m.fontH + 10;  // fader height
  m.capW = 56;            // end-cap label width
  m.cardPad = 9;          // padding between a card's frame and its contents (all sides)
  m.gapY = 0;             // no gap: the cards stack directly, reading as one block
  m.headH = m.fontH + 12; // one line of caption; the box is shorter than the text line
  m.hintH = 0; // filled in by PanelFooterHeight(), which measures the wrapped text
  m.btnH = m.fontH + 12;
  // rowH is the distance from one card's top to the next: the card itself (title +
  // fader + the SAME padding above and below) plus the gap to the next card. Equal
  // padding top and bottom is the point -- the group has to breathe the same at both
  // ends rather than sit flush against the frame's lower edge.
  m.rowH = (m.groupH + m.barH + m.cardPad * 2) + m.gapY;
  // The motion chart: a caption line, the track, and the device verdict line. TWICE the height the
  // old value table used (the user asked for it): a ball needs room to climb a visible path -- at
  // the old height the track was a flat sliver and the motion could not be read.
  m.monGap = 12;
  m.monH = 2 * (5 + 4 * (m.fontH + 2));
  return m;
}

// The height at which the panel needs no scrollbar. Nothing is reserved for a bar: at
// this size the content fits, so none is shown. (3.0 has no response curve.)
static int PanelIdealHeight(const PanelMetrics &m)
{
  return m.pad + m.headH * kSwitchLines + m.rowH * kNumSliders + m.monGap + m.monH + m.pad;
}

// The narrowest client width at which the master switch's caption still fits on ONE line.
// Measured by the check box itself, so it stays correct at another font or DPI; a wider
// system font simply raises the floor.
static int MinPanelWidth(const PanelMetrics &m)
{
  // The check box's glyph is a standard menu check: SM_CXMENUCHECK is its width (17 px
  // here). Button_GetIdealSize was tried first and DOES NOT WORK for a check box -- it
  // returns failure, which silently made this one size too narrow and let the caption
  // wrap, bringing the scrollbar back. Text width comes from the font metric, the glyph
  // from the system metric.
  const int boxW = GetSystemMetrics(SM_CXMENUCHECK);
  const int gap = 6; // between the glyph and its caption
  // The WIDEST of the two captions: either of them may be the one that must not wrap.
  const int a = MeasureTextWidth(kEnableText);
  const int b = MeasureTextWidth(kTouchpadRevText);
  const int fitsCaptions = m.pad * 2 + boxW + gap + (a > b ? a : b);
  // The user asked for 360 as the minimum width (and as the opening width), so the caption fit is a
  // floor, not the answer: whichever is larger wins. A wider font still raises it.
  return (fitsCaptions > kDesignClientW) ? fitsCaptions : kDesignClientW;
}

static void LayoutControls(HWND h); // defined below FitWindowToContent
// Window-placement helpers, defined with the rest of the placement code further down.
static bool WindowInDock(HWND h);
static void MinWindowSize(const PanelMetrics &m, LONG *outW, LONG *outH);
static void CaptureFloatGeom(HWND h);

// The panel's window style -- one source, because three places must agree on it:
// the CreateWindowEx call, the AdjustWindowRectEx padding, and the minimum track size.
// If they disagree the client area is computed for a frame the window does not have.
//
// WS_THICKFRAME is the frame that makes a window resizable by dragging an edge. It was
// missing, which is why the floating panel could not be resized at all. It is added only
// when the panel will FLOAT: inside the docker REAPER owns the frame, and a thick frame
// there would draw a resize edge inside the dock rather than at its border.
static DWORD PanelWindowStyle()
{
  return WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | (g_dockOn ? 0 : WS_THICKFRAME);
}

// Size the window so the laid-out content fits exactly, with no scrollbar. Rather than
// trusting a computed minimum, this asks the LAYOUT what it needs after running once and
// then grows the window to match -- so it cannot be wrong at another font, DPI or theme:
// the caption's real width and the rows' real height are measured by the controls
// themselves, not estimated.
static void FitWindowToContent(HWND h)
{
  const PanelMetrics m = PanelMetricsNow();

  // Everything is derived from what the controls actually need, measured after a real
  // layout pass -- no hand-added minimums, so it cannot be off at another font or DPI.
  //
  //   1. the check box must stay on ONE line: ask it how tall it wants to be for the
  //      current width; taller than one text line means the caption wrapped;
  //   2. the rows must fit without a scrollbar (g_contentH <= client height).
  //
  // If either fails the window is grown and the layout re-run, up to a few times. This
  // only runs while the window is FLOATING: when docked REAPER owns the size and the
  // panel scrolls instead.
  for (int attempt = 0; attempt < 10; ++attempt)
  {
    LayoutControls(h);

    RECT rc;
    GetClientRect(h, &rc);

    // How tall does the switch's caption want to be at this width? Measured directly, so the
    // wrap point is measured rather than estimated. (It used to ask the check box, which no
    // longer exists -- the panel draws the switch.)
    int boxWanted = 0;
    {
      // Measure BOTH captions: either of them may be the one that wraps, and all the panel needs
      // to know is whether ANY of them does.
      const char *caps[2] = {kEnableText, kTouchpadRevText};
      HDC dc = GetDC(h);
      HGDIOBJ oldFont = SelectObject(dc, UiFont(nullptr));
      for (int i = 0; i < 2; ++i)
      {
        RECT br = {0, 0, rc.right - m.pad * 2, 0};
        DrawTextA(dc, caps[i], -1, &br, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        if (br.bottom > boxWanted)
          boxWanted = br.bottom;
      }
      SelectObject(dc, oldFont);
      ReleaseDC(h, dc);
    }

    const bool wrapped = (boxWanted > m.fontH + 2);
    const bool tooShort = (g_contentH > rc.bottom);
    if (!wrapped && !tooShort)
      return; // fits, no scrollbar: done

    // Grow along whichever axis failed (and always keep the current width if the wrap
    // was the only problem, since a wider window fixes the wrap).
    int newW = rc.right;
    if (wrapped)
      newW = (rc.right < MinPanelWidth(m)) ? MinPanelWidth(m) : rc.right + m.fontH * 4;
    const int newH = (tooShort ? g_contentH : rc.bottom);

    RECT wr = {0, 0, newW, newH};
    AdjustWindowRectEx(&wr, PanelWindowStyle(), FALSE, WS_EX_TOOLWINDOW);
    SetWindowPos(h, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
                 SWP_NOMOVE | SWP_NOZORDER);
  }
}

static void LayoutControls(HWND h)
{
  if (!h)
    return;
  PanelMetrics m = PanelMetricsNow();
  RECT rc;
  GetClientRect(h, &rc);
  const int w = rc.right;
  const int cw = w - m.pad * 2;

  // Vertical: the rows keep their compact spacing and start at the top. The panel is
  // NOT stretched to fill a taller window (the groups would drift apart and lose their
  // grouping), so extra height is simply left empty at the bottom.
  //
  // When the window is too SHORT, the content is NOT squeezed -- it scrolls (see the
  // scrollbar handling below). Squeezing was the earlier behaviour and it had a floor;
  // scrolling has none, which is what makes adding more parameters later safe.
  const int avail = rc.bottom - rc.top;
  g_contentH = PanelIdealHeight(m);

  // Scrollbar first: its presence changes the usable width, so it must be decided
  // before the controls are placed. Shown only while the content does not fit.
  const int barW = GetSystemMetrics(SM_CXVSCROLL);
  const bool needScroll = (g_contentH > avail);
  if (needScroll)
  {
    const int maxY = g_contentH - avail;
    if (g_scrollY > maxY) g_scrollY = maxY;
    if (g_scrollY < 0) g_scrollY = 0;
  }
  else
  {
    g_scrollY = 0;
  }
  ShowScrollBar(h, SB_VERT, needScroll);
  if (needScroll)
  {
    SCROLLINFO si = {0};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = g_contentH - 1;
    si.nPage = avail;
    si.nPos = g_scrollY;
    SetScrollInfo(h, SB_VERT, &si, TRUE);
  }
  const int usableW = needScroll ? (w - barW) : w;
  const int y0 = -g_scrollY; // content coordinate 0 sits here

  // The parameter monitor sits under the rows.
  {
    const int my = y0 + m.pad + m.headH * kSwitchLines + m.rowH * kNumSliders + m.monGap;
    g_monBlk = {m.pad, my, usableW - m.pad, my + m.monH};
  }

  // The switches: each is drawn by the panel, so this is only their rectangles. A rectangle is
  // sized to its own caption (glyph + gap + text + a little slack for the focus ring) rather than
  // the full panel width, so each reads as one line of text.
  {
    const int boxW = GetSystemMetrics(SM_CXMENUCHECK);
    const int maxW = usableW - m.pad * 2;
    const char *caps[kSwitchLines] = {kEnableText, kTouchpadRevText};
    RECT *rects[kSwitchLines] = {&g_switchRect, &g_switch2Rect};
    for (int i = 0; i < kSwitchLines; ++i)
    {
      int w = boxW + 6 + MeasureTextWidth(caps[i]) + 8;
      if (w > maxW) w = maxW;
      *rects[i] = {m.pad, y0 + m.pad + m.headH * i, m.pad + w, y0 + m.pad + m.headH * (i + 1)};
    }
    g_hasSwitch = true;
  }

  for (int i = 0; i < kNumSliders; ++i)
  {
    const SliderSpec &s = g_sliders[i];
    const int ly = y0 + m.pad + m.headH * kSwitchLines + m.rowH * i;

    // Card = content + equal padding top and bottom.
    RECT cr = {m.pad, ly, usableW - m.pad, ly + m.cardPad + m.groupH + m.barH + m.cardPad};
    g_cardRect[i] = cr;

    const int ix = cr.left + m.cardPad;
    const int iw = (cr.right - m.cardPad) - ix;
    const int ty = cr.top + m.cardPad;

    SetWindowPos(GetDlgItem(h, s.labelId), nullptr, ix, ty, iw, m.groupH - 2, SWP_NOZORDER);
    SetWindowPos(g_capLo[i], nullptr, ix, ty + m.groupH, m.capW, m.barH, SWP_NOZORDER);
    // A knob, when the row HAS one, takes the far-right end of the row; the "hi" end-cap and the
    // fader then share what is left. A row WITHOUT a knob reserves nothing -- 3.0 has no knobs, and
    // reserving the width anyway silently shortened every fader by the knob's diameter.
    const int kw = s.hasKnob ? m.barH : 0; // square: the knob's diameter is the row height
    if (s.hasKnob)
      SetWindowPos(GetDlgItem(h, g_knobs[i].id), nullptr, ix + iw - kw, ty + m.groupH, kw,
                   m.barH, SWP_NOZORDER);
    SetWindowPos(g_capHi[i], nullptr, ix + iw - kw - m.capW, ty + m.groupH, m.capW, m.barH,
                 SWP_NOZORDER);
    SetWindowPos(GetDlgItem(h, s.sliderId), nullptr, ix + m.capW + 8, ty + m.groupH,
                 iw - (m.capW + 8) - m.capW - kw - 8, m.barH, SWP_NOZORDER);
  }


  // The block's outer rect (the single frame) and the rule under the master switch.
  if (kNumSliders > 0)
    g_groupRect = {g_cardRect[0].left, g_cardRect[0].top,
                   g_cardRect[kNumSliders - 1].right, g_cardRect[kNumSliders - 1].bottom};
  // The rule under the master switch scrolls with the content (y0).
  g_sepRect = {m.pad, y0 + m.pad + m.headH * kSwitchLines + 2, usableW - m.pad,
               y0 + m.pad + m.headH * kSwitchLines + 3};
  g_hasCards = true;

  // The faders paint their own background from the card colour, so they must repaint
  // when the layout moves them.
  for (int i = 0; i < kNumSliders; ++i)
    if (HWND f = GetDlgItem(h, g_sliders[i].sliderId))
      InvalidateRect(f, nullptr, TRUE);
}

// Create the child controls once. LayoutControls() then places them.
static void CreatePanelChildren(HWND h)
{
  const PanelMetrics m = PanelMetricsNow();

  // The master switch is NOT created as a control: the panel draws it (see DrawSwitch) and
  // shows/hides it by calling LayoutControls to set g_switchRect.

  for (int i = 0; i < kNumSliders; ++i)
  {
    const SliderSpec &s = g_sliders[i];
    CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10, h,
                    (HMENU)(INT_PTR)s.labelId, g_hInst, nullptr);
    g_capLo[i] = CreateWindowExA(0, "STATIC", s.lomark,
                                 WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                                 0, 0, 10, 10, h, nullptr, g_hInst, nullptr);
    g_capHi[i] = CreateWindowExA(0, "STATIC", s.himark,
                                 WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
                                 0, 0, 10, 10, h, nullptr, g_hInst, nullptr);
    HWND f = CreateWindowExA(0, SWSC_FADER_CLASS, "", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
                             h, (HMENU)(INT_PTR)s.sliderId, g_hInst, nullptr);
    SetWindowLongPtrA(f, GWLP_USERDATA, (LONG_PTR)i);

    // The knob that sits behind this slider, when the row has one.
    if (s.hasKnob)
    {
      HWND k = CreateWindowExA(0, SWSC_KNOB_CLASS, "", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
                               h, (HMENU)(INT_PTR)g_knobs[i].id, g_hInst, nullptr);
      SetWindowLongPtrA(k, GWLP_USERDATA, (LONG_PTR)i);
    }
  }


  (void)m;
  ApplyFontToChildren(h);
}

// ---------------------------------------------------------------------------
// The TITLE BAR follows the palette.
//
// Windows does not follow REAPER's light/dark state by itself for a window an application creates,
// so the caption would stay light above a dark panel. It is driven here by the SAME `win32_darkmode`
// answer that picks the palette, so the caption and the panel can never disagree. Passing FALSE is
// what turns a dark caption back to light, so this is a two-way switch, not a one-way darkening:
// it FOLLOWS the flag rather than forcing a look.
//
// DwmSetWindowAttribute lives in dwmapi.dll, loaded on demand rather than linked, because the
// attribute number differs between Windows builds (20 on Windows 10 2004 and later, 19 before
// that). If neither is supported the call does nothing and the caption keeps the system colour --
// cosmetic only, never a failure.
#define SWSC_DWMWA_USE_IMMERSIVE_DARK_MODE_OLD 19
#define SWSC_DWMWA_USE_IMMERSIVE_DARK_MODE 20
// 34 = DWMWA_BORDER_COLOR (Windows 11). A dark caption on a dark window looks wrong with a light
// border drawn around it, so the border is set with the caption. Ignored where unsupported.
#define SWSC_DWMWA_BORDER_COLOR 34

static void ApplyTitleBar(HWND h, bool dark)
{
  typedef HRESULT(WINAPI * DwmSetWindowAttribute_t)(HWND, DWORD, LPCVOID, DWORD);
  static DwmSetWindowAttribute_t fn = nullptr;
  static bool tried = false;
  if (!tried)
  {
    tried = true;
    HMODULE dwm = LoadLibraryA("dwmapi.dll");
    if (dwm)
      fn = (DwmSetWindowAttribute_t)GetProcAddress(dwm, "DwmSetWindowAttribute");
  }
  if (!fn || !h)
    return;
  const BOOL v = dark ? TRUE : FALSE;
  if (FAILED(fn(h, SWSC_DWMWA_USE_IMMERSIVE_DARK_MODE, &v, sizeof(v))))
    fn(h, SWSC_DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, &v, sizeof(v));
  // The window border, in the palette's own frame colour, so the whole frame matches the panel.
  const COLORREF border = g_theme.line;
  fn(h, SWSC_DWMWA_BORDER_COLOR, &border, sizeof(border));
}

// The window's SCROLLBAR, in the palette's mode.
//
// A WS_VSCROLL bar is NON-CLIENT and drawn by the OS, so it ignores the panel's painting and comes
// out light inside a dark panel -- the "scrollbar is still light" that was reported. Windows'
// answer is the "DarkMode_Explorer" window theme: applying it to the window makes the standard
// scrollbar and controls render dark, and passing nullptr restores the default, so it is a two-way
// switch that FOLLOWS the panel's mode rather than forcing a look.
//
// Loaded on demand, never linked, so a Windows without it simply keeps the default scrollbar.
//
// SetWindowTheme SENDS WM_THEMECHANGED back to the window, so re-sending the same value is not a
// no-op: it re-enters the panel's handler, which repaints and would apply it again. That was a
// measured 31-level re-entry storm once, so the last value applied to this window is remembered and
// an unchanged request returns without calling in. The handler's own re-entrancy guard stays as
// defence in depth.
static HWND s_barThemeHwnd = nullptr;
static bool s_barThemeDark = false;
static bool s_barThemeHave = false;

static void ApplyScrollbarTheme(HWND h, bool dark)
{
  typedef HRESULT(WINAPI * SetWindowTheme_t)(HWND, LPCWSTR, LPCWSTR);
  static SetWindowTheme_t fn = nullptr;
  static bool tried = false;
  if (!tried)
  {
    tried = true;
    HMODULE ux = LoadLibraryA("uxtheme.dll");
    if (ux)
      fn = (SetWindowTheme_t)GetProcAddress(ux, "SetWindowTheme");
  }
  if (!fn || !h)
    return;
  if (s_barThemeHwnd == h && s_barThemeDark == dark && s_barThemeHave)
    return; // already in that mode on this window: nothing to do, and nothing to re-enter with
  s_barThemeHwnd = h;
  s_barThemeDark = dark;
  s_barThemeHave = true;
  fn(h, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
}

// Re-read the palette and repaint the whole panel. Shared by WM_THEMECHANGED, the re-open path and
// the timer, so a mode change is picked up whether or not REAPER announces it. See OnTimer for why
// the timer is needed at all.
static void RefreshPanelTheme()
{
  if (!g_cfgWnd || !IsWindow(g_cfgWnd))
    return;
  ResolveTheme();
  RebuildThemeBrushes();
  ApplyTitleBar(g_cfgWnd, g_theme.dark);
  ApplyScrollbarTheme(g_cfgWnd, g_theme.dark);
  InvalidateRect(g_cfgWnd, nullptr, TRUE);
  // Every child repaints: the statics draw with the brush just replaced, and the master switch is
  // part of the panel's own painting (DrawSwitch) so it repaints with it.
  for (HWND c = FindWindowExA(g_cfgWnd, nullptr, nullptr, nullptr); c;
       c = FindWindowExA(g_cfgWnd, c, nullptr, nullptr))
    InvalidateRect(c, nullptr, TRUE);
  // The scrollbar is non-client, so the frame must be redrawn as well or it keeps the previous
  // theme's colour even after the theme was switched.
  RedrawWindow(g_cfgWnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
}

// Switch the settings window between REAPER's docker and a normal floating window.
//
// This is done by tearing the window down and building it again from ShowConfigWindow
// with g_dockOn flipped, which is exactly how SWS's dockable windows toggle: a window's
// dock membership is fixed for its lifetime, so the only reliable way to move it in or
// out is a fresh window. The preference is saved, so the next open comes up the same way
// and the choice survives a restart.
static void ToggleDocking(HWND h)
{
  bool isFloatingDocker = false;
  const bool docked = (DockIsChildOfDock && DockIsChildOfDock(h, &isFloatingDocker) >= 0);
  g_dockOn = !docked;
  SaveSettings();
  // DestroyWindow raises WM_CLOSE only for a user's close, so remove explicitly here:
  // the window is going away because its dock state changed, not because it was closed.
  if (docked && DockWindowRemove)
    DockWindowRemove(h);
  DestroyWindow(h);   // clears g_cfgWnd in WM_DESTROY
  ShowConfigWindow(); // recreated with the new g_dockOn
}

static LRESULT CALLBACK CfgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
  {
  case SWSC_FADER_RESET:
  {
    // Double click on a fader: put THAT parameter back to its compiled-in default.
    // A targeted reset, not "restore everything", which is what REAPER's own controls
    // do and keeps the gesture safe to use while tuning.
    for (int i = 0; i < kNumSliders; ++i)
    {
      const SliderSpec &s = g_sliders[i];
      if (s.sliderId != (int)wp)
        continue;
      // The default is part of the row, so a reset cannot restore another control's value.
      *s.value = s.defValue;
      RefreshDerived();
      g_faderPos[i] = ValueToSlider(s, *s.value);
      if (HWND f = GetDlgItem(h, s.sliderId))
        InvalidateRect(f, nullptr, FALSE);
      UpdateLabels();
      SaveSettings();
      AnimRebuild(); // the path and the tick scales are computed from these values: redraw, no ball
      break;
    }
    return 0;
  }
  case SWSC_FADER_CHANGED:
  {
    // A fader moved. Ignore notifications raised while the window is being built.
    if (g_cfgUpdating)
      return 0;
    const int cid = (int)wp;
    for (int i = 0; i < kNumSliders; ++i)
    {
      const SliderSpec &s = g_sliders[i];
      if (s.sliderId == cid)
      {
        *s.value = SliderToValue(s, (int)lp);
        RefreshDerived();      // clamp into range
        UpdateLabels();
        SaveSettings();        // persist live, so closing the window keeps the value
        AnimRebuild();         // keep the picture true; NO ball (see SWSC_FADER_RELEASED)
        break;
      }
    }
    return 0;
  }
  case SWSC_FADER_RELEASED:
    // The drag ended. NOTHING is launched: the chart is driven by the WHEEL, so that what is on
    // screen is what a roll would do with these settings. A ball for "the lever was let go" was tried
    // and the user removed it -- the picture must not animate for an action that is not a wheel
    // (2026-09-16: "拉杆改完值后的动画去掉，只要鼠标滚轮有事件就给动画").
    return 0;
  case SWSC_KNOB_RESET:
  {
    // Double click on a knob: put THAT knob back to its default (the middle of its range).
    // Same targeted gesture as the faders.
    for (int i = 0; i < kNumSliders; ++i)
    {
      if (g_knobs[i].id != (int)wp)
        continue;
      // The knob's default also comes from the row (see SliderSpec), so it cannot be restored
      // from the wrong entry.
      *g_knobs[i].value = g_sliders[i].knobDef;
      g_knobPos[i] = ValueToKnob(i);
      if (HWND k = GetDlgItem(h, g_knobs[i].id))
        InvalidateRect(k, nullptr, FALSE);
      UpdateLabels();
      SaveSettings();
      AnimRebuild(); // no ball: only the wheel launches one
      break;
    }
    return 0;
  }
  case SWSC_KNOB_CHANGED:
  {
    if (g_cfgUpdating)
      return 0;
    const int cid = (int)wp;
    for (int i = 0; i < kNumSliders; ++i)
    {
      if (g_knobs[i].id != cid)
        continue;
      g_knobPos[i] = (int)lp;
      *g_knobs[i].value = KnobValue(i);
      UpdateLabels();
      SaveSettings();
      AnimRebuild();
      break;
    }
    return 0;
  }
  case WM_COMMAND:
    switch (LOWORD(wp))
    {
    // (The master switch is no longer a control, so it does not come through here: its click is
    // handled in WM_LBUTTONDOWN. See g_switchRect.)
    }
    break;
  case WM_CTLCOLORSTATIC:
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORLISTBOX:
    return OnCtlColor(msg, (HDC)wp, (HWND)lp);
  // The master switch is drawn by the panel (see DrawSwitch), so its CLICK is handled here rather
  // than by a control: a left click inside g_switchRect toggles it. Toggling never needs a restart
  // -- the hook stays installed and just stops intercepting when off (see GetMsgProc / OnAction).
  case WM_LBUTTONDOWN:
  {
    if (g_hasSwitch)
    {
      POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
      const RECT *rects[kSwitchLines] = {&g_switchRect, &g_switch2Rect};
      bool *flags[kSwitchLines] = {&g_glideOn, &g_touchpadReverse};
      for (int i = 0; i < kSwitchLines; ++i)
      {
        const RECT &r = *rects[i];
        if (p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom)
        {
          if (!g_cfgUpdating)
          {
            *flags[i] = !*flags[i];
            SaveSettings();
            // The track draws the ENABLED mode's guide in a different colour, so toggling changes
            // which one leads -- repaint it (and the whole panel, since the switch's own glyph
            // changed too). NO ball: the chart is driven by the wheel, and the mode a roll uses is
            // picked up when the next roll arrives (which is why a toggle does not animate here).
            InvalidateRect(h, &g_monBlk, FALSE);
          }
          InvalidateRect(h, &r, FALSE);
          return 0;
        }
      }
    }
    break; // not on the switch: let the default handling have it
  }
  // Clicking the panel takes the focus, so the switch's focus ring follows it and the keyboard
  // still reaches us (see PanelKeyHandler).
  case WM_SETFOCUS:
    if (g_hasSwitch)
    {
      InvalidateRect(h, &g_switchRect, FALSE);
      InvalidateRect(h, &g_switch2Rect, FALSE);
    }
    return 0;
  case WM_KILLFOCUS:
    if (g_hasSwitch)
    {
      InvalidateRect(h, &g_switchRect, FALSE);
      InvalidateRect(h, &g_switch2Rect, FALSE);
    }
    return 0;
  case WM_ERASEBKGND:
  {
    RECT rc;
    GetClientRect(h, &rc);
    FillRect((HDC)wp, &rc, g_theme.bgBrush);
    return 1;
  }
  case WM_PAINT:
    PaintPanel(h);
    return 0;
  case WM_TIMER:
    // The balls. It only repaints the CHART's rectangle (never the whole panel). The timer is
    // stopped here, once the last ball has arrived, so an idle panel costs nothing at all.
    if (wp == kAnimTimerId)
    {
      DWORD now = GetTickCount();
      const long span = (g_anim.spanMs > 0) ? g_anim.spanMs : 1;
      bool any = false;
      for (int i = 0; i < kAnimBalls; ++i)
      {
        if (g_anim.start[i] == 0)
          continue;
        if ((DWORD)(now - g_anim.start[i]) >= (DWORD)span)
          g_anim.start[i] = 0;
        else
          any = true;
      }
      InvalidateRect(h, &g_monBlk, FALSE);
      if (!any)
      {
        KillTimer(h, kAnimTimerId);
        return 0;
      }
      return 0;
    }
    break;
  case WM_SIZE:
    // REAPER resizes the window when it is docked, undocked, or when the dock is
    // resized, so the layout has to follow the size rather than assume the one it was
    // created at.
    LayoutControls(h);
    InvalidateRect(h, nullptr, TRUE);
    if (g_rectTracking)
      CaptureFloatGeom(h);
    return 0;
  case WM_MOVE:
    if (g_rectTracking)
      CaptureFloatGeom(h);
    return 0;
  case WM_EXITSIZEMOVE:
    // One save per completed drag/resize instead of one per intermediate message, so
    // dragging the panel does not hammer the state store.
    if (g_rectTracking)
    {
      CaptureFloatGeom(h);
      SaveSettings();
    }
    return 0;
  case WM_VSCROLL:
  {
    // Only meaningful while the content does not fit; LayoutControls decides whether
    // the bar is shown at all and clamps the offset, so the arithmetic here is simple.
    RECT rc;
    GetClientRect(h, &rc);
    const int maxY = (g_contentH > rc.bottom) ? (g_contentH - rc.bottom) : 0;
    int y = g_scrollY;
    switch (LOWORD(wp))
    {
    case SB_LINEUP:   y -= g_scrollStep; break;
    case SB_LINEDOWN: y += g_scrollStep; break;
    case SB_PAGEUP:   y -= rc.bottom; break;
    case SB_PAGEDOWN: y += rc.bottom; break;
    case SB_TOP:      y = 0; break;
    case SB_BOTTOM:   y = maxY; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
    {
      SCROLLINFO si = {0};
      si.cbSize = sizeof(si);
      si.fMask = SIF_TRACKPOS;
      GetScrollInfo(h, SB_VERT, &si);
      y = si.nTrackPos;
      break;
    }
    default:
      return 0;
    }
    if (y < 0) y = 0;
    if (y > maxY) y = maxY;
    if (y != g_scrollY)
    {
      g_scrollY = y;
      LayoutControls(h);
      InvalidateRect(h, nullptr, TRUE);
    }
    return 0;
  }
  case WM_MOUSEWHEEL:
  {
    // The panel is short enough for a wheel to scroll it, which is what a user will
    // reach for before the scrollbar.
    if (g_contentH <= 0)
      return 0;
    RECT rc;
    GetClientRect(h, &rc);
    const int maxY = (g_contentH > rc.bottom) ? (g_contentH - rc.bottom) : 0;
    if (maxY <= 0)
      return 0; // everything fits: let it pass, nothing to scroll
    const int delta = ((short)HIWORD(wp));
    int y = g_scrollY + (delta < 0 ? g_scrollStep : -g_scrollStep);
    if (y < 0) y = 0;
    if (y > maxY) y = maxY;
    if (y != g_scrollY)
    {
      g_scrollY = y;
      LayoutControls(h);
      InvalidateRect(h, nullptr, TRUE);
    }
    return 0;
  }
  case WM_GETMINMAXINFO:
  {
    // Keep a floor on the size so docking cannot squeeze the controls into nothing.
    // Width floor: below MinPanelWidth the master switch's caption would wrap, which is
    // the one line that must stay on one line. Height floor is small on purpose -- the
    // panel scrolls, so it does not need room for every row at once.
    MINMAXINFO *mmi = (MINMAXINFO *)lp;
    MinWindowSize(PanelMetricsNow(), &mmi->ptMinTrackSize.x, &mmi->ptMinTrackSize.y);
    return 0;
  }
  case WM_CONTEXTMENU:
  {
    // Dockable windows in REAPER are toggled from a right-click menu on the window, so
    // the panel gets one too. Without it there is no way to leave the docker once
    // docked: REAPER remembers the placement and the docker's own tab menu only picks
    // a different edge, so "Dock / Undock" is what makes the choice reversible.
    const bool docked = WindowInDock(h);
    HMENU hm = CreatePopupMenu();
    AppendMenuA(hm, MF_STRING, 1, docked ? "Undock" : "Dock in Docker");
    const int cmd = TrackPopupMenu(hm, TPM_RETURNCMD | TPM_RIGHTBUTTON, GET_X_LPARAM(lp),
                                   GET_Y_LPARAM(lp), 0, h, nullptr);
    DestroyMenu(hm);
    if (cmd == 1)
      ToggleDocking(h);
    return 0;
  }
  case WM_CLOSE:
    // Remember where the floating panel was before it goes away, so reopening it puts it
    // back in the same place instead of the OS default position. Saved here rather than
    // only on drag-end because the panel may be closed right after a move, and because
    // this is the last moment the window still has a valid rectangle.
    CaptureFloatGeom(h);
    SaveSettings();
    g_rectTracking = false;
    // Leave the docker before the window is destroyed, while the window is still alive
    // and REAPER is not mid-teardown. Removing from inside WM_DESTROY instead would run
    // while the docker is already tearing the entry down and could leave a stale entry
    // for our ident string -- after which REAPER still believes the window is placed, so
    // a later instance is not shown and the docker keeps claiming the window.
    if (DockWindowRemove)
      DockWindowRemove(h);
    DestroyWindow(h);
    return 0;
  case WM_DESTROY:
    // Nothing to tell REAPER here: the dock was already left in WM_CLOSE.
    KillTimer(h, kAnimTimerId); // the balls' timer dies with the window it paints
    for (int i = 0; i < kAnimBalls; ++i)
      g_anim.start[i] = 0; // no ball survives the window that drew it
    g_cfgWnd = nullptr;
    g_monWnd = nullptr; // the track must not repaint a destroyed window
    g_hasCards = false;
    g_hasSwitch = false;
    g_rectTracking = false;
    // Forget the scrollbar-theme memo: the next window is a different HWND and a handle can be
    // recycled, so a stale entry could suppress the theme on a new window.
    s_barThemeHwnd = nullptr;
    s_barThemeHave = false;
    return 0;

  case WM_THEMECHANGED:
  case WM_SYSCOLORCHANGE:
  {
    // The panel re-reads its palette here. The re-entrancy guard stays even though the
    // SetWindowTheme call that used to make this message arrive synchronously is gone:
    // WM_SYSCOLORCHANGE can still arrive in bursts, and the guard costs one branch.
    // Defence in depth -- this was a hard hang when it was missing.
    static bool inThemeChange = false;
    if (inThemeChange)
      return 0;
    inThemeChange = true;

    RefreshPanelTheme();

    inThemeChange = false;
    return 0;
  }
  }
  return DefWindowProcA(h, msg, wp, lp);
}

// The order the sliders appear in. Release is LAST on purpose: it is the one that reads
// best next to the response curve below it (it is the "how long does it take to settle"
// knob), and the five are otherwise unrelated, so the order is a presentation choice.
//
// THIS TABLE IS THE WHOLE DESCRIPTION OF THE PANEL'S CONTROLS. One row per setting, and the row
// carries everything about it: its name, its value and range, its end labels, its default, its
// colour, and its knob (value, range, default, name). The control ids are derived from the row's
// position, and the knob records are derived from these rows below, so there is nothing to keep in
// step by hand. To add a control: raise kNumSliders, add its TUNING constants, add one row here.
static void BuildSliderSpecs()
{
  static const SliderSpec kTable[kNumSliders] = {
      // Row order: name, value, min, max, unit, lomark, himark, default, hue -- then the knob block,
      // which these rows leave EMPTY (the model has no knob).
      //
      // The names say what turning them DOES, not what the mechanism is called.
      {"Glide length", &g_windowMs, kWindowMinMs, kWindowMaxMs, "ms", "Snappy", "Gentle",
       kDefaultWindowMs, RGB(120, 190, 255),
       false, nullptr, 0.0, 0.0, 0.0, ""},
      // How far a single, slow notch moves (in wheel deltas): the fine, touchpad-like step you get
      // when turning the wheel slowly, one notch at a time. The travel rises from here to the
      // message's own size as the wheel speeds up (see model::Travel).
      {"Slow step", &g_startDeltas, kStartMin, kStartMax, "d", "Fine", "Coarse",
       kDefaultStart, RGB(255, 190, 120),
       false, nullptr, 0.0, 0.0, 0.0, ""},
      // How much turning it takes to reach the message's full size (in wheel deltas). Larger = the
      // fine, slow region lasts longer before the roll reaches full speed.
      {"Ramp-up", &g_budgetDeltas, kBudgetMin, kBudgetMax, "d", "Quick", "Long",
       kDefaultBudget, RGB(180, 220, 140),
       false, nullptr, 0.0, 0.0, 0.0, ""},
      // How far past the wheel's OWN speed the fastest rolls may climb (1..2x). 1.0 = stop at the
      // wheel's own size; 2.0 = a full-speed roll travels twice what the wheel reported. It changes
      // nothing below full speed (see model::Travel).
      {"Top speed", &g_speedMul, kSpeedMulMin, kSpeedMulMax, "x", "Native", "Double",
       kDefaultSpeedMul, RGB(200, 170, 255),
       false, nullptr, 0.0, 0.0, 0.0, ""},
  };

  for (int i = 0; i < kNumSliders; ++i)
  {
    g_sliders[i] = kTable[i];
    g_sliders[i].sliderId = SliderIdOf(i);
    g_sliders[i].labelId = LabelIdOf(i);
    // The knob record is a VIEW of this row, so the two cannot disagree. A row without a knob gets
    // NO record at all (id 0 = "not a control"): nothing creates, lays out or handles it, so there
    // is no null value to dereference anywhere.
    if (kTable[i].hasKnob)
    {
      g_knobs[i].id = KnobIdOf(i);
      g_knobs[i].value = kTable[i].knobValue;
      g_knobs[i].min = kTable[i].knobMin;
      g_knobs[i].max = kTable[i].knobMax;
      g_knobs[i].name = kTable[i].knobName;
      g_knobPos[i] = ValueToKnob(i);
    }
    else
    {
      g_knobs[i].id = 0;
      g_knobs[i].value = nullptr;
      g_knobs[i].min = g_knobs[i].max = 0.0;
      g_knobs[i].name = "";
      g_knobPos[i] = 0;
    }
  }
}

// --- Window placement --------------------------------------------------------
//
// Whether the window is in a docker has to be asked of REAPER rather than inferred from
// its style, and the question comes up in several places (show, toggle, geometry), so it
// is asked in one place.
static bool WindowInDock(HWND h)
{
  return h && DockIsChildOfDock && DockIsChildOfDock(h, nullptr) >= 0;
}

// The window's minimum track size -- the same numbers WM_GETMINMAXINFO answers with, kept
// in one place so a remembered geometry cannot be restored smaller than the layout allows.
// LONG out-params because that is what MINMAXINFO's track sizes are.
static void MinWindowSize(const PanelMetrics &m, LONG *outW, LONG *outH)
{
  RECT r = {0, 0, MinPanelWidth(m), m.pad * 2 + m.headH * kSwitchLines + m.rowH};
  AdjustWindowRectEx(&r, PanelWindowStyle(), FALSE, WS_EX_TOOLWINDOW);
  if (outW) *outW = r.right - r.left;
  if (outH) *outH = r.bottom - r.top;
}

// Pull a remembered rectangle back onto a monitor that exists. A saved position can end
// up off-screen after the display layout changes, and a window restored there would be
// invisible. Only the position is moved; the size is kept.
static void EnsureOnScreen(RECT &r)
{
  HMONITOR mon = MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi = {0};
  mi.cbSize = sizeof(mi);
  if (!mon || !GetMonitorInfoA(mon, &mi))
    return;
  const RECT wa = mi.rcWork;
  const int w = r.right - r.left;
  const int h = r.bottom - r.top;
  const int maxX = (wa.right - w > wa.left) ? (wa.right - w) : wa.left;
  const int maxY = (wa.bottom - h > wa.top) ? (wa.bottom - h) : wa.top;
  r.left = (r.left < wa.left) ? wa.left : ((r.left > maxX) ? maxX : r.left);
  r.top = (r.top < wa.top) ? wa.top : ((r.top > maxY) ? maxY : r.top);
  r.right = r.left + w;
  r.bottom = r.top + h;
}

// First-run placement: centred on REAPER's main window (slightly above centre, leaving
// room below), the way REAPER places its own dialogs. Only used when no geometry has been
// remembered yet -- after that the user's own placement wins.
static void PlaceCenteredOnMain(HWND h)
{
  if (!g_main || !IsWindow(g_main))
    return;
  RECT w, m;
  if (!GetWindowRect(h, &w) || !GetWindowRect(g_main, &m))
    return;
  const int ww = w.right - w.left;
  const int wh = w.bottom - w.top;
  RECT r;
  r.left = m.left + ((m.right - m.left) - ww) / 2;
  r.top = m.top + ((m.bottom - m.top) - wh) / 3;
  r.right = r.left + ww;
  r.bottom = r.top + wh;
  EnsureOnScreen(r);
  SetWindowPos(h, nullptr, r.left, r.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Remember the top-left corner of a floating window. Docked geometry is skipped: REAPER
// owns that and keeps it itself. The SIZE is deliberately not captured -- the panel always
// opens at its designed size, so only the position is the user's to decide.
static void CaptureFloatGeom(HWND h)
{
  if (!h || WindowInDock(h))
    return;
  RECT r;
  if (GetWindowRect(h, &r))
  {
    g_floatPos.x = r.left;
    g_floatPos.y = r.top;
    g_floatPosValid = true;
  }
}

// The action bound to a key opens the panel the first time and closes it the second, so
// one shortcut (or one menu click) both shows and hides it.
//
// "Is it currently showing?" is not just IsWindowVisible: a window docked into a
// COLLAPSED docker is hidden by REAPER while still being the panel the user asked for.
// Treating that as "not showing" would make the first press appear to do nothing and the
// second open a second window, so a docked window counts as showing regardless.
static void ToggleConfigWindow()
{
  bool showing = false;
  if (g_cfgWnd && IsWindow(g_cfgWnd))
    showing = WindowInDock(g_cfgWnd) || IsWindowVisible(g_cfgWnd);
  if (showing)
    // Posted rather than sent: this runs from REAPER's action/accelerator processing
    // (see PanelKeyHandler), and destroying the window re-entrantly inside that call
    // would tear down a window REAPER is still dispatching to.
    PostMessageA(g_cfgWnd, WM_CLOSE, 0, 0);
  else
    ShowConfigWindow();
}

static void ShowConfigWindow()
{
  // Have the motion chart ready before the panel is shown, so it never opens blank.
  BuildAnimCurves();

  if (g_cfgWnd && IsWindow(g_cfgWnd))
  {
    // The window already exists, so it will NOT be rebuilt -- but REAPER's light/dark state may
    // have changed since it was. Re-read the theme on every open, before the window is brought
    // forward, so reopening the panel after a switch shows the new mode. Without this the panel
    // kept whatever mode it happened to be built in until REAPER announced it.
    RefreshPanelTheme();

    // The window exists but may not be on screen. REAPER's docker HIDES a docked child
    // instead of destroying it when the docker is collapsed, so "exists" is not the
    // same as "visible"; without the branches below the command would foreground a
    // hidden window and appear to do nothing. Order: docked -> activate in the docker;
    // hidden -> show; otherwise -> raise.
    bool isFloatingDocker = false;
    if (DockIsChildOfDock && DockIsChildOfDock(g_cfgWnd, &isFloatingDocker) >= 0)
    {
      if (DockWindowActivate)
        DockWindowActivate(g_cfgWnd);
      return;
    }
    // Not in the docker. REAPER's docker can drop the window back to top-level when its
    // tab is closed, so record that: the next open should come up floating too rather
    // than trying to rejoin the docker.
    if (g_dockOn)
    {
      g_dockOn = false;
      SaveSettings();
    }
    if (!IsWindowVisible(g_cfgWnd))
      ShowWindow(g_cfgWnd, SW_SHOW);
    SetForegroundWindow(g_cfgWnd);
    return;
  }
  BuildSliderSpecs();
  RegisterFaderClass();
  ResolveTheme();
  // While the controls are created and filled in they raise notifications; the guard
  // keeps those from being mistaken for user edits.
  g_cfgUpdating = true;

  const char *cls = "SmoothWheelScrollCfg";
  static bool registered = false;
  if (!registered)
  {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = CfgProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // painted in WM_PAINT with the theme brushes
    wc.lpszClassName = cls;
    RegisterClassA(&wc);
    registered = true;
  }

  RebuildThemeBrushes();

  const PanelMetrics m = PanelMetricsNow();
  const DWORD style = PanelWindowStyle();
  // Size: the panel's DESIGNED size, every time. The narrowest width that keeps the master
  // switch on one line, and the height at which nothing needs scrolling. Opening at a
  // remembered size is what used to make the window grow on each reopen (see g_floatPos);
  // the size is fixed by design, so it is simply recomputed here.
  // 1.6.1's opening size, as the user asked. The panel's own content is shorter, so the
  // lower part of the window is panel background -- what 1.6.1 looked like with less in it.
  RECT wr = {0, 0, kDesignClientW, kDesignClientH};
  // CreateWindowEx takes the size of the WHOLE window, so the caption and border are
  // added to the client size here; otherwise the bottom padding is eaten by the frame.
  AdjustWindowRectEx(&wr, style, FALSE, WS_EX_TOOLWINDOW);

  g_cfgWnd = CreateWindowExA(WS_EX_TOOLWINDOW, cls, "Smooth Wheel Scroll", style,
                             CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left,
                             wr.bottom - wr.top, g_main, nullptr, g_hInst, nullptr);
  g_monWnd = g_cfgWnd; // the monitor repaints through this while the panel is up
  if (!g_cfgWnd)
  {
    g_cfgUpdating = false;
    return;
  }

  CreatePanelChildren(g_cfgWnd);
  LayoutControls(g_cfgWnd);
  // Caption, scrollbar and all the panel's own painting follow the same palette decision, so none
  // of them can disagree; the timer and WM_THEMECHANGED re-apply it when the mode switches.
  ApplyTitleBar(g_cfgWnd, g_theme.dark);
  ApplyScrollbarTheme(g_cfgWnd, g_theme.dark);
  // ASK the layout what it really needs and use that as the size: measure, do not
  // estimate, so the panel opens at exactly the size where nothing scrolls and cannot be
  // wrong at another font, DPI or theme. This also overrides the computed starting size
  // above, so there is one authority for "the designed size".
  //
  // Floating only: when docked, REAPER owns the size and the panel scrolls instead.
  if (!g_dockOn)
    FitWindowToContent(g_cfgWnd);
  PushValuesToSliders();
  g_cfgUpdating = false; // built: notifications are user edits now

  // DOCKED: REAPER places the window -- DockWindowAddEx restores the dock from the
  // placement it keeps in its own configuration, so the plugin passes no geometry at all.
  //
  // FLOATING: the plugin places it. The remembered POSITION is restored if there is one;
  // otherwise it opens centred on REAPER's main window rather than at the OS default
  // position, which is what made it appear in the top-left corner every time. The size is
  // not restored -- it is the designed size, decided above.
  if (g_dockOn && DockWindowAddEx)
  {
    DockWindowAddEx(g_cfgWnd, "Smooth Wheel Scroll", "SmoothWheelScroll_Settings", true);
    if (DockWindowActivate)
      DockWindowActivate(g_cfgWnd);
  }
  else
  {
    if (g_floatPosValid)
    {
      // Keep the size; move only. Clamp the corner back onto a monitor that exists, since
      // a remembered spot can be off-screen after a display change.
      RECT r;
      if (GetWindowRect(g_cfgWnd, &r))
      {
        const int w = r.right - r.left, h = r.bottom - r.top;
        r.left = g_floatPos.x;
        r.top = g_floatPos.y;
        r.right = r.left + w;
        r.bottom = r.top + h;
        EnsureOnScreen(r);
        SetWindowPos(g_cfgWnd, nullptr, r.left, r.top, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
      }
    }
    else
    {
      PlaceCenteredOnMain(g_cfgWnd);
    }
    ShowWindow(g_cfgWnd, SW_SHOW);
    SetForegroundWindow(g_cfgWnd);
    // Only now start following the position, so the creation and placement above do not
    // register as user movements.
    g_rectTracking = true;
  }
}

// Extensions menu entry. REAPER calls this for each customizable menu:
//   flag 0 = the menu is being initialised (only the first time ever)
//   flag 1 = the menu is about to be shown (every time)
// We add on flag 1 rather than 0: the "Main extensions" menu in this install was
// already initialised by other extensions before we loaded, so flag 0 never
// arrives here, while flag 1 always does. A presence check avoids duplicating the
// item when the menu does get initialised by us (or is shown repeatedly).
//
// The label shown in that menu. Short on purpose: the Extensions menu is crowded and
// the full name is long. This is the ONLY place a short form is used -- the action name
// ("Smooth Wheel Scroll: settings...") keeps its full descriptive text, because that is
// what a user searches for in the Actions window.
static const char *kMenuLabel = "SmoothScroll...";

static bool MenuHasCommand(HMENU hm, int cmd)
{
  const int n = GetMenuItemCount(hm);
  for (int i = 0; i < n; ++i)
  {
    if ((int)GetMenuItemID(hm, i) == cmd)
      return true;
    HMENU sub = GetSubMenu(hm, i);
    if (sub && MenuHasCommand(sub, cmd))
      return true;
  }
  return false;
}

static void OnMenuHook(const char *menuidstr, void *menu, int /*flag*/)
{
  if (!menu || !g_cmdTune)
    return;
  // Accept any menu whose id names the extensions menu (REAPER's id is
  // "Main extensions", but match loosely so a renamed/other build still works).
  if (!StrHasI(menuidstr, "extension"))
    return;

  HMENU hm = (HMENU)menu;
  if (MenuHasCommand(hm, g_cmdTune))
    return;
  // The Extensions menu is crowded and the full plugin name is long, so the item is
  // abbreviated here. Only THIS label is short: the action name in the Actions window
  // keeps the full descriptive text ("Smooth Wheel Scroll: settings..."), which is what
  // a user searches for -- so nothing that is referenced elsewhere changes.
  //
  // It is a toggle, so the current state is marked with a check rather than spelled out
  // in the label: that keeps the text short and still shows whether the panel is open.
  const bool showing =
      (g_cfgWnd && IsWindow(g_cfgWnd)) &&
      ((DockIsChildOfDock && DockIsChildOfDock(g_cfgWnd, nullptr) >= 0) ||
       IsWindowVisible(g_cfgWnd));
  MENUITEMINFOA mi = {0};
  mi.cbSize = sizeof(mi);
  mi.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
  mi.wID = (UINT)g_cmdTune;
  mi.fState = MFS_ENABLED | (showing ? MFS_CHECKED : 0);
  mi.dwTypeData = (LPSTR)kMenuLabel;
  InsertMenuItemA(hm, GetMenuItemCount(hm), TRUE, &mi);
}

// --- Keyboard focus: hand shortcuts back to REAPER ---------------------------
//
// The panel is a real top-level window, so once it has focus the keyboard belongs to it:
// keys go to our window procedure, never to REAPER, and the shortcut that opens the panel
// stops working -- the user has to click back into REAPER first. That is the standard
// behaviour of any extension window, and REAPER provides the way out: an "accelerator"
// registration lets a plugin see the keyboard queue and CHOOSE to push a key back onto
// the main window's action table.
//
// Returning -666 does exactly that ("force it to the main window's accel table", per the
// SDK). So every key that the panel itself does not need is passed on, and the shortcut
// keeps working while the panel has focus -- press it again and the panel closes.
//
// This is the same arrangement SWS uses for its dockable windows, which is the reference
// implementation for this API.
static int PanelKeyHandler(MSG *msg, accelerator_register_t *ctx)
{
  (void)ctx;
  if (!msg || !g_cfgWnd || !IsWindow(g_cfgWnd))
    return 0; // 0 = "not my window", let REAPER do its normal thing
  // Only while the panel (or one of its children) actually has the focus; when the focus
  // is elsewhere the key is none of our business.
  const HWND focus = GetFocus();
  if (!focus || !(focus == g_cfgWnd || IsChild(g_cfgWnd, focus)))
    return 0;

  if (msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN)
  {
    char cls[64] = {0};
    const bool haveCls =
        GetFocus() && GetClassNameA(GetFocus(), cls, sizeof(cls)) != 0;

    // Keys a focused control is actually using must reach that control, so they are
    // passed to the window (-1) rather than to REAPER. Only the fader has any:
    // left/right step the value, home/end go to the ends. (The master switch is drawn by the
    // panel rather than being a control, so there is no longer a check box to hand space to.)
    // Returning 1 here would EAT the key before the control saw it, which is why this
    // returns -1 -- the control still needs its own WM_KEYDOWN.
    if (haveCls && !strcmp(cls, SWSC_FADER_CLASS))
      switch (msg->wParam)
      {
      case VK_LEFT:
      case VK_RIGHT:
      case VK_HOME:
      case VK_END:
        return -1; // to the window: the fader handles these itself
      default:
        break;
      }

    // Everything else -- in particular any key bound to a REAPER action, such as our own
    // settings toggle -- is pushed onto the main window's action table.
    return -666;
  }
  return 0;
}

static accelerator_register_t g_accel = {PanelKeyHandler, true, nullptr};
#endif // SWS_NO_SETTINGS_UI

static void RemoveAll()
{
  g_shuttingDown = true;
  StopTimer();
#ifndef SWS_NO_SETTINGS_UI
  if (g_cfgWnd && IsWindow(g_cfgWnd))
  {
    DestroyWindow(g_cfgWnd);
    g_cfgWnd = nullptr;
  }
  // Stop seeing the keyboard queue before the rest of the plugin goes away.
  plugin_register("-accelerator", (void *)&g_accel);
#endif
  if (g_msgHook)
  {
    UnhookWindowsHookEx(g_msgHook);
    g_msgHook = nullptr;
  }
  if (g_animWnd && IsWindow(g_animWnd))
  {
    DestroyWindow(g_animWnd);
    g_animWnd = nullptr;
  }
  if (g_timerPeriodRaised)
  {
    timeEndPeriod(1);
    g_timerPeriodRaised = false;
  }
}

// REAPER ticks this on the main thread; it heals a dropped message hook.
//
// It also WATCHES THE LIGHT/DARK FLAG. Relying on WM_THEMECHANGED alone left the panel half-switched:
// measured, going light -> dark arrived (the whole panel followed) but dark -> light did not, so the
// caption changed and the panel did not, until the window was reopened. REAPER does not promise to
// announce this, and a half-switched panel is worse than a late one, so the flag is simply polled.
// It is one GetPrivateProfileInt (or one get_config_var) against a value already in cache, so the
// cost is nothing; only a CHANGE does any work.
static int g_healthCounter = 0;
static void OnTimer()
{
  if (g_shuttingDown)
    return;
  if (++g_healthCounter < 30)
    return;
  g_healthCounter = 0;
  InstallHook();

  // Follow REAPER's light/dark switch, announced or not. Nothing to follow when the settings panel
  // is compiled out (--no-settings-ui): there is no panel, no palette and no RefreshPanelTheme.
#ifndef SWS_NO_SETTINGS_UI
  if (g_cfgWnd && IsWindow(g_cfgWnd))
  {
    bool dark = g_theme.dark;
    if (ReadAppDarkFlag(&dark) && dark != g_theme.dark)
      RefreshPanelTheme();
  }
#endif
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
static void OnExit()
{
#ifdef SWS_WHEEL_LOG
  WheelLogFlush(); // the DEV log: one last write, so the file holds the very latest messages
#endif
  RemoveAll();
}

// Diagnostics: what is assigned to the panel wheel Mouse Modifier contexts?
//
// These contexts ("Track control panel" / "Mixer control panel" wheel, in the Mouse
// Modifiers preferences) are where REAPER's built-in "Scroll TCP" / "Scroll MCP" live.
// The API reports the assignment as text, but the exact spelling it uses is not
// documented, so it is READ rather than assumed -- the rule that decides whether the
// plugin should take a panel wheel over depends on recognising the default.
//
// Only compiled into the debug build. Called at load, and again whenever a wheel lands on
// one of those panels, so the log ties a reading to the gesture that produced it.
static void DumpMouseModifiers(const char *why)
{
  if (!kDebugLog || !GetMouseModifier)
    return;
  static const char *kCtx[] = {"MM_CTX_TCP_MOUSEWHEEL", "MM_CTX_MCP_MOUSEWHEEL",
                               "MM_CTX_TCP_FADER_MOUSEWHEEL", "MM_CTX_MCP_FADER_MOUSEWHEEL"};
  for (size_t i = 0; i < sizeof(kCtx) / sizeof(kCtx[0]); ++i)
  {
    char report[512] = {0};
    size_t off = 0;
    for (int flag = 0; flag < 16; ++flag) // +1 shift, +2 ctrl, +4 alt, +8 win
    {
      char act[128] = {0};
      GetMouseModifier(kCtx[i], flag, act, (int)sizeof(act));
      if (!act[0])
        continue;
      off += (size_t)_snprintf(report + off, sizeof(report) - off, "[%d]\"%s\" ", flag, act);
      if (off >= sizeof(report) - 40)
        break;
    }
    Log("MMPROBE %s (%s): %s", kCtx[i], why, report);
  }
}

extern "C" __declspec(dllexport) int ReaperPluginEntry(HINSTANCE hInst, reaper_plugin_info_t *rec)
{
  if (!rec)
  {
    RemoveAll();
    if (plugin_register)
    {
      plugin_register("-timer", (void *)OnTimer);
      plugin_register("-hookcommand2", (void *)OnAction);
#ifndef SWS_NO_SETTINGS_UI
      plugin_register("-hookcustommenu", (void *)OnMenuHook);
#endif
    }
    Log("unloaded");
    return 0;
  }

  if (rec->caller_version != REAPER_PLUGIN_VERSION || !rec->GetFunc || !rec->Register)
    return 0;
  if (REAPERAPI_LoadAPI(rec->GetFunc) != 0)
    return 0;

  // The settings window uses no common controls beyond the standard button and
  // static, so nothing needs registering here (the faders are our own class and are
  // registered when the window is first built).


  g_hInst = hInst;
  g_main = rec->hwnd_main ? rec->hwnd_main : GetMainHwnd();
  if (!g_main)
    return 0;

  // Which palette the panel will start in, resolved at load time so it can be read off the log
  // without opening the panel. Only meaningful when the settings panel is compiled in: with
  // --no-settings-ui there is no panel, no palette, and no ResolveTheme to call.
#ifndef SWS_NO_SETTINGS_UI
  ResolveTheme();
#endif

  // Reported to REAPER (and shown in its Extensions list). Keep in step with the
  // version in versions/ and the GitHub release tag.
  // The DEV build names itself distinctly. The two builds are alternatives (see build.sh): if both
  // were somehow loaded they would each install a hook and animate every wheel twice, so the name
  // has to make it obvious which one REAPER is running -- both in the Extensions list and in a
  // user's screenshot, which is often all we get.
#ifdef SWS_WHEEL_LOG
  rec->Register("ext_name", (void *)"Smooth Wheel Scroll 1.7.0 DEV (wheel log)");
#else
  rec->Register("ext_name", (void *)"Smooth Wheel Scroll 1.7.0");
#endif
  rec->Register("ext_vendor", (void *)"SmoothWheelScroll");

  // Load the saved feel before anything uses it. If the master switch was off, the
  // hook still gets installed (see InstallHook) so it can be switched back on at
  // runtime, and until then every wheel is forwarded untouched.
  LoadSettings();

#ifdef SWS_WHEEL_LOG
  // The DEV wheel log's path is derived from the DLL's own location, so its writer has it before
  // any wheel can arrive. Compiled out entirely in a release build.
  WheelLogInit();
#endif

  // Windows' default timer granularity is ~15.6 ms, coarser than g_releaseMs, so
  // the release would land as a single late step and wheel-to-wheel timing would
  // be lumpy. Ask for 1 ms resolution while the plugin is active.
  if (timeBeginPeriod(1) == TIMERR_NOERROR)
    g_timerPeriodRaised = true;

  InstallHook();

  if (!rec->Register("hookcommand2", (void *)OnAction))
    return 0;
  if (!rec->Register("timer", (void *)OnTimer))
    return 0;

  // Expose the settings window as a real main-section action (so it shows in the
  // Actions list and can be bound to a shortcut) plus an Extensions-menu entry.
  // custom_action dispatches through hookcommand2, which OnAction already owns (see
  // the g_cmdTune branch there). A --no-settings-ui build registers none of it.
#ifndef SWS_NO_SETTINGS_UI
  static custom_action_register_t s_tuneAction = {
      0, "SWS_SCROLL_TUNE", "Smooth Wheel Scroll: settings...", nullptr};
  g_cmdTune = rec->Register("custom_action", &s_tuneAction);
  rec->Register("hookcustommenu", (void *)OnMenuHook);
  AddExtensionsMainMenu();
  // Hand keys the panel does not use back to REAPER, so a bound shortcut keeps working
  // while the panel has focus (see PanelKeyHandler).
  rec->Register("accelerator", (void *)&g_accel);
#endif

  rec->Register("atexit", (void *)OnExit);

  DumpMouseModifiers("load");

  Log("loaded main=%p", (void *)g_main);
  return 1;
}

// ---------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
  if (reason == DLL_PROCESS_ATTACH)
    DisableThreadLibraryCalls(hInst);
  return TRUE;
}
