// ---------------------------------------------------------------------------
// platform/mac/wheel_mac.mm -- noticing the wheel, and deciding what is under the pointer.
//
// ★ STATUS: WRITTEN FROM THE DOCUMENTATION, NEVER COMPILED, NEVER RUN.
//
// This is the riskiest file in the port, because it is the one that talks to the operating system
// about wheel events and about what the pointer is over, and neither can be exercised without a Mac.
// The design below is deliberately conservative: where the platform cannot answer a question
// reliably, it says so (PlatHit::valid = false) and the plugin declines to animate rather than
// guessing. A Mac build that does nothing is a much better first release than one that animates the
// wrong thing.
//
// ---------------------------------------------------------------------------
// WHY AN EVENT TAP, AND WHY IT NEEDS A PERMISSION
//
// REAPER on macOS receives its wheel events through the normal AppKit path. There is no public
// "what did the wheel mean to the host" callback, so the only way to see the event first is a
// CGEventTap: a system-wide stream of input events, filtered to scroll events, that runs BEFORE the
// application sees them. That is what Windows' WH_GETMESSAGE hook does here, one layer down.
//
// An event tap that wants to MODIFY or DISCARD events requires the Accessibility permission
// (System Settings > Privacy & Security > Accessibility). Without it the tap either fails to
// create or is created and never fires. Both are handled: WheelHookInstall() returns false, and the
// plugin must then behave exactly like a disabled plugin rather than retrying in a loop.
//
// ---------------------------------------------------------------------------
// WHY THE TAP ONLY OBSERVES, AND THE HOST STILL DOES THE ROUTING
//
// On Windows the plugin's hook decides "is this wheel mine" and, when it is, swallows the message
// and re-drives the action itself with animated values. That works because the hook runs inside
// REAPER's own process and thread.
//
// On macOS the equivalent is possible -- an event tap can return NULL to swallow -- but doing the
// same thing there requires knowing WHICH REAPER action the wheel would have resolved to, and that
// resolution happens inside REAPER's own AppKit handling, which an event tap cannot observe. The
// plugin therefore cannot reproduce the Windows architecture verbatim.
//
// The approach taken here is the portable one the plugin ALREADY supports on Windows for the arrange
// area (see hookcommand2 in the main file): let the host resolve the wheel into an action, and hook
// the action table instead. REAPER calls hookcommand2 on macOS too. The tap's job is therefore only
// to record the most recent wheel's characteristics -- how big it was and whether it came from a
// continuous surface -- so that when hookcommand2 fires a moment later, the plugin can tell whether
// this was a touchpad (pass through) or a notch (animate), exactly as the Windows hook does.
//
// That means the tap does not swallow anything. It is an OBSERVER. This is why first-release risk is
// low: if the tap never fires, hookcommand2 still runs, and the plugin simply treats every wheel as
// a notched one -- the same behaviour it has when Windows' hook is unavailable.
// ---------------------------------------------------------------------------
#include "../../platform.h"

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>

#include <cmath>

namespace
{
CFMachPortRef g_tap = nullptr;
CFRunLoopSourceRef g_tapSrc = nullptr;

// The most recent wheel, reduced to the two facts the plugin actually uses. Written on the tap's
// run loop (which is the main one, see WheelHookInstall), read from hookcommand2 on the same thread,
// so no locking is needed -- and the comment says so because a reader will otherwise reach for one.
double g_lastDelta = 0.0;
bool g_lastHorizontal = false;
bool g_lastContinuous = false;

// Set when the tap is present but the permission is not: reported once, then the plugin behaves as
// if it had never been installed. Kept as a flag rather than a "give up forever" so a user who grants
// permission and reloads the plugin gets a working hook without restarting REAPER.
bool g_denied = false;
bool g_reportedDenied = false;

CGEventRef TapCallback(CGEventTapProxy, CGEventType type, CGEventRef event, void *)
{
  if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput)
  {
    // The system disables a tap whose callback took too long. Re-enable rather than die: the
    // callback below only reads a handful of fields, so a timeout means the machine was busy, not
    // that this code is slow.
    if (g_tap)
      CGEventTapEnable(g_tap, true);
    return event;
  }

