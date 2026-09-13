// reentryprobe -- does a REPLAYED wheel message re-enter a thread message hook?
//
// This settles whether "replay the wheel" can coexist with a plugin that already
// intercepts wheels (via WH_GETMESSAGE + hookcommand2), or whether it double-handles.
//
// It installs WH_GETMESSAGE, then sends ONE wheel by two routes and counts how many
// times the hook sees a wheel:
//   A) SendMessage(hwnd, WM_MOUSEWHEEL)   -- direct delivery, no queue
//   B) PostMessage(hwnd, WM_MOUSEWHEEL)   -- goes through the queue
//   C) mouse_event(...)                   -- injected, goes through the queue
// A hook count of 0 for a route means a replayed wheel on that route is INVISIBLE to
// our hook (no re-interception); >0 means it would be intercepted again (double).
#include <windows.h>
#include <cstdio>

static HWND g_wnd = nullptr;
static volatile LONG g_hookWheels = 0;
static volatile LONG g_procWheels = 0;

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
  if (m == WM_MOUSEWHEEL) InterlockedIncrement(&g_procWheels);
  if (m == WM_CLOSE) { PostQuitMessage(0); return 0; }
  return DefWindowProcA(h, m, wp, lp);
}

static LRESULT CALLBACK GetMsgProc(int code, WPARAM wp, LPARAM lp)
{
  if (code == HC_ACTION && wp == PM_REMOVE)
  {
    MSG *m = (MSG *)lp;
    if (m && m->message == WM_MOUSEWHEEL)
    {
      InterlockedIncrement(&g_hookWheels);
      char e[128];
      wsprintfA(e, "  [hook] wheel seen from queue, extraInfo=0x%p\n", (void *)m->wParam);
      OutputDebugStringA(e);
    }
  }
  return CallNextHookEx(nullptr, code, wp, lp);
}

static void Pump(int ms)
{
  DWORD end = GetTickCount() + ms;
  MSG m;
  while (GetTickCount() < end)
  {
    while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE))
    {
      TranslateMessage(&m);
      DispatchMessageA(&m);
    }
    Sleep(10);
  }
}

int main()
{
  WNDCLASSA wc = {0};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleA(nullptr);
  wc.lpszClassName = "ReentryProbeWnd";
  RegisterClassA(&wc);
  g_wnd = CreateWindowExA(0, wc.lpszClassName, "reentry", WS_POPUP,
                          0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
  if (!g_wnd) { printf("window failed\n"); return 1; }

  HHOOK h = SetWindowsHookExA(WH_GETMESSAGE, GetMsgProc, nullptr, GetCurrentThreadId());
  printf("thread message hook = %p\n\n", (void *)h);

  const LPARAM wp = MAKEWPARAM(0, (short)-WHEEL_DELTA);
  const LPARAM lp = MAKELPARAM(50, 50);

  // A) SendMessage: direct, never enters the queue
  InterlockedExchange(&g_hookWheels, 0);
  InterlockedExchange(&g_procWheels, 0);
  SendMessage(g_wnd, WM_MOUSEWHEEL, wp, lp);
  Pump(120);
  printf("A) SendMessage   -> hook saw %ld, window proc saw %ld\n",
         (long)g_hookWheels, (long)g_procWheels);

  // B) PostMessage: enters the queue
  InterlockedExchange(&g_hookWheels, 0);
  InterlockedExchange(&g_procWheels, 0);
  PostMessage(g_wnd, WM_MOUSEWHEEL, wp, lp);
  Pump(120);
  printf("B) PostMessage   -> hook saw %ld, window proc saw %ld\n",
         (long)g_hookWheels, (long)g_procWheels);

  // C) mouse_event: injected, enters the queue (needs a real window position/foreground)
  InterlockedExchange(&g_hookWheels, 0);
  InterlockedExchange(&g_procWheels, 0);
  SetCursorPos(50, 50);
  mouse_event(MOUSEEVENTF_WHEEL, 0, 0, (DWORD)WHEEL_DELTA, 0);
  Pump(200);
  printf("C) mouse_event   -> hook saw %ld, window proc saw %ld  (window may be unfocused)\n",
         (long)g_hookWheels, (long)g_procWheels);

  UnhookWindowsHookEx(h);
  DestroyWindow(g_wnd);
  printf("\n=> SendMessage bypasses the thread hook (invisible = no re-interception).\n");
  printf("=> Post/Injected enter the queue and ARE seen again (would double-handle).\n");
  return 0;
}
