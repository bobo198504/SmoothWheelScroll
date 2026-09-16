// AFTER the fix: one delivery step = one delta. Does the slow roll stop inflating?
//
//   g++ -std=c++17 -O2 -I../src -o slow_fixed.cpp slow_fixed.cpp
//
// Faithful to the plugin now: ONE step size for every receiver, no top-up. Reports what was
// intended (kept) vs what was actually handed over, for the combinations that used to inflate.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>

using namespace anim3;
static const double kD = 120.0;             // deltas per notch
static const double kU = 15.0;              // 7-bit units per notch
static const double kStepDeltas = kD / kU;  // 8 deltas per 7-bit unit
static const double kStep = 1.0 / kStepDeltas; // one delta, in units (= 0.125)

struct R { double keptDeltas, sentDeltas; long sends; };

// The Glide is unit-agnostic: it hands back whatever unit it is fed. The PLUGIN feeds 7-bit units
// (one notch = 15), so this does too -- feed units, Tick returns units, the step is one delta.
static R roll(double easeMs, double kinetic, double gapMs, int n)
{
  Params P; P.durationMs = easeMs;
  Glide g; g.Reset();
  const double gap = gapMs / 1000.0;
  double accum = 0, msgTime = 0, keptD = 0, sentD = 0;
  int fed = 0; long sends = 0;
  for (double t = 0; t < n * gap + easeMs / 1000.0 + 0.05; t += 0.001) {
    if (fed < n && msgTime <= t + 1e-9) {
      const double gapSec = (fed == 0) ? -1.0 : gap;
      const double kept = kD * (1.0 - EatFraction(gapSec, kD, kinetic)); // deltas
      g.Feed(kept / kStepDeltas, P);                                     // -> 7-bit units
      keptD += kept;
      ++fed; msgTime += gap;
    }
    if (g.Active()) {
      accum += g.Tick(0.001, P); // 7-bit units
      const double m = floor(fabs(accum) / kStep + 0.5);
      if (m >= 1.0) {
        const double s = ((accum < 0) ? -m : m) * kStep;
        accum -= s; sentD += s * kStepDeltas; ++sends;
      }
    }
  }
  return R{keptD, sentD, sends};
}

int main()
{
  printf("ONE step = one delta (%.4f unit). One delivery grade, no top-up.\n", kStep);
  printf("Slow roll: one notch every 150 ms, 10 notches. 'kept' is what SHOULD go out.\n\n");
  printf("  %-8s %-9s %-11s %-11s %-9s %s\n", "ease", "kinetic", "kept(delta)", "sent(delta)", "ratio", "verdict");
  const double eases[] = {20, 300};
  const double kins[] = {1, 6, 12, 60, 120};
  for (int a = 0; a < 2; ++a)
    for (int b = 0; b < 5; ++b) {
      const R r = roll(eases[a], kins[b], 150.0, 10);
      const double ratio = (r.keptDeltas > 0) ? r.sentDeltas / r.keptDeltas : 1.0;
      printf("  %-8.0f %-9.0f %-11.3f %-11.3f %-9.3f %s\n", eases[a], kins[b], r.keptDeltas,
             r.sentDeltas, ratio,
             std::fabs(ratio - 1.0) < 0.12 ? "OK" : (ratio > 1.5 ? "!! INFLATED" : "lost"));
    }

  return 0;
}
