// prefctl -- in a REAPER dialog that has NO action of its own (Preferences, Actions,
// the managers), does a wheel over a native control change a VALUE?
//
// A ComboBox / ListBox responds to the wheel by changing its selection: that is a
// parameter edit, not a scroll, so such controls must be left to REAPER. This prints
// the selection before and after a wheel to show it.
//
// Usage: prefctl.exe "<dialog title substring>"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static DWORD g_pid = 0;
static HWND g_dlg = nullptr;
static char g_want[64];

static BOOL CALLBACK enumTop(HWND h, LPARAM)
{
  DWORD pid = 0;
  GetWindowThreadProcessId(h, &pid);
  if (pid != g_pid)
    return TRUE;
  char cls[64] = {0}, title[128] = {0};
  GetClassNameA(h, cls, sizeof(cls));
  GetWindowTextA(h, title, sizeof(title));
  if (strcmp(cls, "#32770") == 0)
  {
    // case-insensitive substring
    char lt[128], lw[64];
    size_t i;
    for (i = 0; title[i] && i < 127; ++i) lt[i] = (char)tolower((unsigned char)title[i]);
    lt[i] = 0;
    for (i = 0; g_want[i] && i < 63; ++i) lw[i] = (char)tolower((unsigned char)g_want[i]);
    lw[i] = 0;
    if (strstr(lt, lw))
    {
      g_dlg = h;
      printf("dialog \"%s\"\n", title);
      return FALSE;
    }
  }
  return TRUE;
}

struct Ctl { HWND h; char cls[32]; };
static Ctl g_ctl[64];
static int g_nc = 0;
static int g_real = 0;

static void oneWheel(HWND h, int delta)
{
  RECT r = {0};
  GetWindowRect(h, &r);
  SetCursorPos((r.left + r.right) / 2, (r.top + r.bottom) / 2);
  Sleep(g_real ? 120 : 60);
  if (g_real)
  {
    INPUT in = {0};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_WHEEL;
    in.mi.mouseData = (DWORD)delta;
    SendInput(1, &in, sizeof(in));
  }
  else
  {
    SendMessageA(h, WM_MOUSEWHEEL, MAKEWPARAM(0, (short)delta), 0);
  }
  Sleep(g_real ? 140 : 60);
}

static void collect(HWND h)
{
  char cls[64] = {0};
  GetClassNameA(h, cls, sizeof(cls));
  if (g_nc < 64 &&
      (strcmp(cls, "ComboBox") == 0 || strcmp(cls, "ListBox") == 0 ||
       strcmp(cls, "SysListView32") == 0))
  {
    RECT r = {0};
    GetWindowRect(h, &r);
    if (IsWindowVisible(h) && r.right - r.left > 40 && r.bottom - r.top > 14)
    {
      g_ctl[g_nc].h = h;
      strncpy(g_ctl[g_nc].cls, cls, sizeof(g_ctl[g_nc].cls) - 1);
      ++g_nc;
    }
  }
  HWND c = nullptr;
  while ((c = FindWindowExA(h, c, nullptr, nullptr)) != nullptr)
    collect(c);
}

static int selOf(HWND h, const char *cls)
{
  if (strcmp(cls, "ComboBox") == 0)
    return (int)SendMessageA(h, CB_GETCURSEL, 0, 0);
  if (strcmp(cls, "ListBox") == 0)
    return (int)SendMessageA(h, LB_GETCURSEL, 0, 0);
  return (int)SendMessageA(h, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
}

static int cntOf(HWND h, const char *cls)
{
  if (strcmp(cls, "ComboBox") == 0)
    return (int)SendMessageA(h, CB_GETCOUNT, 0, 0);
  if (strcmp(cls, "ListBox") == 0)
    return (int)SendMessageA(h, LB_GETCOUNT, 0, 0);
  return (int)SendMessageA(h, LVM_GETITEMCOUNT, 0, 0);
}

int main(int argc, char **argv)
{
  const char *want = nullptr;
  for (int i = 1; i < argc; ++i)
  {
    if (strcmp(argv[i], "--real") == 0) { g_real = 1; continue; }
    want = argv[i];
  }
  if (!want)
  {
    printf("usage: prefctl.exe [--real] \"<dialog title substring>\"\n");
    return 2;
  }
  strncpy(g_want, want, sizeof(g_want) - 1);
  printf("mode: %s\n", g_real ? "REAL input (through the message queue)" : "SendMessage");

  HWND main = FindWindowA("REAPERwnd", nullptr);
  if (!main)
  {
    printf("REAPERwnd not found\n");
    return 2;
  }
  GetWindowThreadProcessId(main, &g_pid);
  EnumWindows(enumTop, 0);
  if (!g_dlg)
  {
    printf("dialog containing \"%s\" not found\n", g_want);
    return 2;
  }

  collect(g_dlg);
  printf("found %d list/combo controls\n\n", g_nc);
  for (int i = 0; i < g_nc; ++i)
  {
    HWND h = g_ctl[i].h;
    const int cnt = cntOf(h, g_ctl[i].cls);
    if (cnt <= 1)
      continue;
    RECT r = {0};
    GetWindowRect(h, &r);
    const int before = selOf(h, g_ctl[i].cls);

    SetCursorPos((r.left + r.right) / 2, (r.top + r.bottom) / 2);
    Sleep(60);
    for (int k = 0; k < 2; ++k)
    {
      oneWheel(h, 120);
      oneWheel(h, -120);
    }
    const int after = selOf(h, g_ctl[i].cls);
    printf("%-14s rect=%d,%d,%d,%d items=%3d  sel %d -> %d  %s\n", g_ctl[i].cls, r.left,
           r.top, r.right, r.bottom, cnt, before, after,
           before != after ? "*** VALUE CHANGED ***" : "");
  }
  return 0;
}
