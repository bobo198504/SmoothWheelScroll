// listprobe -- measure HOW a REAPER list window scrolls, so the drive method is
// chosen from data rather than guessed.
//
// For every visible list control it reports:
//   - the window class and style bits (LVS_SMOOTHSCROLL / LVS_OWNERDATA / ...)
//   - its scroll state (nMin/nMax/nPage/nPos) => is it item-based or pixel-based?
//   - whether each of three scroll methods actually moves it:
//       M1 WM_MOUSEWHEEL                (wheel replay)
//       M2 WM_VSCROLL SB_LINE{UP,DOWN}  (line stepping)
//       M3 SetScrollPos + SB_THUMBPOSITION (absolute position)
//   - whether the move was immediate or animated (sampled over time)
#include <windows.h>
#include <commctrl.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef LVS_SMOOTHSCROLL
#define LVS_SMOOTHSCROLL 0x00000020L
#endif
#ifndef LVS_OWNERDATA
#define LVS_OWNERDATA 0x00001000L
#endif

struct Win { HWND h; std::string cls; std::string title; };

static BOOL CALLBACK EnumAll(HWND h, LPARAM lp)
{
  auto *v = (std::vector<Win> *)lp;
  char cls[128] = {0}, t[256] = {0};
  GetClassNameA(h, cls, sizeof(cls));
  GetWindowTextA(h, t, sizeof(t));
  v->push_back(Win{h, cls, t});
  return TRUE;
}

static std::vector<Win> Collect()
{
  std::vector<Win> v;
  EnumWindows(EnumAll, (LPARAM)&v);
  for (size_t i = 0; i < v.size() && v.size() < 4000; ++i)
  {
    std::vector<Win> kids;
    EnumChildWindows(v[i].h, EnumAll, (LPARAM)&kids);
    v.insert(v.end(), kids.begin(), kids.end());
  }
  return v;
}

static bool Listish(const std::string &c)
{
  return c == "SysListView32" || c == "SysTreeView32";
}

static long Pos(HWND h)
{
  SCROLLINFO si = {sizeof(si)};
  si.fMask = SIF_ALL;
  if (!GetScrollInfo(h, SB_VERT, &si)) return -1;
  return si.nPos;
}

static void Info(HWND h, const char *tag)
{
  SCROLLINFO si = {sizeof(si)};
  si.fMask = SIF_ALL;
  BOOL ok = GetScrollInfo(h, SB_VERT, &si);
  printf("    %-22s %s nMin=%d nMax=%d nPage=%d nPos=%d\n", tag,
         ok ? "" : "(no scrollinfo)", ok ? (int)si.nMin : 0, ok ? (int)si.nMax : 0,
         ok ? (int)si.nPage : 0, ok ? (int)si.nPos : 0);
}

int main(int argc, char **argv)
{
  const bool doTest = argc > 1 && !strcmp(argv[1], "test");
  auto v = Collect();
  printf("== visible list controls ==\n");
  int idx = 0;
  for (auto &w : v)
  {
    if (!Listish(w.cls) || !IsWindowVisible(w.h))
      continue;
    RECT r;
    GetWindowRect(w.h, &r);
    if (r.right - r.left < 40 || r.bottom - r.top < 40)
      continue;
    const LONG st = (LONG)GetWindowLongPtrA(w.h, GWL_STYLE);
    const LONG ex = (LONG)GetWindowLongPtrA(w.h, GWL_EXSTYLE);
    printf("\n[%d] %p %s  rect=[%d,%d,%d,%d]  style=0x%08lX%s%s  title=\"%s\"\n", idx++,
           (void *)w.h, w.cls.c_str(), r.left, r.top, r.right, r.bottom, st,
           (st & LVS_SMOOTHSCROLL) ? " SMOOTHSCROLL" : "",
           (st & LVS_OWNERDATA) ? " OWNERDATA" : "", w.title.c_str());
    Info(w.h, "before");
    if (!doTest) continue;

    const int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    const long p0 = Pos(w.h);

    // M1: wheel replay
    SendMessage(w.h, WM_MOUSEWHEEL, MAKEWPARAM(0, (short)-WHEEL_DELTA),
                MAKELPARAM(cx, cy));
    Sleep(120);
    const long p1 = Pos(w.h);
    printf("    M1 wheel replay        -> nPos %ld -> %ld  %s\n", p0, p1,
           p1 != p0 ? "MOVED" : "no move");

    // M2: line step
    SendMessage(w.h, WM_VSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), 0);
    Sleep(120);
    const long p2 = Pos(w.h);
    printf("    M2 SB_LINEDOWN         -> nPos %ld -> %ld  %s\n", p1, p2,
           p2 != p1 ? "MOVED" : "no move");

    // M3: absolute position
    SCROLLINFO si = {sizeof(si)};
    si.fMask = SIF_ALL;
    GetScrollInfo(w.h, SB_VERT, &si);
    const int tgt = (int)si.nPos + (si.nPage ? (int)si.nPage / 2 : 1);
    SCROLLINFO s2 = {sizeof(s2)};
    s2.fMask = SIF_POS;
    s2.nPos = tgt;
    SetScrollInfo(w.h, SB_VERT, &s2, TRUE);
    SendMessage(w.h, WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, tgt), 0);
    Sleep(120);
    const long p3 = Pos(w.h);
    printf("    M3 SB_THUMBPOSITION(%d) -> nPos %ld -> %ld  %s\n", tgt, p2, p3,
           p3 != p2 ? "MOVED" : "no move");

    // put it back
    if (p0 >= 0)
    {
      SCROLLINFO s3 = {sizeof(s3)};
      s3.fMask = SIF_POS;
      s3.nPos = (int)p0;
      SetScrollInfo(w.h, SB_VERT, &s3, TRUE);
      SendMessage(w.h, WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, (int)p0), 0);
    }
  }
  printf("\n(done)\n");
  return 0;
}
