// KINETIC ENERGY, in raw deltas. The number is "how much of a slow turn survives".
//
//   g++ -std=c++17 -O2 -I../src -o kinetic_check.cpp kinetic_check.cpp
//
// Checks:
//   1. the gap -> keep mapping at a few settings, INCLUDING the one that must reproduce today's
//      favourite feel (friction 90%  ==  kinetic 12);
//   2. one isolated slow notch: how far it moves, at each setting;
//   3. the floor (1) still moves, and the ceiling (120) eats nothing;
//   4. a fast roll keeps nearly all, whatever the setting.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace anim3;
static const double kU = 15.0;      // units per notch
static const double kDeltasPerUnit = 8.0;

static double roll(int n, double gapMs, double Dms, double kinetic) {
  Params P; P.durationMs = Dms;
  Glide g; g.Reset();
  const double dt = 0.001, gap = gapMs / 1000.0;
  double out = 0, msgTime = 0;
  int fed = 0;
  for (double t = 0; t < n * gap + Dms / 1000.0 + 0.05; t += dt) {
    while (fed < n && msgTime <= t + 1e-9) {
      g.Feed(1.0 * KeepFraction(gap, kinetic), P);
      ++fed; msgTime += gap;
    }
    out += g.Tick(dt, P) * kU;
  }
  return out;
}

int main() {
  printf("one notch = 120 deltas = 15 units.  kinetic is in deltas (1..120).\n");
  printf("today's favourite feel was friction 90%% = keep 12 deltas.\n\n");

  printf("=== 1. gap -> keep, for a few settings ===\n");
  printf("  %-8s", "gap ms");
  for (double k : {1.0, 12.0, 60.0, 120.0}) printf(" %10s", k == 1.0 ? "kin 1" : k == 12.0 ? "kin 12*" : k == 60.0 ? "kin 60" : "kin 120");
  printf("\n");
  for (double gapMs : {0, 10, 20, 40, 60, 100, 150, 200, 400}) {
    printf("  %-8.0f", gapMs);
    for (double k : {1.0, 12.0, 60.0, 120.0}) {
      const double kept = KeepFraction(gapMs / 1000.0, k);
      printf(" %9.1f%%", 100 * kept);
    }
    printf("   (fraction of the message that survives)\n");
  }

  printf("\n=== 2. ONE isolated slow notch (gap 400 ms), D = 300 ms ===\n");
  for (double k : {1.0, 6.0, 12.0, 30.0, 60.0, 120.0}) {
    const double u = roll(1, 400.0, 300.0, k);
    printf("  kinetic %3.0f -> moves %6.3f units = %6.1f deltas = %5.1f%% of a notch%s\n",
           k, u, u * kDeltasPerUnit, 100 * u / kU, k == 12.0 ? "   <-- today's favourite" : "");
  }

  printf("\n=== 3. floor and ceiling ===\n");
  for (double k : {1.0, 120.0}) {
    const double u1 = roll(1, 400.0, 300.0, k);
    const double u10 = roll(10, 250.0, 300.0, k);
    printf("  kinetic %3.0f: slow single notch %.3f units (%.1f delta) | 10 slow notches %.2f units"
           " (was 150.0 with nothing eaten)\n", k, u1, u1 * kDeltasPerUnit, u10);
  }

  printf("\n=== 4. FAST roll (10 ms apart) keeps nearly all, at every setting ===\n");
  for (double k : {1.0, 12.0, 60.0, 120.0}) {
    const double u = roll(20, 10.0, 300.0, k);
    printf("  kinetic %3.0f -> %.2f units of 300.0 (%.1f%%)\n", k, u, 100.0 * u / 300.0);
  }
  return 0;
}
