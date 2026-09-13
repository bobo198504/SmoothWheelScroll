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
#include <commctrl.h>
#include <stdio.h>
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
#ifdef SWS_TUNING_UI
#define REAPERAPI_WANT_AddExtensionsMainMenu
#endif
#include "reaper_plugin.h"
#include "reaper_plugin_functions.h"

// The animation itself lives in a REAPER-free header so it can be exercised
// standalone (test/anim_sim.cpp). Everything below only classifies, feeds in and
// delivers -- it holds no motion math of its own.
#include "anim_core.h"

// ---------------------------------------------------------------------------
// Master switch
//
// true  = glide on: wheel-driven view scroll/zoom is smoothed (see below).
// false = glide off: the plugin is completely inert -- no hook, no timer, no
//         interception -- and REAPER's native wheel handling is untouched.
// ---------------------------------------------------------------------------
static const bool kGlideEnabled = true;

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
// Tunables. These are COMPILED-IN defaults (no ini): the values below are the
// tuned result and are what the plugin always starts from. The settings window
// still allows live experimenting for the current session, and Reset restores
// exactly these.
//
//   1. START    length of the FIRST notch's travel, in % of one wheel notch
//   2. ACCEL    velocity added by each further notch, in the same unit
//   3. RELEASE  time one notch takes to settle (the brake)
// ---------------------------------------------------------------------------
static const double kDefaultStartPct = 15.0;
static const double kDefaultAccelPct = 3.5;
static const double kDefaultReleaseMs = 150.0;

static double g_startPct = kDefaultStartPct;   // 1. START: first-notch travel
static double g_accelPct = kDefaultAccelPct;   // 2. ACCEL: extra per further notch
static double g_releaseMs = kDefaultReleaseMs; // 3. RELEASE: settle time per notch

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

// While a roll is already moving, a later notch ramps over this shorter window. The
// ramp is PER NOTCH (see anim_core.h), which removed the slam the shared ramp caused:
// with one shared ramp, a notch landing mid-ramp had S(x) already high, so ~84% of its
// velocity was injected instantly -- the speed jumped 10 -> 109 and read as stepping.
// A shorter window for those notches still eases them in while keeping the roll energy
// (measured: roll totals back within ~1.5% of 1.0.0; the lone notch is untouched).
static const double kMovingOnsetMs = 30.0;

static const double kBurstGapMs = 250.0; // gap that ends a burst

// Brake-by-rhythm (part of the model): a roll faster than kTempoRefMs loosens the
// brake up to kBrakeRelaxMax, so a quick roll keeps its energy instead of being
// stopped dead between notches. This is what makes a fast roll build up.
static const double kTempoRefMs = 120.0;
static const double kBrakeRelaxMax = 4.0;

