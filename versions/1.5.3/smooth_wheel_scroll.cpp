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
#define REAPERAPI_WANT_GetTrack
// Settings storage is REAPER's own extended state: the plugin keeps no file of its own
// and does not touch REAPER's preferences. See SaveSettings/LoadSettings.
#define REAPERAPI_WANT_GetExtState
#define REAPERAPI_WANT_SetExtState
// Theme colours, so the settings window follows REAPER's look (and its light/dark
// theme) instead of hard-coded faces. See ResolveTheme.
#define REAPERAPI_WANT_GetThemeColor
// Read what the user has assigned to a Mouse Modifier context (track panel / mixer panel
// wheel). This is how the panel rules avoid hijacking a gesture the user has rebound:
// see TcpWheelIsPlainScroll.
#define REAPERAPI_WANT_GetMouseModifier
// Mixer scrolling. The mixer has NO action to replay (its wheel is a Mouse Modifier
// behaviour), so the only interface is SetMixerScroll -- which takes a TRACK, i.e. its
// smallest step is one whole track. See DRIVE_MIXER_SCROLL.
#define REAPERAPI_WANT_GetMixerScroll
#define REAPERAPI_WANT_SetMixerScroll
#define REAPERAPI_WANT_CountTracks
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
static const double kDefaultAccelPct = 3.5;
static const double kAccelMinPct = 0.0;
static const double kAccelMaxPct = 10.0;

// --- 3. RELEASE ---------------------------------------------------------------
static const double kDefaultReleaseMs = 150.0;
static const double kReleaseMinMs = 60.0;
static const double kReleaseMaxMs = 300.0;

// --- 4. HIGH-SPEED HOLD -------------------------------------------------------
// How fast a fast roll is allowed to get. The model has an explicit speed ceiling
// (Params::speedCeiling): a notch stops adding once the roll has reached it, so the top of
// the curve is a definite number instead of wherever a taper happened to leave it.
//
// The brake loosening (kTempoRefMs) rides the same slider: a faster roll keeps a looser
// brake, so it also coasts further. Both grow with the multiplier, so both mean "more" as
// the slider goes up.
//
// The ceiling replaced a soft feed taper (an "escape velocity" knee). That taper was not a
// ceiling -- it always allowed a little more -- and it made the motion double-humped at small
// holds: each notch injects more than the last (Accel) while the taper injects less as speed
// rises, and where those two cross the speed dips before rising again. A factor that reaches
// zero at the ceiling cannot do that: the speed rises to its limit and then falls back.
static const double kSpeedCeiling = 1338.0; // hold = 1
static const double kCeilingPow = 2.5;      // approach sharpness (shape; not on a slider)
static const double kTempoRefMs = 110.0;
static const double kBrakeRelaxMax = 4.0;  // loosening ceiling (shape; not on a slider)
static const double kDefaultHold = 1.0;
static const double kHoldMin = 0.0;
static const double kHoldMax = 2.0;

// The ceiling ramps GEOMETRICALLY from its lowest value (hold = 0) to the tuned value
// (hold = 1), continuing upward past that. Proportional steps keep the whole slider usable
// instead of crowding every interesting change into the top.
//
// The low end is a FLOOR rather than zero: zero means "no limit at all", which is not the
// smallest effect -- an unlimited roll goes FASTER than a barely-limited one, so the slider
// would stop being monotonic at that end. A floor keeps hold = 0 the genuine minimum.
//
// The floor is also kept above ordinary rolling speeds (measured peaks: one notch 17,
// 3 @120ms 86, 6 @60ms 275), so even at its lightest the ceiling does not clip a normal roll
// -- it only limits a genuinely fast one.
static const double kSpeedCeilingFloor = 250.0;
static double CeilingForHold(double hold)
{
  return (hold <= 1.0)
             ? kSpeedCeilingFloor * pow(kSpeedCeiling / kSpeedCeilingFloor, hold)
             : kSpeedCeiling * hold;
}

// The brake-loosening threshold, on the same geometric ramp and with the same reasoning:
// zero would disable the loosening entirely, which is the MOST slack brake (the model clamps
// relax to its lower bound), so "off" would out-travel its neighbour. A small positive floor
// keeps the low end the true minimum.
static const double kTempoRefFloorMs = 6.0;
static double TempoRefForHold(double hold)
{
  return (hold <= 1.0) ? kTempoRefFloorMs * pow(kTempoRefMs / kTempoRefFloorMs, hold)
                       : kTempoRefMs * hold;
}

// --- 5. HIGH-SPEED COAST ------------------------------------------------------
// How much longer a fast roll coasts. g_coast scales the brake reduction; the dead
// zone (kRelFloor, below which nothing changes at all) and the reference speed
// (kRelRef) are shape and stay fixed.
static const double kRelGain = 0.85;
static const double kRelRef = 450.0;
static const double kRelFloor = 40.0;
static const double kDefaultCoast = 1.0;
static const double kCoastMin = 0.0;
static const double kCoastMax = 2.0;

// Runtime copies (what the model actually uses). The settings window edits these.
static double g_startPct = kDefaultStartPct;
static double g_accelPct = kDefaultAccelPct;
static double g_releaseMs = kDefaultReleaseMs;
static double g_hold = kDefaultHold;
static double g_coast = kDefaultCoast;
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
static anim::Params ParamsFor(double startPct, double accelPct, double releaseMs,
                              double hold, double coast)
{
  anim::Params P;
  P.startPct = startPct;
  P.accelPct = accelPct;
  P.releaseMs = releaseMs;
  P.frictionPow = kFrictionPow;
  P.onsetRatio = kOnsetRatio;
  P.movingOnsetMs = kMovingOnsetMs;
  P.onsetFadeNotches = kOnsetFadeNotches;
  P.burstGapMs = kBurstGapMs;
  P.ceilingPow = kCeilingPow;
  P.relRef = kRelRef;
  P.relFloor = kRelFloor;
  P.tempoRefMs = TempoRefForHold(hold);
  P.speedCeiling = CeilingForHold(hold);
  P.relaxMax = kBrakeRelaxMax;
  P.relGain = kRelGain * coast;
  return P;
}

static anim::Params AnimParams()
{
  return ParamsFor(g_startPct, g_accelPct, g_releaseMs, g_hold, g_coast);
}

