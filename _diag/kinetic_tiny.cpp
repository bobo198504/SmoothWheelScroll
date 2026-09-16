// DOES a stream of very small kept amounts accumulate, or is it lost?
//
//   g++ -std=c++17 -O2 -I../src -o kinetic_tiny.cpp kinetic_tiny.cpp
//
// Uses the same delivery shape the plugin uses -- a 1/256-unit grid with a carry -- so it answers
// the question that mattered for the user's worry: at kinetic = 1, a free-spinning wheel whose
// messages each keep LESS than one grid step -- do they still get through?
//
// Units: the Glide is fed in 7-bit units and returns 7-bit units (one notch = 15 units = 120
// deltas, so one delta = 1/8 unit). The kStream grid is 1/256 unit.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>

using namespace anim3;
static const double kDeltasPerUnit = 8.0;
static const double kGrid = 1.0 / 256.0; // units: the finest step the relative form carries

struct R { double keptDeltas, outDeltas; long sends; };

// nMsg messages of `eachDeltas` deltas, `gapMs` apart, each eased over Dms, through `grid`.
static R stream(int nMsg, double eachDeltas, double gapMs, double Dms, double kinetic, double grid)
{
  Params P; P.durationMs = Dms;
  Glide g; g.Reset();
  const double gap = gapMs / 1000.0;
  double carry = 0, outUnits = 0, keptUnits = 0, msgTime = 0;
  int fed = 0;
  R r{0, 0, 0};
  for (double t = 0; t < nMsg * gap + Dms / 1000.0 + 0.05; t += 0.001) {
    while (fed < nMsg && msgTime <= t + 1e-9) {
      const double eaten = EatFraction((fed == 0) ? -1.0 : gap, eachDeltas, kinetic);
      const double kept = eachDeltas * (1.0 - eaten) / kDeltasPerUnit; // units
      g.Feed(kept, P);
      keptUnits += kept;
      ++fed; msgTime += gap;
    }
    const double want = g.Tick(0.001, P); // units
    const double tot = carry + want;
    const double m = (tot < 0) ? -std::floor(-tot / grid + 0.5) : std::floor(tot / grid + 0.5);
    const double sent = m * grid;
    carry = tot - sent;
    if (std::fabs(sent) > 1e-12) ++r.sends;
    outUnits += sent;
  }
  r.keptDeltas = keptUnits * kDeltasPerUnit;
  r.outDeltas = outUnits * kDeltasPerUnit;
  return r;
}

int main()
{
  printf("grid = 1/256 unit = 1/32 delta.  kinetic = 1 keeps 1/120 of each message.\n\n");

  printf("=== A. a stream of 1-delta messages (finest free-spinner), kinetic = 1 ===\n");
  printf("   each message keeps 0.0083 delta = 0.27 grid steps, i.e. BELOW one step\n\n");
  const int counts[4] = {1, 10, 100, 1000};
  for (int i = 0; i < 4; ++i) {
    const R r = stream(counts[i], 1.0, 5.0, 300.0, 1.0, kGrid);
    printf("   %5d messages: kept %8.4f delta -> delivered %8.4f delta in %ld send(s)  (%s)\n",
           counts[i], r.keptDeltas, r.outDeltas, r.sends,
           r.outDeltas > 0.9 * r.keptDeltas ? "accumulates" : "some left over");
  }

  printf("\n=== B. how much is never sent? kinetic = 1, 20-delta messages ===\n");
  const int n2[5] = {1, 2, 3, 5, 10};
  for (int i = 0; i < 5; ++i) {
    const R r = stream(n2[i], 20.0, 5.0, 300.0, 1.0, kGrid);
    printf("   %3d x 20 delta: kept %7.4f -> delivered %7.4f   leftover %6.4f delta = %5.2f steps\n",
           n2[i], r.keptDeltas, r.outDeltas, r.keptDeltas - r.outDeltas,
           (r.keptDeltas - r.outDeltas) / (1.0 / 32.0));
  }

  printf("\n=== C. the whole-unit path floors at one unit, so a gesture always moves ===\n");
  {
    const R r = stream(1, 20.0, 5.0, 300.0, 1.0, 1.0);
    printf("   1 x 20 delta, kinetic 1, whole-unit grid: delivered %.4f delta\n", r.outDeltas);
  }

  printf("\n=== D. sanity: at kinetic 120 nothing is eaten, a long stream is exact ===\n");
  {
    const R r = stream(100, 20.0, 5.0, 300.0, 120.0, kGrid);
    printf("   100 x 20 delta (2000 total): kept %.4f -> delivered %.4f\n",
           r.keptDeltas, r.outDeltas);
  }
  return 0;
}
