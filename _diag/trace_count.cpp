// THE COUNT, not the amount. 10 notches rolled within 200 ms, easing D = 300 ms.
//
//   g++ -std=c++17 -O2 -I../src -o trace_count.cpp trace_count.cpp
//
// Two different things get called "a piece", and the user is asking for the COUNT of them:
//
//   CALLS  = how many times the plugin calls the action in that 10 ms. Each call carries some
//            amount; the amount is a real number, but the CALL is one event.
//   UNITS  = how many who 7-bit units were handed over, i.e. how many times the delivery grid
//            (1/256 of a unit) advanced. This is the finest "piece" REAPER can express.
//
// Both are integers per 10 ms bucket, so the answer has no decimal point.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace anim3;
static const double kU = 15.0;
static const int kNotches = 10;
static const double kGapMs = 20.0;

static void trace(double Dms, double bend) {
  Params P; P.durationMs = Dms; P.shape = kSymS; P.bend = bend;
  Glide g; g.Reset();
  const double dt = 0.001, grid = 1.0 / 256.0;
  const double gap = kGapMs / 1000.0;
  double nextMsg = 0, carry = 0;
  int fed = 0;

  const int kBuckets = 50;
  int calls[kBuckets] = {0};      // number of calls to the action per bucket
  int subSteps[kBuckets] = {0};    // number of 1/256-unit grid advances per bucket
  double amt[kBuckets] = {0};      // amount (units) per bucket
  int b = 0;
  int callsHere = 0, stepsHere = 0; double sumHere = 0;
  int callsTotal = 0; long stepsTotal = 0; double grand = 0;

  for (int i = 0; i <= kBuckets * 10; ++i) {
    const double t = i * dt;
    while (fed < kNotches && nextMsg <= t + 1e-9) { g.Feed(1.0, P); ++fed; nextMsg += gap; }
    const double want = g.Tick(dt, P) * kU;
    const double tot = carry + want;
    const double m = (tot < 0) ? -std::floor(-tot / grid + 0.5) : std::floor(tot / grid + 0.5);
    const double deliv = m * grid;
    carry = tot - deliv;
    if (std::fabs(deliv) > 1e-12) {
      ++callsHere;
      ++callsTotal;
      stepsHere += (int)std::llround(std::fabs(m)); // how many 1/256-unit grid steps
      stepsTotal += (long)std::llround(std::fabs(m));
      sumHere += std::fabs(deliv);
      grand += std::fabs(deliv);
    }
    if (i + 1 == (b + 1) * 10) {
      calls[b] = callsHere; subSteps[b] = stepsHere; amt[b] = sumHere;
      ++b; callsHere = 0; stepsHere = 0; sumHere = 0;
    }
  }

  printf("=== D = %.0f ms, bend = %.1f ===\n", Dms, bend);
  printf("  %-12s %-8s %-8s %-12s %s\n", "time", "notches", "CALLS", "grid steps", "amount(u)");
  for (int i = 0; i < kBuckets; ++i) {
    const int t0 = i * 10, t1 = t0 + 10;
    int fedHere = 0;
    for (int k = 0; k < kNotches; ++k) { const int tk = k * (int)kGapMs; if (tk >= t0 && tk < t1) ++fedHere; }
    if (t0 > 420 && calls[i] == 0 && subSteps[i] == 0 && fedHere == 0) continue;
    printf("  %4d-%4d ms %-8d %-8d %-12d %.4f\n", t0, t1, fedHere, calls[i], subSteps[i], amt[i]);
  }
  printf("  TOTAL: %d calls, %ld grid steps, %.4f units (10 notches = 150 units)\n\n",
         callsTotal, stepsTotal, grand);
}

int main() {
  printf("Roll: %d notches, one every %.0f ms (t = 0 .. %.0f ms). Tick = 1 ms.\n",
         kNotches, kGapMs, (kNotches - 1) * kGapMs);
  printf("1 notch = 120 raw wheel deltas = 15 of REAPER's 7-bit units.\n");
  printf("Delivery grid = 1/256 unit, so one unit = 256 grid steps; 1 notch = 3840 grid steps.\n\n");
  trace(300.0, 0.5);
  trace(300.0, 0.0);
  return 0;
}
