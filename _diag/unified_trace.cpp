// AFTER: one step (1 delta) for EVERY receiver. The vertical axis must stop accumulating.
//
//   g++ -std=c++17 -O2 -I../src -o unified_trace.cpp unified_trace.cpp
//
// Same trace as jump_trace.cpp, but now there is no per-receiver step to choose: the delivery is
// the same for the vertical axis as for the horizontal one. A slow roll at kinetic 1 should hand
// over one delta per notch, smoothly, with nothing held back.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>

using namespace anim3;
static const double kD = 120.0, kU = 15.0;
static const double kStepDeltas = kD / kU;
static const double kStep = 1.0 / kStepDeltas; // one delta, for everyone now

static void trace(const char *label, double easeMs, double kinetic, double gapMs, int n)
{
  Params P; P.durationMs = easeMs;
  Glide g; g.Reset();
  const double gap = gapMs / 1000.0;
  double accum = 0, msgTime = 0;
  int fed = 0;
  double per[64] = {0}, total = 0;
  for (double t = 0; t < n * gap + easeMs / 1000.0 + 0.05; t += 0.001) {
    if (fed < n && msgTime <= t + 1e-9) {
      const double gapSec = (fed == 0) ? -1.0 : gap;
      const double kept = kD * (1.0 - EatFraction(gapSec, kD, kinetic));
      g.Feed(kept / kStepDeltas, P);
      ++fed; msgTime += gap;
    }
    if (g.Active()) {
      accum += g.Tick(0.001, P);
      const double m = floor(fabs(accum) / kStep + 0.5);
      if (m >= 1.0) {
        const double s = m * kStep;
        accum -= (accum < 0 ? -s : s);
        const double d = s * kStepDeltas;
        if (fed >= 1 && fed <= n) per[fed] += d;
        total += d;
      }
    }
  }
  printf("=== %s : ease %.0f ms, kinetic %.0f, gap %.0f ms, %d notches ===\n",
         label, easeMs, kinetic, gapMs, n);
  printf("  per notch (deltas):");
  for (int k = 1; k <= n; ++k) printf(" %5.2f", per[k]);
  printf("\n  total %.3f deltas\n\n", total);
}

int main()
{
  printf("ONE step = one delta for every receiver.\n");
  printf("A 'nothing, nothing, JUMP' pattern would mean it still accumulates; a steady row is fixed.\n\n");
  trace("slow roll, kinetic 1", 20, 1, 300, 12);
  trace("slow roll, kinetic 12", 20, 12, 300, 12);
  trace("slow roll, kinetic 1, ease 300", 300, 1, 300, 12);
  trace("moderate roll, kinetic 1", 20, 1, 100, 12);
  return 0;
}
