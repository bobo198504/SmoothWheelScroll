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
// The motion itself (the animation) lives in anim_core.h and is pure math,
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
#include "anim_core.h"

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
// ---------------------------------------------------------------------------
// TUNING -- the feel parameters.
//
// Grouped by what a user FEELS, not by model constant, because several constants
// work together to produce one sensation: adjusting "high-speed hold" means moving
// that whole group, not one number. Each group is one slider in the settings window.
//
// The *default* values below are the tuned result. The settings window changes the
// runtime copies (g_*), and they are saved through REAPER's own extended state
// (see SaveSettings/LoadSettings), so they survive a restart. "Reset" restores the
// defaults here.
//
// The model's SHAPE is deliberately NOT here and must not become adjustable:
// kFrictionPow, kOnsetRatio and kBurstGapMs stay below with the internals, because
// they define the physics (the release curve, the onset window, and what counts as
// one roll). See AGENTS.md sections 7 and 12.
//
//   1. START      length of the FIRST notch's travel, in % of one wheel notch
//   2. ACCEL      added to each further notch's travel, same unit
//   3. RELEASE    time one notch takes to settle (the brake)
//   4. HIGH-HOLD  how strongly a fast roll is held back from running away (multiplier,
//                 1.0 = the tuned value, 0 = the hold is off entirely)
//   5. HIGH-COAST how much longer a fast roll coasts after the last notch (multiplier,
//                 1.0 = the tuned value, 0 = no extra coast)
//
// (6. RESERVED -- see the marker further down.)
// ---------------------------------------------------------------------------

// --- 1. START -----------------------------------------------------------------
static const double kDefaultStartPct = 15.0;
static const double kStartMinPct = 5.0;   // slider range; also the value clamp
static const double kStartMaxPct = 45.0;

// --- 2. ACCEL -----------------------------------------------------------------
static const double kDefaultAccelPct = 7.0;
static const double kAccelMinPct = 0.0;
static const double kAccelMaxPct = 15.0;

// --- 3. RELEASE ---------------------------------------------------------------
static const double kDefaultReleaseMs = 200.0;
static const double kReleaseMinMs = 60.0;
static const double kReleaseMaxMs = 400.0;

// --- 4. HIGH-SPEED HOLD -------------------------------------------------------
// THE CEILING: how fast a roll is allowed to get. In the curve model each notch advances how
// far up toward the ceiling the roll has climbed (asymptotically -- it approaches the ceiling
// but never passes it), so the top of the motion is a definite number and a sustained roll
// settles just under it instead of running away. The curve is only the EXPRESSION of that
// ceiling: the Hold slider sets the ceiling, the curve's bends shape how the speed gets there.
static const double kCeilingUnit = 1605.6; // hold = 1 (20% above the calibrated base)
static const double kDefaultHold = 1.0;
static const double kHoldMin = 0.0;
static const double kHoldMax = 2.0;

// --- 5. HIGH-SPEED COAST ------------------------------------------------------
// How much of the fall the Coast segment owns, and how far it drops: the larger the Coast,
// the more of the descent happens before the Release segment takes over.
static const double kDefaultCoast = 1.0;
static const double kCoastMin = 0.0;
static const double kCoastMax = 2.0;

// --- THE FIVE KNOBS -----------------------------------------------------------
// The Start knob is the rise's duration (the "Start" segment's width on the curve), and the
// other four bend their segment: 0 = straight, +0.5 bows it up (fast then slow), -0.5 bows it
// down (slow then fast). A fall reads the same bend the other way, which is the "opposite
// before and after the summit" behaviour that was asked for.
//
// EVERY KNOB'S DEFAULT IS THE MIDDLE OF ITS RANGE, so a knob opens centred on its own track and
// "up" and "down" are equally far away. Start's floor is 20 ms on request: below that the rise is
// so short that the curve begins to deform.
static const double kOnsetMinMs = 20.0;
static const double kOnsetMaxMs = 150.0;
static const double kDefaultOnsetMs = (kOnsetMinMs + kOnsetMaxMs) * 0.5;
// The HOLD knob is on a 1..2 scale: it only presses the speed into the ceiling, so it never bows
// down. Its floor is above 1 so the gentlest setting still bows the stretch a little -- at the bare
// 1 the stretch was a straight line, which read as "the knob is off" rather than "least press".
static const double kDefaultBend = 0.0;
static const double kBendMin = -0.5;
static const double kBendMax = 0.5;
static const double kHoldBendMin = 1.25;
static const double kHoldBendMax = 2.0;
static const double kDefaultHoldBend = (kHoldBendMin + kHoldBendMax) * 0.5;
// Each knob's own range, in segment order (Start, Accel, Hold, Coast, Release). Start's knob is not
// a bend at all -- it is the rise time -- so its range is the onset's. Kept here as ONE table so the
// clamp in RefreshDerived and the knob specification below cannot disagree; the Hold knob's separate
// scale is exactly why a single shared range clamped it to nonsense.
static const double kKnobMin[anim::kSegments] = {kOnsetMinMs, kBendMin, kHoldBendMin, kBendMin,
                                                 kBendMin};
static const double kKnobMax[anim::kSegments] = {kOnsetMaxMs, kBendMax, kHoldBendMax, kBendMax,
                                                 kBendMax};

// Runtime copies (what the model actually uses). The settings window edits these.
static double g_startPct = kDefaultStartPct;
static double g_accelPct = kDefaultAccelPct;
static double g_releaseMs = kDefaultReleaseMs;
static double g_hold = kDefaultHold;
static double g_coast = kDefaultCoast;
static double g_onsetMs = kDefaultOnsetMs;      // Start knob
static double g_bend[anim::kSegments] = {kDefaultBend, kDefaultBend, kDefaultHoldBend,
                                         kDefaultBend, kDefaultBend}; // [0] unused, [2] Hold
// Master switch: off = the plugin adds no animation at all and every wheel goes to
// REAPER untouched (see GetMsgProc / OnAction / InstallHook).
static bool g_glideOn = true;
// Whether the settings window should come up inside REAPER's docker. Kept as our own
// preference because REAPER also remembers a placement per ident string, and the two
// together are what caused the window to be dragged back into the dock on every open
// (so it could never be restored to a normal floating window).
static bool g_dockOn = false;
// Where the FLOATING window was last seen, remembered across opens and restarts so the
// panel reappears where the user left it. Without this the window was recreated at the OS
// default position on every open and always came up near the top-left corner.
//
// Only the POSITION is remembered, never the size. The panel has a designed size (the
// narrowest width that keeps the master switch on one line, and the height at which
// nothing needs scrolling), and it opens at that size every time. Remembering the size as
// well is what let the height creep up on every reopen: the saved rect is a WHOLE-window
// rect (caption and border included), but it was being fed back in as if it were a client
// size, so the frame was added a second time on each open -- and since the grown value was
// saved again, it compounded.
//
// Docked geometry is deliberately NOT kept here either: in a docker REAPER owns the
// position and restores it through DockWindowAddEx (reaper.ini), so duplicating that would
// only be a second, competing memory.
static POINT g_floatPos = {0, 0};
static bool g_floatPosValid = false;

// --- 6. RESERVED: transition curve (MARKED, NOT BUILT) ------------------------
// Going from the lone notch (full onset ramp) into a roll (short window) is currently
// eased with a fixed smoothstep over kOnsetFadeNotches. A different / configurable
// transition CURVE was considered and deliberately not built -- it was judged more
// complex than the benefit. The two constants below remain the working fixed
// behaviour; this is a marker for that idea, not a tunable group.
static const double kMovingOnsetMs = 30.0;
static const double kOnsetFadeNotches = 3.0;

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
static const bool kFineValues = true;   // experiment: finer VALUES only, timers unchanged
static const bool kFastTimer = false;

// Animation tick for the plain thread timer. Windows runs this at about 15.6 ms
// here regardless of the requested value.
static UINT kAnimTimerMs = 5;
// Requested period for the multimedia timer, used only when kFastTimer is set.
static UINT kFastTimerMs = 4;

// The motion is anim::Glide (see anim_core.h): a notch queues a VELOCITY, a shared
// smoothstep onset injects it so the motion winds up out of stillness, and
// power-law friction bleeds the velocity off while the position integrates it.
// Overlapping notches queue more velocity, and because that brake is non-linear
// the overlap yields more than the sum of the parts -- a fast roll builds up
// strongly. That build-up is this model's character and is deliberate.
// This function is the only bridge between the three sliders and that model.

// Brake exponent in dv/dt = -c*v^p. p<1 makes the brake build up instead of biting
// hardest at the very start, and makes v reach exactly 0 in finite time (a definite,
// soft stop with no threshold to clip the tail).
static const double kFrictionPow = 0.8;

// Onset: a notch's velocity is injected along a smoothstep over this fraction of
// RELEASE (clamped inside anim_core), so it winds up out of stillness instead of
// arriving at once. The window IS RELEASE (onsetRatio = 1.0), so ONE parameter sets
// both the rise time and the coast time -- they move together, which the user asked
// for. RELEASE therefore means "how long one notch takes, start to stop".
static const double kOnsetRatio = 1.0;

static const double kBurstGapMs = 250.0; // gap that ends a burst

// (The rest of the tunables -- groups 4, 5 and the reserved 6 -- are collected in the
//  TUNING block near the top of this file. What remains here is the model's SHAPE,
//  which must not be tuned.)

// Build the model parameters from the five user-facing values. One place, so the live
// gesture and the panel's response-curve preview cannot drift apart.
//
// Group 4 (HIGH-SPEED HOLD) sets the speed ceiling -- the top the curve is allowed to reach,
// which is what the slider's name promises. Two things move with it, and both mean "more":
//
//   * Params::speedCeiling -- the explicit limit itself;
//   * kTempoRefMs          -- a faster roll keeps a looser brake, so it also coasts further.
//
// Group 5 (HIGH-SPEED COAST): one multiplier scales the brake reduction; the dead zone and
// reference speed are shape and stay fixed.
// The curve model's parameters, from the five sliders and the five knobs.
static anim::Params ParamsFor(double startPct, double accelPct, double releaseMs,
                              double hold, double coast, double onsetMs, const double *bends)
{
  anim::Params P;
  P.startPct = startPct;
  P.accelPct = accelPct;
  P.releaseMs = releaseMs;
  P.hold = hold;
  P.coast = coast;
  P.onsetMs = onsetMs;
  P.burstGapMs = kBurstGapMs;
  P.ceilingUnit = kCeilingUnit;
  for (int i = 0; i < anim::kSegments; ++i)
    P.bend[i] = bends ? bends[i] : 0.0;
  return P;
}

