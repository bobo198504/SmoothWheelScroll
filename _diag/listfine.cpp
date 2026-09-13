// listfine -- can REAPER's tool-window list controls accept FINE (sub-notch) wheel
// deltas, or do they only move in whole items?
//
// Finds a SysListView32 / SysTreeView32 inside a REAPER dialog, parks the cursor over
// it, and sends WM_MOUSEWHEEL with a known total delta, reading the scroll position
// after each send. SendMessage is used deliberately: it bypasses any WH_GETMESSAGE
// hook in REAPER's UI thread, so what is measured is the control's own behaviour.
//
// Usage: listfine.exe
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>

static HWND g_found = nullptr;
static char g_cls[64];
static DWORD g_pid = 0;

static HWND findList(HWND h)
{
  char cls[64] = {0};
  GetClassNameA(h, cls, sizeof(cls));
  if ((strcmp(cls, "SysListView32") == 0 || strcmp(cls, "SysTreeView32") == 0) &&
      IsWindowVisible(h))
  {
    RECT r = {0};
    GetWindowRect(h, &r);
    if (r.right - r.left > 120 && r.bottom - r.top > 120)
    {
      strncpy(g_cls, cls, sizeof(g_cls) - 1);
      return h;
    }
  }
  HWND c = nullptr;
  while ((c = FindWindowExA(h, c, nullptr, nullptr)) != nullptr)
  {
    HWND r = findList(c);
    if (r)
      return r;
  }
  return nullptr;
}

static BOOL CALLBACK enumTop(HWND h, LPARAM)
{
  DWORD pid = 0;
  GetWindowThreadProcessId(h, &pid);
  if (pid != g_pid)
    return TRUE;
  char cls[64] = {0};
  GetClassNameA(h, cls, sizeof(cls));
  if (strcmp(cls, "#32770") != 0)
    return TRUE;
  char title[128] = {0};
  GetWindowTextA(h, title, sizeof(title));
  HWND l = findList(h);
  if (l)
  {
    printf("dialog \"%s\" -> %s %p\n", title, g_cls, (void *)l);
    g_found = l;
    return FALSE;
  }
  return TRUE;
}

static int npos(HWND l)
{
  if (strcmp(g_cls, "SysListView32") == 0)
  {
    int top = (int)SendMessageA(l, LVM_GETTOPINDEX, 0, 0);
    return top;
  }
  // tree: first visible item index order
  return (int)SendMessageA(l, TVM_GETCOUNT, 0, 0);
}

static void send(int delta)
{
  RECT r = {0};
  GetWindowRect(g_found, &r);
  SetCursorPos((r.left + r.right) / 2, (r.top + r.bottom) / 2);
  Sleep(50);
  SendMessageA(g_found, WM_MOUSEWHEEL, MAKEWPARAM(0, (short)delta), 0);
  Sleep(80);
}

static void trial(const char *name, int delta, int count)
{
  // Normalise to the very top first (a positive delta scrolls toward the top), so
  // every trial starts from the same place and always has room to move DOWN. Trials
  // then use negative deltas only. Without this, a list at either end clamps and the
  // measurement reads 0 for the wrong reason.
  for (int i = 0; i < 200 && npos(g_found) > 0; ++i)
    send(120);
  const int before = npos(g_found);
  for (int i = 0; i < count; ++i)
    send(-delta);
  const int after = npos(g_found);
  printf("  %-26s delta=%3d x%-3d total=%5d -> top %d -> %d  (moved %d)\n", name, delta,
         count, delta * count, before, after, after - before);
}

int main(void)
{
  HWND main = FindWindowA("REAPERwnd", nullptr);
  if (!main)
  {
    printf("REAPERwnd not found\n");
    return 2;
  }
  GetWindowThreadProcessId(main, &g_pid);
  EnumWindows(enumTop, 0);
  if (!g_found)
  {
    printf("no list control found in a REAPER dialog; open one (Actions / FX browser)\n");
    return 2;
  }

  printf("control %s -- rows moved for a given total delta, by chunking\n\n", g_cls);

  // A reference: one whole notch on its own, and one notch split into 8.
  printf("reference (whole notch = 120):\n");
  trial("1 x 120", 120, 1);
  trial("8 x 15", 15, 8);

  // The matrix: same totals, different chunk sizes. If the control carries the
  // remainder across messages, every row with the same total moves the same amount.
  const int deltas[] = {1, 5, 8, 15, 30, 40, 60, 120};
  const int totals[] = {120, 240};
  for (size_t t = 0; t < sizeof(totals) / sizeof(totals[0]); ++t)
  {
    printf("\ntotal = %d (should be %d rows if one notch = 3 rows):\n", totals[t],
           (totals[t] / 120) * 3);
    for (size_t d = 0; d < sizeof(deltas) / sizeof(deltas[0]); ++d)
    {
      if (totals[t] % deltas[d])
        continue;
      const int n = totals[t] / deltas[d];
      char nm[32];
      snprintf(nm, sizeof(nm), "%d x %d", n, deltas[d]);
      trial(nm, deltas[d], n);
    }
  }
  return 0;
}
