// ---------------------------------------------------------------------------
// platform/win32/wheel_win32.cpp -- the Win32 side of the wheel-facing interface.
//
// WHAT LIVES HERE AND WHY. Only the two questions the shared code asks that have a different answer
// on each platform:
//
//   LastWheel()        what the most recent wheel was, as a portable event
//   WheelHookDenied()  whether installing the hook was refused
//
// Everything else about the wheel -- installing the hook, the hook procedure itself, deciding what
// is under the cursor and which surface the wheel is driving -- remains in smooth_wheel_scroll.cpp,
// because it is entangled with the action table and the per-axis integrator state. See the
// "platform adaptors" block at the end of that file; moving it wholesale would be a rewrite rather
// than a relocation.
//
// WHY WheelHookDenied() IS ALWAYS FALSE ON WINDOWS: a WH_GETMESSAGE hook needs no user permission.
// It can still fail (hook quota, a hostile third party), but that failure shows up as "no messages
// arrive", which the plugin already handles by scrolling natively -- there is no user-facing
// permission to report. On macOS the answer is real (Accessibility), and the plugin uses it only to
// explain itself in the log, never to change behaviour.
// ---------------------------------------------------------------------------
#include "../../platform.h"

namespace plat
{
// ★ THE WINDOWS IMPLEMENTATION CANNOT ANSWER LastWheel() YET, and says so with a zeroed event.
//
// The main file keeps its wheel characteristics PER AXIS -- two device records, one for the vertical
// wheel and one for the horizontal -- because the touchpad-reverse feature asks specifically about
// the wheel that produced THIS action's axis. Collapsing them into one value here would answer the
// wrong question for one of the two axes.
//
// A zeroed event means "no recent wheel", which the caller treats as a notched device. That is
// exactly what the existing code already concludes when its own record is stale (see
// LastWheelDevice in the main file), so nothing changes on Windows -- and the Mac build, where a
// single tap really does observe everything, fills it properly.
//
// WHEN THE HOOK DOES MOVE HERE (the next step of the port), this becomes the real answer: the two
// records move with it and LastWheel() reports the one matching the axis the caller names. That
// needs an axis parameter -- a change worth making when it can be verified, not before.
PlatWheelEvent LastWheel()
{
  PlatWheelEvent e;
  e.delta = 0.0;
  e.horizontal = false;
  e.continuous = false;
  e.hasExtraInfo = false;
  e.extraInfo = 0;
  return e;
}

bool WheelHookDenied()
{
  return false;
}

} // namespace plat