static anim::Params AnimParams()
{
  return ParamsFor(g_startPct, g_accelPct, g_releaseMs, g_hold, g_coast, g_onsetMs, g_bend);
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
static const double kNotchUnits = 15.0; // 7-bit units per wheel notch
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

// Wheel messages count in WHEEL_DELTA units: one notch is 120. The glide works in 7-bit
// units (one notch is kNotchUnits = 15), so travel is converted into whole wheel deltas when
// it is handed to the mixer -- the finest step is therefore one delta = 1/120 of a notch,
// which is the resolution a real wheel device reports at.
static const double kDeltasPerNotch = 120.0;

// How much glide travel counts as ONE device notch for the mixer, in 7-bit units.
//
// The mixer's own wheel moves one track per notch (measured), and the plugin's job is to
// change only the TIMING, never the distance. The glide does not travel a whole notch: one
// input notch comes out as 1.89 units in the frozen model (the accepted baseline, see
// test/check_v1_baseline.sh), which is why a single notch is a gentle move rather than a
// jump. So that amount of travel is mapped to one full device notch (WHEEL_DELTA deltas),
// and REAPER's own handler turns it into the same one track it always did.
//
// Deliberately a named constant rather than derived from the tunable feel parameters: tuning
// Start for the view's feel must not change the mixer's speed.
static const double kMixerUnitsPerNotch = 1.89;

static const DWORD kWheelFlagMs = 250;   // wheel->action latch validity window

// ---------------------------------------------------------------------------
// Debug logging (compile-time gate)
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
// The RAW wheel readout behind the settings panel's monitor.
//
// A live view of what the plugin ACTUALLY RECEIVES, recorded before any filtering whatever. It is
// there to study real mice -- in particular free-spinning / high-resolution wheels, whose deltas are
// not whole WHEEL_DELTA multiples and are therefore handed straight to REAPER (see
// IsAnimatableWheel). Their clicks are discrete and each one is animated, by its own share of a notch, so the
// first step to supporting them is seeing what they send.
//
// NOTHING here is interpreted: `delta` is the signed value straight out of the message, and the
// gap is just wall-clock time between two messages. No scaling, no clamping, no per-notch division
// beyond the plainly-labelled "notches" figure shown next to the raw value.
struct WheelMonitor
{
  static const int kHist = 8; // how many recent raw deltas to keep
  bool seen = false;
  int delta = 0;              // RAW signed delta, untouched
  DWORD gapMs = 0;            // ms since the previous wheel message (0 on the first)
  DWORD lastTick = 0;
  bool standard = false;      // a WHOLE notch (a plain notched mouse) rather than a fraction of one
  bool touch = false;         // tagged touch/pen-injected
  bool shift = false, ctrl = false, alt = false;
  // The message's raw "extra info" word. Windows tags injected messages here (the MI_WP_SIGNATURE
  // family, e.g. touch/pen), and pointers add their own bits, so this single value is the most
  // direct evidence of WHAT KIND OF DEVICE produced the wheel. Kept raw and shown in hex: a
  // decoded flag would be a guess, and the point of the monitor is to see the truth.
  LPARAM extraInfo = 0;
  // WHERE it happened, kept as the handle and point rather than as strings: resolving those means
  // asking REAPER (GetThingFromPoint) and Windows (GetClassNameA), which is far too heavy to do once
  // per wheel message. They are resolved at paint time instead -- see DrawMonitor.
  HWND under = nullptr;
  POINT pt = {0, 0};
  int hist[kHist] = {0};      // recent raw deltas, newest last
  int histN = 0;
  DWORD total = 0, totalStd = 0;
  // Delta ACCUMULATED since the block was last reset. This is how a device's "delta per revolution"
  // is measured: turn the wheel exactly one turn and read the sum. It is also the number that says
  // whether devices agree on it, which decides whether the plugin needs a per-device figure at all.
  long sumDelta = 0;          // signed; click the block to reset it
  int minStep = 0, maxStep = 0; // smallest / largest |delta| seen since reset (the device's step)
  bool haveStep = false;
  // THE DEVICE IS READ FROM THE NUMBERS (user's rule, 2026-09-16): a plain notched mouse reports
  // whole WHEEL_DELTA multiples -- every click is 120 -- while a free-spinning wheel keeps sending
  // the same value SHORT of WHEEL_DELTA. So runs of each kind are counted and the longer run names
  // the device. Nothing about the device is queried; the deltas themselves decide.
  int fineRun = 0;            // consecutive messages whose |delta| is below one notch
  int wholeRun = 0;           // consecutive messages whose |delta| is a whole notch
};
static WheelMonitor g_mon;
// Where the monitor block is drawn. Declared here rather than with the other panel rectangles
// because the message hook invalidates it, and the hook is defined before the settings window.
// Only meaningful when the panel is compiled in; with --no-settings-ui nothing sets or reads it.
static RECT g_monRect = {0, 0, 0, 0};
static bool g_hasMonitor = false;
// The panel window, for the hook's repaint. Declared here (with a forward name) rather than using
// g_cfgWnd, which lives with the settings-window code further down.
static HWND g_monRepaint = nullptr;

// ---------------------------------------------------------------------------
// Classification: which actions are view scroll / zoom, and how to drive them
// ---------------------------------------------------------------------------
enum Drive
{
  DRIVE_NONE = 0,
  DRIVE_REPLAY,   // re-invoke REAPER's own action with fractional relative values
  DRIVE_MCP_WHEEL // hand the mixer a small wheel message and let REAPER's own code move it
};

// How an action's travel has to be handed over. Most receivers take a continuous
// stream of small relative values and let it coast to a stop. Two axes of the MIDI
// editor cannot: they are discrete receivers, so a stream or a coast has nowhere to
// land and just reads as a lurch. Naming the three cases keeps the distinction in one
// place instead of scattered flags.
enum class Delivery
{
  kStream,    // continuous relative values, with the normal glide (everything else)
  kStepUnits, // whole 7-bit units over time -- a receiver that steps in whole units and
              // loses anything smaller (MIDI editor vertical scroll; the arrange's own
              // vertical scroll and zoom)
  kImmediate  // the whole notch at once, no glide -- MIDI editor vertical ZOOM,
              // a fixed pixel scale, so coasting has nowhere to land
};

// How a given command must be delivered, in ONE place. The scrollbars and the track panel
// drive these commands directly instead of going through kActions, so keeping the rule here
// means the table and those call sites cannot disagree about it.
//
// The main section's vertical axis (scroll and zoom, both directions) takes whole units: its
// receiver loses anything smaller, so a fine stream leaves it dead or jittery. The horizontal
// axis is fine-grained and keeps the full stream.
static Delivery DeliveryForCommand(int section, int command)
{
  if (section != 0 && section != 100)
    return Delivery::kStream; // only the main section's vertical axis is a discrete receiver
  switch (command)
  {
  case 989:  // View: Scroll vertically
  case 978:  // View: Scroll vertically reversed
  case 1000: // View: Zoom vertically
  case 1001: // View: Zoom vertically reversed
    return Delivery::kStepUnits;
  default:
    return Delivery::kStream;
  }
}

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
  g_startPct = Clamp(g_startPct, kStartMinPct, kStartMaxPct);
  g_accelPct = Clamp(g_accelPct, kAccelMinPct, kAccelMaxPct);
  g_releaseMs = Clamp(g_releaseMs, kReleaseMinMs, kReleaseMaxMs);
  g_hold = Clamp(g_hold, kHoldMin, kHoldMax);
  g_coast = Clamp(g_coast, kCoastMin, kCoastMax);
  // The knobs go through the same single source of clamping, so a bad value in the store
  // can never reach the model.
  g_onsetMs = Clamp(g_onsetMs, kOnsetMinMs, kOnsetMaxMs);
  // ONE RANGE PER KNOB. The Hold knob is on its own 1.25..2 scale, not the bend scale, so it must
  // NOT be clamped by kBendMin/kBendMax: doing that squeezed it to +0.5 on every load (and every
  // refresh), so the knob's stored value was destroyed and its stretch lost the bow it is for.
  // The table is the same one the sliders below are built from, so the two cannot drift apart.
  for (int i = 0; i < anim::kSegments; ++i)
    g_bend[i] = Clamp(g_bend[i], kKnobMin[i], kKnobMax[i]);
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
  _snprintf(buf, sizeof(buf), "%.6f", g_startPct);
  SetExtState(kStateSection, "start", buf, true);
  _snprintf(buf, sizeof(buf), "%.6f", g_accelPct);
  SetExtState(kStateSection, "accel", buf, true);
  _snprintf(buf, sizeof(buf), "%.6f", g_releaseMs);
  SetExtState(kStateSection, "release", buf, true);
  _snprintf(buf, sizeof(buf), "%.6f", g_hold);
  SetExtState(kStateSection, "hold", buf, true);
  _snprintf(buf, sizeof(buf), "%.6f", g_coast);
  SetExtState(kStateSection, "coast", buf, true);
  // The five knobs, one key each.
  _snprintf(buf, sizeof(buf), "%.6f", g_onsetMs);
  SetExtState(kStateSection, "rise", buf, true);
  static const char *kBendKey[anim::kSegments] = {"bend0", "bend1", "bend2", "bend3", "bend4"};
  for (int i = 0; i < anim::kSegments; ++i)
  {
    _snprintf(buf, sizeof(buf), "%.6f", g_bend[i]);
    SetExtState(kStateSection, kBendKey[i], buf, true);
  }
  SetExtState(kStateSection, "glide", g_glideOn ? "1" : "0", true);
  SetExtState(kStateSection, "dock", g_dockOn ? "1" : "0", true);
  // Floating window POSITION as "x y". Written only once the window has actually been
  // placed, so an install that has never moved it does not pin a position it never had.
  if (g_floatPosValid)
  {
    _snprintf(buf, sizeof(buf), "%ld %ld", (long)g_floatPos.x, (long)g_floatPos.y);
    SetExtState(kStateSection, "pos", buf, true);
  }
  // Clear the key an earlier build used for the whole window rect. It is no longer read
  // (the size is not remembered any more), and leaving a stale value behind would be a
  // confusing thing to find in the ini later.
  SetExtState(kStateSection, "win", "", true);
  Log("settings saved: start=%.2f accel=%.2f release=%.1f hold=%.2f coast=%.2f glide=%d dock=%d",
      g_startPct, g_accelPct, g_releaseMs, g_hold, g_coast, g_glideOn ? 1 : 0,
      g_dockOn ? 1 : 0);
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
  g_startPct = kDefaultStartPct;
  g_accelPct = kDefaultAccelPct;
  g_releaseMs = kDefaultReleaseMs;
  g_hold = kDefaultHold;
  g_coast = kDefaultCoast;
  g_onsetMs = kDefaultOnsetMs;
  // Per-KNOB defaults, not kDefaultBend for all of them: the Hold knob lives on its own 1.25..2
  // scale, so blanket-assigning the bend default would drop it to 0 (a straight Hold stretch) on
  // every load -- the bow the knob is supposed to have would vanish on restart.
  static const double kKnobDefaults[anim::kSegments] = {
      kDefaultBend, kDefaultBend, kDefaultHoldBend, kDefaultBend, kDefaultBend};
  for (int i = 0; i < anim::kSegments; ++i)
    g_bend[i] = kKnobDefaults[i];
  g_glideOn = true;

  double v = 0.0;
  if (LoadDouble("start", &v)) g_startPct = v;
  if (LoadDouble("accel", &v)) g_accelPct = v;
  if (LoadDouble("release", &v)) g_releaseMs = v;
  if (LoadDouble("hold", &v)) g_hold = v;
  if (LoadDouble("coast", &v)) g_coast = v;
  if (LoadDouble("rise", &v)) g_onsetMs = v;
  static const char *kBendKey[anim::kSegments] = {"bend0", "bend1", "bend2", "bend3", "bend4"};
  for (int i = 0; i < anim::kSegments; ++i)
    if (LoadDouble(kBendKey[i], &v))
      g_bend[i] = v;
  if (GetExtState)
  {
    const char *g = GetExtState(kStateSection, "glide");
    if (g && *g)
      g_glideOn = (g[0] != '0');
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
  Log("settings loaded: start=%.2f accel=%.2f release=%.1f hold=%.2f coast=%.2f glide=%d dock=%d",
      g_startPct, g_accelPct, g_releaseMs, g_hold, g_coast, g_glideOn ? 1 : 0,
      g_dockOn ? 1 : 0);
}

// (There is no "reset everything" function any more: a RESET button was replaced by
//  double-clicking a fader, which restores just that one parameter -- the gesture
//  REAPER uses on its own faders and knobs. Restoring everything at once has no
//  natural gesture and no longer has a control, so the code is gone rather than left
//  unreachable.)

struct ActionSpec
{
  int section;              // KbdSectionInfo::uniqueID (0 main; 100 = main alt)
  int command;
  bool horizontal;          // which axis' glide the action belongs to
  Drive drive;
  // True for a REAPER action whose name mentions "mousewheel". Those are the
  // actions REAPER lets a relative amount drive (mousewheel, MIDI CC, OSC), so
  // they are admitted without the wheel latch (see OnAction).
  bool relativeAction = false;
  Delivery delivery = Delivery::kStream;
};

// Every view scroll / zoom here is delivered by REAPER'S OWN ACTION: the plugin
// only feeds that action an animated relative value and never touches the view
// itself. See AGENTS.md -- that is the whole contract.
//
//   * real scrolling / zooming -> DRIVE_REPLAY. The action takes a relative value
//     continuously (a wheel notch is 15 units), so the glide's fractional travel
//     is handed back in small pieces and the action does the moving.
//   * "one page" scroll actions are deliberately NOT listed: each call jumps a
//     whole page and ignores the relative value, so animating one would either
//     mean driving the view ourselves (forbidden) or replaying it many times
//     (many page jumps). They are inherently discrete, so they pass through
//     untouched and stay entirely REAPER's.
static const ActionSpec kActions[] = {
    // ---- main section: scrolling ----
    // The VERTICAL actions are discrete receivers: they take whole units (the same treatment
    // the MIDI editor's vertical axis gets). A value below one unit is simply lost by the
    // receiver, which is why a small notch would not move and the finest steps would jitter.
    // This is NOT a sensitivity control: removing it was tried and made tiny moves jitter,
    // with the feel otherwise unchanged, so it is kept. The HORIZONTAL axis is fine-grained
    // and keeps the full stream.
    {0, 988, true, DRIVE_REPLAY},  // View: Scroll horizontally
    {0, 977, true, DRIVE_REPLAY},  // View: Scroll horizontally reversed
    {0, 989, false, DRIVE_REPLAY, false, Delivery::kStepUnits},  // View: Scroll vertically
    {0, 978, false, DRIVE_REPLAY, false, Delivery::kStepUnits},  // Scroll vertically reversed
    // ---- main section: zooming ----
    {0, 990, true, DRIVE_REPLAY},   // View: Zoom horizontally
    {0, 979, true, DRIVE_REPLAY},   // View: Zoom horizontally reversed
    {0, 1000, false, DRIVE_REPLAY, false, Delivery::kStepUnits}, // View: Zoom vertically
    {0, 1001, false, DRIVE_REPLAY, false, Delivery::kStepUnits}, // Zoom vertically reversed
    // ---- MIDI editor: scroll + zoom, all through the section's own actions
    //      (section->onAction can execute these with a relative value).
    //      The vertical axis is a discrete receiver (see Delivery), so its two
    //      actions name the delivery they need; the horizontal axis is continuous.
    {32060, 40430, false, DRIVE_REPLAY, false, Delivery::kImmediate}, // Zoom vertically
    {32060, 40431, true, DRIVE_REPLAY},  // View: Zoom horizontally
    {32060, 40432, false, DRIVE_REPLAY, false, Delivery::kStepUnits}, // Scroll vertically
    {32060, 40433, true, DRIVE_REPLAY},  // View: Scroll horizontally
    {32060, 40660, true, DRIVE_REPLAY},  // View: Scroll horizontally reversed
    {32060, 40661, false, DRIVE_REPLAY, false, Delivery::kStepUnits}, // v-scroll rev
    {32060, 40662, true, DRIVE_REPLAY},  // View: Zoom horizontally reversed
    {32060, 40663, false, DRIVE_REPLAY, false, Delivery::kImmediate}, // v-zoom rev
};

static bool LookupAction(int section, int command, ActionSpec &out)
{
  for (size_t i = 0; i < sizeof(kActions) / sizeof(kActions[0]); ++i)
  {
    const ActionSpec &a = kActions[i];
    // Section 100 (main alt-recording) shares the main action table.
    const bool secMatch = (a.section == section) ||
                          (a.section == 0 && section == 100);
    if (secMatch && a.command == command)
    {
      out = a;
      return true;
    }
  }
  return false;
}

// Case-insensitive substring test. REAPER names the same idea both "mousewheel"
// and "Mousewheel", so any marker must be matched without regard to case.
static bool StrHasI(const char *s, const char *sub)
{
  if (!s || !sub)
    return false;
  const size_t n = strlen(sub);
  for (; *s; ++s)
  {
    size_t i = 0;
    while (i < n && s[i] &&
           tolower((unsigned char)s[i]) == tolower((unsigned char)sub[i]))
      ++i;
    if (i == n)
      return true;
  }
  return false;
}

// Name-based classification, used when the explicit id table has no entry.
//
// Rule (as requested): any action whose name is a View and mentions "mousewheel"
// is bound -- in the main section and in the MIDI editor alike. Anything carrying
// that marker can receive wheel information, so the plugin's animated relative
// value can be handed straight to it. Matching is case-insensitive because
// REAPER writes both "mousewheel" and "Mousewheel", and it keys on a single word
// rather than a full "(MIDI CC relative/mousewheel)" spelling so a reworded
// marker still binds.
//
// The action NAME is what we key on, never a key binding, so re-bound and custom
// actions follow automatically.
//
// A second, narrower family is also accepted as a fallback: a plain action whose
// name mentions the wheel and is a Zoom or a Scroll. Three kinds are deliberately
// excluded from THAT family, because each call jumps a whole step regardless of
// the value handed in, so feeding it many small values would multiply the jump:
//   - "snap to theme-defined sizes": each call snaps one size step;
//   - track/envelope "height" adjusters: likewise step-based.
//
// "one page" is excluded from BOTH families -- see the guard in the body. Its name
// carries the mousewheel marker like the relative actions do, but that marker only
// says what may DRIVE it; the page actions still jump a whole page per call and
// ignore the value handed in.
static bool ClassifyByName(KbdSectionInfo *sec, int command, ActionSpec &out)
{
  if (!sec || !kbd_getTextFromCmd)
    return false;
  // Only sections whose relative replay path we know how to dispatch (main, and
  // the MIDI editor -- the only other section with wheel-driven view actions).
  const int sid = sec->uniqueID;
  if (sid != 0 && sid != 100 && sid != 32060)
    return false;

  const char *nm = kbd_getTextFromCmd(command, sec);
  if (!nm || !*nm)
    return false;
  if (!strstr(nm, "View"))
    return false;

  // "one page" is a fixed-step action no matter what else its name says: one call
  // scrolls a whole page and the value handed in is ignored. The reported runaway is
  // exactly this -- "View: Scroll view vertically one page (MIDI CC relative/mousewheel)"
  // carries the mousewheel marker, so it was taken for a relative action and fed the
  // delivered stream (one notch arrives as many small increments). Each increment
  // scrolled a whole page, so a single notch crossed ~100 tracks.
  //
  // The marker in that name says only what is ALLOWED to drive the action; it does not
  // promise the action consumes a relative amount. So this is excluded from BOTH families
  // and left entirely to REAPER, which gives the intended native behaviour: one notch,
  // one page. Nothing else about the name-based rule changes.
  if (strstr(nm, "one page"))
    return false;

  const bool relativeFamily = StrHasI(nm, "mousewheel");
  if (!relativeFamily)
  {
    if (!strstr(nm, "wheel"))
      return false;
    if (!strstr(nm, "Zoom") && !strstr(nm, "Scroll"))
      return false;
    if (strstr(nm, "snap to theme") || strstr(nm, "height") ||
        strstr(nm, "Modify"))
      return false;
  }

  out.section = sid;
  out.command = command;
  out.horizontal = strstr(nm, "horizontally") != nullptr;
  out.drive = DRIVE_REPLAY; // reproduce exactly what the action does
  out.relativeAction = relativeFamily;
  // The MIDI editor's vertical axis is a discrete receiver. Keyed on section + axis +
  // name (never a specific id), so a re-worded action still matches.
  if ((sid == 32060) && !out.horizontal)
  {
    if (strstr(nm, "Scroll"))
      out.delivery = Delivery::kStepUnits;  // moves in whole note rows
    else if (strstr(nm, "Zoom"))
      out.delivery = Delivery::kImmediate;  // a fixed pixel scale
  }
  // The main section's vertical axis keeps the whole-unit delivery (see DeliveryForCommand).
  if ((sid == 0 || sid == 100) && !out.horizontal)
    out.delivery = DeliveryForCommand(sid, command);
  return true;
}

// ---------------------------------------------------------------------------
// Integrator -- one per axis. Holds only the delivery plumbing (which drive,
// which action, which window); the motion itself lives in anim::Glide.
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
  // Identity of the current operation, kept separately from the glide's activity
  // so the accel streak survives the short gaps between notches.
  Drive lastDrive = DRIVE_NONE;
  int lastSection = 0, lastCommand = 0;
  anim::Glide glide;         // the actual motion
};

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

