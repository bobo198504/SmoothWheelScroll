// input_probe -- drive real mouse-wheel input at chosen screen points so the
// plugin's debug log records, per surface:
//   * the window under the cursor and REAPER's own hit-test token (GetThingFromPoint)
//   * the action REAPER resolves that wheel to (hookcommand2)
//
// The point of the modifier option is the MIDI editor: a plain wheel over the note
// area is a view zoom, while Alt+wheel over the same pixels edits the selected
// notes' velocity. That is a PARAMETER wheel in a View window, so it is the case the
// plugin must leave alone. Whether the modifier reaches the hook is measured, not
// assumed.
//
// Usage:  input_probe.exe [--fast] [--mod alt|ctrl|shift] <x> <y> [<x> <y> ...]
//         input_probe.exe [--fast] [--mod alt] --grid <L> <T> <R> <B> <step>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fast = 0;
static WORD g_modVk = 0;

static void modDown(void)
{
  if (!g_modVk)
    return;
  INPUT in = {0};
  in.type = INPUT_KEYBOARD;
  in.ki.wVk = g_modVk;
  SendInput(1, &in, sizeof(in));
  Sleep(40);
}

static void modUp(void)
{
  if (!g_modVk)
    return;
  INPUT in = {0};
  in.type = INPUT_KEYBOARD;
  in.ki.wVk = g_modVk;
  in.ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(1, &in, sizeof(in));
  Sleep(40);
}

static void wheelAt(int x, int y, int notches)
{
  SetCursorPos(x, y);
  Sleep(g_fast ? 15 : 60);
  modDown();
  for (int i = 0; i < (notches < 0 ? -notches : notches); ++i)
  {
    INPUT in = {0};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_WHEEL;
    in.mi.mouseData = (notches > 0) ? WHEEL_DELTA : (DWORD)(-WHEEL_DELTA);
    SendInput(1, &in, sizeof(in));
    Sleep(g_fast ? 20 : 90);
  }
  modUp();
  Sleep(g_fast ? 20 : 120);
}

int main(int argc, char **argv)
{
  int a = 1;
  for (;;)
  {
    if (a < argc && strcmp(argv[a], "--fast") == 0) { g_fast = 1; ++a; continue; }
    if (a + 1 < argc && strcmp(argv[a], "--mod") == 0)
    {
      if (strcmp(argv[a + 1], "alt") == 0) g_modVk = VK_MENU;
      else if (strcmp(argv[a + 1], "ctrl") == 0) g_modVk = VK_CONTROL;
      else if (strcmp(argv[a + 1], "shift") == 0) g_modVk = VK_SHIFT;
      a += 2;
      continue;
    }
    break;
  }
  if (argc - a < 2)
  {
    printf("usage: input_probe.exe [--fast] [--mod alt|ctrl|shift] x y [x y ...] | --grid L T R B step\n");
    return 2;
  }

  HWND main = FindWindowA("REAPERwnd", nullptr);
  if (!main)
  {
    printf("REAPERwnd not found\n");
    return 2;
  }
  SetForegroundWindow(main);
  Sleep(200);

  if (strcmp(argv[a], "--grid") == 0 && argc - a >= 6)
  {
    const int L = atoi(argv[a + 1]), T = atoi(argv[a + 2]);
    const int R = atoi(argv[a + 3]), B = atoi(argv[a + 4]);
    const int st = atoi(argv[a + 5]) > 0 ? atoi(argv[a + 5]) : 24;
    int k = 0;
    for (int y = T; y < B; y += st)
      for (int x = L; x < R; x += st, ++k)
        wheelAt(x, y, 1);
    printf("done %d points\n", k);
    return 0;
  }

  for (int i = a; i + 1 < argc; i += 2)
  {
    const int x = atoi(argv[i]), y = atoi(argv[i + 1]);
    printf("point %d,%d\n", x, y);
    fflush(stdout);
    wheelAt(x, y, 1);
  }
  return 0;
}
