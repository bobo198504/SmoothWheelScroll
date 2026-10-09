// WINDOW SURVEY -- what windows exist in the REAPER process, and what classes are they?
//
//   g++ -std=c++17 -O2 -o windowsurvey.exe windowsurvey.cpp && ./windowsurvey.exe
//
// WHY: before deciding whether an ARA2 pitch editor can be reached at all, the first question is
// WHICH PROCESS its window belongs to. A window in the REAPER process is reachable by the plugin's
// message hook; a window in the plugin's own process is not reachable, ever, and no amount of
// cleverness changes that.
//
// The survey is READ-ONLY: it enumerates, prints, and sends nothing.

#include <windows.h>
#include <stdio.h>
#include <string.h>

struct Row { char cls[96]; int count; RECT first; DWORD pid; };
static Row g_rows[256];
static int g_n = 0;

static void Note(const char *cls, DWORD pid, const RECT &r)
{
  for (int i = 0; i < g_n; ++i)
  {
    if (strcmp(g_rows[i].cls, cls) == 0 && g_rows[i].pid == pid)
    {
      ++g_rows[i].count;
      return;
    }
  }
  if (g_n < 256)
  {
    Row &row = g_rows[g_n++];
    strncpy(row.cls, cls, sizeof(row.cls) - 1);
    row.cls[sizeof(row.cls) - 1] = 0;
    row.count = 1;
    row.first = r;
    row.pid = pid;
  }
}

static BOOL CALLBACK ChildProc(HWND h, LPARAM lp)
{
  const DWORD wantPid = (DWORD)lp;
  DWORD pid = 0;
  GetWindowThreadProcessId(h, &pid);
  if (pid != wantPid)
    return TRUE;
  char cls[96] = {0};
  GetClassNameA(h, cls, sizeof(cls));
  RECT r = {0};
  GetWindowRect(h, &r);
  Note(cls, pid, r);
  EnumChildWindows(h, ChildProc, lp); // recurse: the interesting windows are usually nested
  return TRUE;
}

