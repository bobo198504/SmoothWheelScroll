// FRICTION, verified on the plugin's own header.
//
//   g++ -std=c++17 -O2 -I../src -o friction_check.cpp friction_check.cpp
//
// The rule: a message arriving a long time after the last is SLOW and gets most of itself eaten;
// a message arriving on top of the last is FAST and keeps nearly all of it. Speed = the gap since
// the previous message, with anim3::kFrictionRefSec as "fully slow".
//
// Checks: (1) the gap -> keep mapping; (2) a slow, one-notch-at-a-time turn moves a short distance
// (the point of the feature); (3) a fast roll keeps nearly everything; (4) friction 0 = the old
// exact conservation, unchanged.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace anim3;
static const double kU = 15.0; // units per notch

// Feed `n` messages of 1 notch each, `gapMs` apart; ease each over Dms. Returns units delivered.
static double roll(int n, double gapMs, double Dms, double friction) {
  Params P; P.durationMs = Dms;
  Glide g; g.Reset();
  const double dt = 0.001;
  const double gap = gapMs / 1000.0;
  double out = 0;
  double msgTime = 0.0;
  int fed = 0;
  const double end = n * gap + Dms / 1000.0 + 0.05;
  for (double t = 0; t < end; t += dt) {
    while (fed < n && msgTime <= t + 1e-9) {
      const double f = FrictionFactor(gap, friction);
      g.Feed(1.0 * (1.0 - f), P);
      ++fed; msgTime += gap;
    }
    out += g.Tick(dt, P) * kU;
  }
  return out;
}

int main() {
  printf("one notch = %.0f units. friction ref gap = %.0f ms.\n\n", kU, kFrictionRefSec * 1000);

  printf("=== 1. the mapping: gap since last message -> fraction KEPT ===\n");
  for (double gapMs : {0, 20, 50, 100, 150, 200, 300, 500}) {
    const double kept = 1.0 - FrictionFactor(gapMs / 1000.0, 0.9);
    printf("   gap %4.0f ms -> keep %5.1f%% (eats %4.1f%%)   [friction=90%%]\n",
           gapMs, 100 * kept, 100 * (1 - kept));
  }

  printf("\n=== 2. ONE notch at a time (the slow case). D = 300 ms ===\n");
  for (double fr : {0.0, 0.9}) {
    const double u = roll(1, 400.0, 300.0, fr);
    printf("   friction %3.0f%%: one isolated notch moves %6.3f units = %5.3f notch"
           "   (was 1.000 before friction)\n", 100 * fr, u, u / kU);
  }

  printf("\n=== 3. 10-notch roll at several speeds, friction 90%. D = 300 ms ===\n");
  printf("   %-14s %-10s %-12s %s\n", "roll speed", "kept", "units out", "vs no friction");
  for (double gapMs : {300, 200, 100, 60, 40, 20, 10}) {
    const double u90 = roll(10, gapMs, 300.0, 0.9);
    const double u0 = roll(10, gapMs, 300.0, 0.0);
    printf("   %4.0f ms/notch %-10.0f%% %-12.3f %.1f%%\n", gapMs, 100.0 * (u90 / u0), u90,
           100.0 * u90 / u0);
  }

  printf("\n=== 4. friction 0 must still be EXACT (take how much, give how much) ===\n");
  for (double gapMs : {250, 60, 33}) {
    const double u = roll(10, gapMs, 300.0, 0.0);
    printf("   10 @%3.0f ms, friction 0%%: %9.4f units (want 150.0000)  %s\n", gapMs, u,
           std::fabs(u - 150.0) < 0.01 ? "OK" : "!! REGRESSED");
  }
  return 0;
}
