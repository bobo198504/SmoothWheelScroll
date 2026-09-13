// listfine2 -- CLEAN measurement of whether a REAPER list control accumulates FINE
// (sub-notch) wheel deltas, or only reacts to whole notches.
//
// The previous attempt was wrong: it read LVM_GETTOPINDEX while the control was still
// animating its scroll, so the numbers were a race, not a behaviour. This version:
//   - parks the list at the very top with SB_TOP (instant, no animation),
//   - WAITS for the position to stop changing before each reading,
//   - reads the scrollbar position as well as the top index,
//   - runs the same total delta at several chunk sizes, which is the actual question:
//     if the control carries the remainder across messages, every chunking moves the
//     same amount; if it truncates per message, small chunks move nothing.
//
// Usage: listfine2.exe ["<dialog title substring>"]     (default "Actions")
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

static HWND findList(HWND h)
{
  char c[64] = {0};
  GetClassNameA(h, c, sizeof(c));
  if (strcmp(c, "SysListView32") == 0 && IsWindowVisible(h))
  {
    RECT r = {0};
    GetWindowRect(h, &r);
    if (r.right - r.left > 100 && r.bottom - r.top > 100)
      return h;
  }
  HWND k = nullptr;
  while ((k = FindWindowExA(h, k, nullptr, nullptr)) != nullptr)
  {
    HWND r = findList(k);
    if (r)
      return r;
  }
  return nullptr;
}

static int topIndex(HWND l) { return (int)SendMessageA(l, LVM_GETTOPINDEX, 0, 0); }

static int sbPos(HWND l)
{
  SCROLLINFO si = {0};
  si.cbSize = sizeof(si);
  si.fMask = SIF_ALL;
  if (!GetScrollInfo(l, SB_VERT, &si))
    return -1;
  return si.nPos;
}

static int sbMax(HWND l)
{
  SCROLLINFO si = {0};
  si.cbSize = sizeof(si);
  si.fMask = SIF_ALL;
  if (!GetScrollInfo(l, SB_VERT, &si))
    return -1;
  return si.nMax - (int)si.nPage + 1;
}

// Block until the position stops moving for 4 consecutive samples (~400 ms).
static void settle(HWND l)
{
  int last = -999999, same = 0;
  for (int i = 0; i < 100; ++i)
  {
    Sleep(100);
    const int p = topIndex(l) * 1000 + (sbPos(l) & 0xFFF);
    if (p == last)
    {
      if (++same >= 4)
        return;
    }
    else
    {
      same = 0;
      last = p;
    }
  }
}

static void wheel(HWND l, int d)
{
  SendMessageA(l, WM_MOUSEWHEEL, MAKEWPARAM(0, (short)d), 0);
}

static void toTop(HWND l)
{
  SendMessageA(l, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);
  settle(l);
}

static void trial(HWND l, const char *name, int d, int n)
{
  toTop(l);
  const int b = topIndex(l), bp = sbPos(l);
  for (int i = 0; i < n; ++i)
  {
    wheel(l, -d); // negative = scroll down, away from the top we just parked at
    Sleep(8);
  }
  settle(l);
  const int a = topIndex(l), ap = sbPos(l);
  printf("  %-16s d=%3d n=%-4d total=%5d | top %d -> %d (%+d) | sbPos %d -> %d\n", name,
         d, n, d * n, b, a, a - b, bp, ap);
}

int main(int argc, char **argv)
{
  if (argc > 1)
    strncpy(g_want, argv[1], sizeof(g_want) - 1);

  HWND m = FindWindowA("REAPERwnd", nullptr);
  if (!m)
  {
    printf("REAPERwnd not found\n");
    return 2;
  }
  GetWindowThreadProcessId(m, &g_pid);
  EnumWindows(enumTop, 0);
  if (!g_dlg)
  {
    printf("dialog \"%s\" not found\n", g_want);
    return 2;
  }
  g_list = findList(g_dlg);
  if (!g_list)
  {
    printf("no SysListView32 in \"%s\"\n", g_want);
    return 2;
  }

  printf("dialog \"%s\"  list=%p  items=%d  sbMax=%d\n", g_want, (void *)g_list,
         (int)SendMessageA(g_list, LVM_GETITEMCOUNT, 0, 0), sbMax(g_list));
  printf("\nreference (one whole notch, twice):\n");
  trial(g_list, "1 x 120", 120, 1);
  trial(g_list, "1 x 120", 120, 1);
  printf("\nsame total (120) at different chunk sizes:\n");
  trial(g_list, "3 x 40", 40, 3);
  trial(g_list, "8 x 15", 15, 8);
  trial(g_list, "15 x 8", 8, 15);
  trial(g_list, "24 x 5", 5, 24);
  trial(g_list, "120 x 1", 1, 120);
  return 0;
}
