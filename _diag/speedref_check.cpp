// THE NEW CONTROL: "Speed ref" -- the notch gap at which the eating is at full strength.
//
//   g++ -std=c++17 -O2 -I../src -o speedref_check.cpp speedref_check.cpp
//
// The problem it fixes: with the reference fixed at 200 ms, ordinary slow rolling sits on the
// steep part of the ramp, so a small change of hand speed changes the travel a lot. Raising the
// reference moves ordinary rolling into the FLAT part, where every notch moves the same amount.
//
// This shows, for kinetic 1 and 12, how flat the per-notch amount is across the gaps a hand
// actually produces, at three reference settings.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>

using namespace anim3;
static const double kD = 120.0;

static double keptDeltas(double gapMs, double kinetic, double refMs)
{
  const double slowRate = 120.0 * 1000.0 / refMs; // deltas/s
  return kD * (1.0 - EatFraction(gapMs / 1000.0, kD, kinetic, slowRate));
}

static void table(double kinetic, double refMs)
{
  printf("  kinetic %.0f, speed ref %.0f ms |", kinetic, refMs);
  const double gaps[] = {400, 300, 250, 200, 175, 150, 125, 100, 75, 50};
  for (int i = 0; i < 10; ++i) printf(" %6.1f", keptDeltas(gaps[i], kinetic, refMs));
  printf("\n");
}

int main()
{
  printf("Per-notch travel (deltas) across hand speeds. Columns are gaps: 400..50 ms.\n");
  printf("A FLAT row = same amount every notch (no 'sudden jump').\n\n");

  printf("=== kinetic 1 (eats most) ===\n");
  const double refs[] = {200, 300, 400, 600, 800};
  for (int i = 0; i < 5; ++i) table(1, refs[i]);

  printf("\n=== kinetic 12 (the tuned value) ===\n");
  for (int i = 0; i < 5; ++i) table(12, refs[i]);

  printf("\n=== and the default must not move: kinetic 12, ref 200, vs the old fixed rule ===\n");
  {
    // the old rule was exactly this: refSec = received/600 = 0.2 s
    int bad = 0;
    const double gaps[] = {400, 250, 200, 175, 150, 100, 50};
    for (int i = 0; i < 7; ++i) {
      const double now = keptDeltas(gaps[i], 12, 200);
      const double old = kD * (1.0 - EatFraction(gaps[i] / 1000.0, kD, 12));
      if (std::fabs(now - old) > 1e-9) { ++bad; printf("  !! differs at %.0f ms\n", gaps[i]); }
    }
    printf("  %s\n", bad ? "FAILED" : "OK: identical to the old fixed rule at the default");
  }
  return 0;
}
