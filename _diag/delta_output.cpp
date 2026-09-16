// IS 1-DELTA OUTPUT PRECISION ENOUGH? Or does dropping the fractional part cost smoothness?
//
//   g++ -std=c++17 -O2 -I../src -o delta_output.cpp delta_output.cpp
//
// The reasoning to test: the INPUT is integers, smallest step 1 delta (1/120 notch). So maybe the
// OUTPUT only needs integer deltas too, and the 1/256-unit fraction is wasted.
//
// The catch to check: input step and output step are compared over DIFFERENT time spans. An input
// delta of 1 arrives at a device's message rate; the output is spread over the easing window, so
// each frame's slice is a fraction of a delta. Rounding that slice UP to 1 delta makes the motion
// arrive in lumps. This measures the lump size and how long a single delta of movement takes.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace anim3;
static const double kNotchUnits = 15.0;

// Deliver one notch over D ms. grid = the smallest step allowed, in 7-bit units.
//   grid = 1/256 -> the current finest step
//   grid = 1/8   -> one raw delta      (since 1 delta = 1/8 unit)
//   grid = 1.0   -> one whole 7-bit unit (1/15 notch)
struct Res { double maxStep; long calls; double idleMs; double longestZeroRunMs; };

static Res one(double Dms, double gridUnits, double tickMs) {
  Params P; P.durationMs = Dms; P.shape = kSymS; P.bend = 0.0; // linear: the cleanest comparison
  Glide g; g.Reset(); g.Feed(1.0, P);
  const double dt = tickMs / 1000.0;
  double carry = 0, out = 0, maxStep = 0, zeroRun = 0, longestZero = 0;
  long calls = 0;
  for (double t = 0; t < Dms / 1000.0 + 0.05; t += dt) {
    const double want = g.Tick(dt, P) * kNotchUnits;
    const double tot = carry + want;
    const double m = (tot < 0) ? -std::floor(-tot / gridUnits + 0.5)
                               : std::floor(tot / gridUnits + 0.5);
    const double deliv = m * gridUnits;
    carry = tot - deliv;
    if (std::fabs(deliv) > 1e-12) { ++calls; if (std::fabs(deliv) > maxStep) maxStep = std::fabs(deliv); zeroRun = 0; }
    else { zeroRun += dt * 1000.0; if (zeroRun > longestZero) longestZero = zeroRun; }
    out += deliv;
  }
  return Res{maxStep, calls, longestZero, longestZero};
}

static void show(const char *label, double grid, double tick) {
  // A "frame gap": consecutive ticks that delivered nothing = the motion visibly stalling.
  for (double D : {300.0, 100.0, 10.0}) {
    const Res r = one(D, grid, tick);
    printf("  %-16s D=%3.0f ms tick=%.1f ms | largest step %8.5f u = %7.4f delta | calls %4ld |"
           " longest stall %6.1f ms\n",
           label, D, tick, r.maxStep, r.maxStep / (1.0 / 8.0), r.calls, r.longestZeroRunMs);
  }
}

int main() {
  printf("1 notch = 15 units = 120 deltas. 1 delta = 1/8 unit = 0.125 unit.\n");
  printf("One notch over D ms, delivered on a given grid.\n\n");

  printf("=== A. THE CATCH: a single notch, spread over D, at each output granularity ===\n");
  printf("     (tick 1 ms; 'stall' = how long the output goes completely quiet mid-gesture)\n\n");
  show("finest (1/256 u)", 1.0 / 256.0, 1.0);
  show("1 delta (1/8 u)", 1.0 / 8.0, 1.0);
  show("1 unit (1/15 ntch)", 1.0, 1.0);

  printf("\n=== B. a SLOW roll: 1 notch per 300 ms (a gentle creep) ===\n\n");
  show("finest (1/256 u)", 1.0 / 256.0, 1.0);
  show("1 delta (1/8 u)", 1.0 / 8.0, 1.0);
  show("1 unit (1/15 ntch)", 1.0, 1.0);

  printf("\n=== C. how long does ONE delta of movement take, if we only ever send whole deltas? ===\n");
  printf("     (that is the size of one visible lump on a slow roll)\n\n");
  for (double D : {300.0, 150.0, 50.0, 10.0}) {
    // At what rate does one notch come in? Take a slow roll: 1 notch per D ms means the whole
    // notch arrives over D, so the average gap between 1-delta steps is D/120.
    printf("  D=%3.0f ms -> one delta lands every %.2f ms on average, step = 1/120 notch = %.4f px at 1px/unit? (see below)\n",
           D, D / 120.0, 1.0 / 120.0);
  }

  printf("\n=== D. the real question: ONE SENDING at 60 Hz (16.7 ms). How much does it move? ===\n");
  printf("     with finest grid vs 1-delta grid, a slow notch over 300 ms\n\n");
  {
    const double D = 300.0, tick = 16.7;
    for (double grid : {1.0 / 256.0, 1.0 / 8.0, 1.0}) {
      Params P; P.durationMs = D; P.shape = kSymS; P.bend = 0.0;
      Glide g; g.Reset(); g.Feed(1.0, P);
      const double dt = tick / 1000.0;
      double carry = 0, out = 0, maxStep = 0;
      long calls = 0;
      printf("    grid=%-10s |", grid == 1.0 / 256.0 ? "1/256 u" : grid == 1.0 / 8.0 ? "1 delta" : "1 unit");
      for (double t = 0; t < D / 1000.0 + 0.02; t += dt) {
        const double want = g.Tick(dt, P) * kNotchUnits;
        const double tot = carry + want;
        const double m = std::floor(std::fabs(tot) / grid + 0.5);
        const double deliv = (tot < 0 ? -1 : 1) * m * grid;
        carry = tot - deliv;
        if (std::fabs(deliv) > 1e-12) ++calls;
        if (std::fabs(deliv) > maxStep) maxStep = std::fabs(deliv);
        out += deliv;
        printf(" %6.3f", std::fabs(deliv) / (1.0 / 8.0));
      }
      printf("  | deltas per send above; largest = %.2f delta, sends=%ld, total=%.4f u\n",
             maxStep / (1.0 / 8.0), calls, out);
    }
  }
  return 0;
}
