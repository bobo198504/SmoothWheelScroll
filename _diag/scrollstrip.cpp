// Read-only: locate the exact strip of the arrange view's scrollbars.
//
// Scans a fine line across the right edge and the bottom edge of REAPERTrackListWindow and
// prints only where the window class CHANGES, so the scrollbar strip (if it is a distinct
// surface) shows up as a transition rather than a wall of identical rows.
//
// Build/run:  g++ -O2 _diag/scrollstrip.cpp -o /tmp/ss && /tmp/ss <reaper-pid>
#include <windows.h>
#include <stdio.h>
#include <string.h>

static void ClassAt(int x, int y, char *out, size_t n)
{
  POINT p = {x, y};
  HWND h = WindowFromPoint(p);
  out[0] = 0;
  if (h)
    GetClassNameA(h, out, n);
  if (!out[0])
    snprintf(out, n, "(none)");
}

int main(int argc, char **argv)
{
  (void)argv;
  if (argc < 2)
  {
    printf("usage: scrollstrip <reaper-pid>\n");
    return 2;
  }
  HWND main = FindWindowA("REAPERwnd", nullptr);
  HWND arr = main ? FindWindowExA(main, nullptr, "REAPERTrackListWindow", nullptr) : nullptr;
  if (!arr)
  {
    printf("arrange window not found\n");
    return 1;
  }
  RECT ar = {0};
  GetWindowRect(arr, &ar);
  printf("arrange = (%ld,%ld)-(%ld,%ld)\n\n", ar.left, ar.top, ar.right, ar.bottom);

  printf("== horizontal scan across the RIGHT edge, at y = top+80 ==\n");
  {
    const int y = ar.top + 80;
    char prev[128] = "";
    for (int x = ar.right - 30; x <= ar.right + 6; ++x)
    {
      char c[128];
      ClassAt(x, y, c, sizeof(c));
      if (strcmp(c, prev) != 0)
      {
        printf("   x=%4d : %s\n", x, c);
        strcpy(prev, c);
      }
    }
  }

  printf("\n== horizontal scan across the RIGHT edge, at y = mid ==\n");
  {
    const int y = (ar.top + ar.bottom) / 2;
    char prev[128] = "";
    for (int x = ar.right - 30; x <= ar.right + 6; ++x)
    {
      char c[128];
      ClassAt(x, y, c, sizeof(c));
      if (strcmp(c, prev) != 0)
      {
        printf("   x=%4d : %s\n", x, c);
        strcpy(prev, c);
      }
    }
  }

  printf("\n== vertical scan across the BOTTOM edge, at x = left+80 ==\n");
  {
    const int x = ar.left + 80;
    char prev[128] = "";
    for (int y = ar.bottom - 30; y <= ar.bottom + 6; ++y)
    {
      char c[128];
      ClassAt(x, y, c, sizeof(c));
      if (strcmp(c, prev) != 0)
      {
        printf("   y=%4d : %s\n", y, c);
        strcpy(prev, c);
      }
    }
  }

  printf("\n== vertical scan across the BOTTOM edge, at x = right-200 ==\n");
  {
    const int x = ar.right - 200;
    char prev[128] = "";
    for (int y = ar.bottom - 30; y <= ar.bottom + 6; ++y)
    {
      char c[128];
      ClassAt(x, y, c, sizeof(c));
      if (strcmp(c, prev) != 0)
      {
        printf("   y=%4d : %s\n", y, c);
        strcpy(prev, c);
      }
    }
  }
  return 0;
}
