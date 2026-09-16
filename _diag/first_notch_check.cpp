// VERIFY: a steady slow roll must not start with one tiny notch and then jump.
//
//   g++ -std=c++17 -O2 -I../src -o first_notch_check.cpp first_notch_check.cpp
//
// The gap is now the REAL time since the axis last saw a wheel message, across operations. This
// simulates: an idle period, then a steady roll at a fixed interval, and prints the travel of
// every notch. If the first notch is treated differently from the rest, it shows up immediately.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>

using namespace anim3;
static const double D = 120.0;

// `idleMs` before the first notch, then `gapMs` between notches.
static void roll(const char *name, double idleMs, double gapMs, int n, double slowMove)
{
  printf("  %-34s", name);
  double last = -1.0;     // -1 = nothing seen yet
  double t = idleMs / 1000.0;
  for (int i = 0; i < n; ++i) {
    const double gap = (last < 0.0) ? -1.0 : (t - last);
    const double moved = D * TravelFraction(gap, D, slowMove);
    printf(" %6.1f", moved);
    last = t; t += gapMs / 1000.0;
  }
  printf("\n");
}

int main()
{
  printf("travel per notch (deltas), one row = one gesture. slow end %.0f ms, fast end %.0f ms.\n\n",
         kSlowGapSec * 1000, kFastGapSec * 1000);

  printf("=== slow-move = 1 (the default) ===\n");
  printf("  %-34s %s\n", "case", "1st  2nd  3rd  4th  5th  6th");
  roll("slow roll 300ms, after 1s idle", 1000, 300, 6, 1.0);
  roll("slow roll 200ms, after 1s idle", 1000, 200, 6, 1.0);
  roll("medium roll 150ms, after 1s idle", 1000, 150, 6, 1.0);
  roll("fast roll 50ms, after 1s idle", 1000, 50, 6, 1.0);
  roll("fast roll 50ms, no idle before", 0, 50, 6, 1.0);

  printf("\n=== slow-move = 12 (the earlier tuned value) ===\n");
  roll("slow roll 300ms, after 1s idle", 1000, 300, 6, 12.0);
  roll("medium roll 150ms, after 1s idle", 1000, 150, 6, 12.0);
  roll("fast roll 50ms, after 1s idle", 1000, 50, 6, 12.0);

  printf("\n=== is the FIRST notch now the same as the rest? (a steady roll) ===\n");
  {
    int bad = 0;
    const double gaps[] = {300, 250, 200, 150, 100, 75, 50};
    for (int i = 0; i < 7; ++i) {
      double last = -1, t = 0.0, first = 0, second = 0;
      for (int k = 0; k < 2; ++k) {
        const double gap = (last < 0.0) ? -1.0 : (t - last);
        const double m = D * TravelFraction(gap, D, 1.0);
        if (k == 0) first = m; else second = m;
        last = t; t += gaps[i] / 1000.0;
      }
      const bool ok = std::fabs(first - second) < 1e-9;
      if (!ok) { ++bad; printf("  gap %.0f ms: first %.1f, second %.1f  <- STILL DIFFERENT\n",
                               gaps[i], first, second); }
    }
    printf("  %s\n", bad ? "FAILED" : "OK: with a real idle time before it, the first notch of a "
                                      "steady roll moves the same as the rest");
  }

  printf("\n=== and a lone deliberate notch (long idle) is still the slow move ===\n");
  printf("  1 notch after 2 s idle, slow-move 1: %.3f delta\n",
         D * TravelFraction(-1.0, D, 1.0));
  printf("  1 notch after 2 s idle, real gap 2 s: %.3f delta\n",
         D * TravelFraction(2.0, D, 1.0));
  return 0;
}
