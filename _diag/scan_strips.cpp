// scan_strips -- find the window at every x across the TCP/arrange boundary, so the
// "resize divider" strips beside the track list can be identified by measurement.
//
// Reports, for each horizontal run of identical window class, the x range, the class,
// and the class RECT. Read-only.
//
// Usage: scan_strips.exe [y ...]
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static HWND g_main = nullptr;

static void describe(HWND h, int x, int y, char *out, size_t n)
{
  char cls[64] = {0};
  RECT r = {0};
  if (h)
  {
    GetClassNameA(h, cls, sizeof(cls));
    GetWindowRect(h, &r);
  }
  snprintf(out, n, "x %5d..%-5d cls=%-20s rect=%d,%d,%d,%d", x, x, cls, r.left, r.top,
           r.right, r.bottom);
}

static void scanRow(int y)
{
  RECT mr = {0};
  GetWindowRect(g_main, &mr);
  printf("\n=== scanline y=%d ===\n", y);

  HWND prev = nullptr;
  int runStart = mr.left;
  for (int x = mr.left; x <= mr.right; ++x)
  {
    POINT p = {x, y};
    HWND h = (x < mr.right) ? WindowFromPoint(p) : nullptr;
    if (x == mr.left)
    {
      prev = h;
      runStart = x;
      continue;
    }
    if (h != prev || x == mr.right)
    {
      char cls[64] = {0};
      RECT r = {0};
      if (prev)
      {
        GetClassNameA(prev, cls, sizeof(cls));
        GetWindowRect(prev, &r);
      }
      printf("  x %5d..%-5d (w=%4d) cls=%-20s hwnd=%p rect=%d,%d,%d,%d\n", runStart, x - 1,
             x - runStart, cls, (void *)prev, r.left, r.top, r.right, r.bottom);
      prev = h;
      runStart = x;
    }
  }
}

int main(int argc, char **argv)
{
  g_main = FindWindowA("REAPERwnd", nullptr);
  if (!g_main)
  {
    printf("REAPERwnd not found\n");
    return 2;
  }
  RECT mr = {0};
  GetWindowRect(g_main, &mr);

  HWND tcp = FindWindowExA(g_main, nullptr, "REAPERTCPDisplay", nullptr);
  RECT tr = {0};
  if (tcp)
    GetWindowRect(tcp, &tr);
  HWND arrange = GetDlgItem(g_main, 1000);
  RECT ar = {0};
  if (arrange)
    GetWindowRect(arrange, &ar);
  printf("main   = %p rect=%d,%d,%d,%d\n", (void *)g_main, mr.left, mr.top, mr.right, mr.bottom);
  printf("TCP    = %p rect=%d,%d,%d,%d\n", (void *)tcp, tr.left, tr.top, tr.right, tr.bottom);
  printf("arrange= %p rect=%d,%d,%d,%d\n", (void *)arrange, ar.left, ar.top, ar.right, ar.bottom);

  if (argc > 1)
    for (int i = 1; i < argc; ++i)
      scanRow(atoi(argv[i]));
  else
  {
    int rows[3] = {tr.top + 20, (tr.top + tr.bottom) / 2, tr.bottom - 20};
    for (int i = 0; i < 3; ++i)
      scanRow(rows[i]);
  }
  return 0;
}
