// Read-only enumeration of REAPER's windows: class names, rects and visibility.
//
// Used to identify the surface families by their REAL window classes instead of guessing:
// the mixer (MCP) and the arrange view's own scrollbars. Nothing is written or sent --
// it only asks Windows what exists.
//
// Build/run:  g++ -O2 _diag/surfaces.cpp -o /tmp/surf && /tmp/surf
#include <windows.h>
#include <stdio.h>
#include <string.h>

static DWORD g_pid = 0;

static void Indent(int n)
{
  for (int i = 0; i < n; ++i) putchar(' ');
}

static const char *Interesting(const char *cls)
{
  if (!cls) return "";
  static const char *kW[] = {"MCP", "TCP", "Scroll", "scroll", "Arrange", "arrange",
                             "Mixer", "mixer", "Dock", "Rea"};
  for (size_t i = 0; i < sizeof(kW) / sizeof(kW[0]); ++i)
    if (strstr(cls, kW[i]))
      return "  <== interesting";
  return "";
}

static BOOL CALLBACK ChildProc(HWND h, LPARAM lp)
{
  const int depth = (int)lp;
  DWORD pid = 0;
  GetWindowThreadProcessId(h, &pid);
  if (pid != g_pid)
    return TRUE;

  char cls[128] = {0}, txt[128] = {0};
  GetClassNameA(h, cls, sizeof(cls));
  GetWindowTextA(h, txt, sizeof(txt));
  RECT r = {0};
  GetWindowRect(h, &r);
  Indent(depth);
  printf("%-34s vis=%d (%ld,%ld %ldx%ld)%s%s\n", cls, IsWindowVisible(h) ? 1 : 0,
         r.left, r.top, r.right - r.left, r.bottom - r.top, Interesting(cls),
         *txt ? " [text]" : "");
  EnumChildWindows(h, ChildProc, depth + 2);
  return TRUE;
}

static BOOL CALLBACK TopProc(HWND h, LPARAM)
{
  DWORD pid = 0;
  GetWindowThreadProcessId(h, &pid);
  if (pid != g_pid)
    return TRUE;
  char cls[128] = {0}, txt[128] = {0};
  GetClassNameA(h, cls, sizeof(cls));
  GetWindowTextA(h, txt, sizeof(txt));
  RECT r = {0};
  GetWindowRect(h, &r);
  printf("TOP %-30s vis=%d (%ld,%ld %ldx%ld)%s \"%s\"\n", cls,
         IsWindowVisible(h) ? 1 : 0, r.left, r.top, r.right - r.left, r.bottom - r.top,
         Interesting(cls), txt);
  EnumChildWindows(h, ChildProc, 2);
  return TRUE;
}

int main(int argc, char **argv)
{
  g_pid = (argc > 1) ? (DWORD)atoi(argv[1]) : 0;
  if (!g_pid)
  {
    printf("usage: surfaces <reaper-pid>\n");
    return 2;
  }
  printf("pid=%lu\n", (unsigned long)g_pid);
  EnumWindows(TopProc, 0);
  return 0;
}
