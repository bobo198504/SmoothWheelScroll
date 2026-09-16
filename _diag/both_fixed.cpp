// BOTH receivers, after the fix: no inflation anywhere, and no forced top-up.
//
//   g++ -std=c++17 -O2 -I../src -o both_fixed.cpp both_fixed.cpp
//
// stream    : step = 1 delta   (0.125 unit)
// stepUnits : step = 1 unit    -- unchanged for the receiver that rounds sub-units up
// Neither forces a whole unit at the end any more.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>

using namespace anim3;
static const double kD = 120.0, kU = 15.0;
static const double kStepDeltas = kD / kU;              // 8 deltas per unit
static const double kStreamStep = 1.0 / kStepDeltas;    // 1 delta in units
static const double kUnitStep = 1.0;                    // 1 unit

struct R { double keptDeltas, sentDeltas; long sends; };

static R roll(double easeMs, double kinetic, double gapMs, int n, double step, bool topUp)
{
  Params P; P.durationMs = easeMs;
  Glide g; g.Reset();
  const double gap = gapMs / 1000.0;
  double accum = 0, msgTime = 0, kept = 0, sent = 0;
  int fed = 0; long sends = 0; bool sentAny = false;
  for (double t = 0; t < n * gap + easeMs / 1000.0 + 0.05; t += 0.001) {
    if (fed < n && msgTime <= t + 1e-9) {
      if (!g.Active()) sentAny = false;
      const double gapSec = (fed == 0) ? -1.0 : gap;
      const double k = kD * (1.0 - EatFraction(gapSec, kD, kinetic));
      g.Feed(k / kStepDeltas, P);
      kept += k;
      ++fed; msgTime += gap;
    }
    if (g.Active()) {
      const bool was = g.Active();
      accum += g.Tick(0.001, P);
      const double m = floor(fabs(accum) / step + 0.5);
      if (m >= 1.0) {
        const double s = m * step;
        accum -= (accum < 0 ? -s : s); sent += s * kStepDeltas; ++sends; sentAny = true;
      }
      if (topUp && was && !g.Active() && !sentAny && accum != 0.0) {
        sent += ((accum < 0) ? -1.0 : 1.0) * kStepDeltas; accum = 0; ++sends;
      }
    }
  }
  return R{kept, sent, sends};
}

int main()
{
  printf("Slow roll: one notch every 150 ms, 10 notches. 'kept' is what was SUPPOSED to go out.\n\n");
  printf("  %-22s %-8s %-11s %-11s %s\n", "path / ease / kinetic", "(no top-up)", "kept", "sent", "ratio");
  const double kins[] = {1, 12, 120};
  const double eases[] = {20, 300};
  for (int e = 0; e < 2; ++e)
    for (int k = 0; k < 3; ++k) {
      const R s = roll(eases[e], kins[k], 150, 10, kStreamStep, false);
      printf("  stream   ease %-3.0f kin %-3.0f   %-11s %-11.3f %-11.3f %.3f\n",
             eases[e], kins[k], "y", s.keptDeltas, s.sentDeltas, s.sentDeltas / s.keptDeltas);
    }
  printf("\n  the same for the whole-unit receiver (step = 1 unit):\n");
  for (int e = 0; e < 2; ++e)
    for (int k = 0; k < 3; ++k) {
      const R u = roll(eases[e], kins[k], 150, 10, kUnitStep, false);
      printf("  unit     ease %-3.0f kin %-3.0f   %-11s %-11.3f %-11.3f %.3f\n",
             eases[e], kins[k], "y", u.keptDeltas, u.sentDeltas, u.sentDeltas / u.keptDeltas);
    }

  printf("\n=== the OLD behaviour, for comparison: step 1 unit WITH the top-up ===\n");
  {
    const R o = roll(20, 1, 150, 10, kUnitStep, true);
    printf("  unit + top-up, ease 20, kin 1: kept %.3f -> sent %.3f  (ratio %.2f -- the reported jump)\n",
           o.keptDeltas, o.sentDeltas, o.sentDeltas / o.keptDeltas);
  }
  return 0;
}
