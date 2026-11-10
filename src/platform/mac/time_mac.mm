// ---------------------------------------------------------------------------
// platform/mac/time_mac.mm -- the frame clock and the monotonic clock, on macOS.
//
// ★ STATUS: SOURCE READY; the latest change still needs macOS CI and a real-Mac run.
//
// WHY OBJECTIVE-C++. dispatch is a C API, so plain C would nearly do; but the panel and the event
// tap want AppKit and ApplicationServices respectively, and keeping one .mm per platform avoids a
// mixed toolchain.
//
// ★ THE CADENCE QUESTION, which is the only part of this file that affects how the plugin FEELS.
//
// On Windows the frame clock is SetTimer(5 ms) and it actually fires about every 15.6 ms (the
// system clamps it; AGENTS 10.1 measured 15.58 ms mean). The model is rate-independent -- one notch
// delivers the same total travel at 50, 64, 240 and 1000 Hz, and the largest single step shrinks as
// the rate rises -- so a Mac timer that honours a 5 ms request exactly would produce the same
// DISTANCE in more, smaller steps.
//
// That is not obviously wrong, but it is a different feel, and shipping a port that feels different
// from the original is exactly the kind of drift this project's rules exist to prevent. The choice
// made here is therefore to ASK FOR THE WINDOWS REQUEST (5 ms) BUT HAVE THE DISPATCH SOURCE COALESCE
// TO THE WINDOWS OBSERVED CADENCE, by scheduling with an interval of kCoalesceMs and letting the
// leeway widen it. A tester who finds the Mac version too coarse or too dense can change ONE
// constant here and compare, without touching the model.
//
// The alternative -- honour 5 ms and enjoy genuinely smoother motion on Mac -- is defensible and may
// be what the user prefers. It is NOT taken by default because "the port feels different" is much
// harder to diagnose later than "the port feels the same".
// ---------------------------------------------------------------------------
#include "../../platform.h"

#include <dispatch/dispatch.h>
#include <mach/mach_time.h>

#include <ctime>

namespace
{
// ---------------------------------------------------------------------------
// The tick, marshalled onto the main thread.
//
// dispatch_source_t fires on whatever queue it was created on, so the source is created on the MAIN
// QUEUE for the same reason the Windows build posts to a UI-thread message window: REAPER must be
// called from the thread that owns its state. Creating the source on the main queue means the
// handler ALREADY runs there and no extra hop is needed -- unlike Windows, where the multimedia
// timer runs on its own thread and must PostMessage.
// ---------------------------------------------------------------------------
dispatch_source_t g_source = nullptr;

plat::TickFn g_tick = nullptr;
void *g_tickUser = nullptr;
bool g_running = false;

// The interval asked of the dispatch timer. See the cadence note in the file header: this is the
// one knob that decides whether the Mac build feels as coarse as Windows or smoother.
//
// Deliberately NOT 5.0. Windows asks 5 ms and gets ~15.6; asking 5 here would get ~5. Matching the
// OBSERVED Windows value is what makes the two platforms indistinguishable in the hand.
static const double kCoalesceMs = 15.6;

// A discrete timer drifts unless the deadline is absolute, so the next fire time is carried
// forward rather than restarted from "now" each time. This mirrors what the Windows timer does in
// practice and keeps the average interval stable over a long glide.
dispatch_time_t g_next = 0;

void ScheduleNext()
{
  if (!g_source)
    return;
  dispatch_source_set_timer(g_source, g_next, DISPATCH_TIME_FOREVER, 0);
  g_next = dispatch_time(g_next, (int64_t)(kCoalesceMs * (double)NSEC_PER_MSEC));
}

void OnTick(void *)
{
  if (g_tick)
    g_tick(g_tickUser);
  ScheduleNext();
}
} // namespace

namespace plat
{
void TimeInit()
{
  // mach_timebase gives the ratio between mach_absolute_time ticks and nanoseconds; it is not
  // 1:1 on Apple Silicon (it is 125/3 there), so it must be read rather than assumed.
  static mach_timebase_info_data_t tb = {0, 0};
  if (tb.denom == 0)
    mach_timebase_info(&tb);
}

void TimeShutdown()
{
  TimerStop();
  if (g_source)
  {
    // Break the function pointer into the shared library before the bundle can unload, then cancel
    // and release the source. The source is suspended after TimerStop(), so no further callback can
    // start; release reclaims the object that was previously leaked on every reload.
    dispatch_source_set_event_handler_f(g_source, nullptr);
    dispatch_source_cancel(g_source);
#if defined(OS_OBJECT_USE_OBJC) && OS_OBJECT_USE_OBJC
    g_source = nullptr;
#else
    dispatch_release(g_source);
    g_source = nullptr;
#endif
  }
  g_tick = nullptr;
  g_tickUser = nullptr;
  g_next = 0;
}

void TimerStart(int periodMs, TickFn tick, void *user)
{
  (void)periodMs; // the cadence is decided by kCoalesceMs, see the file header
  g_tick = tick;
  g_tickUser = user;
  if (g_running)
    return; // idempotent, like the Windows implementation

  if (!g_source)
  {
    // Created once and reused. Creating it on the main queue is what keeps the handler on the UI
    // thread; the global queue would need a dispatch_async(main) hop and an extra frame of latency.
    g_source = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    if (!g_source)
      return;
    // A PLAIN C FUNCTION, not a block: blocks need -fblocks and the Block runtime linked, and a
    // port that has never been compiled should avoid the extra build-system requirements. The
    // handler needs no captured state -- g_tick and g_tickUser are file statics.
    dispatch_source_set_event_handler_f(g_source, &OnTick);
    dispatch_set_context(g_source, nullptr);
  }
  g_next = dispatch_time(DISPATCH_TIME_NOW, (int64_t)(kCoalesceMs * (double)NSEC_PER_MSEC));
  ScheduleNext();
  dispatch_resume(g_source);
  g_running = true;
}

void TimerStop()
{
  if (g_source && g_running)
  {
    dispatch_suspend(g_source);
    g_running = false;
  }
}

bool TimerRunning() { return g_running; }

double Now()
{
  // Monotonic, in milliseconds: mach_absolute_time is unaffected by the user changing the clock,
  // which the model's window arithmetic relies on (see the Windows Now()).
  static mach_timebase_info_data_t tb = {0, 0};
  if (tb.denom == 0)
    mach_timebase_info(&tb);
  const uint64_t t = mach_absolute_time();
  return (double)t * (double)tb.numer / (double)tb.denom / 1e6;
}

unsigned long WallMs()
{
  // For log timestamps and age tests only; a wall clock is correct here precisely because those want
  // the time the user sees.
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (unsigned long)((uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL);
}

} // namespace plat