int main()
{
  printf("REAPER window survey (read-only)\n\n");

  HWND main = FindWindowA("REAPERwnd", nullptr);
  if (!main)
  {
    printf("REAPERwnd not found -- is REAPER running?\n");
    return 1;
  }
  DWORD reaperPid = 0;
  GetWindowThreadProcessId(main, &reaperPid);
  printf("REAPER pid = %lu\n\n", (unsigned long)reaperPid);

  // --- 1. Every distinct window class in the REAPER PROCESS. ---
  //
  // A class here means the plugin's hook can see that window's wheel. A class NOT here but visible
  // on screen belongs to some other process, and is out of reach.
  //
  // NOTE: this walks ALL TOP-LEVEL windows of the process, not just the children of REAPERwnd. An
  // earlier version started at REAPERwnd and missed an ARA editor entirely -- a plugin's window can
  // be a top-level window of its own, parented only to the desktop. Missing it would have produced
  // the wrong answer to the whole question.
  printf("  [1] window classes INSIDE the REAPER process (reachable):\n");
  EnumChildWindows(main, ChildProc, (LPARAM)reaperPid);
  Note("REAPERwnd", reaperPid, RECT{0, 0, 0, 0});

  // Every top-level window that belongs to the REAPER process, plus its descendants.
  struct TopCtx { DWORD pid; };
  for (HWND w = GetTopWindow(nullptr); w; w = GetWindow(w, GW_HWNDNEXT))
  {
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (pid != reaperPid)
      continue;
    char cls[96] = {0};
    GetClassNameA(w, cls, sizeof(cls));
    RECT r = {0};
    GetWindowRect(w, &r);
    Note(cls, pid, r);
    EnumChildWindows(w, ChildProc, (LPARAM)reaperPid);
  }

  // Sort by name so the list is readable, and so a foreign-looking class stands out.
  for (int i = 0; i < g_n; ++i)
    for (int j = i + 1; j < g_n; ++j)
      if (strcmp(g_rows[i].cls, g_rows[j].cls) > 0)
      {
        Row t = g_rows[i];
        g_rows[i] = g_rows[j];
        g_rows[j] = t;
      }

  for (int i = 0; i < g_n; ++i)
    printf("      %-34s x%-4d first=%ld,%ld,%ld,%ld\n", g_rows[i].cls, g_rows[i].count,
           g_rows[i].first.left, g_rows[i].first.top, g_rows[i].first.right, g_rows[i].first.bottom);

  // --- 1b. Every VISIBLE top-level window of the REAPER process, with its title. ---
  //
  // The class list above collapses duplicates; this one does not, so a single plugin editor is easy
  // to spot by name and size even when its class looks like something generic (#32770 is a plain
  // dialog and several unrelated windows share it).
  printf("\n  [1b] visible top-level windows OF THE REAPER PROCESS (with titles):\n");
  for (HWND w = GetTopWindow(nullptr); w; w = GetWindow(w, GW_HWNDNEXT))
  {
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (pid != reaperPid || !IsWindowVisible(w))
      continue;
    RECT r = {0};
    GetWindowRect(w, &r);
    if (r.right - r.left < 80 || r.bottom - r.top < 60)
      continue;
    char cls[96] = {0};
    GetClassNameA(w, cls, sizeof(cls));
    char title[160] = {0};
    GetWindowTextA(w, title, sizeof(title));
    printf("      hwnd=%p %-26s rect=%ld,%ld,%ld,%ld (%ldx%ld)\n", (void *)w, cls, r.left, r.top,
           r.right, r.bottom, r.right - r.left, r.bottom - r.top);
    if (title[0])
      printf("               title: %s\n", title);
  }

  // --- 2. EVERY process that owns a visible top-level window, and its title. ---
  //
  // This is where an ARA2 editor shows up if it runs in its own process: a separate exe with a
  // window on screen, which section [1] cannot contain.
  printf("\n  [2] visible top-level windows in OTHER processes (NOT reachable by the plugin):\n");
  int others = 0;
  struct { char title[128]; char cls[96]; DWORD pid; char exe[96]; RECT r; } list[64];
  int ln = 0;
  for (HWND w = GetTopWindow(nullptr); w && ln < 64; w = GetWindow(w, GW_HWNDNEXT))
  {
    if (!IsWindowVisible(w))
      continue;
    RECT r = {0};
    GetWindowRect(w, &r);
    if (r.right - r.left < 60 || r.bottom - r.top < 40)
      continue;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (pid == reaperPid)
      continue;
    char title[128] = {0};
    GetWindowTextA(w, title, sizeof(title));
    char cls[96] = {0};
    GetClassNameA(w, cls, sizeof(cls));
    char exe[96] = {0};
    HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hp)
    {
      DWORD n = sizeof(exe);
      QueryFullProcessImageNameA(hp, 0, exe, &n);
      CloseHandle(hp);
    }
    strncpy(list[ln].title, title, sizeof(list[ln].title) - 1);
    strncpy(list[ln].cls, cls, sizeof(list[ln].cls) - 1);
    strncpy(list[ln].exe, exe, sizeof(list[ln].exe) - 1);
    list[ln].pid = pid;
    list[ln].r = r;
    ++ln;
  }
  for (int i = 0; i < ln; ++i)
  {
    const char *base = strrchr(list[i].exe, '\\');
    base = base ? base + 1 : list[i].exe;
    printf("      pid=%-7lu %-22s cls=%-26s rect=%ld,%ld,%ld,%ld\n", (unsigned long)list[i].pid, base,
           list[i].cls, list[i].r.left, list[i].r.top, list[i].r.right, list[i].r.bottom);
    if (list[i].title[0])
      printf("               title: %s\n", list[i].title);
    ++others;
  }
  if (!others)
    printf("      (none)\n");

  printf("\n  --- how to read this ---\n");
  printf("  * Look for the ARA editor's class in [1]: if it is there, the plugin's hook CAN see its\n");
  printf("    wheel and the mixer's approach becomes worth testing.\n");
  printf("  * If it appears in [2] instead (its own .exe with its own window), the plugin can NEVER\n");
  printf("    reach it: the wheel message never enters the REAPER process at all.\n");
  return 0;
}
