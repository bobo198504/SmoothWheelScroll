// TRACE the actual delivery sequence for a STEADY slow roll: does a notch-by-notch output show a
// "nothing ... nothing ... JUMP" pattern (accumulation), or a smooth rise (the eating curve)?
//
//   g++ -std=c++17 -O2 -I../src -o jump_trace.cpp jump_trace.cpp
//
// Both mechanisms can look like "suddenly speeds up", so this separates them by printing every
// send, the notch number it happened on, and both delivery paths (fine stream / whole unit).

#include "anim3_core.h"
#include <cstdio>
#include <cmath>

using namespace anim3;
static const double kD = 120.0, kU = 15.0;
static const double kStepDeltas = kD / kU;
static const double kStreamStep = 1.0 / kStepDeltas; // 1 delta
static const double kUnitStep = 1.0;                 // 1 unit

static void trace(const char *label, double step, double easeMs, double kinetic, double gapMs, int n)
{
  Params P; P.durationMs = easeMs;
  Glide g; g.Reset();
  const double gap = gapMs / 1000.0;
  double accum = 0, msgTime = 0;
  int fed = 0;
  double sentOnNotch[64] = {0};
  printf("=== %s : ease %.0f ms, kinetic %.0f, gap %.0f ms, %d notches ===\n",
         label, easeMs, kinetic, gapMs, n);
  double t = 0;
  const double end = n * gap + easeMs / 1000.0 + 0.05;
  for (; t < end; t += 0.001) {
    if (fed < n && msgTime <= t + 1e-9) {
      const double gapSec = (fed == 0) ? -1.0 : gap;
      const double kept = kD * (1.0 - EatFraction(gapSec, kD, kinetic));
      g.Feed(kept / kStepDeltas, P);
      ++fed; msgTime += gap;
    }
    if (g.Active()) {
      accum += g.Tick(0.001, P);
      const double m = floor(fabs(accum) / step + 0.5);
      if (m >= 1.0) {
        const double s = m * step;
        accum -= (accum < 0 ? -s : s);
        if (fed >= 1 && fed <= n) sentOnNotch[fed] += s * kStepDeltas;
      }
    }
  }
  printf("  notch : sent (deltas)\n");
  for (int k = 1; k <= n; ++k)
    printf("  %5d : %7.3f %s\n", k, sentOnNotch[k],
           std::fabs(sentOnNotch[k]) > 0.001 ? "" : "(nothing)");
  printf("\n");
}

int main()
{
  printf("If a notch sends NOTHING and a later notch sends a lot, that is ACCUMULATION.\n");
  printf("If every notch sends about the same, the 'speed up' is the EATING CURVE.\n\n");
  trace("whole-unit (vertical), slow", kUnitStep, 20, 1, 300, 16);
  trace("whole-unit (vertical), slow", kUnitStep, 20, 12, 300, 16);
  trace("fine stream (horizontal), slow", kStreamStep, 20, 1, 300, 16);
  trace("whole-unit, faster roll", kUnitStep, 20, 1, 100, 16);
  return 0;
}
