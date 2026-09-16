// WHEN is the eat applied -- all at once up front, or spread across the window? Measure the real
// per-millisecond output of ONE notch.
//
//   g++ -std=c++17 -O2 -I../src -o eat_timing.exe eat_timing.cpp && ./eat_timing.exe

#include "model.h"
#include <cstdio>
#include <cmath>

static const double kNotchDeltas = 120.0;
static const double kNotchUnits = 15.0;
static const double kDeltasPerUnit = 8.0;

int main()
{
  const double X = 50.0;      // precision 50 ms
  const double eatFrac = 0.90; // "what if it eats 90%"
  printf("precision X = %.0f ms, one notch = %.0f deltas, eat = %.0f%%\n\n", X, kNotchDeltas,
         100.0 * eatFrac);

  model::Params P;
  P.windowMs = X;
  P.eatFrac = eatFrac;
  model::Axis g;
  g.Reset();
  g.Feed(kNotchUnits, P); // ONE notch

  printf("  %-8s %-14s %s\n", "ms", "delta out", "cumulative");
  const double dt = 0.001;
  int ms = 0;
  double cum = 0;
  for (double t = 0; t < X / 1000.0 + 0.002; t += dt)
  {
    const double s = g.Tick(dt, P) * kDeltasPerUnit;
    cum += s;
    if (s != 0.0)
      printf("  %-8.0f %-14.4f %.4f\n", (t + dt) * 1000.0, s, cum);
    if (!g.Active())
      break;
    (void)ms;
  }
  printf("\n  total = %.4f deltas  (120 * (1 - %.2f) = %.4f)\n", cum, eatFrac,
         kNotchDeltas * (1.0 - eatFrac));
  printf("\n  READ IT THIS WAY: if the eat had happened across the window, the FIRST ms would\n");
  printf("  hand over about (120/50)*(1-0.90) = %.3f delta and the total would grow all the way.\n",
         120.0 / 50.0 * 0.10);
  printf("  If the eat happened ALL AT ONCE up front, then what is spread over the window is the\n");
  printf("  ALREADY-REDUCED 12 deltas, i.e. %.4f per ms.\n", 12.0 / 50.0);
  return 0;
}
