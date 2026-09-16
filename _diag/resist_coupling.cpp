// Does the resistance effect depend on the EASING? Measure taken% at several easings.
//
//   g++ -std=c++17 -O2 -I../src -o resist_coupling.cpp resist_coupling.cpp

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace anim3;
static const double kDPU = 8.0;

static double roll(int n, double gapMs, double easeMs, double resist)
{
  Params P;
  P.durationMs = easeMs;
  P.takePerSec = (resist * 200.0) / kDPU;
  P.snap = (1.0 / kDPU) * 0.5;
  Glide g; g.Reset();
  const double dt = 0.001, gap = gapMs / 1000.0;
  double msgTime = 0, out = 0;
  int fed = 0;
  for (double t = 0; t < n * gap + 4.0; t += dt) {
    while (fed < n && msgTime <= t + 1e-9) { g.Feed(15.0); ++fed; msgTime += gap; }
    out += g.Tick(dt, P);
    if (fed >= n && !g.Active()) break;
  }
  return out * kDPU;
}

int main()
{
  printf("TAKEN %% of one notch (ease = how long the pool takes to drain).\n");
  printf("a fixed take-per-time can only eat while there IS a pool, so the easing sets its chance.\n\n");
  printf("  %-10s", "resistance");
  const double eases[] = {10, 20, 50, 100, 200, 300};
  for (int i = 0; i < 6; ++i) printf(" %7.0fms", eases[i]);
  printf("\n");
  const double rs[] = {1, 12, 60, 110};
  for (int r = 0; r < 4; ++r) {
    printf("  %-10.0f", rs[r]);
    for (int e = 0; e < 6; ++e) {
      const double o = roll(1, 2000.0, eases[e], rs[r]);
      printf(" %7.1f%%", 100 * (1 - o / 120.0));
    }
    printf("   <- one isolated notch\n");
  }

  printf("\n=== and does 'faster = less taken' hold, at ease 300? ===\n");
  printf("  %-14s %s\n", "gap/notch", "taken% (resistance 12)");
  const double gaps[] = {400, 300, 200, 150, 100, 50, 20};
  for (int i = 0; i < 7; ++i) {
    const int n = 10;
    const double o = roll(n, gaps[i], 300.0, 12.0);
    printf("  %-14.0f %.1f%%\n", gaps[i], 100 * (1 - o / (120.0 * n)));
  }
  printf("\n=== the same at ease 20 ===\n");
  for (int i = 0; i < 7; ++i) {
    const int n = 10;
    const double o = roll(n, gaps[i], 20.0, 12.0);
    printf("  %-14.0f %.1f%%\n", gaps[i], 100 * (1 - o / (120.0 * n)));
  }
  return 0;
}
