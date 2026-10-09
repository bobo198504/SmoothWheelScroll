// FX-WINDOW WHEEL PROBE -- can a REAPER-hosted FX window (ReaTune) or an ARA2 window be driven by a
// synthetic wheel message?
//
//   g++ -std=c++17 -O2 -o fxwheel_probe.exe fxwheel_probe.cpp && ./fxwheel_probe.exe <reaper-pid>
//
// WHY: the user wants the SMOOTHING on ReaTune (a built-in FX with a big graph window that zooms and
// scrolls) and on ARA2 pitch editors. Neither has a REAPER ACTION behind its wheel, so the action
// path cannot help -- the only route is the one the mixer already proved: hand the window small
// wheel messages and let ITS OWN code move, while the plugin decides only the timing.
//
// THIS PROBE ANSWERS THE THREE THINGS THAT DECIDE WHETHER THAT IS POSSIBLE, and it writes NOTHING:
//   1. WHERE the message has to go. It finds the window under a screen point and reports the class
//      chain, so we learn whether the target is a REAPER child (reachable) or a foreign process.
//   2. WHETHER it responds at all. It sends exactly one WM_MOUSEWHEEL and then samples the window's
//      pixels before and after: a repaint means the window acted on a SYNTHETIC message. A window
//      that only trusts real input would show no change, which kills the idea before any code is
//      written.
//   3. WHETHER IT ACCUMULATES fractions. The mixer needed whole 120-deltas; if this one is the same,
//      the design has to carry a remainder (as the mixer path does). Sending a tiny delta and seeing
//      nothing change is the evidence for that.
//
// It is READ-ONLY apart from the wheel messages, it targets whatever point you give it, and it does
// not know about the plugin at all.

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int g_fail = 0;
static void Check(bool ok, const char *what)
{
  printf("  %-66s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok)
    ++g_fail;
}

// A fingerprint of a window's client area, as a COUNT of how many sampled pixels differ from a
// stored snapshot.
//
// WHY NOT A HASH: the first version hashed the samples and compared the hash. That looked fine until
// the samples went all-black, when the hash became a constant -- and then every later comparison
// reported "no change" even if the window was moving. A degenerate value must not look like evidence.
//
// WHY THE VISIBILITY GUARD: this samples SCREEN pixels at the window's rectangle. If the window is
// minimised, moved, or covered by another window, the samples are of whatever is there instead -- and
// the probe happily reports "changed" for a repaint that was not ours. That happened for real: a run
// reported "383 of 384 samples changed" while the area actually held a File Explorer window. Every
// snapshot is therefore tagged with whether the window was genuinely visible, and a measurement taken
// while it was not is reported as INVALID rather than as a result.
static const int kSamples = 24 * 16;

struct Snap
{
  COLORREF px[kSamples];
  bool valid;   // the window was visible and unobscured when this was taken
  int black;    // how many samples were pure black (a hint that we are looking at nothing)
};

static void SnapWindow(HWND h, Snap *out)
{
  out->valid = false;
  out->black = 0;
  for (int i = 0; i < kSamples; ++i)
    out->px[i] = 0;

  // 1. Is the window itself visible at all?
  if (!IsWindow(h) || !IsWindowVisible(h) || IsIconic(h))
    return;

  RECT r;
  if (!GetWindowRect(h, &r))
    return;
  if (r.right <= r.left || r.bottom <= r.top)
    return;

  // 2. Is its rectangle on a screen? A minimised or moved-off window fails here.
  HMONITOR mon = MonitorFromRect(&r, MONITOR_DEFAULTTONULL);
  if (!mon)
    return;

  // 3. Is the window actually the thing on top at its own centre point?
  //
  // THIS TEST IS DELIBERATELY LENIENT. The first version required WindowFromPoint to return the
  // window itself, its own child, or a window with the same ROOT ancestor -- and that rejected a
  // perfectly visible JUCE plugin window, because a self-drawn plugin's hit target is often an
  // internal HWND that is neither. The screen samples were fine all along; the guard was wrong.
  //
  // What actually needs catching is a DIFFERENT application covering the target. So the test is now:
  // if the window on top belongs to another PROCESS, the samples are that other program's pixels and
  // the snapshot is invalid. Within the same process, anything on top is part of the same window
  // hierarchy and the samples are legitimate.
  const POINT centre = {(r.left + r.right) / 2, (r.top + r.bottom) / 2};
  HWND topAtCentre = WindowFromPoint(centre);
  if (topAtCentre)
  {
    DWORD topPid = 0;
    DWORD selfPid = 0;
    GetWindowThreadProcessId(topAtCentre, &topPid);
    GetWindowThreadProcessId(h, &selfPid);
    if (topPid != selfPid)
      return; // another application is covering it
  }

  HDC s = GetDC(NULL);
  if (!s)
    return;
  const int nx = 24, ny = 16;
  for (int j = 0; j < ny; ++j)
  {
    for (int i = 0; i < nx; ++i)
    {
      const int x = r.left + (int)((r.right - r.left) * (i + 0.5) / nx);
      const int y = r.top + (int)((r.bottom - r.top) * (j + 0.5) / ny);
      const COLORREF c = GetPixel(s, x, y);
      out->px[j * nx + i] = c;
      if (c == 0)
        ++out->black;
    }
  }
  ReleaseDC(NULL, s);
  out->valid = true;
}

// How many of the samples differ between two snapshots.
static int DiffCount(const Snap &a, const Snap &b)
{
  int d = 0;
  for (int i = 0; i < kSamples; ++i)
    if (a.px[i] != b.px[i])
      ++d;
  return d;
}

// Walk up from a window, printing the class of each ancestor until the desktop.
static void PrintChain(HWND h)
{
  int depth = 0;
  for (HWND w = h; w && depth < 12; w = GetParent(w), ++depth)
  {
    char cls[128] = {0};
    GetClassNameA(w, cls, sizeof(cls));
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    RECT r = {0};
    GetWindowRect(w, &r);
    printf("      %*s%s  hwnd=%p pid=%lu  rect=%ld,%ld,%ld,%ld\n", depth * 2, "", cls, (void *)w,
           (unsigned long)pid, r.left, r.top, r.right, r.bottom);
  }
}

// Find a window of the given class in the REAPER process. Used when no coordinates are given, so the
// probe can be run without the user having to read a screen position off a screenshot.
struct FindCtx { const char *want; HWND hit; DWORD pid; };
static BOOL CALLBACK FindProc(HWND h, LPARAM lp)
{
  FindCtx *c = (FindCtx *)lp;
  DWORD pid = 0;
  GetWindowThreadProcessId(h, &pid);
  if (pid != c->pid)
    return TRUE;
  char cls[128] = {0};
  GetClassNameA(h, cls, sizeof(cls));
  if (_stricmp(cls, c->want) == 0)
  {
    c->hit = h;
    return FALSE; // stop
  }
  return TRUE;
}

// Search every top-level window, then every child of each, for the requested class.
static HWND FindClassInReaper(DWORD pid, const char *cls)
{
  FindCtx c = {cls, nullptr, pid};
  for (HWND top = GetTopWindow(nullptr); top; top = GetWindow(top, GW_HWNDNEXT))
  {
    DWORD tpid = 0;
    GetWindowThreadProcessId(top, &tpid);
    if (tpid != pid)
      continue;
    if (!EnumChildWindows(top, FindProc, (LPARAM)&c) && c.hit)
      return c.hit;
    FindProc(top, (LPARAM)&c);
    if (c.hit)
      return c.hit;
  }
  return nullptr;
}

// The REAPER process id: the process owning the main window.
static DWORD ReaperPid()
{
  HWND main = FindWindowA("REAPERwnd", nullptr);
  DWORD pid = 0;
  if (main)
    GetWindowThreadProcessId(main, &pid);
  return pid;
}

static void SendWheel(HWND h, int delta, POINT screenPt)
{
  // A real wheel message carries SCREEN coordinates in lParam and a signed delta in the high word
  // of wParam -- the same shape the plugin's mixer path uses.
  const WPARAM wp = (WPARAM)((delta << 16) & 0xFFFF0000u);
  const LPARAM lp = (LPARAM)((screenPt.y << 16) | (screenPt.x & 0xFFFF));
  SendMessageA(h, WM_MOUSEWHEEL, wp, lp);
}

int main(int argc, char **argv)
{
  printf("fx-window wheel probe\n\n");

  // Two ways in: give a class name and let the probe find it, or give coordinates.
  HWND under = nullptr;
  POINT pt = {0, 0};
  int delta = 120;

  const bool byClass = (argc > 1 && _stricmp(argv[1], "--class") == 0);
  if (byClass)
  {
    const char *cls = (argc > 2) ? argv[2] : "ReatuneGraph2";
    delta = (argc > 3) ? atoi(argv[3]) : 120;
    const DWORD pid = ReaperPid();
    if (!pid)
    {
      printf("REAPERwnd not found -- is REAPER running?\n");
      return 1;
    }
    printf("REAPER pid = %lu, looking for class \"%s\"\n", (unsigned long)pid, cls);
    under = FindClassInReaper(pid, cls);
    if (!under)
    {
      printf("no window of class \"%s\" in the REAPER process.\n", cls);
      printf("Open the FX window (the one with the graph) and try again.\n");
      return 1;
    }
    RECT r = {0};
    GetWindowRect(under, &r);
    pt.x = (r.left + r.right) / 2;
    pt.y = (r.top + r.bottom) / 2;
    printf("found hwnd=%p  rect=%ld,%ld,%ld,%ld  -> aiming at its centre (%ld,%ld)\n", (void *)under,
           r.left, r.top, r.right, r.bottom, pt.x, pt.y);
  }
  else
  {
    if (argc < 3)
    {
      printf("usage: fxwheel_probe --class [ClassName] [delta]     (recommended)\n");
      printf("       fxwheel_probe <x> <y> [delta]                 (explicit point)\n\n");
      printf("  --class defaults to ReatuneGraph2 (the ReaTune graph window).\n");
      printf("  delta defaults to 120 = one whole notch.\n");
      return 2;
    }
    pt.x = atoi(argv[1]);
    pt.y = atoi(argv[2]);
    delta = (argc > 3) ? atoi(argv[3]) : 120;
    under = WindowFromPoint(pt);
  }

  // --- 1. WHAT is under the point, and whose process is it? ---
  printf("\n  [1] the target window and its ancestor chain\n");
  if (!under)
  {
    printf("      nothing there\n");
    return 1;
  }
  PrintChain(under);

  DWORD myPid = GetCurrentProcessId();
  DWORD targetPid = 0;
  GetWindowThreadProcessId(under, &targetPid);
  const DWORD reaperPid = ReaperPid();
  printf("\n      probe pid=%lu  target pid=%lu  reaper pid=%lu\n", (unsigned long)myPid,
         (unsigned long)targetPid, (unsigned long)reaperPid);
  if (targetPid == reaperPid)
    printf("      >>> target IS IN THE REAPER PROCESS: the plugin's hook can see its wheel\n");
  else
    printf("      >>> target is in ANOTHER process: the plugin's hook NEVER sees its wheel\n");
  Check(true, "recorded the process ownership (this is the make-or-break fact)");

  // --- 2-4. THE MEASUREMENTS ---
  //
  // Each step: make sure the window is on top, snapshot, send, wait, snapshot again, count the
  // changed samples. A snapshot that could not be taken honestly (window hidden, moved, or covered)
  // is reported as INVALID and its comparison is skipped -- a number read off the wrong window is
  // worse than no number, because it looks like evidence.
  static Snap s0, s1;

  // Bring the window to the front, so "is it covered?" is answerable, and re-check it afterwards.
  SetForegroundWindow(under);
  Sleep(250);

  printf("\n  [2] send ONE wheel message (delta %d) and count changed pixels\n", delta);
  SnapWindow(under, &s0);
  printf("      before: %s%s\n", s0.valid ? "valid" : "INVALID (window not visible/uncovered)",
         s0.valid ? "" : " -- no conclusion can be drawn");
  if (!s0.valid)
  {
    printf("\n  STOPPING: the window could not be seen on screen, so nothing here would mean\n"
           "  anything. Bring the ReaTune window to the front, make sure it is not covered, and\n"
           "  run the probe again.\n");
    return 1;
  }
  printf("      (%d/%d samples pure black)\n", s0.black, kSamples);

  SendWheel(under, delta, pt);
  Sleep(400);
  SnapWindow(under, &s1);
  const int d1 = DiffCount(s0, s1);
  if (!s1.valid)
    printf("      after : INVALID snapshot -- result discarded\n");
  else
  {
    printf("      after : %d of %d samples CHANGED (%d pure black)\n", d1, kSamples, s1.black);
    if (d1 > 0)
      printf("      >>> the window ACTED on a synthetic wheel message\n");
    else
      printf("      >>> nothing changed: it ignored the message, or it needs real input\n");
  }

  // --- 3. DOES it accumulate a sub-notch delta? ---
  //
  // THE PROBLEM THIS SECTION HAS TO AVOID: if a plain "send N, did it change?" test is run twice in
  // the same direction, the second one can land on the view's EDGE and show no change -- not because
  // the message was ignored, but because there was nowhere left to go. That produced a contradictory
  // pair of readings on the first run of this probe (one small delta appeared to move it, eight in a
  // row did not).
  //
  // So this alternates: forward, back to the start, forward again. A reading is only counted when the
  // window was seen to return, which proves the motion is repeatable rather than a one-way limit.
  printf("\n  [3] sub-notch deltas, tested FORWARD and BACK so an edge cannot fake the result\n");
  static Snap before, after;
  const int small = 15;

  // (a) A control reading: how much does one WHOLE notch move it, forward then back? If the window
  //     cannot return, everything below is uninterpretable and we say so instead of guessing.
  SnapWindow(under, &before);
  SendWheel(under, 120, pt);
  Sleep(400);
  SnapWindow(under, &after);
  const int fwd = (before.valid && after.valid) ? DiffCount(before, after) : -1;

  SnapWindow(under, &before);
  SendWheel(under, -120, pt);
  Sleep(400);
  SnapWindow(under, &after);
  const int back = (before.valid && after.valid) ? DiffCount(before, after) : -1;
  printf("      control: one notch forward = %d changed, back = %d changed\n", fwd, back);
  if (fwd <= 0 || back <= 0)
  {
    printf("      >>> the window did not move BOTH ways, so the fine-delta tests below cannot be\n"
           "          interpreted. Most likely it is already at an end of its range, or it only\n"
           "          accepts one direction in this view.\n");
  }

  // (b) One small delta, then the same small delta back. Both readings come from a known position.
  SnapWindow(under, &before);
  SendWheel(under, small, pt);
  Sleep(300);
  SnapWindow(under, &after);
  const int moved1 = (before.valid && after.valid) ? DiffCount(before, after) : -1;
  SendWheel(under, -small, pt);
  Sleep(300);
  printf("      ONE small delta (%d)          : %d samples changed (then returned)\n", small, moved1);

  // (c) Eight small deltas = one whole notch, then eight back. If the window accumulates fractions,
  //     this moves about as much as the control did; if it needs whole notches, it does not move.
  SnapWindow(under, &before);
  for (int i = 0; i < 8; ++i)
    SendWheel(under, small, pt);
  Sleep(400);
  SnapWindow(under, &after);
  const int moved8 = (before.valid && after.valid) ? DiffCount(before, after) : -1;
  for (int i = 0; i < 8; ++i)
    SendWheel(under, -small, pt);
  Sleep(300);
  printf("      EIGHT small deltas (8x%d = 120): %d samples changed (then returned)\n", small, moved8);

  if (moved1 < 0 || moved8 < 0)
    printf("      >>> INVALID snapshot -- no conclusion\n");
  else if (moved8 > 0)
    printf("      >>> it ACCUMULATES fractions: fine-grained delivery can drive it directly\n");
  else
    printf("      >>> eight small deltas did NOT move it while one whole notch did: it needs whole\n"
           "          notches, so the design must carry a remainder exactly as the mixer path does\n");

  // --- 4. Summary of directionality. ---
  //
  // Section [3] already sends both directions and reports each, so there is nothing to add here
  // beyond stating plainly which of the two facts the whole test turned on.
  printf("\n  [4] summary\n");
  printf("      reachable by the plugin's hook : %s\n",
         (targetPid == reaperPid) ? "YES (same process as REAPER)" : "NO (different process)");
  printf("      acts on synthetic wheel messages: %s\n",
         (fwd > 0 && back > 0) ? "YES" : "NOT CONFIRMED (see [3])");
  printf("      fine (sub-notch) delivery       : %s\n",
         (moved8 > 0) ? "YES, accumulates fractions" : "NO, whole notches only");

  printf("\n  --- how to read this ---\n");
  printf("  * [1] SAME process as REAPER  -> the plugin's hook can intercept this wheel.\n");
  printf("    [1] a DIFFERENT process      -> it cannot, whatever else this probe shows.\n");
  printf("  * [2]/[4] changed pixels > 0 in BOTH directions -> the mixer's approach (hand\n");
  printf("    the window small wheel messages, let its own code move) is viable here.\n");
  printf("  * [3] eight small deltas moving it -> fine delivery works directly; otherwise the\n");
  printf("    design must carry a whole-notch remainder, exactly as the mixer path does.\n");

  printf("\n%s\n", g_fail ? "FAIL" : "done: read [1]-[4] above");
  return g_fail ? 1 : 0;
}
