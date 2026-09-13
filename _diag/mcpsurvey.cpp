// Read-only: survey the mixer (MCP) -- structure, scrollbar band, and the same
// system-metric comparison. Preparation only; nothing is written or sent.
//
// Build/run:  g++ -O2 _diag/mcpsurvey.cpp -o /tmp/mcps && /tmp/mcps
#include <windows.h>
#include <stdio.h>
#include <string.h>

struct Item { HWND h; RECT r; char cls[64]; char txt[96]; int depth; };
static Item g_items[64];
static int g_n = 0;
static DWORD g_pid = 0;

static BOOL CALLBACK ChildProc(HWND h, LPARAM lp)
{
  const int depth = (int)lp;
  DWORD pid = 0;
  GetWindowThreadProcessId(h, &pid);
  if (pid != g_pid) return TRUE;
  char cls[64] = {0};
  GetClassNameA(h, cls, sizeof(cls));
  if (strstr(cls, "MCP") || strstr(cls, "mcp"))
  {
    if (g_n < 64)
    {
      Item &it = g_items[g_n++];
      it.h = h;
      it.depth = depth;
      GetWindowRect(h, &it.r);
      strncpy(it.cls, cls, sizeof(it.cls) - 1);
      GetWindowTextA(h, it.txt, sizeof(it.txt));
    }
    // do NOT recurse into the panel: its children are endless and not needed
    return TRUE;
  }
  EnumChildWindows(h, ChildProc, depth + 1);
  return TRUE;
}

static void Profile(const char *label, int x0, int y0, int dx, int dy, int n)
{
  printf("  %s\n", label);
  HDC s = GetDC(NULL);
  for (int i = 0; i < n; ++i)
  {
    const int x = x0 + dx * i, y = y0 + dy * i;
    COLORREF c = GetPixel(s, x, y);
    printf("     %s(%d) = %02x%02x%02x\n", dx ? "x" : "y", dx ? x : y, GetRValue(c),
           GetGValue(c), GetBValue(c));
  }
  ReleaseDC(NULL, s);
}

int main(int argc, char **argv)
{
  g_pid = (argc > 1) ? (DWORD)strtoul(argv[1], nullptr, 10) : 0;
  if (!g_pid) { printf("usage: mcpsurvey <reaper-pid>\n"); return 2; }

  HWND main = FindWindowA("REAPERwnd", nullptr);
  printf("REAPERwnd = %p\n", (void *)main);
  EnumChildWindows(main, ChildProc, 0);

  printf("\n== MCP windows (%d) ==\n", g_n);
  for (int i = 0; i < g_n; ++i)
  {
    Item &it = g_items[i];
    printf("  [%d] %-20s (%ld,%ld) %ldx%ld %s\n", i, it.cls, it.r.left, it.r.top,
           it.r.right - it.r.left, it.r.bottom - it.r.top, *it.txt ? it.txt : "");
  }

  // Union rect of all MCP windows, to find the mixer's outer bounds.
  if (g_n)
  {
    RECT u = g_items[0].r;
    for (int i = 1; i < g_n; ++i)
    {
      if (g_items[i].r.left < u.left) u.left = g_items[i].r.left;
      if (g_items[i].r.top < u.top) u.top = g_items[i].r.top;
      if (g_items[i].r.right > u.right) u.right = g_items[i].r.right;
      if (g_items[i].r.bottom > u.bottom) u.bottom = g_items[i].r.bottom;
    }
    printf("\n  MCP union = (%ld,%ld)-(%ld,%ld)  %ldx%ld\n", u.left, u.top, u.right, u.bottom,
           u.right - u.left, u.bottom - u.top);

    printf("\n== colour profile: bottom edge of the mixer (scan UP) ==\n");
    Profile("x = union.left+60", u.left + 60, u.bottom - 1, 0, -1, 24);
    printf("\n== colour profile: right edge of the mixer (scan LEFT) ==\n");
    Profile("y = union.top+60", u.right - 1, u.top + 60, -1, 0, 24);
  }

  printf("\n== system metrics ==\n");
  printf("   SM_CXVSCROLL=%d SM_CYHSCROLL=%d\n", GetSystemMetrics(SM_CXVSCROLL),
         GetSystemMetrics(SM_CYHSCROLL));
  return 0;
}
