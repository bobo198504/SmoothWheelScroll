// ---------------------------------------------------------------------------
// platform/win32/time_win32.cpp -- the frame clock and the monotonic clock.
//
// WHY THIS FILE EXISTS SEPARATELY FROM THE REST. The Mac port needs the same two services with a
// completely different implementation (a GCD dispatch source instead of a Win32 timer), and the
// code above them only ever wants "call me every few milliseconds, on a thread that may call the
// host" and "tell me the time". Keeping them here is what lets the Mac build supply its own without
// touching the plugin.
//
// THE ONE PIECE OF BEHAVIOUR THAT MUST SURVIVE THE PORT, and the reason this is not a trivial
// wrapper:
//
//   The Windows timer CANNOT run faster than about 15.6 ms here (SetTimer is clamped by the system
//   scheduling quantum; AGENTS 10.1 measured 15.58 ms mean for a 5 ms request). The model is
//   rate-independent -- one notch delivers the same total at 50, 64, 240 and 1000 Hz -- but the
//   NUMBER of visible steps tracks the frame rate, so a Mac build whose GCD timer really does fire
//   every 5 ms would feel visibly denser than this one. That difference is a deliberate porting
//   question, not an accident to be discovered later: see the note in TimeStart below.
// ---------------------------------------------------------------------------
#include "../../platform.h"

#include <windows.h>
#include <mmsystem.h>

namespace
{
// The message window that receives the tick. A PRIVATE window (HWND_MESSAGE parent) created by the
// plugin: the tick must run on the UI thread, and a message window is the least intrusive way to
// ask the UI thread to do something without borrowing one of REAPER's windows.
HWND g_animWnd = nullptr;
UINT_PTR g_timer = 0; // SetTimer handle (the shipped path)
MMRESULT g_mmTimer = 0; // timeSetEvent handle (the faster path, currently unused)
DWORD g_uiThreadId = 0;

plat::TickFn g_tick = nullptr;
void *g_tickUser = nullptr;
bool g_running = false;

void CALLBACK AnimProc(HWND, UINT, UINT_PTR, DWORD)
{
  // Runs on the timer thread for the multimedia timer, and on the UI thread for SetTimer. Either
  // way the work is POSTED to the message window so it always executes on the UI thread.
  if (g_animWnd)
    PostMessageA(g_animWnd, WM_APP + 1, 0, 0);
}

LRESULT CALLBACK AnimWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  if (msg == WM_APP + 1)
  {
    if (g_tick)
      g_tick(g_tickUser);
    return 0;
  }
  return DefWindowProcA(h, msg, wp, lp);
}

bool EnsureWindow()
{
  if (g_animWnd)
    return true;
  static bool registered = false;
  if (!registered)
  {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = AnimWndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "SmoothWheelScroll_Tick";
    RegisterClassA(&wc);
    registered = true;
  }
  g_animWnd = CreateWindowExA(0, "SmoothWheelScroll_Tick", "", 0, 0, 0, 0, 0, HWND_MESSAGE,
                              nullptr, GetModuleHandleA(nullptr), nullptr);
  return g_animWnd != nullptr;
}
} // namespace

namespace plat
{
void TimeInit()
{
  g_uiThreadId = GetCurrentThreadId();
  timeBeginPeriod(1); // let the timer resolve to milliseconds rather than the scheduler quantum
}

void TimeShutdown()
{
  TimerStop();
  if (g_animWnd)
  {
    DestroyWindow(g_animWnd);
    g_animWnd = nullptr;
  }
  timeEndPeriod(1);
}

void TimerStart(int periodMs, TickFn tick, void *user)
{
  g_tick = tick;
  g_tickUser = user;
  if (g_running)
    return; // idempotent: the caller may be careless about double starts
  if (!EnsureWindow())
    return;
  // ★ THE SHIPPED PATH IS SetTimer, NOT timeSetEvent, and the Mac port must match its CADENCE, not
  // its REQUEST. A 5 ms request here yields about 15.6 ms in practice (AGENTS 10.1). A Mac GCD
  // timer asking for 5 ms would get close to 5 ms and the plugin would feel denser there than on
  // Windows. The Mac implementation therefore targets the MEASURED Windows cadence (see the note in
  // platform/mac/time_mac.mm); do not "fix" one side to match its own request value.
  g_timer = SetTimer(nullptr, 0, (UINT)periodMs, AnimProc);
  g_running = (g_timer != 0);
}

void TimerStop()
{
  if (g_timer)
  {
    KillTimer(nullptr, g_timer);
    g_timer = 0;
  }
  if (g_mmTimer)
  {
    timeKillEvent((UINT)(UINT_PTR)g_mmTimer);
    g_mmTimer = 0;
  }
  g_running = false;
}

bool TimerRunning() { return g_running; }

double Now()
{
  // Monotonic: QueryPerformanceCounter is unaffected by the user changing the system clock, which
  // matters because the model's window arithmetic is difference-based and a clock jump would open
  // or close a window retroactively.
  static LARGE_INTEGER freq = {0};
  static LARGE_INTEGER start = {0};
  LARGE_INTEGER c;
  if (freq.QuadPart == 0)
  {
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
  }
  QueryPerformanceCounter(&c);
  return (double)(c.QuadPart - start.QuadPart) * 1000.0 / (double)freq.QuadPart;
}

unsigned long WallMs() { return (unsigned long)GetTickCount(); }

} // namespace plat
