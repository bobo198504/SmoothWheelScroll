// What does ONE call to the action carry when several windows are open at once?
//
//   g++ -std=c++17 -O2 -I../src -o model3_sum.exe model3_sum.cpp
//
// The question: with 3 windows running, does the action receive 3 (instead of 1) in a single call?
//
// The answer: each window hands over (dt/D) of a notch per tick, and the call carries the SUM of
// the open windows' slices. With N windows open the call carries N * (dt/D) notches -- N times ONE
// window's slice, but that slice is a fraction of a notch, not a whole notch. Measured in a STEADY
// state (messages keep coming), so the open count is exactly D/interval.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace anim3;
static const double kU = 15.0; // units per notch

// Continuous feed every ivSec; sample the per-tick delivery in steady state (t > D).
static void steady(double ivSec, double D) {
  Params P; P.durationMs = D; P.shape = kLinear;
  Glide g; g.Reset();
  const double dt = 0.001, grid = 1.0 / 256.0;
  double msgNext = 0, carry = 0, sample = -1, sampleWindows = -1;
  const double end = D / 1000.0 + 0.5;
  for (double t = 0.0; t < end; t += dt) {
    while (msgNext <= t + 1e-9) { g.Feed(1.0, P); msgNext += ivSec; }
    const double want = g.Tick(dt, P) * kU;
    const double total = carry + want;
    const double m = std::floor(std::fabs(total) / grid + 0.5);
    const double deliv = (total < 0 ? -1 : 1) * m * grid;
    carry = total - deliv;
    // Sample late, in steady state, on a tick where the encoder actually emitted.
    if (t > D / 1000.0 + 0.1 && sample < 0 && std::fabs(want) > 1e-12) {
      sample = std::fabs(want);
      sampleWindows = g.InFlight();
    }
  }
  const double perWindow = dt / (D / 1000.0) * kU;
  printf("  interval %6.1f ms -> %2d windows open | one call = %7.4f units = %7.5f notch "
         "(%5.2f%% of a notch)  [%d x %.4f]\n",
         ivSec * 1000.0, (int)llround(sampleWindows), sample, sample / kU,
         100.0 * sample / kU, (int)llround(sampleWindows), perWindow);
}

int main() {
  const double D = 200.0;
  printf("one notch = %.0f units; tick = 1 ms; window D = %.0f ms.\n", kU, D);
  printf("A single window hands over dt/D per tick = %.4f units = %.5f notch.\n\n", 1.0 / (D / 1.0) * kU, 1.0 / D);

  printf("=== a single call, with N windows open (steady state). 'one call' = one 1 ms tick. ===\n\n");
  steady(D / 1.0 / 1000.0 * 1000 / 1000, D); // 200 ms -> 1 window
  steady(100.0 / 1000.0, D);                 // 100 ms -> 2
  steady(66.7 / 1000.0, D);                  // ~3
  steady(50.0 / 1000.0, D);                  // 4
  steady(25.0 / 1000.0, D);                  // 8
  steady(10.0 / 1000.0, D);                  // 20

  printf("\n=== for reference: a NATIVE notch is ONE call of 15.0000 units (100%% of a notch) ===\n");
  printf("    so even 20 overlapping windows hand over %.2f%% of a notch per call.\n",
         100.0 * (20 * (1.0 / D) * kU) / kU);

  printf("\n=== the RATE is what multiplies; the TOTAL stays conserved ===\n");
  for (double ivMs : {200.0, 100.0, 50.0, 25.0}) {
    Params P; P.durationMs = D; P.shape = kLinear;
    Glide g; g.Reset();
    const double dt = 0.001, iv = ivMs / 1000.0;
    double msgNext = 0, carry = 0, out = 0, in = 0;
    for (double t = 0.0; t < 1.0 + D / 1000.0 + 0.2; t += dt) {
      while (msgNext <= t + 1e-9 && msgNext < 1.0) { g.Feed(1.0, P); in += kU; msgNext += iv; }
      const double want = g.Tick(dt, P) * kU;
      const double total = carry + want;
      const double m = std::floor(std::fabs(total) / (1.0 / 256.0) + 0.5);
      const double deliv = (total < 0 ? -1 : 1) * m * (1.0 / 256.0);
      carry = total - deliv; out += deliv;
    }
    printf("  interval %5.0f ms: fed %.0f units (=%.0f notches) -> out %.2f units (=%.4f notches)\n",
           ivMs, in, in / kU, out, out / kU);
  }
  return 0;
}
