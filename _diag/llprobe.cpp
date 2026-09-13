// llprobe -- can a DLL-in-a-host install WH_MOUSE_LL, and how does it behave here?
//
// Read-only evidence gathering:
//   1. read LowLevelHooksTimeout from the registry (and the documented default)
//   2. install WH_MOUSE_LL and inject wheel events, to confirm the hook fires and
//      to see the injected-event flags
//   3. report how injected events are flagged
#include <windows.h>
#include <cstdio>

static volatile LONG g_events = 0;
static volatile LONG g_wheel = 0;
static volatile LONG g_injected = 0;
static volatile LONG g_lowerIl = 0;

static LRESULT CALLBACK LLProc(int code, WPARAM wp, LPARAM lp)
{
  if (code == HC_ACTION)
  {
    InterlockedIncrement(&g_events);
    if (wp == WM_MOUSEWHEEL)
    {
      InterlockedIncrement(&g_wheel);
      MSLLHOOKSTRUCT *m = (MSLLHOOKSTRUCT *)lp;
      if (m->flags & LLMHF_INJECTED) InterlockedIncrement(&g_injected);
      if (m->flags & LLMHF_LOWER_IL_INJECTED) InterlockedIncrement(&g_lowerIl);
    }
  }
  return CallNextHookEx(nullptr, code, wp, lp);
}

int main()
{
  printf("== C1: LowLevelHooksTimeout (HKCU\\Control Panel\\Desktop) ==\n");
  HKEY k = nullptr;
  if (RegOpenKeyExA(HKEY_CURRENT_USER, "Control Panel\\Desktop", 0, KEY_READ, &k) == ERROR_SUCCESS)
  {
    DWORD v = 0, sz = sizeof(v), type = 0;
    if (RegQueryValueExA(k, "LowLevelHooksTimeout", nullptr, &type, (LPBYTE)&v, &sz) == ERROR_SUCCESS)
      printf("   present: %lu ms\n", (unsigned long)v);
    else
      printf("   not set -> system default applies\n");
    RegCloseKey(k);
  }
  printf("   (documented: Win10 1709+ caps it at 1000 ms; exceeded value falls back to 1000)\n");

  printf("\n== C2: install WH_MOUSE_LL from this process ==\n");
  HHOOK h = SetWindowsHookExA(WH_MOUSE_LL, LLProc, nullptr, 0);
  printf("   SetWindowsHookEx(WH_MOUSE_LL) -> %p  %s\n", (void *)h,
         h ? "INSTALLED" : "FAILED");
  if (!h)
  {
    printf("   GetLastError=%lu\n", (unsigned long)GetLastError());
    return 1;
  }

  printf("\n== C3: do injected wheel events reach the hook, and how are they flagged? ==\n");
  const int N = 5;
  for (int i = 0; i < N; ++i)
  {
    mouse_event(MOUSEEVENTF_WHEEL, 0, 0, (DWORD)(WHEEL_DELTA), 0);
    Sleep(60);
  }
  // Pump, so the LL hook (which needs a message loop on this thread) can run.
  for (int i = 0; i < 40; ++i)
  {
    MSG m;
    while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE))
    {
      TranslateMessage(&m);
      DispatchMessageA(&m);
    }
    Sleep(25);
  }

  printf("   hook callbacks: total=%ld  wheel=%ld  flagged INJECTED=%ld  LOWER_IL=%ld\n",
         (long)g_events, (long)g_wheel, (long)g_injected, (long)g_lowerIl);
  printf("   => hook fired=%s ; injected events %s flagged\n",
         g_events ? "YES" : "NO",
         (g_injected == g_wheel) ? "ALL" : (g_injected ? "SOME" : "NOT"));

  printf("\n== C4: is the hook still installed after pumping? ==\n");
  printf("   (a silent removal would show as wheel callbacks stopping; we saw %ld)\n", (long)g_wheel);

  UnhookWindowsHookEx(h);
  printf("\n== C5: unhooked cleanly ==\n");
  return 0;
}
