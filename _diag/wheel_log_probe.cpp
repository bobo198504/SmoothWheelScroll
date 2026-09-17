// The DEV wheel log's ring: does it read in the right ORDER, and does the file say what it should?
//
// The whole value of the log is that a tester can send it back and the messages can be read oldest
// to newest. Two things can silently ruin that, and neither is visible by eye in a text file:
//
//   1. the ring WRAPS -- once it holds kMax messages it overwrites the oldest, and the natural
//      array order then starts in the middle. If the accessor does not account for that, the report
//      reads backwards or scrambled, and a tester's round trip is wasted;
//   2. the GAP between messages is what a free-spinning wheel is judged on, so it must be measured
//      from the previous message, not left at zero.
//
// It includes the REAL src/wheel_log.h, not a copy of it, so it tests the shipping logic (the
// project's rule; see routing.h / device.h).
//
// g++ -std=c++17 -O2 -I. _diag/wheel_log_probe.cpp -o /tmp/wlp && /tmp/wlp
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include "../src/wheel_log.h"

// The probe writes its file next to itself (the current directory), not to a POSIX path: this is
// run on Windows, where "/tmp/..." is not a directory it can create. The earlier version used a
// hard-coded /tmp path and the "read the file back" checks silently saw an empty string.
static const char *kOutPath = "wl_probe_out.txt";
static const char *kTmpPath = "wl_probe_out.tmp";

static int failures = 0;
static void Check(bool ok, const char *what)
{
  printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok)
    ++failures;
}

int main()
{
  printf("wheel log ring: order, gaps, wrap, tally, file text\n\n");

  // --- order and gaps, below the wrap point ---
  {
    WheelLog lg;
    lg.Init(kOutPath, kTmpPath);
    for (int i = 0; i < 5; ++i)
    {
      WheelLogRec r;
      ZeroMemory(&r, sizeof(r));
      r.delta = 120;
      r.dev = 1; // notched
      lg.Record(r, (double)i * 0.1); // 100 ms apart
    }
    Check(lg.Count() == 5, "holds 5 after 5 records");
    bool ordered = true, gapsOk = true;
    for (int i = 0; i < lg.Count(); ++i)
    {
      if (lg.At(i).delta != 120)
        ordered = false;
      const double want = (i == 0) ? 0.0 : 100.0;
      if (fabs(lg.At(i).gapMs - want) > 0.01)
        gapsOk = false;
    }
    Check(ordered, "At(i) is oldest..newest");
    Check(gapsOk, "gap measured from the previous message (first = 0)");
  }

  // --- THE WRAP: fill past kMax and confirm the window slides, oldest first ---
  {
    WheelLog lg;
    lg.Init(kOutPath, kTmpPath);
    const int total = WheelLog::kMax + 12; // 12 more than it holds
    for (int i = 0; i < total; ++i)
    {
      WheelLogRec r;
      ZeroMemory(&r, sizeof(r));
      r.delta = i + 1; // 1..total, so the identity of each record is its delta
      r.dev = 1;
      lg.Record(r, (double)i * 0.01);
    }
    Check(lg.Count() == WheelLog::kMax, "never holds more than kMax");
    // The OLDEST held must be total-kMax+1 (the first 12 were overwritten).
    const int wantOldest = total - WheelLog::kMax + 1;
    Check(lg.At(0).delta == wantOldest, "after wrap, At(0) is the oldest SURVIVOR");
    Check(lg.At(lg.Count() - 1).delta == total, "after wrap, the last is the newest");
    bool monotone = true;
    for (int i = 1; i < lg.Count(); ++i)
      if (lg.At(i).delta <= lg.At(i - 1).delta)
        monotone = false;
    Check(monotone, "after wrap, still strictly oldest -> newest");
  }

  // --- the per-verdict tally counts distinct magnitudes ---
  {
    WheelLog lg;
    lg.Init(kOutPath, kTmpPath);
    for (int i = 0; i < 6; ++i)
    {
      WheelLogRec r;
      ZeroMemory(&r, sizeof(r));
      r.delta = (i < 4) ? 15 : 30; // a free-spinner with two detents
      r.dev = 2;                   // kFreeSpin
      lg.Record(r, (double)i * 0.02);
    }
    // Write the file and read it back: the tally line must name both magnitudes and their counts.
    lg.Flush("# probe\n");
    FILE *f = fopen(kOutPath, "r");
    char buf[4096] = {0};
    if (f)
    {
      size_t n = fread(buf, 1, sizeof(buf) - 1, f);
      buf[n] = 0;
      fclose(f);
    }
    Check(strstr(buf, "free-spin") != nullptr, "tally names the verdict");
    Check(strstr(buf, "15 x4") != nullptr, "tally counts 15 x4");
    Check(strstr(buf, "30 x2") != nullptr, "tally counts 30 x2");
    Check(strstr(buf, "extra-word  anim  device") != nullptr, "file has the column header");
    Check(strstr(buf, "most recent 6 messages") != nullptr, "file states how many it holds");
  }

  // --- the gesture-end signal that decides when to write ---
  {
    WheelLog lg;
    lg.Init(kOutPath, kTmpPath);
    WheelLogRec r;
    ZeroMemory(&r, sizeof(r));
    r.delta = 120;
    r.dev = 1;
    Check(!lg.Record(r, 0.00), "first message: no gesture end");
    Check(!lg.Record(r, 0.05), "50 ms later: no gesture end");
    Check(lg.Record(r, 1.00), "1 s later: gesture end (flush cue)");
  }

  // --- a too-long window class cannot overflow the field ---
  {
    WheelLog lg;
    lg.Init(kOutPath, kTmpPath);
    WheelLogRec r;
    ZeroMemory(&r, sizeof(r));
    r.delta = 120;
    r.dev = 1;
    for (int i = 0; i < (int)sizeof(r.wincls) - 1; ++i)
      r.wincls[i] = 'X';
    lg.Record(r, 0.0);
    Check(lg.At(0).wincls[sizeof(r.wincls) - 1] == 0, "window class stays NUL-terminated");
  }

  // --- writability: a good folder is Ready, a bad one says so instead of silently failing ---
  {
    WheelLog good;
    good.Init(kOutPath, kTmpPath);
    Check(good.Ready(), "a writable folder -> Ready()");
    Check(!good.PathKnownButReadOnly(), "a writable folder -> not reported read-only");

    WheelLog bad;
    bad.Init("Z:\\definitely\\not\\here\\wl.txt", "Z:\\definitely\\not\\here\\wl.tmp");
    Check(!bad.Ready(), "an unwritable path -> not Ready()");
    Check(bad.PathKnownButReadOnly(), "an unwritable path -> reported read-only");
  }

  printf("\n%s\n", failures ? "FAIL" : "OK: ring order, wrap, gaps, tally and file text all hold");
  return failures ? 1 : 0;
}
