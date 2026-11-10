// ---------------------------------------------------------------------------
// platform/mac/wheel_mac.mm -- noticing REAPER's wheel events.
//
// This is a PROCESS-LOCAL AppKit monitor, not a system-wide event tap. The plugin already runs
// inside REAPER, so [NSEvent addLocalMonitorForEventsMatchingMask:...] sees exactly the events
// delivered to REAPER. That is narrower and safer than a session-wide stream, does not need the
// Accessibility permission, and cannot record a wheel that the user turned in another application.
//
// The monitor only OBSERVES: it returns the same event. REAPER still resolves the wheel into one of
// its actions, and hookcommand2 in the shared code decides whether to animate or pass that action
// through. The monitor's job is to remember the most recent wheel, including whether it came from a
// precise/continuous surface, before that action callback arrives.
// ---------------------------------------------------------------------------
#include "../../platform.h"

#import <Cocoa/Cocoa.h>

#include <cmath>

namespace
{
id g_monitor = nil;
id g_resignObserver = nil;

// Written and read on the main thread: the local monitor, NSApplication notification and REAPER's
// action callback all arrive there. Keep a plain snapshot rather than retaining NSEvent so no object
// lifetime outlives the monitor or the shared library.
PlatWheelEvent g_lastWheel;
unsigned long long g_sequence = 0;

void InvalidateWheel()
{
  g_lastWheel = PlatWheelEvent();
  g_lastWheel.sequence = ++g_sequence;
}

NSEvent *ObserveInput(NSEvent *event)
{
  if (!event || event.type != NSEventTypeScrollWheel)
  {
    // A keyboard shortcut or mouse click is unrelated input. Clear the old wheel before its action
    // can arrive, so a wheel from moments ago cannot be attributed to this new command.
    InvalidateWheel();
    return event;
  }

  const double dx = event.scrollingDeltaX;
  const double dy = event.scrollingDeltaY;
  if (!std::isfinite(dx) || !std::isfinite(dy) || (dx == 0.0 && dy == 0.0))
  {
    InvalidateWheel();
    return event;
  }

  g_lastWheel.valid = true;
  g_lastWheel.horizontal = std::fabs(dx) > std::fabs(dy);
  // Precise deltas and non-empty gesture/momentum phases identify the continuous family. The
  // conservative direction matters: if a device is ambiguous, treating it as continuous leaves it
  // native rather than applying a second easing layer.
  g_lastWheel.continuous = event.hasPreciseScrollingDeltas ||
                           event.phase != NSEventPhaseNone ||
                           event.momentumPhase != NSEventPhaseNone;
  g_lastWheel.delta = g_lastWheel.horizontal ? dx : dy;
  g_lastWheel.timestampMs = plat::Now();
  g_lastWheel.sequence = ++g_sequence;
  return event; // observer only: REAPER still resolves and receives the event
}
} // namespace

namespace plat
{
bool WheelHookInstall()
{
  if (g_monitor)
    return true;

  InvalidateWheel();
  const NSEventMask mask = NSEventMaskScrollWheel | NSEventMaskKeyDown |
                           NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown |
                           NSEventMaskOtherMouseDown;
  g_monitor = [NSEvent addLocalMonitorForEventsMatchingMask:mask
                                                    handler:^NSEvent *(NSEvent *event) {
                                                      return ObserveInput(event);
                                                    }];
  if (!g_monitor)
    return false;

  // Switching to another application ends any pending wheel->action pairing. A local monitor alone
  // does not observe that transition because no input event necessarily crosses REAPER's event loop.
  g_resignObserver =
      [NSNotificationCenter.defaultCenter addObserverForName:NSApplicationDidResignActiveNotification
                                                      object:NSApp
                                                       queue:NSOperationQueue.mainQueue
                                                  usingBlock:^(NSNotification *) {
                                                    InvalidateWheel();
                                                  }];
  return true;
}

void WheelHookRemove()
{
  if (g_monitor)
  {
    [NSEvent removeMonitor:g_monitor];
    g_monitor = nil;
  }
  if (g_resignObserver)
  {
    [NSNotificationCenter.defaultCenter removeObserver:g_resignObserver];
    g_resignObserver = nil;
  }
  InvalidateWheel();
}

PlatHit HitTest()
{
  // A standalone class-name hit test has no faithful Cocoa equivalent. The local monitor still
  // carries NSEvent.window for future surface-specific work, but the platform interface's current
  // answer remains honest: "cannot tell" means use REAPER's action-driven path, not guess a view.
  PlatHit hit;
  hit.window = nullptr;
  hit.valid = false;
  return hit;
}

PlatWheelEvent LastWheel() { return g_lastWheel; }

// A local monitor cannot be denied by system privacy settings. Kept as a named answer so callers do
// not need platform-specific branches; false also correctly means "use the normal fallback".
bool WheelHookDenied() { return false; }

} // namespace plat