static anim::Params AnimParams()
{
  anim::Params P;
  P.startPct = g_startPct;
  P.accelPct = g_accelPct;
  P.releaseMs = g_releaseMs;
  P.frictionPow = kFrictionPow;
  P.onsetRatio = kOnsetRatio;
  P.movingOnsetMs = kMovingOnsetMs;
  P.tempoRefMs = kTempoRefMs;
  P.relaxMax = kBrakeRelaxMax;
  P.burstGapMs = kBurstGapMs;
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

static const double kAccelMinPct = 0.0;   // slider minimum, no extra acceleration
static const double kAccelMaxPct = 100.0; // slider maximum

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

static const DWORD kWheelFlagMs = 250;   // wheel->action latch validity window

// ---------------------------------------------------------------------------
// Debug logging (compile-time gate)
// ---------------------------------------------------------------------------
#ifdef SWS_DEBUG_LOG
static const bool kDebugLog = true;
#else
static const bool kDebugLog = false;
#endif

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
static bool g_replaying = false;        // bypass flag for our own replay calls
static bool g_timerPeriodRaised = false; // whether timeBeginPeriod(1) is active

// ---------------------------------------------------------------------------
// Classification: which actions are view scroll / zoom, and how to drive them
// ---------------------------------------------------------------------------
enum Drive
{
  DRIVE_NONE = 0,
  DRIVE_REPLAY // re-invoke REAPER's own action with fractional relative values
};

#ifdef SWS_TUNING_UI
// Tuning window (defined further down, also only in a tuning build); its action id
// is handled in OnAction.
static void ShowConfigWindow();
static int g_cmdTune;
#endif

// Slider ranges, one source of truth for the window and the clamps. Settings are
// compiled in (no ini in this build); the window is for live experimenting and
// Reset returns to the kDefault* values.
static const double kStartMinPct = 5.0;
static const double kStartMaxPct = 100.0;
static const double kReleaseMinMs = 50.0;
static const double kReleaseMaxMs = 300.0;

static void RefreshDerived()
{
  if (g_startPct < kStartMinPct)
    g_startPct = kStartMinPct;
  else if (g_startPct > kStartMaxPct)
    g_startPct = kStartMaxPct;

  if (g_accelPct < kAccelMinPct)
    g_accelPct = kAccelMinPct;
  else if (g_accelPct > kAccelMaxPct)
    g_accelPct = kAccelMaxPct;

  if (g_releaseMs < kReleaseMinMs)
    g_releaseMs = kReleaseMinMs;
  else if (g_releaseMs > kReleaseMaxMs)
    g_releaseMs = kReleaseMaxMs;
}

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
  // True ONLY for the MIDI editor's vertical SCROLL (the pitch axis). That view moves
  // in whole note rows, so the sub-unit stream used everywhere else cannot be
  // expressed there and reads as a lurch at the end of a roll. Delivery for these is
  // stepped in whole units instead; see DeliverTravel. Nothing else is affected.
  bool pitchQuantized = false;
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
    //      The two VERTICAL SCROLL entries carry pitchQuantized: the piano roll moves
    //      in whole note rows, so they get whole-unit delivery (see ActionSpec).
    {32060, 40430, false, DRIVE_REPLAY}, // View: Zoom vertically
    {32060, 40431, true, DRIVE_REPLAY},  // View: Zoom horizontally
    {32060, 40432, false, DRIVE_REPLAY, false, true}, // View: Scroll vertically
    {32060, 40433, true, DRIVE_REPLAY},  // View: Scroll horizontally
    {32060, 40660, true, DRIVE_REPLAY},  // View: Scroll horizontally reversed
    {32060, 40661, false, DRIVE_REPLAY, false, true}, // View: Scroll vertically reversed
    {32060, 40662, true, DRIVE_REPLAY},  // View: Zoom horizontally reversed
    {32060, 40663, false, DRIVE_REPLAY}, // View: Zoom vertically reversed
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
//   - "one page"  : each call scrolls a whole page;
//   - "snap to theme-defined sizes": each call snaps one size step;
//   - track/envelope "height" adjusters: likewise step-based.
// (The mousewheel family above is not subject to this, since those actions take a
// relative amount by design.)
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

  const bool relativeFamily = StrHasI(nm, "mousewheel");
  if (!relativeFamily)
  {
    if (!strstr(nm, "wheel"))
      return false;
    if (!strstr(nm, "Zoom") && !strstr(nm, "Scroll"))
      return false;
    if (strstr(nm, "one page") || strstr(nm, "snap to theme") ||
        strstr(nm, "height") || strstr(nm, "Modify"))
      return false;
  }

  out.section = sid;
  out.command = command;
  out.horizontal = strstr(nm, "horizontally") != nullptr;
  out.drive = DRIVE_REPLAY; // reproduce exactly what the action does
  out.relativeAction = relativeFamily;
  // The MIDI editor's vertical scroll moves in whole note rows (see ActionSpec), so
  // it is delivered in whole units regardless of which path classified it. Keyed on
  // the axis and section, not on a specific id, so a re-worded name still matches.
  out.pitchQuantized = (sid == 32060) && !out.horizontal &&
                       strstr(nm, "Scroll") != nullptr;
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
  // Set for a receiver whose axis moves in whole steps (the MIDI editor's pitch rows):
  // its travel is delivered in whole units instead of sub-unit steps. See DeliverTravel.
  bool pitchQuantized = false;
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
static void ApplyTravelNow(Integrator &g, double signedTravel)
{
  const double mag = fabs(signedTravel);
  if (mag <= 0.0)
    return;

  switch (g.drive)
  {
  case DRIVE_NONE:
    return;
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
                 double units, HWND replayHwnd = nullptr, bool pitchQuantized = false)
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
  g.pitchQuantized = pitchQuantized;
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
  if (g_releaseMs <= kSyncReleaseMs)
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
static void DeliverTravel(Integrator &g, double step)
{
  if (step == 0.0)
    return;

  if (g.drive == DRIVE_REPLAY)
  {
    g.accum += step;
    if (g.pitchQuantized)
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

#ifdef SWS_TUNING_UI
  // Our own registered actions are dispatched through hookcommand2 (that is the
  // documented callback for custom_action), so handle them here rather than in a
  // separate hookcommand -- which never fires for a custom_action.
  if (command == g_cmdTune)
  {
    ShowConfigWindow();
    return true;
  }
#endif

  if (kDebugLog)
  {
    const DWORD lt = (DWORD)InterlockedCompareExchange(&g_wheelTick, 0, 0);
    Log("HOOK sec=%d cmd=%d val=%d val2=%d relmode=%d hwnd=%p latch=%d",
        sec ? sec->uniqueID : -999, command, val, val2, relmode, (void *)hwnd, lt ? 1 : 0);
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

  // Glide disabled: leave the action entirely to REAPER (native = no slide).
  if (!kGlideEnabled)
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
       spec.pitchQuantized);
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

      const bool plain = (GetKeyState(VK_SHIFT) & 0x8000) == 0 &&
                         (GetKeyState(VK_CONTROL) & 0x8000) == 0 &&
                         (GetKeyState(VK_MENU) & 0x8000) == 0;

      // Consume only when the cursor is on one of the surfaces we drive AND the
      // wheel is unmodified. Everything else falls through, where the latch is
      // armed so the action path (arrange scroll/zoom) can recognise the wheel.
      // The arrange view is NOT taken here: it resolves to an action, so it falls
      // through with the latch armed and the action path (hookcommand2) smooths it.
      // Here we only take the surfaces that produce no action of their own.
      const double actSign = (delta > 0) ? 1.0 : -1.0; // wheel-up = view-up
      char thing[128] = {0};
      HWND surface = kGlideEnabled && plain ? SurfaceWindow(under, m->pt, thing, sizeof(thing)) : nullptr;
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

      if (surface)
      {
        char cls[64] = {0};
        GetClassNameA(surface, cls, sizeof(cls));
        const double notches = fabs(delta) / 120.0;

        if (strcmp(cls, "MIDIPianoWindow") == 0)
        {
          // MIDI editor piano keys: vertical scroll via the editor's own action.
          // This is the SAME pitch axis the MIDI note area scrolls on, so it gets the
          // same whole-unit delivery (see ActionSpec::pitchQuantized).
          HWND editor = MIDIEditor_GetActive ? MIDIEditor_GetActive() : nullptr;
          Kick(g_vert, DRIVE_REPLAY, 32060, 40432, actSign, notches, editor, true);
          if (kDebugLog)
            Log("wheel -> piano keys notches=%.2f", notches);
        }
        else
        {
          // Track control panel (body, chrome or resize edge): the SAME vertical
          // track view scroll the arrange uses, so it goes through that action
          // (989) -- the plugin hands the action a value, it does not move a view.
          Kick(g_vert, DRIVE_REPLAY, 0, 989, actSign, notches, surface);
          if (kDebugLog)
            Log("wheel -> tcp notches=%.2f", notches);
        }

        StartTimer();
        InterlockedExchange(&g_wheelTick, 0);
        m->message = WM_NULL; // swallowed; the surface must not also jump
        return CallNextHookEx(g_msgHook, code, wParam, lParam);
      }

      // Not consumed here: arm the latch for the action path (arrange view).
      InterlockedExchange(&g_wheelTick, (LONG)GetTickCount());
    }
  }
  return CallNextHookEx(g_msgHook, code, wParam, lParam);
}

static void InstallHook()
{
  // Glide off: install nothing at all. The extension loads (so it is visible to
  // REAPER) but adds no hook, no timer work and no interception -- the wheel is
  // entirely REAPER's.
  if (!kGlideEnabled)
    return;

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
#ifdef SWS_TUNING_UI
enum
{
  IDC_S_START = 1001,
  IDC_S_ACCEL,
  IDC_S_RELEASE,
  IDC_L_START = 1101,
  IDC_L_ACCEL,
  IDC_L_RELEASE,
  IDC_L_HINT = 1201,
  IDC_B_RESET,
  IDC_B_CLOSE
};

struct SliderSpec
{
  int sliderId, labelId;
  const char *name;
  double *value;
  double min, max; // slider positions 0..1000 map linearly to [min,max]
  const char *unit;
};

static SliderSpec g_sliders[3];
static HWND g_cfgWnd = nullptr;
static bool g_cfgUpdating = false;

static double SliderToValue(const SliderSpec &s, int pos)
{
  return s.min + (s.max - s.min) * (pos / 1000.0);
}
static int ValueToSlider(const SliderSpec &s, double v)
{
  if (s.max <= s.min)
    return 0;
  int p = (int)((v - s.min) / (s.max - s.min) * 1000.0 + 0.5);
  if (p < 0) p = 0;
  if (p > 1000) p = 1000;
  return p;
}

static void UpdateLabels()
{
  char buf[128];
  for (int i = 0; i < 3; ++i)
  {
    const SliderSpec &s = g_sliders[i];
    const double step = (s.max - s.min) / 1000.0;
    const int dec = (step >= 0.1) ? 1 : 2;
    _snprintf(buf, sizeof(buf), "%s: %.*f %s", s.name, dec, *s.value, s.unit);
    SetWindowTextA(GetDlgItem(g_cfgWnd, s.labelId), buf);
  }
  // Show the ramp in the Start slider's own unit (percent of one wheel notch),
  // so the numbers predict the feel directly. Steps assume an unhurried rhythm;
  // rolling faster multiplies each step by the tempo boost.
  char hint[256];
  _snprintf(hint, sizeof(hint),
            "of one wheel notch, steady rhythm:  1st %.0f%%  2nd %.0f%%  3rd %.0f%%  4th %.0f%%   [faster rolling multiplies the steps]",
            g_startPct, g_startPct + g_accelPct, g_startPct + 2 * g_accelPct,
            g_startPct + 3 * g_accelPct);
  SetWindowTextA(GetDlgItem(g_cfgWnd, IDC_L_HINT), hint);
}

static void PushValuesToSliders()
{
  g_cfgUpdating = true;
  for (int i = 0; i < 3; ++i)
  {
    const SliderSpec &s = g_sliders[i];
    SendDlgItemMessageA(g_cfgWnd, s.sliderId, TBM_SETPOS, TRUE, ValueToSlider(s, *s.value));
  }
  g_cfgUpdating = false;
  UpdateLabels();
}

static LRESULT CALLBACK CfgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
  {
  case WM_HSCROLL:
  {
    if (g_cfgUpdating)
      break;
    HWND from = (HWND)lp;
    for (int i = 0; i < 3; ++i)
    {
      const SliderSpec &s = g_sliders[i];
      if (GetDlgItem(h, s.sliderId) == from)
      {
        const int pos = (int)SendMessage(from, TBM_GETPOS, 0, 0);
        *s.value = SliderToValue(s, pos);
        RefreshDerived();
        UpdateLabels();
        break;
      }
    }
    return 0;
  }
  case WM_COMMAND:
    switch (LOWORD(wp))
    {
    case IDC_B_RESET:
      g_startPct = kDefaultStartPct;
      g_accelPct = kDefaultAccelPct;
      g_releaseMs = kDefaultReleaseMs;
      RefreshDerived();
      PushValuesToSliders();
      return 0;
    case IDC_B_CLOSE:
      DestroyWindow(h);
      return 0;
    }
    break;
  case WM_CLOSE:
    DestroyWindow(h);
    return 0;
  case WM_DESTROY:
    g_cfgWnd = nullptr;
    return 0;
  }
  return DefWindowProcA(h, msg, wp, lp);
}

// Build the window once; ShowConfigWindow() creates or re-focuses it.
static void BuildSliderSpecs()
{
  g_sliders[0] = {IDC_S_START, IDC_L_START, "Start", &g_startPct, kStartMinPct, kStartMaxPct, "%"};
  g_sliders[1] = {IDC_S_ACCEL, IDC_L_ACCEL, "Accel per notch", &g_accelPct, kAccelMinPct, kAccelMaxPct, "%"};
  g_sliders[2] = {IDC_S_RELEASE, IDC_L_RELEASE, "Release", &g_releaseMs, kReleaseMinMs, kReleaseMaxMs, "ms"};
}

static void ShowConfigWindow()
{
  if (g_cfgWnd && IsWindow(g_cfgWnd))
  {
    SetForegroundWindow(g_cfgWnd);
    return;
  }
  BuildSliderSpecs();

  const char *cls = "SmoothWheelScrollCfg";
  static bool registered = false;
  if (!registered)
  {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = CfgProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = cls;
    RegisterClassA(&wc);
    registered = true;
  }

  // Simple fixed layout, top-to-bottom.
  const int margin = 16, labelH = 18, sliderH = 32, rowH = 54, width = 420;
  const int y0 = 14;
  g_cfgWnd = CreateWindowExA(WS_EX_TOOLWINDOW, cls, "Smooth Wheel Scroll - tuning",
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                             CW_USEDEFAULT, CW_USEDEFAULT, width, y0 + rowH * 3 + 70,
                             g_main, nullptr, g_hInst, nullptr);
  if (!g_cfgWnd)
    return;

  for (int i = 0; i < 3; ++i)
  {
    const SliderSpec &s = g_sliders[i];
    const int ly = y0 + rowH * i;
    CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_LEFT,
                    margin, ly, width - margin * 2, labelH, g_cfgWnd, (HMENU)(INT_PTR)s.labelId,
                    g_hInst, nullptr);
    HWND sl = CreateWindowExA(0, TRACKBAR_CLASSA, "",
                              WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                              margin, ly + labelH, width - margin * 2, sliderH,
                              g_cfgWnd, (HMENU)(INT_PTR)s.sliderId, g_hInst, nullptr);
    SendMessage(sl, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));
    SendMessage(sl, TBM_SETPAGESIZE, 0, 25);
  }

  const int hintY = y0 + rowH * 3 + 4;
  CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_LEFT,
                  margin, hintY, width - margin * 2, labelH * 2, g_cfgWnd,
                  (HMENU)(INT_PTR)IDC_L_HINT, g_hInst, nullptr);
  CreateWindowExA(0, "BUTTON", "Reset", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                  margin, hintY + labelH * 2 + 6, 80, 26, g_cfgWnd,
                  (HMENU)(INT_PTR)IDC_B_RESET, g_hInst, nullptr);
  CreateWindowExA(0, "BUTTON", "Close", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                  margin + 90, hintY + labelH * 2 + 6, 80, 26, g_cfgWnd,
                  (HMENU)(INT_PTR)IDC_B_CLOSE, g_hInst, nullptr);

  PushValuesToSliders();
  ShowWindow(g_cfgWnd, SW_SHOW);
  SetForegroundWindow(g_cfgWnd);
}

