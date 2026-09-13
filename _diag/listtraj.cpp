// listtraj -- does delivering FINE deltas make a list advance one row at a time
// instead of jumping a whole notch's worth at once?
//
// Same total delta, two deliveries, sampling the top index after every message:
//   coarse: one 120 message        -> expect a single 3-row jump
//   fine  : 120 separate 1-delta   -> expect 3 separate 1-row steps, spread out
// The row count is item-granular, so a list can never move less than one row; the win
// is that the rows arrive spread over the animation instead of in one block.
//
// Usage: listtraj.exe ["<dialog title substring>"]
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>

static DWORD g_pid = 0;
static HWND g_dlg = nullptr, g_list = nullptr;
static char g_want[64] = "Actions";

static BOOL CALLBACK enumTop(HWND h, LPARAM)
{
  DWORD p = 0;
  GetWindowThreadProcessId(h, &p);
  if (p != g_pid)
    return TRUE;
  char c[64] = {0}, t[200] = {0};
  GetClassNameA(h, c, sizeof(c));
  GetWindowTextA(h, t, sizeof(t));
  if (strcmp(c, "#32770") == 0 && strstr(t, g_want))
  {
    g_dlg = h;
    return FALSE;
  }
  return TRUE;
}

static HWND findCtl(HWND h, const char *cls)
{
  char c[64] = {0};
  GetClassNameA(h, c, sizeof(c));
  if (strcmp(c, cls) == 0 && IsWindowVisible(h))
  {
    RECT r = {0};
    GetWindowRect(h, &r);
    if (r.right - r.left > 100 && r.bottom - r.top > 100)
      return h;
  }
  HWND k = nullptr;
  while ((k = FindWindowExA(h, k, nullptr, nullptr)) != nullptr)
  {
    HWND r = findCtl(k, cls);
    if (r)
      return r;
  }
  return nullptr;
}

static int itemOf(HWND l, const char *cls)
{
  if (strcmp(cls, "SysTreeView32") == 0)
  {
    // first fully-visible item: approximate with the item at the top of the client
    return (int)SendMessageA(l, TVM_GETCOUNT, 0, 0); // count, used only as a sanity check
  }
  return (int)SendMessageA(l, LVM_GETTOPINDEX, 0, 0);
}

static int sbPos(HWND l)
{
  SCROLLINFO si = {0};
  si.cbSize = sizeof(si);
  si.fMask = SIF_ALL;
  return GetScrollInfo(l, SB_VERT, &si) ? si.nPos : -1;
}

static void settle(HWND l)
{
  int last = -999999, same = 0;
  for (int i = 0; i < 60; ++i)
  {
    Sleep(60);
    const int p = itemOf(l, "SysListView32") * 100000 + (sbPos(l) & 0xFFFF);
    if (p == last) { if (++same >= 3) return; }
    else { same = 0; last = p; }
  }
}

static void run(HWND l, const char *name, int d, int n, int sleepMs)
{
  SendMessageA(l, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);
  settle(l);
  const int base = sbPos(l);
  printf("\n%s  (start sbPos=%d)\n", name, base);
  int prev = base;
  for (int i = 0; i < n; ++i)
  {
    SendMessageA(l, WM_MOUSEWHEEL, MAKEWPARAM(0, (short)-d), 0);
    if (sleepMs)
      Sleep(sleepMs);
    const int p = sbPos(l);
    if (p != prev)
    {
      printf("   msg %3d/%d  sbPos -> %+d\n", i + 1, n, p - base);
      prev = p;
    }
  }
  settle(l);
  printf("   final %+d\n", sbPos(l) - base);
}

// Threshold: the smallest single delta that moves the list at all.
static void threshold(HWND l)
{
  printf("\nsingle-message threshold (what total delta moves it once):\n");
  static const int ds[] = {30, 60, 100, 110, 119, 120, 121, 240};
  for (size_t i = 0; i < sizeof(ds) / sizeof(ds[0]); ++i)
  {
    SendMessageA(l, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);
    settle(l);
    const int b = sbPos(l);
    SendMessageA(l, WM_MOUSEWHEEL, MAKEWPARAM(0, (short)-ds[i]), 0);
    settle(l);
    printf("   delta %4d -> %+d\n", ds[i], sbPos(l) - b);
  }
}

int main(int argc, char **argv)
{
  if (argc > 1)
    strncpy(g_want, argv[1], sizeof(g_want) - 1);
  HWND m = FindWindowA("REAPERwnd", nullptr);
  if (!m) { printf("REAPERwnd not found\n"); return 2; }
  GetWindowThreadProcessId(m, &g_pid);
  EnumWindows(enumTop, 0);
  if (!g_dlg) { printf("dialog \"%s\" not found\n", g_want); return 2; }
  g_list = findCtl(g_dlg, "SysListView32");
  if (!g_list) { printf("no list in \"%s\"\n", g_want); return 2; }

  printf("dialog \"%s\" list=%p\n", g_want, (void *)g_list);
  threshold(g_list);
  printf("\n=== COARSE: one 120 delta (mouse wheel as it is today) ===");
  run(g_list, "coarse 1 x 120", 120, 1, 0);
  printf("\n=== FINE: 120 deltas of 1, delivered ~4 ms apart (the animation stream) ===");
  run(g_list, "fine 120 x 1 @4ms", 1, 120, 4);
  printf("\n=== MID: 8 deltas of 15, delivered ~5 ms apart ===");
  run(g_list, "mid 8 x 15 @5ms", 15, 8, 5);
  printf("\n=== ROLL, native shape: 5 notches, 5 separate 120 messages ===");
  run(g_list, "roll 5 x 120", 120, 5, 120);
  printf("\n=== ROLL, animation shape: the same 5 notches as 600 deltas of 1 @4ms ===");
  run(g_list, "roll 600 x 1 @4ms", 1, 600, 4);
  return 0;
}