// units: wheel units (fractional ok). The actual motion (streak/accel/rhythm and
// the notch curve) is entirely anim::Glide's; this only names the operation and
// hands it the notch's travel in the drive's own unit.
static void Kick(Integrator &g, Drive drive, int section, int command, double wheelSign,
                 double units, HWND replayHwnd = nullptr,
                 Delivery delivery = Delivery::kStream)
{
  if (units <= 0.0)
    return;

  const bool wasActive = g.glide.Active();
  // Same operation = same drive/action/target. Deliberately NOT tied to the
  // glide being active: the accel streak must survive the short gaps between
  // notches, so a roll that briefly settles still keeps building.
  const bool sameOp = g.lastDrive == drive && g.lastSection == section &&
                      g.lastCommand == command;

  g.drive = drive;
  g.section = section;
  g.command = command;
  g.replayHwnd = replayHwnd;
  g.delivery = delivery;
  g.lastDrive = drive;
  g.lastSection = section;
  g.lastCommand = command;

  const double sign = wheelSign;
  const double unit = OneNotchUnit();
  // Identity names the operation inside the glide, so a different action (or a
  // direction reversal, which the glide also checks) restarts the velocity and
  // the streak instead of inheriting the previous burst.
  const int identity = section * 100000 + command;

  if (!sameOp)
  {
    // New operation: drop any half-finished carry. The glide is fully reset, so
    // no motion from the previous operation leaks into this one.
    g.accum = 0.0;
    g.glide.Reset();
  }
  if (!sameOp || !wasActive)
    g.last = Now(); // starting from rest: the tick clock restarts here
  if (!wasActive)
    g.sentThisBurst = false; // a gesture beginning from rest can be topped up at its end

  g.glide.Kick(units, sign, identity, Now(), AnimParams());

  if (kDebugLog)
    Log("kick drive=%d cmd=%d units=%.2f streak=%d D=%.3f",
        (int)drive, command, units, g.glide.Streak(),
        unit * units * anim::NotchFrac(AnimParams(), g.glide.Streak()));

  // Release shorter than the timer can express: deliver the notch synchronously
  // (the animation would only be one frame anyway) and skip the glide.
  // Release shorter than the timer can express, or a receiver where a coast has
  // nowhere to land: deliver the notch at once and skip the glide.
  if (g.delivery == Delivery::kImmediate || g_releaseMs <= kSyncReleaseMs)
  {
    ApplyTravelNow(g, sign * unit * units * anim::NotchFrac(AnimParams(), g.glide.Streak()));
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
  if (!g.glide.Active() || dt <= 0.0)
    return;
  const double step = g.glide.Tick(dt, AnimParams());
  // Defensive: a non-finite step would poison every receiver. Drop the glide
  // rather than forward it.
  if (!(step > -1e30 && step < 1e30))
  {
    g.glide.Reset();
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
    if (!g.glide.Active())
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
    const bool wasActive = g.glide.Active();
    TickIntegrator(g, dt);

    // A whole-unit receiver (kStepUnits) moves in whole units, and a gesture whose travel
    // never reaches one would be delivered nothing at all -- the axis simply would not
    // respond. When such a gesture ends, send one unit in the direction it was going, which
    // is the smallest move this receiver can make. A gesture that did reach a unit keeps its
    // own remainder for the next one, so ordinary use is unchanged.
    if (wasActive && !g.glide.Active() && g.delivery == Delivery::kStepUnits &&
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
  if (!g_vert.glide.Active() && !g_horz.glide.Active())
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

  // Glide off: leave the action entirely to REAPER (native = no slide, and the
  // original wheel parameter is what reaches it, untouched).
  if (!g_glideOn)
  {
    if (kDebugLog)
      Log("glide off: native pass-through sec=%d cmd=%d", sec->uniqueID, command);
    return false;
  }

  if (kDebugLog)
    Log("MATCH sec=%d cmd=%d relmode=%d val=%d hwnd=%p -> drive=%d delivery=%d",
        sec->uniqueID, command, relmode, val, (void *)hwnd, (int)spec.drive,
        (int)spec.delivery);

  // Decode the relative wheel value: 1..63 = +, 65..127 = -.
  int raw = val & 0x7f;
  if (raw == 0)
    return false;
  const double wheelSign = (raw > 63) ? -1.0 : 1.0;
  const double units = (raw > 63) ? (128 - raw) : raw;
  const double notches = units / kNotchUnits;

  Integrator &g = spec.horizontal ? g_horz : g_vert;
  Kick(g, spec.drive, spec.section, spec.command, wheelSign, notches, hwnd,
       spec.delivery);
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

// WHICH WHEELS THIS PLUGIN ANIMATES.
//
// Any real wheel message with a nonzero delta, except one injected by touch or a pen (Windows tags
// those in the message's extra info). That now INCLUDES a free-spinning / high-resolution wheel,
// which reports each click as a fraction of WHEEL_DELTA (15, 20, 25 ...) rather than a whole 120.
//
// The test used to require a whole WHEEL_DELTA multiple, which shut such wheels out entirely. That
// was the wrong reading of them: they are not touchpads sending a continuous trickle -- their clicks
// are discrete, and each one has to move something or the user simply does not feel that click. So
// they are animated like any other wheel, and the model is told how much of a notch each message
// carries (see anim::Glide::Kick), which is what makes a small click move a small way.
//
// Touch and pen input stays out: it is already a fine, continuous stream from the OS, so animating it
// again would be smoothing something that is not stepping in the first place.
static const LPARAM kTouchSignature = 0xFF515700; // MI_WP_SIGNATURE
static const LPARAM kTouchMask = 0xFFFFFF00;

static bool IsTouchInjected()
{
  return (GetMessageExtraInfo() & kTouchMask) == kTouchSignature;
}

static bool IsAnimatableWheel(int delta)
{
  if (delta == 0)
    return false;
  if (IsTouchInjected())
    return false;
  return true;
}

// Whether this delta is a whole number of notches, i.e. a plain notched mouse's message. Used only
// for the wording of the settings monitor's readout -- both kinds are animated now.
static bool IsWholeNotch(int delta)
{
  return delta != 0 && (delta % WHEEL_DELTA) == 0;
}

// Record one raw wheel for the settings monitor. Called from the message hook BEFORE any decision
// is taken, so what it shows is what arrived -- including the wheels this plugin will pass straight
// through.
//
// THE HOOK MUST STAY CHEAP. This runs inside the message hook, once per wheel message, and a
// free-spinning mouse sends them dozens of times a second: measured, an earlier version called
// REAPER's GetThingFromPoint here, which walks the window tree, and the panel juddered. So only the
// values that are free or nearly free are taken here (the delta already in hand, the clock, the
// modifiers, the window handle). The window class and REAPER's hit-test string are fetched later, in
// DrawMonitor, when the block is actually painted -- and that paint is already coalesced by the
// message queue, so a burst of wheels costs one lookup, not one per message.
static void MonitorRawWheel(HWND under, int delta, bool standard, bool touch)
{
  const DWORD now = GetTickCount();
  g_mon.gapMs = g_mon.seen ? (now - g_mon.lastTick) : 0;
  g_mon.lastTick = now;
  g_mon.seen = true;
  g_mon.delta = delta;
  g_mon.standard = standard;
  g_mon.touch = touch;
  g_mon.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  g_mon.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  g_mon.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
  g_mon.under = under;
  g_mon.pt = g_wheelPt;
  // Cheap (a thread-local read), and it is the one value that can tell devices apart -- see the
  // field's comment. Taken here so it is the extra info of THIS message, not of a later one.
  g_mon.extraInfo = GetMessageExtraInfo();
  for (int i = 0; i + 1 < WheelMonitor::kHist; ++i)
    g_mon.hist[i] = g_mon.hist[i + 1];
  g_mon.hist[WheelMonitor::kHist - 1] = delta;
  if (g_mon.histN < WheelMonitor::kHist)
    ++g_mon.histN;
  ++g_mon.total;
  if (standard)
    ++g_mon.totalStd;
  g_mon.sumDelta += delta;
  {
    const int mag = (delta < 0) ? -delta : delta;
    if (!g_mon.haveStep) { g_mon.minStep = g_mon.maxStep = mag; g_mon.haveStep = true; }
    else { if (mag < g_mon.minStep) g_mon.minStep = mag; if (mag > g_mon.maxStep) g_mon.maxStep = mag; }
    // Run counters: a whole notch or a fraction of one. See the fields' comment.
    if (mag > 0 && (mag % WHEEL_DELTA) == 0) { ++g_mon.wholeRun; g_mon.fineRun = 0; }
    else if (mag > 0)                        { ++g_mon.fineRun;  g_mon.wholeRun = 0; }
  }

  // Repaint just the monitor block, if the panel is up. InvalidateRect only marks the region; the
  // message loop coalesces repeated marks, so a burst of wheel messages costs one repaint.
  if (g_monRepaint && g_hasMonitor && IsWindow(g_monRepaint))
    InvalidateRect(g_monRepaint, &g_monRect, FALSE);
}

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

      // THE MONITOR IS FED HERE, before every decision below. It has to be: the wheels it is most
      // wanted for -- free-spinning and high-resolution ones -- are the ones the very next test
      // sends straight back to REAPER, so anything recorded after it would never see them. See
      // MonitorRawWheel.
#ifndef SWS_NO_SETTINGS_UI
      MonitorRawWheel(under, delta, IsWholeNotch(delta), IsTouchInjected());
#endif

      // What REAPER says is under the cursor. This is the official hit-test and the
      // value control, the window class plus this hit-test decide (see above).

      // Standard mouse wheel only; anything else (high-resolution wheel,
      // touchpad, touch/pen) is left entirely to REAPER, latch cleared so the
      // action path stays out of it too.
      if (!IsAnimatableWheel(delta))
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
          // The vertical bar's Alt gestures are the vertical ZOOM actions, which take whole
          // units; everything else here is a continuous scroll/zoom and keeps the fine stream.
          Kick(g_vert, DRIVE_REPLAY, 0, cmd, actSign, notches, under,
               DeliveryForCommand(0, cmd));
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
          Kick(g_horz, DRIVE_MCP_WHEEL, 0, 0, actSign, notches, mcp);
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
            // "Scroll TCP" scrolls (continuous); "Adjust vertical zoom" is the zoom action,
            // which takes whole units.
            Kick(g_vert, DRIVE_REPLAY, 0, cmd, actSign, notches, surface,
                 DeliveryForCommand(0, cmd));
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
          Kick(g_vert, DRIVE_REPLAY, 32060, 40432, actSign, notches, editor,
               Delivery::kStepUnits);
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
static const char *kEnableText =
    "Enable smooth scrolling   (off = REAPER's native wheel)";

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
static const int kNumSliders = 5;

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
  // rise's duration (ms) and the other four are their curve segment's bend.
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
static RECT g_switchRect = {0, 0, 0, 0}; // client coordinates, already scrolled
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

static void ApplyFontToChildren(HWND parent)
{
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
    const double step = (s.max - s.min) / 1000.0;
    const int dec = (step >= 0.1) ? 1 : 2;
    // The knob's value belongs in the same title, so a row reads as one control: its
    // setting and the curve property behind it. The Start knob is a time, the other four
    // are bends (shown signed, since 0 is meaningful and the sign is the direction).
    if (i == 0)
      _snprintf(buf, sizeof(buf), "%s: %.*f %s   rise %.0f ms", s.name, dec, *s.value,
                s.unit, g_onsetMs);
    else
      _snprintf(buf, sizeof(buf), "%s: %.*f %s   bend %+.2f", s.name, dec, *s.value, s.unit,
                g_bend[i]);
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
// The response-curve block: where it sits (parent client coords, already scrolled) and
// whether to paint it. The curve itself is computed from the model in PaintCurve, so it
// always matches the current parameter values.
static RECT g_curveRect = {0, 0, 0, 0};
static bool g_hasCurve = false;

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

// The response curve block: a schematic of what the five settings do, speed against time.
// Each setting owns one segment of the silhouette -- see BuildCurve for the mapping.
//
// The axes carry REAL numbers (the model's own peak speed and settle time) while the drawn
// shape is schematic, so the labels stay meaningful. Gridlines and axis labels are drawn in
// PaintCurve; this block only holds the shared constants and the plot geometry.
// Where the curve's peak is placed vertically: 0.85 of the box, so the top is never touched.
static const double kWavePeakFill = 0.98;

// The time window of the most recently built curve (what the X axis spans) and its Y span,
// in real units, so PaintCurve can place ticks at round VALUES rather than at fractions of
// the box. A tick at "every 200 ms" slides and re-counts as the axis rescales; a tick at
// "every fifth of the width" sits still whatever the settings do, which tells the reader
// nothing.
static double g_curveTFromMs = 0.0; // X axis start, milliseconds
static double g_curveTEndMs = 0.0;  // X axis end, milliseconds
static double g_curveAxisY = 0.0;   // Y axis full span, speed units
// The segment JOINTS (normalised x, 0..1) of the most recently built curve, so PaintCurve can
// draw each parameter's stretch in that parameter's own colour. The curves are the breakpoints
// between: 0->Start, 1->Accel, 2->Hold, 3->Coast, 4->Release. -1 means that segment is absent
// (a zero-length coast), so the next one takes over.
static double g_curveJoint[6] = {0, 0, 0, 0, 0, 1};
static int g_curveJoints = 0;

// The finest "nice" step (1, 2 or 5 times a power of ten) whose tick count still fits
// within `maxTicks`, so asking for more ticks actually produces more of them.
//
// Simply dividing by a count does not: the 1/2/5 rounding snaps the step UP, so a larger
// request can land on the same step (and therefore the same, smaller, number of lines).
// Walking the candidates and taking the first that fits always gives the densest readable
// grid.
static double NiceStep(double range, int maxTicks)
{
  if (range <= 0.0 || maxTicks <= 0)
    return 1.0;
  const double mag = pow(10.0, floor(log10(range)));
  const double mults[3] = {1.0, 2.0, 5.0};
  for (int decade = -1; decade <= 2; ++decade)
  {
    const double base = mag * pow(10.0, decade);
    for (int i = 0; i < 3; ++i)
    {
      const double step = mults[i] * base;
      if (step <= 0.0)
        continue;
      // Number of interior ticks this step would produce.
      if ((int)floor((range - 1e-9) / step) <= maxTicks && step >= range / (maxTicks + 1) * 0.999)
        return step;
    }
  }
  return range / (maxTicks + 1.0); // fallback: an even split
}

// A small version of the UI font for the tick labels.
static HFONT g_curveFont = nullptr;
static HFONT CurveFont()
{
  if (!g_curveFont)
  {
    NONCLIENTMETRICSA ncm = {0};
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
    {
      LONG h = ncm.lfMessageFont.lfHeight;
      // lfHeight is normally negative (point height); scale whichever sign it is. The curve's
      // tick numbers are deliberately SMALL -- they only need to be legible, not prominent --
      // so they are shrunk well below the dialog font. The floor stops them vanishing at a
      // larger base size.
      const LONG scaled = (h < 0) ? -(LONG)(-h * 0.68) : (LONG)(h * 0.68);
      ncm.lfMessageFont.lfHeight = (scaled > -8 && scaled < 0) ? -8 : scaled;
      g_curveFont = CreateFontIndirectA(&ncm.lfMessageFont);
    }
    if (!g_curveFont)
      g_curveFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
  }
  return g_curveFont;
}

static const int kCurveBurstNotches = 10;
static const double kCurveBurstGapMs = 30.0;

// Run the model's own burst once, purely to obtain the two numbers the axes show: how fast
// the peak gets, and how long the motion lasts. The SHAPE drawn is not this trace (see
// BuildCurve); these anchors keep the tick labels truthful.
static void CurveAnchors(const anim::Params &P, double *peakV, double *settleMs)
{
  const double dt = 0.0005;
  const double gap = kCurveBurstGapMs / 1000.0;
  anim::Glide g;
  double t = 0.0, nextKick = 0.0, peak = 0.0;
  int kicked = 0;
  for (int i = 0; i < 40000; ++i)
  {
    if (kicked < kCurveBurstNotches && t + 1e-12 >= nextKick)
    {
      g.Kick(1.0, +1.0, 1, t, P);
      ++kicked;
      nextKick += gap;
    }
    g.Tick(dt, P);
    t += dt;
    const double v = fabs(g.Velocity());
    if (v > peak)
      peak = v;
    if (kicked >= kCurveBurstNotches && !g.Active())
      break;
  }
  *peakV = peak;
  *settleMs = t * 1000.0;
}

// The response curve.
//
// The line is built from the SAME functions the glide moves by -- one curve, five segments, each
// bulging about its own chord -- so the picture and the feel cannot disagree in SHAPE. The chart
// does draw it with wider rise weights (anim::kChartW*), because a roll's rise lasts as long as the
// mouse keeps turning and a static picture has to give it room; the drawn shape is otherwise the
// model's. Each of the five segments is drawn in its parameter's colour, and the X axis is real
// time, so the Start segment is as narrow as its duration really is.
//
// Axis NUMBERS stay real too: X runs the length of one gesture (the rise plus the fall), and
// the joints are published so the drawing can colour each segment by its owner.
// Publish what the drawing needs to know BEFORE it can be measured: where the two axes end, where
// the joints fall, and how tall the shape itself is.
//
// This is separate from the sampling because it must run FIRST. Tick labels have to be measured to
// learn how much room the plot has, and the plot's aspect is what the bulges are built against (see
// anim::SegPt) -- so the order is: axes, then measure, then aspect, then sample. Nothing here
// depends on the aspect, so there is no circularity.
static double g_curvePeakV = 1.0;    // the gesture's peak speed (the Y axis top before its margin)
static double g_curveShapeTop = 1.0; // the shape's own maximum: the summit joint's height
static void CurveAxes(const anim::Params &P)
{
  // The axes are REAL: X runs the length of one gesture (rise + fall) in milliseconds, and Y
  // spans the peak speed that gesture actually reaches. Both come from running the model
  // (CurveAnchors), so the tick labels are true values and the shape is scaled to fill the box.
  //
  // The Y span MUST track the peak. It was pinned to the normalised curve's own 0..1 for a
  // while, which made the labels meaningless (they read "1") and froze the height, so a taller
  // hump and a shorter one drew identically -- the "Y no longer scales, and the numbers are
  // wrong" that was reported.
  double peakV = 0.0, settleMs = 0.0;
  CurveAnchors(P, &peakV, &settleMs);
  if (peakV <= 0.0 || settleMs <= 0.0)
    peakV = 1.0, settleMs = 1.0;

  anim::Shape s;
  anim::BuildShape(P, &s);
  const double totalSec = s.riseSec + s.fallSec;
  g_curveTFromMs = 0.0;
  g_curveTEndMs = (totalSec > 0.0) ? (totalSec * 1000.0) : settleMs;
  // The frame's top is placed so the curve's PEAK sits at a fixed fraction of the box
  // (kWavePeakFill), never touching the top: the curve is approaching the ceiling and must be seen
  // not to reach it, so the margin at the top is part of the reading, not wasted space.
  g_curveAxisY = (peakV > 0.0) ? (peakV / kWavePeakFill) : 1.0;

  // Publish the joints, so PaintCurve can split the line into five coloured runs. They come
  // FROM THE MODEL (GestureShape), not from a second copy of the arithmetic here: if the two
  // disagreed, the colours would be attached to the wrong stretches of the line. The joints (and
  // the shape's top) are unaffected by the aspect, which is what lets them be taken here.
  anim::GestureShape gs;
  anim::BuildGestureChart(P, &gs, 1.0);
  g_curveJoints = 0;
  for (int i = 0; i <= anim::kSegments; ++i)
    g_curveJoint[g_curveJoints++] = gs.x[i];
  g_curvePeakV = peakV;
  // The shape's own maximum, i.e. the summit joint: used to scale the drawing (see below).
  g_curveShapeTop = (gs.y[3] > 1e-9) ? gs.y[3] : 1.0;
}

// Sample the curve, in the aspect it is being drawn at.
//
// `ax` is shape-x units per shape-y unit on the page, taken from the box the line is going into.
// It is what makes each segment's bulge perpendicular to its chord ON SCREEN rather than in raw
// numbers -- see anim::SegPt, where the reason (x is about 290 px per unit, y about 114) is set out.
static void BuildCurve(const anim::Params &P, double ax, float *ys, float *xs, int n)
{
  const double shapeTop = g_curveShapeTop;
  const double peakV = g_curvePeakV;
  for (int i = 0; i < n; ++i)
  {
    const double phi = (double)i / (double)(n - 1);
    // The curve is 2-D: each segment bulges PERPENDICULAR to its own chord (in the drawing space),
    // so the point moves sideways as well as up and the drawn x is the curve's own x, not the
    // parameter. Sampling only y and plotting it at even x would bend the bulge back to vertical,
    // which is exactly what was reported as wrong.
    double px = 0.0, py = 0.0;
    anim::GestureChartPoint(P, ax, phi, &px, &py, 0);
    xs[i] = (float)px;
    // Scale the SHAPE so its own maximum is the peak speed. Multiplying the raw shape by the peak
    // was wrong: the shape's top is below 1 (it never reaches the ceiling), so the line fell short
    // of the peak and the drawn height did not match the Y numbers.
    double f = py / shapeTop * peakV;
    if (f < 0.0) f = 0.0;
    if (f > g_curveAxisY) f = g_curveAxisY;
    ys[i] = (float)f;
  }
}
// Anti-aliased polylines, via GDI+ loaded on demand.
//
// GDI's Polyline is aliased: a thin diagonal line comes out as a visible staircase, which
// reads as grainy. GDI+ draws the same geometry anti-aliased. It is loaded on demand and never
// linked, so the plugin keeps no hard dependency on it and simply falls back to the aliased GDI
// path if it is not there. The only consequence of its absence is a slightly rougher line.
namespace gdiplus_util
{
typedef void GpGraphics;
typedef void GpPen;
typedef int GpStatus;

// Mirrors GdiplusStartupInput. Version must be 1; the rest are the safe defaults.
struct StartupInput
{
  UINT32 version;
  void *callback;
  BOOL noThread;
  BOOL noCodecs;
};

struct Api
{
  bool tried = false;
  bool ready = false;
  ULONG_PTR token = 0;
  int (WINAPI *startup)(ULONG_PTR *, const void *, void *) = nullptr;
  void (WINAPI *shutdown)(ULONG_PTR) = nullptr;
  GpStatus (WINAPI *fromHdc)(HDC, GpGraphics **) = nullptr;
  GpStatus (WINAPI *smoothing)(GpGraphics *, int) = nullptr;
  GpStatus (WINAPI *makePen)(UINT, float, int, GpPen **) = nullptr;
  GpStatus (WINAPI *drawLines)(GpGraphics *, GpPen *, const POINT *, int) = nullptr;
  GpStatus (WINAPI *deletePen)(GpPen *) = nullptr;
  GpStatus (WINAPI *deleteGraphics)(GpGraphics *) = nullptr;
};

static Api &api()
{
  static Api a;
  if (a.tried)
    return a;
  a.tried = true;
  HMODULE m = LoadLibraryA("gdiplus.dll");
  if (!m)
    return a;
  a.startup = (decltype(a.startup))GetProcAddress(m, "GdiplusStartup");
  a.shutdown = (decltype(a.shutdown))GetProcAddress(m, "GdiplusShutdown");
  a.fromHdc = (decltype(a.fromHdc))GetProcAddress(m, "GdipCreateFromHDC");
  a.smoothing = (decltype(a.smoothing))GetProcAddress(m, "GdipSetSmoothingMode");
  a.makePen = (decltype(a.makePen))GetProcAddress(m, "GdipCreatePen1");
  a.drawLines = (decltype(a.drawLines))GetProcAddress(m, "GdipDrawLinesI");
  a.deletePen = (decltype(a.deletePen))GetProcAddress(m, "GdipDeletePen");
  a.deleteGraphics = (decltype(a.deleteGraphics))GetProcAddress(m, "GdipDeleteGraphics");
  if (!a.startup || !a.shutdown || !a.fromHdc || !a.smoothing || !a.makePen || !a.drawLines ||
      !a.deletePen || !a.deleteGraphics)
    return a;
  StartupInput in = {1, nullptr, FALSE, FALSE};
  if (a.startup(&a.token, &in, nullptr) != 0)
    return a;
  a.ready = true;
  return a;
}

static void shutdown()
{
  Api &a = api();
  if (a.ready && a.shutdown)
    a.shutdown(a.token);
  a.ready = false;
}

// Draw a polyline anti-aliased. Returns false (without drawing) if GDI+ is unavailable, so
// the caller can fall back.
static bool drawPolyline(HDC dc, const POINT *pts, int n, COLORREF c, int width)
{
  Api &a = api();
  if (!a.ready)
    return false;
  GpGraphics *g = nullptr;
  if (a.fromHdc(dc, &g) != 0 || !g)
    return false;
  // 4 = SmoothingModeAntiAlias, 2 = UnitPixel. Colour is GDI+ ARGB.
  a.smoothing(g, 4);
  GpPen *pen = nullptr;
  const UINT argb = 0xFF000000u | ((UINT)GetRValue(c) << 16) | ((UINT)GetGValue(c) << 8) |
                    (UINT)GetBValue(c);
  if (a.makePen(argb, (float)width, 2, &pen) == 0 && pen)
  {
    a.drawLines(g, pen, pts, n);
    a.deletePen(pen);
  }
  a.deleteGraphics(g);
  return true;
}
} // namespace gdiplus_util

// The thickness of the two "bar" strokes in the panel: the fader's groove and the response curve.
// ONE constant for both, deliberately: the curve was asked to match the fader's bar, and two
// separate numbers would drift apart the next time either is adjusted.
static const int kBarThickness = 4;

// Draw one stretch of the response curve in the hue of the setting that owns it: the colour
// is that slider at its current strength, so the curve and the control agree at a glance.
// Anti-aliased via GDI+ where available, plain GDI otherwise.
static void DrawCurveRun(HDC dc, const POINT *pts, int n, int segIdx)
{
  if (n < 2 || segIdx < 0 || segIdx >= kNumSliders)
    return;
  // kBarThickness: the same weight as the fader's groove below it, so the two read as one family.
  // It was 2 px before, which was itself raised from 1 px because a hairline disappeared on a
  // high-DPI screen. Both the GDI+ and the plain-GDI path use it, so the weight does not change
  // when GDI+ is unavailable.
  const int kCurveWidth = kBarThickness;
  const COLORREF col = SliderColor(segIdx, SliderAmount(segIdx));
  if (!gdiplus_util::drawPolyline(dc, pts, n, col, kCurveWidth))
  {
    HPEN pen = CreatePen(PS_SOLID, kCurveWidth, col);
    HGDIOBJ old = SelectObject(dc, pen);
    Polyline(dc, pts, n);
    SelectObject(dc, old);
    DeleteObject(pen);
  }
}

static void PaintCurve(HDC dc)
{
  const RECT &r = g_curveRect;
  if (r.right - r.left < 40 || r.bottom - r.top < 40)
    return;

  if (g_cardBrush)
    FillRect(dc, &r, g_cardBrush);

  // Build once per parameter change: sampling costs ~1 ms and a slider drag repaints
  // continuously.
  //
  // The sample count is generous (it costs nothing here) so the spline is followed closely and
  // the line reads as a smooth stroke rather than a chain of visible chords. The points are
  // then drawn in coloured runs (see below), and a finer grid also keeps the joints between
  // those runs tight.
  enum { kN = 480 };
  static float ys[kN];
  static float xs[kN]; // the curve's own x at each sample (see BuildCurve)
  static bool have = false;
  // Everything the curve depends on: the five sliders AND the five knobs. A knob left out
  // here would leave the line stale while the motion changed, which is the one thing this
  // drawing must not do.
  static double lStart = 0, lAccel = 0, lRelease = 0, lHold = 0, lCoast = 0, lOnset = 0;
  static double lBend[anim::kSegments] = {9, 9, 9, 9, 9};
  bool changed = !have || lStart != g_startPct || lAccel != g_accelPct ||
                 lRelease != g_releaseMs || lHold != g_hold || lCoast != g_coast ||
                 lOnset != g_onsetMs;
  for (int i = 0; i < anim::kSegments && !changed; ++i)
    if (lBend[i] != g_bend[i])
      changed = true;
  if (changed)
  {
    // Axes and joints only. The SAMPLES have to wait for the box: the bulges are built against the
    // aspect the line will be drawn at, and that aspect is not known until the tick labels have been
    // measured (see below).
    CurveAxes(AnimParams());
    lStart = g_startPct;
    lAccel = g_accelPct;
    lRelease = g_releaseMs;
    lHold = g_hold;
    lCoast = g_coast;
    lOnset = g_onsetMs;
    for (int i = 0; i < anim::kSegments; ++i)
      lBend[i] = g_bend[i];
    have = true;
  }
  if (g_curveTEndMs <= 0.0 || g_curveAxisY <= 0.0)
    return;

  // Tick labels need room: X labels sit under the axis, Y labels left of it. Their widths
  // are measured rather than guessed, so a bigger font or a longer number cannot overlap
  // the plot.
  HGDIOBJ oldFont = SelectObject(dc, CurveFont());
  char xFirst[32] = {0}, yFirst[32] = {0};
  _snprintf(xFirst, sizeof(xFirst), "%d", (int)g_curveTEndMs);
  _snprintf(yFirst, sizeof(yFirst), "%d", (int)g_curveAxisY);
  SIZE szX = {0}, szY = {0}, szV = {0};
  GetTextExtentPoint32A(dc, xFirst, (int)strlen(xFirst), &szX);
  GetTextExtentPoint32A(dc, yFirst, (int)strlen(yFirst), &szY);
  GetTextExtentPoint32A(dc, "v", 1, &szV);
  const int fontH = szX.cy > 0 ? szX.cy : 12;

  // The axis LABELS ("v" and "t") go OUTSIDE the plot next to the tick numbers, never over
  // the curve: "v" above the column of Y numbers, "t" right of the row of X numbers. Their
  // space is reserved here so the plot shrinks to make room instead of being covered.
  const int padL = szY.cx + 5;          // Y tick numbers
  const int padB = fontH + 4;           // X tick numbers
  const int padT = fontH + 4;           // room for "v" above them
  const int padR = szV.cx + 6;          // room for "t" right of them

  const int x0 = r.left + padL, x1 = r.right - padR;
  const int yb = r.bottom - padB, yt = r.top + padT;
  if (x1 - x0 < 16 || yb - yt < 16)
  {
    SelectObject(dc, oldFont);
    return;
  }

  const int spanX = x1 - x0 - 1;
  const int spanY = yb - 1 - yt;

  // Sample the curve at the aspect of THIS box.
  //
  // The aspect is how many shape-x units one shape-y unit is worth where the line is drawn. The
  // whole gesture spans spanX pixels across; the shape's own top (g_curveShapeTop) is drawn at
  // kWavePeakFill of the span's height (see the vertex mapping below, where ys is scaled to the
  // peak), so one shape-y unit is kWavePeakFill*spanY/g_curveShapeTop pixels. Therefore
  //
  //     ax = (spanX px per shape-x) / (kWavePeakFill*spanY/g_curveShapeTop px per shape-y)
  //        = spanX * g_curveShapeTop / (kWavePeakFill * spanY)
  //
  // shape-x units. With that factor the measured chord angle equals the drawn one, so a bulge
  // perpendicular in measured numbers is perpendicular on the page -- which is what anim::SegPt
  // needs. It is measured here, after the labels, because the labels are what decided the box.
  //
  // `changed` is part of the condition, not just the aspect: a parameter can move the shape while
  // leaving the aspect unchanged (a bigger peak with the same shape height, say), and the samples
  // must follow the parameters, not the box alone.
  {
    static double lAx = 0.0;
    static int lSpanX = 0, lSpanY = 0;
    const double ax = (spanX > 0 && spanY > 0)
                          ? ((double)spanX * g_curveShapeTop / (kWavePeakFill * (double)spanY))
                          : 1.0;
    if (have && (changed || lAx != ax || lSpanX != spanX || lSpanY != spanY))
    {
      BuildCurve(AnimParams(), ax, ys, xs, kN);
      lAx = ax;
      lSpanX = spanX;
      lSpanY = spanY;
    }
  }

  // Where a TIME falls on the axis. The window is [g_curveTFromMs, g_curveTEndMs] and the
  // mapping is LINEAR (the schematic has no reason to distort time), so the fraction is
  // simply (t - tFrom) / (tEnd - tFrom).
  struct XPos
  {
    static int At(double tMs, int x0, int spanX)
    {
      const double span = g_curveTEndMs - g_curveTFromMs;
      if (span <= 0.0)
        return x0;
      double u = (tMs - g_curveTFromMs) / span;
      if (u < 0.0) u = 0.0;
      if (u > 1.0) u = 1.0;
      return x0 + (int)((double)spanX * u + 0.5);
    }
  };

  SetBkMode(dc, TRANSPARENT);

  // Ticks at round VALUES, not at fractions of the box. The axes rescale with the settings,
  // so a fixed fraction would sit still and say nothing; a tick every 500 ms (or every 100
  // speed units) moves and re-counts as the curve rescales, which is what makes the scale
  // readable.
  //
  // The grid is drawn DENSE (many lines) while the LABELS are thinned to whatever fits: a
  // dense grid reads as a scale, but dense numbers collide into noise. Same value steps for
  // both; only the labels are dropped when they would overlap.
  //
  // Ticks are aligned to round values INSIDE the visible window, so they read 200/400/600
  // rather than starting wherever the trimmed window happens to begin.
  //
  // The counts are high on purpose: a finely graduated grid reads as a scale, which is the
  // point of having it at all.
  const double tSpanMs = g_curveTEndMs - g_curveTFromMs;
  const double stepX = NiceStep(tSpanMs, 20);
  const double stepY = NiceStep(g_curveAxisY, 14);
  const double firstX = ceil(g_curveTFromMs / stepX) * stepX;

  if (g_gridBrush)
  {
    for (double t = firstX; t < g_curveTEndMs - 1e-9; t += stepX)
    {
      const int gx = XPos::At(t, x0, spanX);
      RECT v = {gx, yt, gx + 1, yb};
      FillRect(dc, &v, g_gridBrush);
    }
    for (double v = stepY; v < g_curveAxisY - 1e-9; v += stepY)
    {
      const int gy = yb - (int)((double)spanY * (v / g_curveAxisY) + 0.5);
      RECT h = {x0, gy, x1, gy + 1};
      FillRect(dc, &h, g_gridBrush);
    }
  }

  // Axes: Y up the left, X along the bottom.
  if (g_lineBrush)
  {
    RECT ay = {x0, yt, x0 + 1, yb};
    RECT ax = {x0, yb - 1, x1, yb};
    FillRect(dc, &ay, g_lineBrush);
    FillRect(dc, &ax, g_lineBrush);
  }

  // Axis labels, OUTSIDE the plot so they never cover the curve: "v" sits above the column
  // of Y numbers (top-left), "t" to the right of the row of X numbers (bottom-right). Just
  // the two letters -- enough to read the plot as speed against time.
  SetTextColor(dc, g_theme.sub);
  {
    const int vx = x0 - 3 - szY.cx;
    TextOutA(dc, vx < r.left ? r.left : vx, r.top + 1, "v", 1);
    TextOutA(dc, r.right - szV.cx - 1, yb + 2, "t", 1);
  }

  // Tick labels. Faint (g_theme.axisText, a tone below the axis letters) and without unit
  // suffixes: they show the SCALE rather than being read as measurements, so the smaller and
  // quieter they are the better -- as long as a digit is still identifiable.
  SetTextColor(dc, g_theme.axisText);
  {
    // X labels, left to right, dropping any that would touch the previous one.
    int lastRight = -10000;
    for (double t = firstX; t < g_curveTEndMs - 1e-9; t += stepX)
    {
      char b[32];
      _snprintf(b, sizeof(b), "%d", (int)(t + 0.5));
      SIZE s = {0};
      GetTextExtentPoint32A(dc, b, (int)strlen(b), &s);
      const int gx = XPos::At(t, x0, spanX);
      int tx = gx - s.cx / 2;
      if (tx < r.left + 1) tx = r.left + 1;        // keep it inside the box
      if (tx + s.cx > r.right - 1) tx = r.right - 1 - s.cx;
      if (tx < lastRight + 4)
        continue; // would collide with the previous label
      TextOutA(dc, tx, yb + 2, b, (int)strlen(b));
      lastRight = tx + s.cx;
    }

    // Y labels, bottom to top, dropping any that would touch the previous one.
    int lastTop = 100000;
    for (double v = stepY; v < g_curveAxisY - 1e-9; v += stepY)
    {
      char b[32];
      _snprintf(b, sizeof(b), "%d", (int)(v + 0.5));
      SIZE s = {0};
      GetTextExtentPoint32A(dc, b, (int)strlen(b), &s);
      const int gy = yb - (int)((double)spanY * (v / g_curveAxisY) + 0.5);
      const int ty = gy - s.cy / 2;
      if (ty + s.cy > lastTop - 2)
        continue; // would collide with the label above
      TextOutA(dc, x0 - 3 - s.cx, ty, b, (int)strlen(b));
      lastTop = ty;
    }
  }

  // The curve, drawn in SEGMENTS: each stretch takes the hue of the setting that owns it, so
  // which control bends which part of the shape is visible at a glance. The hue's saturation
  // carries the value (see SliderColor), matching the same slider's groove below.
  //
  // Adjacent runs share their boundary sample, so cutting the line into coloured pieces opens
  // no gap and adds no corner. Drawn anti-aliased through GDI+ when available, with the plain
  // GDI path as the fallback.
  POINT pts[kN];
  for (int i = 0; i < kN; ++i)
  {
    // The sample's own x (the curve is 2-D), clamped into the plot.
    double xf = xs[i];
    if (xf < 0.0) xf = 0.0;
    if (xf > 1.0) xf = 1.0;
    pts[i].x = x0 + (int)((double)spanX * xf + 0.5);
    // ys is a REAL speed now, so it is divided by the axis span before being turned into
    // pixels -- the same normalisation the grid lines use. Without this the line was drawn
    // spanY times the speed, i.e. hundreds of pixels, straight out of the box.
    double frac = (g_curveAxisY > 0.0) ? ((double)ys[i] / g_curveAxisY) : 0.0;
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    pts[i].y = yb - 1 - (int)((double)spanY * frac + 0.5);
  }

  // Segment s spans normalised x [g_curveJoint[s], g_curveJoint[s+1]]. Convert those to sample
  // indices, drawing [start..end] inclusive and sharing the boundary sample.
  for (int s = 0; s + 1 < g_curveJoints; ++s)
  {
    const int start = (int)(g_curveJoint[s] * (kN - 1) + 0.5);
    const int end = (int)(g_curveJoint[s + 1] * (kN - 1) + 0.5);
    if (end <= start)
      continue; // an empty stretch (a zero-length coast) has nothing to draw
    DrawCurveRun(dc, &pts[start], end - start + 1, s);
  }

  SelectObject(dc, oldFont);

  // Frame last, so the axes, ticks and curve sit inside a clean outline.
  if (g_lineBrush)
    FrameRect(dc, &r, g_lineBrush);
}

// The master switch, drawn by the panel itself. See g_switchRect for why it is not a control.
// The tick is read from g_glideOn (the state the rest of the plugin and the settings store use),
// so there is no second copy of it to keep in step.
static void DrawSwitch(HDC dc)
{
  if (!g_hasSwitch)
    return;
  const RECT &rc = g_switchRect;
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
  if (g_glideOn)
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
  DrawTextA(dc, kEnableText, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

  // Focus ring, drawn the way a dialog draws it, so the switch still shows the keyboard.
  if (GetFocus() == g_cfgWnd)
    DrawFocusRect(dc, &rc);

  SetBkMode(dc, oldBk);
  SelectObject(dc, oldFont);
}

// The raw wheel monitor: what the plugin is actually being fed, before any decision.
//
// Deliberately blunt. It shows the RAW delta and the gap between messages, and says plainly whether
// that delta is one this plugin will animate (a whole WHEEL_DELTA multiple, not touch-injected) or
// one it hands to REAPER. That last line is the whole point: a free-spinning mouse sends partial
// deltas, and seeing their actual size and spacing is the first step to supporting them.
static void DrawMonitor(HDC dc)
{
  if (!g_hasMonitor)
    return;
  const RECT &r = g_monRect;
  if (r.right - r.left < 40 || r.bottom - r.top < 30)
    return;

  if (g_cardBrush)
    FillRect(dc, &r, g_cardBrush);
  if (g_lineBrush)
    FrameRect(dc, &r, g_lineBrush);

  HGDIOBJ oldFont = SelectObject(dc, UiFont(nullptr));
  const int oldBk = SetBkMode(dc, TRANSPARENT);
  const int padX = 8, lineH = 15;
  int y = r.top + 5;
  char b[256];

  // Heading, in the same quiet colour the curve's numbers use: it is a label, not data.
  SetTextColor(dc, g_theme.sub);
  TextOutA(dc, r.left + padX, y, "Wheel monitor (raw, before any processing)", 42);
  y += lineH;

  SetTextColor(dc, g_theme.text);
  if (!g_mon.seen)
  {
    TextOutA(dc, r.left + padX, y, "Roll the wheel anywhere to see what arrives.", 44);
    SetBkMode(dc, oldBk);
    SelectObject(dc, oldFont);
    return;
  }

  // Raw value, and how it compares with one notch. Both figures are shown so the raw number is
  // never dressed up as something it is not.
  _snprintf(b, sizeof(b), "delta = %d      (one notch = %d)      notches = %.4f",
            g_mon.delta, WHEEL_DELTA, (double)g_mon.delta / (double)WHEEL_DELTA);
  TextOutA(dc, r.left + padX, y, b, (int)strlen(b));
  y += lineH;

  // THE MEASUREMENT LINE. SUM is the total delta since the block was reset (click it to reset), so
  // turning the wheel one turn and reading it gives that device's delta per revolution. The step
  // range is the size of a single click.
  if (g_mon.haveStep)
    _snprintf(b, sizeof(b), "SUM since reset = %ld      step size = %d..%d      clicks = %lu",
              g_mon.sumDelta, g_mon.minStep, g_mon.maxStep, (unsigned long)g_mon.total);
  else
    _snprintf(b, sizeof(b), "SUM since reset = 0      step size = -      clicks = 0");
  TextOutA(dc, r.left + padX, y, b, (int)strlen(b));
  y += lineH;

  // THE DEVICE, read from the delta pattern rather than queried (see the fields' comment): a run of
  // sub-notch deltas means a free-spinning wheel, a run of whole notches means a plain mouse.
  {
    const char *kind;
    if (g_mon.touch) kind = "touchpad / pen (already fine, left to REAPER)";
    else if (g_mon.fineRun >= 2) kind = "free-spinning  (repeated values below one notch)";
    else if (g_mon.wholeRun >= 1) kind = "notched mouse  (whole notches)";
    else kind = "(roll the wheel)";
    _snprintf(b, sizeof(b), "device = %s      fine-run = %d   whole-run = %d",
              kind, g_mon.fineRun, g_mon.wholeRun);
    SetTextColor(dc, g_theme.text);
    TextOutA(dc, r.left + padX, y, b, (int)strlen(b));
    y += lineH;
  }

  // Timing: a free-spinning wheel fires far more often than a notched one, and the gap is how a
  // device reports its own rate.
  if (g_mon.gapMs)
    _snprintf(b, sizeof(b), "gap since previous = %lu ms      total wheels seen = %lu",
              (unsigned long)g_mon.gapMs, (unsigned long)g_mon.total);
  else
    _snprintf(b, sizeof(b), "gap since previous = -      (first)      total wheels seen = %lu",
              (unsigned long)g_mon.total);
  TextOutA(dc, r.left + padX, y, b, (int)strlen(b));
  y += lineH;

  // The verdict: what this plugin does with this message. Both kinds of wheel are animated now --
  // what differs is how much of a notch the message carries.
  SetTextColor(dc, g_theme.text);
  if (g_mon.touch)
    _snprintf(b, sizeof(b), "source: touch/pen-injected  ->  passed through (already continuous)");
  else if (g_mon.standard)
    _snprintf(b, sizeof(b), "source: notched mouse  (whole multiple of %d)  ->  smoothed, %.2f notch",
              WHEEL_DELTA, (double)g_mon.delta / (double)WHEEL_DELTA);
  else
    _snprintf(b, sizeof(b), "source: high-resolution / free-spinning  (%.3f of a notch)  ->  smoothed by that share",
              (double)g_mon.delta / (double)WHEEL_DELTA);
  TextOutA(dc, r.left + padX, y, b, (int)strlen(b));
  y += lineH;

  // The message's raw extra-info word, in hex, plus whether it matches the touch signature. This is
  // the most direct evidence of the device behind the wheel: two devices that send the same delta at
  // the same rate can still differ here, and this is the only field that says so.
  SetTextColor(dc, g_theme.sub);
  _snprintf(b, sizeof(b), "extra info = 0x%08lX   touch/pen signature: %s",
            (unsigned long)g_mon.extraInfo, g_mon.touch ? "MATCH" : "no");
  TextOutA(dc, r.left + padX, y, b, (int)strlen(b));
  y += lineH;
  SetTextColor(dc, g_theme.text);

  // Modifiers and where it landed: the same surface the classification will use. The class name and
  // REAPER's hit-test string are resolved HERE rather than in the hook -- see MonitorRawWheel for
  // why (they are too heavy to ask once per wheel message).
  char cls[48] = {0}, thing[64] = {0};
  if (g_mon.under)
    GetClassNameA(g_mon.under, cls, sizeof(cls));
  if (GetThingFromPoint)
    GetThingFromPoint(g_mon.pt.x, g_mon.pt.y, thing, (int)sizeof(thing));
  char mods[24];
  _snprintf(mods, sizeof(mods), "%s%s%s", g_mon.shift ? "Shift " : "",
            g_mon.ctrl ? "Ctrl " : "", g_mon.alt ? "Alt" : "");
  _snprintf(b, sizeof(b), "mods = %-9s  under = %s   thing = \"%s\"",
            mods[0] ? mods : "(none)", cls[0] ? cls : "(none)", thing);
  TextOutA(dc, r.left + padX, y, b, (int)strlen(b));
  y += lineH;

  // The last few raw deltas, oldest to newest: enough to see a device's characteristic step.
  SetTextColor(dc, g_theme.sub);
  {
    char hist[200] = {0};
    int n = 0;
    n += _snprintf(hist + n, sizeof(hist) - n, "recent deltas: ");
    for (int i = WheelMonitor::kHist - g_mon.histN; i < WheelMonitor::kHist; ++i)
      n += _snprintf(hist + n, sizeof(hist) - n, "%d ", g_mon.hist[i]);
    TextOutA(dc, r.left + padX, y, hist, n);
  }

  SetBkMode(dc, oldBk);
  SelectObject(dc, oldFont);
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

  // The response curve sits below the parameter block and paints itself.
  if (g_hasCurve)
    PaintCurve(dc);

  // The raw-wheel monitor sits last, under the curve.
  DrawMonitor(dc);

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
      ReleaseCapture();
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
// is the rise's duration in ms, and the other four are their segment's bend (see anim_core.h).
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
  int curveH, curveGap; // the response curve block under the sliders
  int monH, monGap;     // the raw-wheel monitor block under the curve
};

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
  // The response-curve block: a box showing how one wheel notch travels over time.
  // Tall enough for the shape (rise, summit, fall, tail) to read at a glance, plus room for
  // the tick labels along the bottom and left edges (see the padding in PaintCurve). Raised
  // 20% (108 -> 130) on request: the vertical differences between settings are easier to see
  // in a taller box.
  m.curveH = 130;
  m.curveGap = 14;
  // The wheel monitor: a heading line plus a few rows of raw readout. Shorter than the curve
  // block -- it is a table of values, not a graph.
  m.monH = 154;
  m.monGap = 14;
  return m;
}

// The height at which the panel needs no scrollbar. Nothing is reserved for a bar: at
// this size the content fits, so none is shown.
static int PanelIdealHeight(const PanelMetrics &m)
{
  return m.pad + m.headH + m.rowH * kNumSliders + m.curveGap + m.curveH + m.monGap + m.monH +
         m.pad;
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
  return m.pad * 2 + boxW + gap + MeasureTextWidth(kEnableText);
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
      RECT br = {0, 0, rc.right - m.pad * 2, 0};
      HDC dc = GetDC(h);
      HGDIOBJ oldFont = SelectObject(dc, UiFont(nullptr));
      DrawTextA(dc, kEnableText, -1, &br, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
      SelectObject(dc, oldFont);
      ReleaseDC(h, dc);
      boxWanted = br.bottom;
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

  // The master switch: the panel draws it, so this is just its rectangle. Sized to the caption
  // (glyph + gap + the text, plus a little slack for the focus rectangle) rather than the full
  // panel width, so it reads as one line of text.
  {
    const int boxW = GetSystemMetrics(SM_CXMENUCHECK);
    const int capW = MeasureTextWidth(kEnableText);
    int w = boxW + 6 + capW + 8;
    const int maxW = usableW - m.pad * 2;
    if (w > maxW) w = maxW;
    g_switchRect = {m.pad, y0 + m.pad, m.pad + w, y0 + m.pad + m.headH};
    g_hasSwitch = true;
  }

  for (int i = 0; i < kNumSliders; ++i)
  {
    const SliderSpec &s = g_sliders[i];
    const int ly = y0 + m.pad + m.headH + m.rowH * i;

    // Card = content + equal padding top and bottom.
    RECT cr = {m.pad, ly, usableW - m.pad, ly + m.cardPad + m.groupH + m.barH + m.cardPad};
    g_cardRect[i] = cr;

    const int ix = cr.left + m.cardPad;
    const int iw = (cr.right - m.cardPad) - ix;
    const int ty = cr.top + m.cardPad;

    SetWindowPos(GetDlgItem(h, s.labelId), nullptr, ix, ty, iw, m.groupH - 2, SWP_NOZORDER);
    SetWindowPos(g_capLo[i], nullptr, ix, ty + m.groupH, m.capW, m.barH, SWP_NOZORDER);
    // The knob takes the far-right end of the row; the "hi" end-cap and the fader then share
    // what is left, so the knob sits behind its slider without squeezing the track to nothing.
    const int kw = m.barH; // square: the knob's diameter is the row height
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
  g_sepRect = {m.pad, y0 + m.pad + m.headH + 2, usableW - m.pad,
               y0 + m.pad + m.headH + 3};
  g_hasCards = true;

  // The response-curve block, directly below the parameter cards. Full usable width; the
  // caller keeps PanelIdealHeight() in step with this so the panel opens tall enough.
  {
    const int cy = y0 + m.pad + m.headH + m.rowH * kNumSliders + m.curveGap;
    g_curveRect = {m.pad, cy, usableW - m.pad, cy + m.curveH};
    g_hasCurve = true;
  }

  // The wheel monitor, last: it shows what the plugin is being fed, for studying real mice.
  {
    const int my = y0 + m.pad + m.headH + m.rowH * kNumSliders + m.curveGap + m.curveH +
                   m.monGap;
    g_monRect = {m.pad, my, usableW - m.pad, my + m.monH};
    g_hasMonitor = true;
  }

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

    // The knob that sits behind this slider.
    HWND k = CreateWindowExA(0, SWSC_KNOB_CLASS, "", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
                             h, (HMENU)(INT_PTR)g_knobs[i].id, g_hInst, nullptr);
    SetWindowLongPtrA(k, GWLP_USERDATA, (LONG_PTR)i);
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
      if (g_hasCurve)
        InvalidateRect(h, &g_curveRect, FALSE); // reset changes the curve too
      SaveSettings();
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
        // The response curve is computed from the model, so it has to be redrawn whenever
        // a parameter moves -- that is the whole point of showing it.
        if (g_hasCurve)
          InvalidateRect(h, &g_curveRect, FALSE);
        SaveSettings();        // persist live, so closing the window keeps the value
        break;
      }
    }
    return 0;
  }
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
      if (g_hasCurve)
        InvalidateRect(h, &g_curveRect, FALSE);
      SaveSettings();
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
      if (g_hasCurve)
        InvalidateRect(h, &g_curveRect, FALSE);
      SaveSettings();
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
    const POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
    if (g_hasSwitch &&
        p.x >= g_switchRect.left && p.x < g_switchRect.right &&
        p.y >= g_switchRect.top && p.y < g_switchRect.bottom)
    {
      if (!g_cfgUpdating)
      {
        g_glideOn = !g_glideOn;
        SaveSettings();
      }
      InvalidateRect(h, &g_switchRect, FALSE);
      return 0;
    }
    // Clicking the monitor block resets its SUM, so a revolution can be measured from zero.
    if (g_hasMonitor &&
        p.x >= g_monRect.left && p.x < g_monRect.right &&
        p.y >= g_monRect.top && p.y < g_monRect.bottom)
    {
      g_mon.sumDelta = 0;
      g_mon.total = 0;
      g_mon.totalStd = 0;
      g_mon.haveStep = false;
      g_mon.fineRun = g_mon.wholeRun = 0;
      InvalidateRect(h, &g_monRect, FALSE);
      return 0;
    }
    break; // not on the switch or the monitor: let the default handling have it
  }
  // Clicking the panel takes the focus, so the switch's focus ring follows it and the keyboard
  // still reaches us (see PanelKeyHandler).
  case WM_SETFOCUS:
    if (g_hasSwitch)
      InvalidateRect(h, &g_switchRect, FALSE);
    return 0;
  case WM_KILLFOCUS:
    if (g_hasSwitch)
      InvalidateRect(h, &g_switchRect, FALSE);
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
    g_cfgWnd = nullptr;
    g_monRepaint = nullptr;
    g_hasCards = false;
    g_hasSwitch = false;
    g_hasMonitor = false;
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
      // name              value        min            max           unit lomark     himark
      //   default            hue                     knob value   knob min    knob max    knob def
      //                                                    knob name
      {"Start", &g_startPct, kStartMinPct, kStartMaxPct, "%", "Slower", "Faster",
       kDefaultStartPct, RGB( 90, 190, 255),
       &g_onsetMs, kKnobMin[0], kKnobMax[0], kDefaultOnsetMs, "Start (rise ms)"},
      {"Accel per notch", &g_accelPct, kAccelMinPct, kAccelMaxPct, "%", "Gentler", "Stronger",
       kDefaultAccelPct, RGB(120, 225, 140),
       &g_bend[1], kKnobMin[1], kKnobMax[1], kDefaultBend, "Accel bend"},
      {"High-speed hold", &g_hold, kHoldMin, kHoldMax, "x", "Less", "More",
       kDefaultHold, RGB(255, 205,  90),
       &g_bend[2], kKnobMin[2], kKnobMax[2], kDefaultHoldBend, "Hold bend"},
      {"High-speed coast", &g_coast, kCoastMin, kCoastMax, "x", "Less", "More",
       kDefaultCoast, RGB(255, 150, 110),
       &g_bend[3], kKnobMin[3], kKnobMax[3], kDefaultBend, "Coast bend"},
      {"Release", &g_releaseMs, kReleaseMinMs, kReleaseMaxMs, "ms", "Quicker", "Longer",
       kDefaultReleaseMs, RGB(200, 160, 255),
       &g_bend[4], kKnobMin[4], kKnobMax[4], kDefaultBend, "Release bend"},
  };

  for (int i = 0; i < kNumSliders; ++i)
  {
    g_sliders[i] = kTable[i];
    g_sliders[i].sliderId = SliderIdOf(i);
    g_sliders[i].labelId = LabelIdOf(i);
    // The knob record is a VIEW of this row, so the two cannot disagree.
    g_knobs[i].id = KnobIdOf(i);
    g_knobs[i].value = kTable[i].knobValue;
    g_knobs[i].min = kTable[i].knobMin;
    g_knobs[i].max = kTable[i].knobMax;
    g_knobs[i].name = kTable[i].knobName;
    g_knobPos[i] = ValueToKnob(i);
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
  RECT r = {0, 0, MinPanelWidth(m), m.pad * 2 + m.headH + m.rowH};
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
  RECT wr = {0, 0, MinPanelWidth(m), PanelIdealHeight(m)};
  // CreateWindowEx takes the size of the WHOLE window, so the caption and border are
  // added to the client size here; otherwise the bottom padding is eaten by the frame.
  AdjustWindowRectEx(&wr, style, FALSE, WS_EX_TOOLWINDOW);

  g_cfgWnd = CreateWindowExA(WS_EX_TOOLWINDOW, cls, "Smooth Wheel Scroll", style,
                             CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left,
                             wr.bottom - wr.top, g_main, nullptr, g_hInst, nullptr);
  if (!g_cfgWnd)
  {
    g_cfgUpdating = false;
    return;
  }
  g_monRepaint = g_cfgWnd; // the message hook repaints the monitor through this

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
  rec->Register("ext_name", (void *)"Smooth Wheel Scroll 1.6.1");
  rec->Register("ext_vendor", (void *)"SmoothWheelScroll");

  // Load the saved feel before anything uses it. If the master switch was off, the
  // hook still gets installed (see InstallHook) so it can be switched back on at
  // runtime, and until then every wheel is forwarded untouched.
  LoadSettings();

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
