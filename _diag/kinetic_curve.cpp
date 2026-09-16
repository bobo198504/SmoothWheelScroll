// WHAT is tunable in how the eating depends on wheel speed?
//
//   g++ -std=c++17 -O2 -I../src -o kinetic_curve.cpp kinetic_curve.cpp
//
// Today there are two independent things:
//   1. DEPTH   -- the `Kinetic` slider (1..120 deltas): how much is eaten at FULL slowness.
//   2. FALLOFF -- FIXED: the eating reaches full strength at `kKineticSlowRate` (600 delta/s, i.e.
//                 one notch per 200 ms) and falls off LINEARLY to zero as the roll gets faster.
//
// (1) is a knob. (2) is not. This prints what (2) currently means in plain terms -- how fast you
// must roll before the eating lets go -- and what it would mean at other thresholds, so the choice
// is made on numbers.

#include <cstdio>
#include <cmath>
#include <initializer_list>

static const double kNotchDeltas = 120.0;

static double eatFrac(double speedDeltasPerSec, double slowRate, double kineticDeltas,
                      bool smoothRamp) {
  const double keepSlow = kineticDeltas / kNotchDeltas;
  const double eatSlow = 1.0 - (keepSlow > 1.0 ? 1.0 : keepSlow < 0.0 ? 0.0 : keepSlow);
  if (eatSlow <= 0.0) return 0.0;
  // FAITHFUL to the code: f = gap / (received/slowRate) = slowRate / speed. So the eating is at
  // FULL strength for any speed AT OR BELOW the threshold, and falls off as the roll gets faster.
  double f = (speedDeltasPerSec > 0.0) ? (slowRate / speedDeltasPerSec) : 1.0;
  if (f > 1.0) f = 1.0;
  if (smoothRamp) f = f * f * (3.0 - 2.0 * f);
  return eatSlow * f;
}

int main() {
  const double kin = 12.0; // a tuned value
  printf("Kinetic fixed at %.0f deltas (keeps %.0f%% at full slowness).\n", kin, 100 * kin / 120);
  printf("Eating for a NOTCHED mouse, by how fast you roll:\n\n");

  printf("=== TODAY: full eating for any roll AT OR BELOW 600 delta/s, then linear falloff ===\n");
  printf("  %-16s %-12s %s\n", "roll speed", "eats", "in plain terms");
  const double sp[] = {150, 300, 600, 900, 1200, 2400, 4800};
  const char *words[] = {"1 notch / 800ms", "1 notch / 400ms", "1 notch / 200ms (threshold)",
                         "1 notch / 133ms", "1 notch / 100ms", "1 notch / 50ms",
                         "1 notch / 25ms"};
  for (int i = 0; i < 7; ++i)
    printf("  %-16.0f %-12.1f%% %s\n", sp[i], 100 * eatFrac(sp[i], 600, kin, false), words[i]);

  printf("\n=== if the THRESHOLD changed (still linear), what would you feel? ===\n");
  printf("  full eating at or below this speed; half of it at twice the speed:\n\n");
  printf("  %-14s %-26s %-22s\n", "threshold", "full eating at/below", "half eating at");
  const double thr[] = {300, 600, 1200, 2400};
  for (int i = 0; i < 4; ++i) {
    const char *full = thr[i] == 300 ? "1 notch / 400ms" : thr[i] == 600 ? "1 notch / 200ms"
                        : thr[i] == 1200 ? "1 notch / 100ms" : "1 notch / 50ms";
    const char *half = thr[i] == 300 ? "1 notch / 200ms" : thr[i] == 600 ? "1 notch / 100ms"
                        : thr[i] == 1200 ? "1 notch / 50ms" : "1 notch / 25ms";
    printf("  %-14.0f %-26s %-22s\n", thr[i], full, half);
  }

  printf("\n  reading: a LOW threshold lets the eating quit as soon as you roll briskly;\n");
  printf("           a HIGH threshold keeps eating even when you roll quite fast.\n");

  printf("\n=== the SHAPE of the falloff, at threshold 600 ===\n");
  printf("  %-16s %-14s %-14s\n", "roll speed", "linear", "smooth (S)");
  for (int i = 2; i < 7; ++i) {
    const double s = sp[i];
    printf("  %-16.0f %-14.1f %-14.1f\n", s, 100 * eatFrac(s, 600, kin, false),
           100 * eatFrac(s, 600, kin, true));
  }
  printf("  (both agree at the threshold and far above it; they differ just above the threshold)\n");
  return 0;
}