// Extensions menu entry. REAPER calls this for each customizable menu:
//   flag 0 = the menu is being initialised (only the first time ever)
//   flag 1 = the menu is about to be shown (every time)
// We add on flag 1 rather than 0: the "Main extensions" menu in this install was
// already initialised by other extensions before we loaded, so flag 0 never
// arrives here, while flag 1 always does. A presence check avoids duplicating the
// item when the menu does get initialised by us (or is shown repeatedly).
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
  MENUITEMINFOA mi = {0};
  mi.cbSize = sizeof(mi);
  mi.fMask = MIIM_ID | MIIM_STRING | MIIM_STATE;
  mi.wID = (UINT)g_cmdTune;
  mi.fState = MFS_ENABLED;
  mi.dwTypeData = (LPSTR)"Smooth Wheel Scroll settings...";
  InsertMenuItemA(hm, GetMenuItemCount(hm), TRUE, &mi);
}
#endif // SWS_TUNING_UI

static void RemoveAll()
{
  g_shuttingDown = true;
  StopTimer();
#ifdef SWS_TUNING_UI
  if (g_cfgWnd && IsWindow(g_cfgWnd))
  {
    DestroyWindow(g_cfgWnd);
    g_cfgWnd = nullptr;
  }
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

extern "C" __declspec(dllexport) int ReaperPluginEntry(HINSTANCE hInst, reaper_plugin_info_t *rec)
{
  if (!rec)
  {
    RemoveAll();
    if (plugin_register)
    {
      plugin_register("-timer", (void *)OnTimer);
      plugin_register("-hookcommand2", (void *)OnAction);
#ifdef SWS_TUNING_UI
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

  // Trackbar class, needed only by the tuning window.
#ifdef SWS_TUNING_UI
  INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_BAR_CLASSES};
  InitCommonControlsEx(&icc);
#endif

  g_hInst = hInst;
  g_main = rec->hwnd_main ? rec->hwnd_main : GetMainHwnd();
  if (!g_main)
    return 0;

  // Reported to REAPER (and shown in its Extensions list). Keep in step with the
  // version in versions/ and the GitHub release tag.
  rec->Register("ext_name", (void *)"Smooth Wheel Scroll 1.3.7");
  rec->Register("ext_vendor", (void *)"SmoothWheelScroll");

  // Glide off: register nothing and hook nothing, so the extension is inert and
  // REAPER's native wheel handling is untouched. Still load successfully so the
  // plugin appears in REAPER's extension list.
  if (!kGlideEnabled)
  {
    Log("loaded with glide disabled (native wheel only)");
    return 1;
  }

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

  // Expose the tuning window -- TUNING BUILDS ONLY. The released DLL registers no
  // action, no menu item and no window, so a user has nothing to open and nothing
  // to configure; it always runs the compiled-in defaults. custom_action dispatches
  // through hookcommand2, which OnAction already owns (see the g_cmdTune branch).
#ifdef SWS_TUNING_UI
  static custom_action_register_t s_tuneAction = {
      0, "SWS_SCROLL_TUNE", "Smooth Wheel Scroll: settings...", nullptr};
  g_cmdTune = rec->Register("custom_action", &s_tuneAction);
  rec->Register("hookcustommenu", (void *)OnMenuHook);
  AddExtensionsMainMenu();
#endif

  rec->Register("atexit", (void *)OnExit);

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
