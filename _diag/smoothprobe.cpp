// smoothprobe -- can a REAPER list be made to scroll smoothly WITHOUT replaying
// wheels and WITHOUT driving it ourselves?
//
// Tests two mechanisms on a real REAPER ListView:
//   T1  SendMessage(WM_MOUSEWHEEL) with the cursor parked over the list.
//       (A ListView's wheel handler may use GetMessagePos(), not the lParam point,
//        so the earlier probe may have failed only because the cursor was elsewhere.)
//   T2  LVS_SMOOTHSCROLL (0x20) enabled on the control, then the SAME sends:
//       a list with that style animates its own scrolling natively. REAPER's tree
//       views already carry it; its list views do not.
//
// It samples nPos over time so an animated (smoothed) scroll is distinguishable from
// an instant jump.
#include <windows.h>
#include <commctrl.h>
#include <cstdio>
#include <string>
#include <vector>

#ifndef LVS_SMOOTHSCROLL
#define LVS_SMOOTHSCROLL 0x00000020L
#endif

struct Win { HWND h; std::string cls; };
static BOOL CALLBACK EnumAll(HWND h, LPARAM lp)
{
  auto *v = (std::vector<Win> *)lp;
  char c[128] = {0};
  GetClassNameA(h, c, sizeof(c));
  v->push_back(Win{h, c});
  return TRUE;
}
static std::vector<Win> Collect()
{
  std::vector<Win> v;
  EnumWindows(EnumAll, (LPARAM)&v);
  for (size_t i = 0; i < v.size() && v.size() < 4000; ++i)
  {
    std::vector<Win> k;
    EnumChildWindows(v[i].h, EnumAll, (LPARAM)&k);
    v.insert(v.end(), k.begin(), k.end());
  }
  return v;
}
static int Pos(HWND h)
{
  SCROLLINFO si = {sizeof(si)};
  si.fMask = SIF_ALL;
  if (!GetScrollInfo(h, SB_VERT, &si)) return -1;
  return si.nPos;
}

int main()
{
  auto v = Collect();
  HWND list = nullptr;
  RECT lr = {0};
  for (auto &w : v)
  {
    if (w.cls != "SysListView32" || !IsWindowVisible(w.h)) continue;
    RECT r;
    GetWindowRect(w.h, &r);
    if (r.right - r.left < 100 || r.bottom - r.top < 100) continue;
    if (Pos(w.h) < 0) continue;
    list = w.h; lr = r; break;
  }
  if (!list) { printf("no visible list found\n"); return 1; }
  printf("list %p rect=[%d,%d,%d,%d] style=0x%08lX smooth=%d nPos=%d\n\n",
         (void *)list, lr.left, lr.top, lr.right, lr.bottom,
         (unsigned long)GetWindowLongPtrA(list, GWL_STYLE),
         (GetWindowLongPtrA(list, GWL_STYLE) & LVS_SMOOTHSCROLL) ? 1 : 0, Pos(list));

  const int cx = (lr.left + lr.right) / 2, cy = (lr.top + lr.bottom) / 2;
  printf("parking cursor at (%d,%d) over the list\n\n", cx, cy);
  SetCursorPos(cx, cy);
  Sleep(250);

  const LPARAM wp = MAKEWPARAM(0, (short)-WHEEL_DELTA);
  const LPARAM lp = MAKELPARAM(cx, cy);

  auto sendAndSample = [&](const char *tag) {
    int before = Pos(list);
    SendMessage(list, WM_MOUSEWHEEL, wp, lp);
    int after1 = Pos(list);
    // sample for a few frames to see if it keeps moving (animated)
    int prev = after1, moves = 0;
    for (int i = 0; i < 20; ++i)
    {
      Sleep(16);
      int p = Pos(list);
      if (p != prev) { ++moves; prev = p; }
    }
    int afterAll = Pos(list);
    printf("%-34s nPos %d -> %d (immediate) -> %d after 320ms; frames with movement=%d  %s\n",
           tag, before, after1, afterAll, moves,
           (afterAll == before) ? "NO MOVE"
           : (moves >= 3 ? "ANIMATED (smooth)" : "instant jump"));
  };

  sendAndSample("T1 SendMessage (cursor over)");

  // T2: enable native smooth scroll on the control and retry
  LONG st = (LONG)GetWindowLongPtrA(list, GWL_STYLE);
  if (!(st & LVS_SMOOTHSCROLL))
  {
    SetWindowLongPtrA(list, GWL_STYLE, st | LVS_SMOOTHSCROLL);
    SetWindowPos(list, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    printf("\n(enabled LVS_SMOOTHSCROLL on the control; style now 0x%08lX)\n\n",
           (unsigned long)GetWindowLongPtrA(list, GWL_STYLE));
    sendAndSample("T2 SendMessage + LVS_SMOOTHSCROLL");
    // Revert: this probe must not leave REAPER's window modified.
    SetWindowLongPtrA(list, GWL_STYLE, st);
    SetWindowPos(list, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    printf("(style reverted to 0x%08lX)\n\n",
           (unsigned long)GetWindowLongPtrA(list, GWL_STYLE));
  }
  else
  {
    printf("\n(list already has LVS_SMOOTHSCROLL)\n");
  }
  return 0;
}
