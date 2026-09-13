// wheelprobe -- does a given list window actually respond to a replayed
// WM_MOUSEWHEEL? This answers it by measurement instead of assumption.
//
// Modes:
//   list  : enumerate REAPER's windows (class + title + scroll state)
//   open  : ask REAPER to open the action list (Shift+/), then enumerate
//   test  : for every scrollable list-ish window, note the scroll position, send
//           WM_MOUSEWHEEL, then report whether the position moved.
#include <windows.h>
#include <commctrl.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct Win { HWND h; std::string cls, title; };

static BOOL CALLBACK EnumAll(HWND h, LPARAM lp)
{
  auto *v = (std::vector<Win> *)lp;
  char cls[128] = {0}, t[256] = {0};
  GetClassNameA(h, cls, sizeof(cls));
  GetWindowTextA(h, t, sizeof(t));
  Win w; w.h = h; w.cls = cls; w.title = t;
  v->push_back(w);
  return TRUE;
}

static std::vector<Win> Collect()
{
  std::vector<Win> v;
  EnumWindows(EnumAll, (LPARAM)&v);
  size_t n = v.size();
  for (size_t i = 0; i < n; ++i)
  {
    std::vector<Win> kids;
    EnumChildWindows(v[i].h, EnumAll, (LPARAM)&kids);
    for (auto &k : kids) v.push_back(k);
    n = v.size();
    if (v.size() > 4000) break;
  }
  return v;
}

static bool IsListish(const std::string &c)
{
  if (c == "SysListView32" || c == "SysTreeView32") return true;
  if (strncmp(c.c_str(), "AVWDL_", 6) == 0) return true;
  return false;
}

static int ScrollPos(HWND h, int bar)
{
  SCROLLINFO si = {sizeof(si)};
  si.fMask = SIF_ALL;
  if (!GetScrollInfo(h, bar, &si)) return -12345;
  return si.nPos;
}

static HWND FindReaper()
{
  std::vector<Win> v;
  EnumWindows(EnumAll, (LPARAM)&v);
  for (auto &w : v)
  {
    if (w.cls == "REAPERwnd") return w.h; // main window
  }
  return nullptr;
}

static void OpenActionList(HWND main)
{
  SetForegroundWindow(main);
  Sleep(400);
  // Shift + '/' = '?', REAPER's default "Show action list".
  INPUT in[4] = {};
  in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_SHIFT;
  in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = VK_OEM_2;
  in[2].type = INPUT_KEYBOARD; in[2].ki.wVk = VK_OEM_2; in[2].ki.dwFlags = KEYEVENTF_KEYUP;
  in[3].type = INPUT_KEYBOARD; in[3].ki.wVk = VK_SHIFT;  in[3].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(4, in, sizeof(INPUT));
  Sleep(1200);
}

int main(int argc, char **argv)
{
  const char *mode = argc > 1 ? argv[1] : "list";
  HWND main = FindReaper();
  printf("reaper main = %p\n", (void *)main);
  if (!main) return 2;

  if (!strcmp(mode, "open") || !strcmp(mode, "test"))
  {
    OpenActionList(main);
    if (!strcmp(mode, "open"))
    {
      auto v = Collect();
      printf("\n== windows after opening action list ==\n");
      for (auto &w : v)
        if (IsListish(w.cls) || strstr(w.title.c_str(), "Action"))
          printf("  %p  %-22s  \"%s\"  vpos=%d\n", (void *)w.h, w.cls.c_str(), w.title.c_str(),
                 ScrollPos(w.h, SB_VERT));
      return 0;
    }
  }

  auto v = Collect();
  printf("\n== list-ish windows ==\n");
  std::vector<Win> cand;
  for (auto &w : v)
  {
    if (!IsListish(w.cls)) continue;
    if (!IsWindowVisible(w.h)) continue;
    int vp = ScrollPos(w.h, SB_VERT);
    RECT r; GetWindowRect(w.h, &r);
    printf("  %p  %-22s  rect=[%d,%d,%d,%d]  vpos=%d  title=\"%s\"\n",
           (void *)w.h, w.cls.c_str(), r.left, r.top, r.right, r.bottom, vp, w.title.c_str());
    cand.push_back(w);
  }

  printf("\n== wheel replay test ==\n");
  for (auto &w : cand)
  {
    RECT r; GetWindowRect(w.h, &r);
    const int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    int before = ScrollPos(w.h, SB_VERT);
    // Replay a full wheel notch downward, addressed to this window at its centre.
    SendMessage(w.h, WM_MOUSEWHEEL, MAKEWPARAM(0, (short)-WHEEL_DELTA),
                MAKELPARAM(cx, cy));
    Sleep(150);
    int after = ScrollPos(w.h, SB_VERT);
    printf("  %-22s vpos %d -> %d  %s\n", w.cls.c_str(), before, after,
           (after != before) ? "SCROLLED  (wheel replay works)" :
           ((before == -12345) ? "no scrollbar" : "no move   (wheel replay ignored)"));
  }
  return 0;
}