// The compiled-in defaults, used only as the curve's reference line.
static anim::Params DefaultParams()
{
  return ParamsFor(kDefaultStartPct, kDefaultAccelPct, kDefaultReleaseMs, kDefaultHold,
                   kDefaultCoast);
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
static const int kRelIntMax = 63;       // max |integer part| in the relative form

// How much glide travel equals ONE mixer track.
//
// The mixer has no relative-value action to replay: the only interface is
// SetMixerScroll, whose unit is a whole track. So the glide's travel has to be converted
// into tracks, and this is the scale. It is set so that ONE input notch moves ONE track,
// which keeps the mixer's response per notch the same as REAPER's own -- the glide then
// changes only the TIMING (spread out, with a tail), not the distance.
//
// One notch produces 1.89 units of travel in the frozen model (the accepted baseline
// value, see test/check_v1_baseline.sh), so that is the conversion. It is deliberately a
// named constant rather than derived from the tunable feel parameters: tuning Start for
// the view's feel must not change the mixer's speed.
static const double kMixerUnitsPerTrack = 1.89;

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
// ---------------------------------------------------------------------------
enum Drive
{
  DRIVE_NONE = 0,
  DRIVE_REPLAY,      // re-invoke REAPER's own action with fractional relative values
  DRIVE_MIXER_SCROLL // move the mixer by SetMixerScroll (no action exists for it)
};

// How an action's travel has to be handed over. Most receivers take a continuous
// stream of small relative values and let it coast to a stop. Two axes of the MIDI
// editor cannot: they are discrete receivers, so a stream or a coast has nowhere to
// land and just reads as a lurch. Naming the three cases keeps the distinction in one
// place instead of scattered flags.
enum class Delivery
{
  kStream,    // continuous relative values, with the normal glide (everything else)
  kStepUnits, // whole 7-bit units over time -- MIDI editor vertical SCROLL, which
              // moves in whole note rows and cannot express a sub-unit step
  kImmediate  // the whole notch at once, no glide -- MIDI editor vertical ZOOM,
              // a fixed pixel scale, so coasting has nowhere to land
};

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
  g_glideOn = true;

  double v = 0.0;
  if (LoadDouble("start", &v)) g_startPct = v;
  if (LoadDouble("accel", &v)) g_accelPct = v;
  if (LoadDouble("release", &v)) g_releaseMs = v;
  if (LoadDouble("hold", &v)) g_hold = v;
  if (LoadDouble("coast", &v)) g_coast = v;
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
    {0, 988, true, DRIVE_REPLAY},  // View: Scroll horizontally
    {0, 977, true, DRIVE_REPLAY},  // View: Scroll horizontally reversed
    {0, 989, false, DRIVE_REPLAY}, // View: Scroll vertically
    {0, 978, false, DRIVE_REPLAY}, // View: Scroll vertically reversed
    // ---- main section: zooming ----
    {0, 990, true, DRIVE_REPLAY},   // View: Zoom horizontally
    {0, 979, true, DRIVE_REPLAY},   // View: Zoom horizontally reversed
    {0, 1000, false, DRIVE_REPLAY}, // View: Zoom vertically
    {0, 1001, false, DRIVE_REPLAY}, // View: Zoom vertically reversed
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
//   DRIVE_REPLAY : raw wheel units
static void ApplyMixerScrollTracks(int deltaTracks); // defined below DeliverTravel

static void ApplyTravelNow(Integrator &g, double signedTravel)
{
  const double mag = fabs(signedTravel);
  if (mag <= 0.0)
    return;

  switch (g.drive)
  {
  case DRIVE_NONE:
    return;
  case DRIVE_MIXER_SCROLL:
  {
    // Release at or below the sync threshold: step the whole track(s) at once. Same
    // one-track granularity, just without the glide.
    const double tracks = signedTravel / kMixerUnitsPerTrack;
    const int whole = (int)((tracks >= 0.0) ? floor(tracks + 0.5) : ceil(tracks - 0.5));
    if (whole != 0)
      ApplyMixerScrollTracks(-whole);
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

  g.glide.Kick(unit * units, sign, identity, Now(), AnimParams());

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

// Deliver a (fractional, signed) amount of travel in the drive's own unit.
// Whole-unit drives (replay/list) accumulate the fraction so slow motion still
// advances and fast motion does not dump many units in one frame.
// Move the mixer by (re)positioning its leftmost visible track.
//
// There is NO action to replay for the mixer: its wheel is a Mouse Modifier behaviour
// (MM_CTX_MCP_MOUSEWHEEL -> "Scroll MCP"), so the only interface is SetMixerScroll, which
// takes a track -- one whole track is the smallest step it can express. Whole-track steps
// are therefore delivered over time with the fraction carried, exactly like the MIDI
// editor's pitch axis (Delivery::kStepUnits). That is the finest this receiver can be
// asked for; what the glide adds is the TIMING -- the steps are spread out and continue
// after the last notch, instead of one instant jump per notch and stopping dead.
//
// deltaTracks is signed: positive moves toward HIGHER track numbers (to the right).
static void ApplyMixerScrollTracks(int deltaTracks)
{
  if (deltaTracks == 0 || !GetMixerScroll || !SetMixerScroll || !CountTracks || !GetTrack)
    return;
  const int n = CountTracks(nullptr);
  if (n <= 0)
    return;
  // Where the mixer currently is: the leftmost visible track. Found by pointer, so this
  // follows whatever the user did natively in between.
  MediaTrack *left = GetMixerScroll();
  int idx = 0;
  if (left)
    for (int i = 0; i < n; ++i)
      if (GetTrack(nullptr, i) == left)
      {
        idx = i;
        break;
      }
  int target = idx + deltaTracks;
  if (target < 0) target = 0;
  if (target > n - 1) target = n - 1;
  if (target != idx)
    SetMixerScroll(GetTrack(nullptr, target));
}

static void DeliverTravel(Integrator &g, double step)
{
  if (step == 0.0)
    return;

  if (g.drive == DRIVE_MIXER_SCROLL)
  {
    g.accum += step;
    // The mixer's own step is one track per notch. The glide's travel is in the action's
    // 7-bit units, one notch of INPUT being kNotchUnits -- so this converts travel into
    // whole tracks and carries whatever is left, rather than dropping it, so slow motion
    // arrives as the glide accumulates. See kMixerUnitsPerTrack for the scale and why it
    // is a named constant.
    const double tracks = g.accum / kMixerUnitsPerTrack;
    const int whole = (int)fabs(tracks);
    if (whole >= 1)
    {
      const int signedTracks = (tracks < 0.0) ? -whole : whole;
      // The glide's positive sign means "wheel up". Wheel-up on the mixer should reveal
      // EARLIER tracks, i.e. move toward lower indices -- hence the negation.
      ApplyMixerScrollTracks(-signedTracks);
      g.accum -= signedTracks * kMixerUnitsPerTrack;
      if (kDebugLog)
        Log("MIXER emit %d track(s); accum left %.4f (units/track=%.3f)", signedTracks,
            g.accum, kMixerUnitsPerTrack);
    }
    return;
  }

  if (g.drive == DRIVE_REPLAY)
  {
    g.accum += step;
    if (g.delivery == Delivery::kStepUnits)
    {
      // Whole units only. This receiver (the MIDI editor's pitch axis) steps in whole
      // note rows, so a sub-unit stream cannot be represented: it can only land as an
      // occasional row step, which reads as a lurch at the tail of a roll. Whole 7-bit
      // units are still a relative value and still delivered over time -- the arrival
      // is just stepped, which is the finest this receiver can express. The fraction
      // is carried, so nothing is lost.
      const double m = floor(fabs(g.accum) + 0.5);
      if (m >= 1.0)
      {
        const int whole = (int)m;
        const double signed_send = (g.accum < 0.0) ? -(double)whole : (double)whole;
        g.accum -= signed_send;
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
    TickIntegrator(g, dt);
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
    Log("MATCH sec=%d cmd=%d relmode=%d val=%d hwnd=%p -> drive=%d", sec->uniqueID, command,
        relmode, val, (void *)hwnd, (int)spec.drive);

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

// Standard mouse wheel only: a notch is a whole WHEEL_DELTA multiple, and the
// message must not be touch/pen-injected (Windows tags those in the extra info).
// Anything else -- a high-resolution/free-spinning wheel, a touchpad, touch or pen --
// is left entirely to REAPER, so this plugin only ever changes the plain wheel.
static const LPARAM kTouchSignature = 0xFF515700; // MI_WP_SIGNATURE
static const LPARAM kTouchMask = 0xFFFFFF00;

static bool IsStandardMouseNotch(int delta)
{
  if (delta == 0 || (delta % WHEEL_DELTA) != 0)
    return false;
  const LPARAM extra = GetMessageExtraInfo();
  if ((extra & kTouchMask) == kTouchSignature)
    return false;
  return true;
}

static LRESULT CALLBACK GetMsgProc(int code, WPARAM wParam, LPARAM lParam)
{
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

      // Standard mouse wheel only; anything else (high-resolution wheel,
      // touchpad, touch/pen) is left entirely to REAPER, latch cleared so the
      // action path stays out of it too.
      if (!IsStandardMouseNotch(delta))
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
          Kick(g_vert, DRIVE_REPLAY, 0, cmd, actSign, notches, under);
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
      // it has to be driven directly through SetMixerScroll rather than replayed.
      //
      // The mixer is a deeper window than the track panel (REAPERwnd -> #32770 -> #32770
      // -> REAPERMCPDisplay), so its class is looked for up the parent chain, not on the
      // window under the cursor. Driven only while the user still has "Scroll MCP" on that
      // combination; anything else (unassigned, or a custom choice) is left to REAPER.
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
          char asg[64] = {0};
          PanelWheelAssignment("MM_CTX_MCP_MOUSEWHEEL", shift, ctrl, alt, asg, sizeof(asg));
          // With the API present, only the built-in "Scroll MCP" is ours; unassigned
          // ("0") and anything the user picked instead pass straight through. Without the
          // API, fall back to the plain wheel, which is what the default is bound to.
          const bool isScrollMcp = (asg[0] == 0) ? plain : (strcmp(asg, "1 m") == 0);
          if (isScrollMcp)
          {
            const double notches = fabs(delta) / 120.0;
            Kick(g_horz, DRIVE_MIXER_SCROLL, 0, 0, actSign, notches, mcp);
            if (kDebugLog)
              Log("TAKE #%ld mcp assignment=\"%s\" notches=%.2f", wheelSeq, asg, notches);
            StartTimer();
            InterlockedExchange(&g_wheelTick, 0);
            m->message = WM_NULL; // swallowed; REAPER's own Scroll MCP must not also run
            return CallNextHookEx(g_msgHook, code, wParam, lParam);
          }
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
            Kick(g_vert, DRIVE_REPLAY, 0, cmd, actSign, notches, surface);
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
// Five sliders, one per feel group, plus the on/off switch. REAPER's own look is
// used: the control colours are read from the theme through GetThemeColor, and the
// window follows the theme's light/dark state automatically (a theme with a dark
// background gets light text, and vice versa). If a theme colour cannot be read the
// code falls back to the Windows system colour, so a window is always legible.

enum
{
  IDC_CHK_GLIDE = 1001,
  IDC_S_START = 1010,
  IDC_S_ACCEL,
  IDC_S_RELEASE,
  IDC_S_HOLD,
  IDC_S_COAST,
  IDC_L_START = 1110, // value labels; order must match the sliders above
  IDC_L_ACCEL,
  IDC_L_RELEASE,
  IDC_L_HOLD,
  IDC_L_COAST,
};

static const int kNumSliders = 5;

struct SliderSpec
{
  int sliderId, labelId;
  const char *name;
  double *value;
  double min, max;      // slider positions 0..1000 map linearly to [min,max]
  const char *unit;
  const char *lomark;   // shown at the left end
  const char *himark;   // shown at the right end
};

static SliderSpec g_sliders[kNumSliders];
static int g_faderPos[kNumSliders]; // 0..1000, one per fader (parallel to g_sliders)

// --- Per-parameter colours --------------------------------------------------
//
// Each of the five settings owns one segment of the response curve, and each gets its own
// hue so the two can be matched at a glance: the thin line under a slider, that slider's
// thumb, and the part of the curve it drives all share the hue. Saturation carries the
// VALUE -- fully saturated at the slider's maximum, faint at the minimum -- so how much a
// setting is contributing is visible without reading the number. The faint end is kept
// above zero saturation on purpose: a setting at its minimum still shows its colour (it is
// still doing something), it is just pale.
//
// Hues are spread so neighbours are easy to tell apart, and all are light enough to read on
// a dark panel (ResolveTheme keeps them as-is in dark mode and darkens them for light mode,
// so they hold up on either).
static const COLORREF kSliderHue[kNumSliders] = {
    RGB( 90, 190, 255), // Start  - blue
    RGB(120, 225, 140), // Accel  - green
    RGB(255, 205,  90), // Hold   - amber
    RGB(255, 150, 110), // Coast  - orange
    RGB(200, 160, 255), // Release- violet
};

// Blend between the panel background and the hue by `amount` (0..1). Amount 0 gives a colour
// barely off the background (the faint end) rather than the background itself, so the hue is
// always traceable. Defined with the theme (it needs g_theme.card).
static COLORREF SliderColor(int idx, double amount);
static double SliderAmount(int idx);

static HWND g_cfgWnd = nullptr;
static bool g_cfgUpdating = false;
// The controls that need repositioning on resize. Held by handle rather than looked up
// by id, because the end-cap labels have no id of their own.
static HWND g_chkGlide = nullptr;
static HWND g_capLo[kNumSliders], g_capHi[kNumSliders];
// (No footer label handles: the two footer texts were removed -- they occupied space
//  without telling the user anything they could act on.)
// Scrolling state. The panel keeps its compact spacing always; when the window is too
// short for the content it scrolls instead of squeezing the rows. That is what makes
// adding more parameters later safe -- they just extend the scrollable height.
static int g_contentH = 0;   // full height the content needs
static int g_scrollY = 0;    // current scroll offset, >= 0
static int g_scrollStep = 40;
// Remembered so the window theme is re-applied exactly when the scrollbar is shown or
// hidden, and not on every layout pass. Reset with the window.
static bool g_barThemed = false;
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
  const COLORREF c = kSliderHue[idx];
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

static void ResolveTheme()
{
  // Try the theme's own window-background keys in order and take the first that
  // resolves, so the panel matches the theme whatever this REAPER version names it.
  // A theme that cannot be read falls back to a system colour, and the choice is
  // logged so it is never a guess.
  static const char *kBgKeys[] = {"col_main_bg", "col_arrangebg", "col_tl_bg",
                                  "col_mixerbg", "window_bg"};
  int raw = -1;
  const char *usedKey = "(none)";
  if (GetThemeColor)
    for (size_t i = 0; i < sizeof(kBgKeys) / sizeof(kBgKeys[0]); ++i)
    {
      const int c = GetThemeColor(kBgKeys[i], 0);
      if (c >= 0) { raw = c; usedKey = kBgKeys[i]; break; }
    }
  const COLORREF bg = (raw >= 0) ? (COLORREF)(raw & 0xFFFFFF) : GetSysColor(COLOR_BTNFACE);
  g_theme.bg = bg;

  // Perceived brightness (Rec. 601) decides light-on-dark vs dark-on-light, so the
  // panel follows the theme with no dark-mode query at all. That is deliberate: REAPER
  // has had a dark-mode API on and off across versions, so deriving it from the theme's
  // own background colour keeps working regardless.
  const int lum = (GetRValue(bg) * 299 + GetGValue(bg) * 587 + GetBValue(bg) * 114) / 1000;
  g_theme.dark = (lum < 128);
  Log("theme: %s raw=%d -> bg=%02x%02x%02x lum=%d %s", usedKey, raw, GetRValue(bg),
      GetGValue(bg), GetBValue(bg), lum, g_theme.dark ? "dark" : "light");

  if (g_theme.dark)
  {
    g_theme.text = RGB(235, 235, 235);
    g_theme.sub = RGB(170, 170, 170);
    g_theme.axisText = RGB(130, 130, 130); // tick numbers: there, but not shouty
    g_theme.edit = RGB(48, 48, 48);
    g_theme.track = RGB(70, 70, 70);
  }
  else
  {
    g_theme.text = RGB(30, 30, 30);
    g_theme.sub = RGB(110, 110, 110);
    g_theme.axisText = RGB(150, 150, 150);
    g_theme.edit = RGB(255, 255, 255);
    g_theme.track = RGB(205, 205, 205);
  }

  // Fader groove and thumb. gen_vol* is what REAPER's generic windows use, but it is
  // NOT always readable through GetThemeColor (it returned -1 on this dark theme), so
  // col_buttonbg / col_main_bg2 -- which are readable -- are tried as well. Whatever
  // is not provided is DERIVED from the background, which guarantees a usable value on
  // any theme.
  struct Cand { const char *key; int *slot; int fallbackStep; };
  int fbg = -1, fth = -1, fcard = -1, fline = -1;
  if (GetThemeColor)
  {
    static const char *kGroove[] = {"gen_volbg_horz", "col_main_bg2", "col_main_editbk"};
    static const char *kThumb[]  = {"gen_volthumb_horz", "col_buttonbg", "col_main_3dhl"};
    static const char *kCard[]   = {"col_buttonbg", "col_main_bg2"};
    static const char *kLine[]   = {"col_main_3dsh", "col_tl_bg"};
    for (size_t i = 0; i < 3 && fbg < 0; ++i)  fbg  = GetThemeColor(kGroove[i], 0);
    for (size_t i = 0; i < 3 && fth < 0; ++i)  fth  = GetThemeColor(kThumb[i], 0);
    for (size_t i = 0; i < 2 && fcard < 0; ++i) fcard = GetThemeColor(kCard[i], 0);
    for (size_t i = 0; i < 2 && fline < 0; ++i) fline = GetThemeColor(kLine[i], 0);
  }

  // Derive anything the theme did not give. "+step" lifts a dark theme and darkens a
  // light one by the same small amount, so structure reads on either.
  // The outline must be clearly readable AGAINST the card, not just against the
  // background, so the step is larger than a subtle one. Measured requirement: a
  // 1px line whose contrast with its own fill is under ~20 levels is invisible.
  const int step = (lum < 128) ? 30 : -30;
  const COLORREF dcard = RGB(ClampInt(GetRValue(bg) + step / 2, 0, 255),
                             ClampInt(GetGValue(bg) + step / 2, 0, 255),
                             ClampInt(GetBValue(bg) + step / 2, 0, 255));
  const COLORREF dline = RGB(ClampInt(GetRValue(bg) + step, 0, 255),
                             ClampInt(GetGValue(bg) + step, 0, 255),
                             ClampInt(GetBValue(bg) + step, 0, 255));
  // The thumb must be clearly brighter (dark theme) or darker (light theme) than the
  // card so the handle is obviously a handle.
  const COLORREF dthumb = RGB(ClampInt(GetRValue(bg) + step * 4, 0, 255),
                              ClampInt(GetGValue(bg) + step * 4, 0, 255),
                              ClampInt(GetBValue(bg) + step * 4, 0, 255));

  g_theme.card  = (fcard >= 0) ? (COLORREF)(fcard & 0xFFFFFF) : dcard;
  g_theme.line  = (fline >= 0) ? (COLORREF)(fline & 0xFFFFFF) : dline;
  g_theme.faderBg = (fbg >= 0) ? (COLORREF)(fbg & 0xFFFFFF) : dline;
  g_theme.faderThumb = (fth >= 0) ? (COLORREF)(fth & 0xFFFFFF) : dthumb;
  // Gridlines: a whisper of the line colour, between the card and the outline. It must be a
  // FIXED step from the background (not derived from `line`, which can be a theme colour
  // that is already close to the card) so it is always just barely visible on any theme.
  g_theme.grid = RGB(ClampInt(GetRValue(bg) + step / 4, 0, 255),
                     ClampInt(GetGValue(bg) + step / 4, 0, 255),
                     ClampInt(GetBValue(bg) + step / 4, 0, 255));

  // Contrast guards. A groove that matches the card would be invisible, and a thumb
  // that matches its groove would vanish -- both are checked and replaced with the
  // derived colours, so the fader is always readable whatever the theme supplies.
  int dr = abs((int)GetRValue(g_theme.faderThumb) - (int)GetRValue(g_theme.faderBg));
  int dg = abs((int)GetGValue(g_theme.faderThumb) - (int)GetGValue(g_theme.faderBg));
  int db = abs((int)GetBValue(g_theme.faderThumb) - (int)GetBValue(g_theme.faderBg));
  if (dr + dg + db < 60)
    g_theme.faderThumb = dthumb;

  dr = abs((int)GetRValue(g_theme.faderBg) - (int)GetRValue(g_theme.card));
  dg = abs((int)GetGValue(g_theme.faderBg) - (int)GetGValue(g_theme.card));
  db = abs((int)GetBValue(g_theme.faderBg) - (int)GetBValue(g_theme.card));
  if (dr + dg + db < 40)
    g_theme.faderBg = dline;   // one step further from the card

  Log("theme: card=%06x line=%06x groove=%06x thumb=%06x",
      (unsigned)g_theme.card, (unsigned)g_theme.line,
      (unsigned)g_theme.faderBg, (unsigned)g_theme.faderThumb);
}

static void UpdateLabels()
{
  char buf[128];
  for (int i = 0; i < kNumSliders; ++i)
  {
    const SliderSpec &s = g_sliders[i];
    const double step = (s.max - s.min) / 1000.0;
    const int dec = (step >= 0.1) ? 1 : 2;
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
static const double kCurveYSpace = 1.12;    // headroom above the peak, so it does not touch

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
      g.Kick(OneNotchUnit(), +1.0, 1, t, P);
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
// This draws a SCHEMATIC of what the five settings do, not the model's raw trace. Each
// setting owns one segment of the silhouette, which is how the controls read:
//
//   Start    the first rise; a larger value makes it steeper
//   Accel    the second rise, which only ever goes UP (never flat, never down)
//   Hold     the curvature of that second rise's later half: 0 presses it flat, larger values
//            let it climb more. However flat it is pressed, the summit stays the summit
//   Coast    the first descent; its length goes to zero with the parameter, so at 0 the summit
//            meets Release directly and the fall is a single segment
//   Release  the final tail down to zero, longer with the parameter
//
// The axis NUMBERS stay real: X ends at the model's own settle time and Y tops out at the
// model's own peak speed (see CurveAnchors), so the labels keep their meaning even though the
// shape is schematic. Drawing the raw trace instead tied the picture to however the mechanisms
// happened to interact that run, which produced double-humped or flattened shapes at some
// settings -- and those mechanisms are not what these five controls are for.
static void BuildCurve(const anim::Params &P, float *ys, int n)
{
  for (int i = 0; i < n; ++i)
    ys[i] = 0.0f;

  double peakV = 0.0, settleMs = 0.0;
  CurveAnchors(P, &peakV, &settleMs);
  if (peakV <= 0.0 || settleMs <= 0.0)
    return;

  // Publish the axis ranges: real values, so the ticks mean something.
  g_curveTFromMs = 0.0;
  g_curveTEndMs = settleMs;
  g_curveAxisY = peakV * kCurveYSpace;

  // Segment widths, as fractions of the whole. Start and Accel take the rise, with Accel in
  // two halves so that Hold has a "later half" to bend. Coast and Release share the fall.
  const double wStart = 0.30;
  const double wAccelHalf = 0.21; // Accel is 0.42 wide, in two halves

  // Each rising stretch is defined by an ANGLE, not a raw slope, so the numbers are the ones a
  // reader thinks in ("the first part comes up at about 40 degrees"). The angle is turned into
  // a slope in the plot's own normalised space, so the drawn angle also depends on the
  // window's aspect -- which is expected, and is why these are relative values.
  //
  // Ranges, as agreed:
  //   Start  10 deg (min) .. 60 deg (max)   -- wide, so the setting is clearly visible
  //   Accel  15 deg (min) .. 60 deg (max)   -- never 0: the wheel's own speed is always a
  //                                            factor, so the second stretch always climbs
  //   Hold    3 deg (min) .. 35 deg (max)   -- never flat: at minimum the later half still
  //                                            climbs, just so gently it reads as levelling off
  struct Deg { static double Slope(double d) { return tan(d * 3.14159265358979 / 180.0); } };

  const double startAt = g_startPct / kStartMaxPct;
  const double accelAt = (kAccelMaxPct > kAccelMinPct)
                             ? Clamp((g_accelPct - kAccelMinPct) / (kAccelMaxPct - kAccelMinPct),
                                     0.0, 1.0)
                             : 0.0;
  const double holdAt = (kHoldMax > kHoldMin) ? Clamp(g_hold / kHoldMax, 0.0, 1.0) : 0.0;

  const double sStart = Deg::Slope(10.0 + 50.0 * startAt);
  const double sAccel = Deg::Slope(15.0 + 45.0 * accelAt);
  const double sHold = Deg::Slope(3.0 + 32.0 * holdAt);

  const double yStart = wStart * sStart;
  const double yAccelMid = yStart + wAccelHalf * sAccel;
  const double xSummit = wStart + 2.0 * wAccelHalf;
  const double ySummit = yAccelMid + wAccelHalf * sHold;
  if (ySummit <= 0.0)
    return;

  // Coast: how long the first descent runs. Hold lengthens it -- a roll that keeps its energy
  // also carries further past the last notch -- and Coast is the setting that adds to that.
  const double wCoast = g_coast * (0.10 + 0.12 * g_hold);
  // Release: the final tail. Kept short relative to the rise; the earlier value left the
  // descent owning too much of the width.
  const double relNorm = Clamp((g_releaseMs - kReleaseMinMs) / (kReleaseMaxMs - kReleaseMinMs),
                               0.0, 1.0);
  const double wRelease = 0.20 + 0.28 * relNorm;
  const double xTotal = xSummit + wCoast + wRelease;

  // Segment joints for the colouring: the five settings' stretches, in normalised x. A
  // zero-width coast is dropped so the summit joins Release directly.
  int nj = 0;
  g_curveJoint[nj++] = 0.0;                              // start of Start
  g_curveJoint[nj++] = wStart / xTotal;                  // start of Accel
  g_curveJoint[nj++] = (wStart + wAccelHalf) / xTotal;   // start of Hold
  g_curveJoint[nj++] = xSummit / xTotal;                 // start of Coast (the summit)
  if (wCoast > 1e-9)
    g_curveJoint[nj++] = (xSummit + wCoast) / xTotal;    // start of Release
  g_curveJoint[nj++] = 1.0;                              // end
  g_curveJoints = nj;
  if (xTotal <= 0.0)
    return;

  // Breakpoints in normalised space (both axes 0..1), joined by a MONOTONE CUBIC spline.
  //
  // A polyline would put a visible corner at every join, and at the summit a sharp spike --
  // the picture should read as a motion, which is curved. A plain Catmull-Rom would round the
  // corners but overshoot: it can rise above the summit and dip below zero between points,
  // which is visibly wrong. A monotone cubic (Fritsch-Carlson) is the fit that is smooth at
  // every join AND cannot overshoot, so each segment's rise/fall stays monotonic, which is
  // exactly the rule the controls promise (Accel only ever goes up, Hold only bends it).
  //
  // The rise needs only four points; the release is sampled so its ease-out is followed
  // closely.
  static double bx[64], by[64];
  int nb = 0;
  bx[nb] = 0.0;                                 by[nb++] = 0.0;
  bx[nb] = wStart / xTotal;                     by[nb++] = yStart;
  bx[nb] = (wStart + wAccelHalf) / xTotal;      by[nb++] = yAccelMid;
  bx[nb] = xSummit / xTotal;                    by[nb++] = ySummit;

  // Where the fall starts and how far it has dropped by the end of the coast.
  //
  // How FAR the coast drops scales with the coast setting, so the head of the release
  // follows it continuously: at 0 there is no coast segment and the release begins at the
  // summit; as the setting grows the release begins lower and lower. A fixed drop (it was a
  // flat 45%) made a cliff -- the tiniest coast snapped the release head down to 45%, which
  // looked like a step rather than a longer descent.
  double xFallStart = xSummit / xTotal, yFallStart = ySummit;
  const double xCoastEnd = (xSummit + wCoast) / xTotal;
  double coastDrop = g_coast * 0.275; // 0 -> 0, 2 -> 0.55
  if (coastDrop > 0.55)
    coastDrop = 0.55;
  const double yCoastEnd = ySummit * (1.0 - coastDrop);
  if (wCoast > 1e-9)
  {
    bx[nb] = xCoastEnd; by[nb++] = yCoastEnd;
    xFallStart = xCoastEnd; yFallStart = yCoastEnd;
  }

  // Release: an ease-out from where the fall got to, down to zero.
  const int kRelPts = 16;
  for (int k = 1; k <= kRelPts && nb < 63; ++k)
  {
    const double u = (double)k / kRelPts;
    const double e = 1.0 - u;
    bx[nb] = xFallStart + (1.0 - xFallStart) * u;
    by[nb++] = yFallStart * e * e;
  }

  // Fritsch-Carlson tangents: the secants between points, then limited so the cubic cannot
  // overshoot. Where the direction reverses (the summit, the floor) the tangent is zeroed, so
  // those points come out as smooth extrema rather than corners.
  static double sec[64], tan[64];
  for (int i = 0; i < nb - 1; ++i)
    sec[i] = (by[i + 1] - by[i]) / ((bx[i + 1] > bx[i]) ? (bx[i + 1] - bx[i]) : 1e-9);
  tan[0] = sec[0];
  tan[nb - 1] = sec[nb - 2];
  for (int i = 1; i < nb - 1; ++i)
    tan[i] = (sec[i - 1] * sec[i] <= 0.0) ? 0.0 : (sec[i - 1] + sec[i]) * 0.5;
  for (int i = 0; i < nb - 1; ++i)
  {
    if (sec[i] == 0.0)
    {
      tan[i] = 0.0;
      tan[i + 1] = 0.0;
      continue;
    }
    const double a = tan[i] / sec[i], b = tan[i + 1] / sec[i];
    const double s = a * a + b * b;
    if (s > 9.0)
    {
      const double t = 3.0 / sqrt(s);
      tan[i] = t * a * sec[i];
      tan[i + 1] = t * b * sec[i];
    }
  }

  const double yScale = 1.0 / ySummit;
  int seg = 0;
  for (int i = 0; i < n; ++i)
  {
    const double u = (double)i / (double)(n - 1);
    while (seg + 2 < nb && bx[seg + 1] < u)
      ++seg;
    const double x0 = bx[seg], x1 = bx[seg + 1];
    const double h = (x1 > x0) ? (x1 - x0) : 1e-9;
    double t = (u - x0) / h;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    const double t2 = t * t, t3 = t2 * t;
    // Hermite basis.
    const double h00 = 2 * t3 - 3 * t2 + 1;
    const double h10 = t3 - 2 * t2 + t;
    const double h01 = -2 * t3 + 3 * t2;
    const double h11 = t3 - t2;
    const double y = h00 * by[seg] + h10 * h * tan[seg] + h01 * by[seg + 1] +
                     h11 * h * tan[seg + 1];
    double f = y * yScale;
    if (f < 0.0)
      f = 0.0;
    if (f > 1.0)
      f = 1.0;
    ys[i] = (float)f;
  }
}
// Anti-aliased polylines, via GDI+ loaded on demand.
//
// GDI's Polyline is aliased: a thin diagonal line comes out as a visible staircase, which
// reads as grainy. GDI+ draws the same geometry anti-aliased. It is loaded the same way as
// uxtheme and dwmapi above -- on demand, never linked -- so the plugin keeps no hard
// dependency on it and simply falls back to the aliased GDI path if it is not there. The
// only consequence of its absence is a slightly rougher line.
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

// Draw one stretch of the response curve in the hue of the setting that owns it: the colour
// is that slider at its current strength, so the curve and the control agree at a glance.
// Anti-aliased via GDI+ where available, plain GDI otherwise.
static void DrawCurveRun(HDC dc, const POINT *pts, int n, int segIdx)
{
  if (n < 2 || segIdx < 0 || segIdx >= kNumSliders)
    return;
  const COLORREF col = SliderColor(segIdx, SliderAmount(segIdx));
  if (!gdiplus_util::drawPolyline(dc, pts, n, col, 1))
  {
    HPEN pen = CreatePen(PS_SOLID, 1, col);
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
  static bool have = false;
  static double lStart = 0, lAccel = 0, lRelease = 0, lHold = 0, lCoast = 0;
  if (!have || lStart != g_startPct || lAccel != g_accelPct || lRelease != g_releaseMs ||
      lHold != g_hold || lCoast != g_coast)
  {
    BuildCurve(AnimParams(), ys, kN);
    lStart = g_startPct;
    lAccel = g_accelPct;
    lRelease = g_releaseMs;
    lHold = g_hold;
    lCoast = g_coast;
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
    pts[i].x = x0 + (int)((double)spanX * i / (kN - 1) + 0.5);
    pts[i].y = yb - 1 - (int)((double)spanY * ys[i] + 0.5);
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

  // The response curve sits below the parameter block and paints itself.
  if (g_hasCurve)
    PaintCurve(dc);

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

  // Groove: a thin bar down the middle of the control, in this slider's own hue so the
  // control and the curve segment it drives can be matched. The hue's saturation carries the
  // value, so the groove itself shows how much the setting is contributing.
  const int grooveH = 4;
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

// The master switch's caption. It is also the widest single line the panel has to keep
// on one line, so the minimum width is derived from it (see MinPanelWidth) rather than
// being a number that happens to look right at one font size.
static const char *kEnableText =
    "Enable smooth scrolling   (off = REAPER's native wheel)";

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
  return m;
}

// The height at which the panel needs no scrollbar. Nothing is reserved for a bar: at
// this size the content fits, so none is shown.
static int PanelIdealHeight(const PanelMetrics &m)
{
  return m.pad + m.headH + m.rowH * kNumSliders + m.curveGap + m.curveH + m.pad;
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
static void ApplyWindowTheme(HWND h, bool dark); // defined below (theming section)
static void ForgetWindowTheme();                  // defined with it; called on destroy
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

    // How tall does the check box want to be at this width? Its own answer, so the
    // wrap point is measured rather than estimated.
    int boxWanted = 0;
    if (g_chkGlide)
    {
      // DT_CALCRECT on the button's own DC gives the wrapped height for its width.
      RECT br = {0, 0, rc.right - m.pad * 2, 0};
      HDC dc = GetDC(g_chkGlide);
      HGDIOBJ oldFont = SelectObject(dc, UiFont(nullptr));
      char text[256] = {0};
      GetWindowTextA(g_chkGlide, text, sizeof(text));
      DrawTextA(dc, text, -1, &br, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
      SelectObject(dc, oldFont);
      ReleaseDC(g_chkGlide, dc);
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
  if (!h || !g_chkGlide)
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
  // A scrollbar that appears only AFTER the window theme was set (the panel is made
  // short, or REAPER docks it into a cramped slot) would otherwise be created with the
  // default light theme. Re-applying the window theme when the scrollbar comes and goes
  // keeps it dark whenever the panel is -- and costs nothing when nothing changed.
  if (needScroll != g_barThemed)
  {
    ApplyWindowTheme(h, g_theme.dark);
    g_barThemed = needScroll;
  }
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

  SetWindowPos(g_chkGlide, nullptr, m.pad, y0 + m.pad, usableW - m.pad * 2, m.headH,
               SWP_NOZORDER);

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
    SetWindowPos(g_capHi[i], nullptr, ix + iw - m.capW, ty + m.groupH, m.capW, m.barH,
                 SWP_NOZORDER);
    SetWindowPos(GetDlgItem(h, s.sliderId), nullptr, ix + m.capW + 8, ty + m.groupH,
                 iw - (m.capW + 8) * 2, m.barH, SWP_NOZORDER);
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

  g_chkGlide = CreateWindowExA(0, "BUTTON",
                               "Enable smooth scrolling   (off = REAPER's native wheel)",
                               WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                               0, 0, 10, 10, h, (HMENU)(INT_PTR)IDC_CHK_GLIDE,
                               g_hInst, nullptr);
  SendMessage(g_chkGlide, BM_SETCHECK, g_glideOn ? BST_CHECKED : BST_UNCHECKED, 0);

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
  }


  (void)m;
  ApplyFontToChildren(h);
}

// Give the window a dark title bar when the panel is in dark mode. Windows does not
// follow the theme by itself for a window an application creates, so the caption stays
// light without this.
//
// DwmSetWindowAttribute lives in dwmapi.dll, which is loaded on demand rather than
// linked: the attribute number differs between Windows builds (20 on Windows 10 2004
// and later, 19 before that), and both are tried. If neither is supported the call
// simply does nothing and the caption keeps the system colour -- cosmetic only.
#define SWSC_DWMWA_USE_IMMERSIVE_DARK_MODE_OLD 19
#define SWSC_DWMWA_USE_IMMERSIVE_DARK_MODE 20

static void ApplyDarkTitleBar(HWND h, bool dark)
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
  if (!fn)
    return;
  const BOOL v = dark ? TRUE : FALSE;
  if (FAILED(fn(h, SWSC_DWMWA_USE_IMMERSIVE_DARK_MODE, &v, sizeof(v))))
    fn(h, SWSC_DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, &v, sizeof(v));
}

// The window's own scrollbar is drawn by the window THEME, not by our painting, so it
// ignores the panel's colours and stays light inside a dark panel. Windows ships an
// (undocumented but stable since 1809) "DarkMode_Explorer" theme that renders the
// standard scrollbar and controls dark; applying it to the window is the supported way
// to get a dark scrollbar without owning the drawing. Light mode passes nullptr, which
// restores the default theme -- so this is a two-way switch, not a one-way darkening.
// Memo of the last theme applied to the panel, so the same request is not re-sent. See
// ApplyWindowTheme for why re-sending is harmful.
static HWND s_themeHwnd = nullptr;
static bool s_themeDark = false;
static bool s_themeHave = false;

static void ApplyWindowTheme(HWND h, bool dark)
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
  if (!fn)
    return;
  // Only when the wanted state actually differs from what was last applied to this window.
  //
  // SetWindowTheme SENDS WM_THEMECHANGED (verified), so re-applying the same theme is not
  // a no-op: it re-enters this window's handler, which applies it again. Measured: that
  // drives 31 levels of re-entry per call. The handler has its own guard against actually
  // recursing, but this check removes the storm at the source -- and it also avoids
  // rebuilding the theme brushes over and over, which is where the risk was.
  if (s_themeHwnd == h && s_themeDark == dark && s_themeHave)
    return;
  s_themeHwnd = h;
  s_themeDark = dark;
  s_themeHave = true;

  fn(h, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
  // A window's own WS_VSCROLL bar lives in its non-client area, so switching the theme
  // only takes effect once the frame is redrawn; invalidating the client area alone
  // leaves the scrollbar in the previous mode.
  RedrawWindow(h, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
}

static void ForgetWindowTheme()
{
  s_themeHwnd = nullptr;
  s_themeHave = false;
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
      // Must follow the BuildSliderSpecs order: Start, Accel, Hold, Coast, Release.
      static const double kDefaultPerSlider[kNumSliders] = {
          kDefaultStartPct, kDefaultAccelPct, kDefaultHold, kDefaultCoast,
          kDefaultReleaseMs};
      *s.value = kDefaultPerSlider[i];
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
  case WM_COMMAND:
    switch (LOWORD(wp))
    {
    case IDC_CHK_GLIDE:
    {
      // Toggling the switch never needs a restart: the hook stays installed and just
      // stops intercepting when off (see GetMsgProc / OnAction).
      // Ignore notifications raised while the window is being built (g_cfgUpdating),
      // and only act on a REAL change -- otherwise the initial fill would save a value
      // the user never chose (glide=0 then glide=1 was observed in the log).
      if (g_cfgUpdating)
        return 0;
      const bool on = (SendDlgItemMessageA(h, IDC_CHK_GLIDE, BM_GETCHECK, 0, 0) == BST_CHECKED);
      if (on != g_glideOn)
      {
        g_glideOn = on;
        SaveSettings();
      }
      return 0;
    }
    }
    break;
  case WM_CTLCOLORSTATIC:
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORLISTBOX:
    return OnCtlColor(msg, (HDC)wp, (HWND)lp);
  // WM_CTLCOLORBTN is deliberately NOT handled: returning a brush there turns off
  // visual styles for the button, so the check box would render in the old flat style
  // and look less like the rest of the application, not more.
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
    g_hasCards = false;
    g_chkGlide = nullptr;
    g_barThemed = false;
    g_rectTracking = false;
    // Forget the theme "last applied" memo: the next window is a different HWND, and the
    // memo is keyed on the handle, so this is belt-and-braces rather than required.
    ForgetWindowTheme();
    return 0;
  case WM_THEMECHANGED:
  case WM_SYSCOLORCHANGE:
  {
    // SetWindowTheme() below POSTS WM_THEMECHANGED (and WM_SYSCOLORCHANGE can arrive in
    // bursts), so without this guard the handler re-enters itself: the window theme is
    // applied, which raises the message, which applies the theme again -- an unbounded
    // recursion that hangs REAPER. Verified in isolation (SetWindowTheme re-sends it).
    //
    // The guard is a plain flag because the re-entry is synchronous: the nested message
    // is delivered before SetWindowTheme returns.
    static bool inThemeChange = false;
    if (inThemeChange)
      return 0;
    inThemeChange = true;

    ResolveTheme();
    RebuildThemeBrushes();
    ApplyWindowTheme(h, g_theme.dark);
    InvalidateRect(h, nullptr, TRUE);
    // Child statics draw their background with the brush we just replaced, so they must
    // repaint too or they keep the old theme's colour.
    for (HWND c = FindWindowExA(h, nullptr, nullptr, nullptr); c;
         c = FindWindowExA(h, c, nullptr, nullptr))
      InvalidateRect(c, nullptr, TRUE);

    inThemeChange = false;
    return 0;
  }
  }
  return DefWindowProcA(h, msg, wp, lp);
}

// The order the sliders appear in. Release is LAST on purpose: it is the one that reads
// best next to the response curve below it (it is the "how long does it take to settle"
// knob), and the five are otherwise unrelated, so the order is a presentation choice.
static void BuildSliderSpecs()
{
  g_sliders[0] = {IDC_S_START, IDC_L_START, "Start", &g_startPct,
                  kStartMinPct, kStartMaxPct, "%", "Slower", "Faster"};
  g_sliders[1] = {IDC_S_ACCEL, IDC_L_ACCEL, "Accel per notch", &g_accelPct,
                  kAccelMinPct, kAccelMaxPct, "%", "Gentler", "Stronger"};
  g_sliders[2] = {IDC_S_HOLD, IDC_L_HOLD, "High-speed hold", &g_hold,
                  kHoldMin, kHoldMax, "x", "Less", "More"};
  g_sliders[3] = {IDC_S_COAST, IDC_L_COAST, "High-speed coast", &g_coast,
                  kCoastMin, kCoastMax, "x", "Less", "More"};
  g_sliders[4] = {IDC_S_RELEASE, IDC_L_RELEASE, "Release", &g_releaseMs,
                  kReleaseMinMs, kReleaseMaxMs, "ms", "Quicker", "Longer"};
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
    // The window exists but may not be on screen. REAPER's docker HIDES a docked child
    // instead of destroying it when the docker is collapsed, so "exists" is not the
    // same as "visible"; without the branches below the command would foreground a
    // hidden window and appear to do nothing. Order: docked -> activate in the docker;
    // hidden -> show; otherwise -> raise.
    ApplyDarkTitleBar(g_cfgWnd, g_theme.dark);
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

  CreatePanelChildren(g_cfgWnd);
  LayoutControls(g_cfgWnd);
  // ASK the layout what it really needs and use that as the size: measure, do not
  // estimate, so the panel opens at exactly the size where nothing scrolls and cannot be
  // wrong at another font, DPI or theme. This also overrides the computed starting size
  // above, so there is one authority for "the designed size".
  //
  // Floating only: when docked, REAPER owns the size and the panel scrolls instead.
  if (!g_dockOn)
    FitWindowToContent(g_cfgWnd);
  ApplyDarkTitleBar(g_cfgWnd, g_theme.dark);
  ApplyWindowTheme(g_cfgWnd, g_theme.dark);

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
    // passed to the window (-1) rather than to REAPER. Only the two cases that matter:
    //   - the fader: left/right step the value, home/end go to the ends;
    //   - the check box: space toggles it.
    // Returning 1 here would EAT the key before the control saw it, which is why these
    // return -1 -- the control still needs its own WM_KEYDOWN.
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
    if (haveCls && !strcmp(cls, "Button") && msg->wParam == VK_SPACE)
      return -1; // to the window: space toggles the focused check box

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


static int g_healthCounter = 0;
static void OnTimer()
{
  if (g_shuttingDown)
    return;
  if (++g_healthCounter < 30)
    return;
  g_healthCounter = 0;
  InstallHook();
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

  // Reported to REAPER (and shown in its Extensions list). Keep in step with the
  // version in versions/ and the GitHub release tag.
  rec->Register("ext_name", (void *)"Smooth Wheel Scroll 1.5.3");
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
