// findwnd -- recursively list every window in the REAPER process whose class matches
// a substring, with its rect and scrollbar state. Read-only.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

static DWORD g_pid = 0;
static const char *g_needle;
static int g_hits = 0;

static void walk(HWND h, int depth)
{
  char cls[64] = {0};
  GetClassNameA(h, cls, sizeof(cls));
  RECT r = {0};
  GetWindowRect(h, &r);
  if (strstr(cls, g_needle))
  {
    ++g_hits;
    printf("%*s%s  hwnd=%p  rect=%d,%d,%d,%d  visible=%d\n", depth * 2, "", cls, (void *)h,
           r.left, r.top, r.right, r.bottom, IsWindowVisible(h) ? 1 : 0);
    SCROLLINFO si = {0};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    if (GetScrollInfo(h, SB_VERT, &si))
      printf("%*s  V: nMin=%d nMax=%d page=%d pos=%d\n", depth * 2, "", si.nMin, si.nMax,
             (int)si.nPage, si.nPos);
    if (GetScrollInfo(h, SB_HORZ, &si))
      printf("%*s  H: nMin=%d nMax=%d page=%d pos=%d\n", depth * 2, "", si.nMin, si.nMax,
             (int)si.nPage, si.nPos);
  }
  HWND c = nullptr;
  while ((c = FindWindowExA(h, c, nullptr, nullptr)) != nullptr)
    walk(c, depth + 1);
}

static BOOL CALLBACK et(HWND h, LPARAM)
{
  DWORD pid = 0;
  GetWindowThreadProcessId(h, &pid);
  if (pid != g_pid)
    return TRUE;
  walk(h, 0);
  return TRUE;
}

int main(int argc, char **argv)
{
  g_needle = (argc > 1) ? argv[1] : "MIDI";
  HWND m = FindWindowA("REAPERwnd", nullptr);
  if (!m) { printf("REAPERwnd not found\n"); return 2; }
  GetWindowThreadProcessId(m, &g_pid);
  printf("searching class names containing \"%s\"\n", g_needle);
  EnumWindows(et, 0);
  if (!g_hits) printf("(no match)\n");
  return 0;
}