  // ── the two facts ───────────────────────────────────────────────────────
  //
  // kCGScrollWheelEventDeltaAxis1 is the vertical amount and DeltaAxis2 the horizontal one. Which
  // one is "real" depends on the device:
  //   * a legacy mouse reports INTEGRAL line counts (1, 2, 3...) on the axis it turns;
  //   * a touchpad or Magic Mouse reports a CONTINUOUS, fractional pixel-ish amount, and marks the
  //     event with kCGScrollWheelEventIsContinuous = 1.
  //
  // The tap records whichever axis moved, in kWheelUnit (120) units, because that is the unit the
  // whole plugin is calibrated in -- one notch must arrive as 120 so that Travel(), the row-height
  // gain and the delivery grids keep their tuned values. A line-based device is multiplied by
  // kWheelUnit; a continuous one is NOT scaled to 120, because its magnitude has no notch to
  // correspond to, and (see below) it never reaches the model anyway.
  const int64_t dy = CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis1);
  const int64_t dx = CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis2);
  const bool continuous =
      CGEventGetIntegerValueField(event, kCGScrollWheelEventIsContinuous) != 0;

  const bool horizontal = (dx != 0 && dy == 0);
  const double raw = horizontal ? (double)dx : (double)-dy; // CG's vertical sign is inverted vs ours

  g_lastHorizontal = horizontal;
  g_lastContinuous = continuous;
  // A continuous device passes its own magnitude through unscaled; a line device becomes notches.
  g_lastDelta = continuous ? raw : raw * kWheelUnit;

  return event; // OBSERVER ONLY -- see the file header on why nothing is swallowed
}
} // namespace

namespace plat
{
bool WheelHookInstall()
{
  if (g_tap)
    return true;
  g_reportedDenied = false;
  g_denied = false;

  // Listen only: an observing tap is not supposed to need more permission than this, and asking
  // for less makes the Accessibility prompt less alarming than a listen+modify tap would.
  const CGEventMask mask = CGEventMaskBit(kCGEventScrollWheel);

  // The callback runs on the run loop this source is added to -- the MAIN one, which is where
  // REAPER calls hookcommand2. That is what makes the shared state single-threaded.
  g_tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly,
                           mask, TapCallback, nullptr);
  if (!g_tap)
  {
    // Almost always "Accessibility permission not granted". Reported once by the caller.
    g_denied = true;
    g_reportedDenied = true;
    return false;
  }

  g_tapSrc = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, g_tap, 0);
  if (!g_tapSrc)
  {
    CFRelease(g_tap);
    g_tap = nullptr;
    return false;
  }
  CFRunLoopAddSource(CFRunLoopGetMain(), g_tapSrc, kCFRunLoopCommonModes);
  CGEventTapEnable(g_tap, true);
  return true;
}

void WheelHookRemove()
{
  if (g_tapSrc)
  {
    CFRunLoopRemoveSource(CFRunLoopGetMain(), g_tapSrc, kCFRunLoopCommonModes);
    CFRelease(g_tapSrc);
    g_tapSrc = nullptr;
  }
  if (g_tap)
  {
    CGEventTapEnable(g_tap, false);
    CFRelease(g_tap);
    g_tap = nullptr;
  }
}

PlatHit HitTest()
{
  // ★ THIS IS THE PART THE MAC BUILD CANNOT ANSWER THE WAY WINDOWS DOES.
  //
  // Windows asks WindowFromPoint for the HWND under the cursor and reads its CLASS NAME, then the
  // plugin matches that name against a short list (MIDIWindow, REAPERTCPDisplay, MIDIPianoWindow...).
  // macOS has no window class names: REAPER is a Cocoa app and its views are objects, not registered
  // window classes. Enumerating another process's windows for a "class name" would need accessibility
  // APIs that expose only AX roles, not REAPER's internal view identities.
  //
  // So this returns valid = false and the plugin must fall back to its action-only path. That is not
  // a stub -- it is the correct answer for a platform where the question has no answer, and the
  // plugin already has a working "no hit information" mode (it is what runs today when the Windows
  // hook cannot tell either).
  //
  // IF SOMEONE WANTS TO DO BETTER, the honest options are:
  //   1. Use REAPER's own GetThingFromPoint (already used in the main file) -- it answers WHICH
  //      THING the cursor is over and works on both platforms; that may be enough on its own.
  //   2. Ask the host for the mouse-modifier context instead of the window class
  //      (MM_CTX_* by cursor position) -- the main file already does this for the TCP panel.
  // Neither needs a window class, and both are better than guessing.
  PlatHit hit;
  hit.window = nullptr;
  hit.valid = false;
  return hit;
}

// The tap's readings, for the plugin to consult when the host reports the resolved action.
// Declared in platform.h so the Mac build can supply them and the Windows build can map them onto
// its existing per-axis device records.
PlatWheelEvent LastWheel()
{
  PlatWheelEvent e;
  e.delta = g_lastDelta;
  e.horizontal = g_lastHorizontal;
  e.continuous = g_lastContinuous;
  e.hasExtraInfo = false;
  return e;
}

bool WheelHookDenied() { return g_denied && g_reportedDenied; }

} // namespace plat
