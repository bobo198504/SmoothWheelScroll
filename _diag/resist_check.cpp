// VERIFY the resistance model: one accumulated total, a fixed take per time, easing pays the rest.
//
//   g++ -std=c++17 -O2 -I../src -o resist_check.cpp resist_check.cpp
//
// The question this must answer: does "turn faster -> less is taken" really come out of the
// arithmetic, and what does each setting feel like at a slow turn and at a fast roll?

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace anim3;
static const double kDeltasPerUnit = 8.0; // one notch = 120 deltas = 15 units

// Feed n notches, one every gapMs, and tick every 1 ms. Returns deltas handed over.
static double roll(int n, double gapMs, double easeMs, double resist)
{
  Params P;
  P.durationMs = easeMs;
  P.takePerSec = (resist * 100.0) / kDeltasPerUnit; // deltas/10ms -> units/sec
  P.snap = (1.0 / kDeltasPerUnit) * 0.5;
  Glide g; g.Reset();
  const double dt = 0.001;
  const double gap = gapMs / 1000.0;
  double msgTime = 0, out = 0;
  int fed = 0;
  for (double t = 0; t < n * gap + 2.0; t += dt) {
    while (fed < n && msgTime <= t + 1e-9) {
      g.Feed(15.0);            // one notch = 15 units
      ++fed; msgTime += gap;
    }
    out += g.Tick(dt, P);
    if (fed >= n && !g.Active()) break;
  }
  return out * kDeltasPerUnit;
}

int main()
{
  printf("one notch = 120 deltas. 'in' is what the wheel sent, 'out' what reached the action.\n\n");

  printf("=== A. ONE notch on its own (the pure slow turn), ease 10 ms ===\n");
  printf("  %-10s %-12s %-12s %s\n", "resistance", "in", "out", "taken");
  const double r[] = {1, 12, 60, 110};
  for (int i = 0; i < 4; ++i) {
    const double o = roll(1, 2000.0, 10.0, r[i]);
    printf("  %-10.0f %-12.0f %-12.1f %.1f%%\n", r[i], 120.0, o, 100 * (1 - o / 120));
  }

  printf("\n=== B. a steady roll at several speeds, resistance 1 (the default) ===\n");
  printf("  %-14s %-12s %-12s %s\n", "gap per notch", "in", "out", "taken");
  const double gaps[] = {500, 300, 200, 150, 100, 50, 20};
  for (int i = 0; i < 7; ++i) {
    const int n = 10;
    const double o = roll(n, gaps[i], 10.0, 1.0);
    printf("  %-14.0f %-12.0f %-12.1f %.1f%%\n", gaps[i], 120.0 * n, o,
           100 * (1 - o / (120.0 * n)));
  }

  printf("\n=== C. the same sweep at resistance 12 ===\n");
  for (int i = 0; i < 7; ++i) {
    const int n = 10;
    const double o = roll(n, gaps[i], 10.0, 12.0);
    printf("  %-14.0f %-12.0f %-12.1f %.1f%%\n", gaps[i], 120.0 * n, o,
           100 * (1 - o / (120.0 * n)));
  }

  printf("\n=== D. does 'faster = less taken' hold? (resistance 12, taken %) ===\n");
  for (int i = 0; i < 7; ++i) {
    const int n = 10;
    const double o = roll(n, gaps[i], 10.0, 12.0);
    printf("  %.0f ms/notch -> taken %.1f%%%s\n", gaps[i], 100 * (1 - o / (120.0 * n)),
           gaps[i] <= 50 ? "   <- fast: should be small" : "");
  }
  return 0;
}
