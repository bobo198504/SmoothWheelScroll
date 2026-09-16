// HORIZONTAL ZOOM specifically (the action 990 path): the stream delivery, 1 delta per step.
// Does IT show a jump? And where does any remaining jump come from?
//
//   g++ -std=c++17 -O2 -I../src -o hzoom_trace.cpp hzoom_trace.cpp
//
// Two candidate causes are separated here:
//   A. DELIVERY accumulation -- a notch that sends nothing and a later one that sends a lot.
//   B. The EATING CURVE -- the first notch of a gesture is treated as "as slow as it gets", and
//      the eating ramps steeply, so a small change of hand speed changes the amount a lot.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>

using namespace anim3;
static const double kD = 120.0, kU = 15.0;
static const double kStepDeltas = kD / kU;
static const double kStep = 1.0 / kStepDeltas; // one delta -- the stream step now

// Per-notch delivered deltas, for a steady roll at `gapMs`.
static void row(const char *name, double easeMs, double kinetic, double gapMs, int n)
{
  Params P; P.durationMs = easeMs;
  Glide g; g.Reset();
  const double gap = gapMs / 1000.0;
  double accum = 0, msgTime = 0;
  int fed = 0;
  double per[64] = {0};
  for (double t = 0; t < n * gap + easeMs / 1000.0 + 0.05; t += 0.001) {
    if (fed < n && msgTime <= t + 1e-9) {
      const double gapSec = (fed == 0) ? -1.0 : gap; // the plugin's first-message rule
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
        if (fed >= 1 && fed <= n) per[fed] += s * kStepDeltas;
      }
    }
  }
  printf("  %-30s", name);
  for (int k = 1; k <= n; ++k) printf(" %6.2f", per[k]);
  printf("\n");
}

int main()
{
  printf("HORIZONTAL ZOOM path (stream). Per notch, delivered deltas. ease 20 ms.\n");
  printf("A steady row = no delivery jump. A '1.00 then 60' start = the eating curve.\n\n");

  printf("=== kinetic 1 (what you tested) ===\n");
  const double gaps[] = {300, 250, 200, 175, 150, 125, 100};
  const char *gn[] = {"gap 300ms (very slow)", "gap 250ms", "gap 200ms (threshold)",
                      "gap 175ms", "gap 150ms", "gap 125ms", "gap 100ms"};
  for (int i = 0; i < 7; ++i) row(gn[i], 20, 1, gaps[i], 8);

  printf("\n=== kinetic 12 (the value you liked) ===\n");
  for (int i = 0; i < 7; ++i) row(gn[i], 20, 12, gaps[i], 8);

  printf("\n=== how steep is the eating curve at kinetic 1? (single notch, by gap) ===\n");
  printf("  %-12s %-14s %s\n", "gap ms", "kept deltas", "vs the slowest case");
  const double base = kD * (1.0 - EatFraction(-1.0, kD, 1.0));
  for (int i = 0; i < 7; ++i) {
    const double kept = kD * (1.0 - EatFraction(gaps[i] / 1000.0, kD, 1.0));
    printf("  %-12.0f %-14.2f x%.0f\n", gaps[i], kept, base > 0 ? kept / base : 0);
  }
  return 0;
}
