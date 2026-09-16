// CONCRETE TRACE: 10 notches rolled within 200 ms, easing D = 300 ms.
// What does the action actually receive, in 10 ms buckets?
//
//   g++ -std=c++17 -O2 -I../src -o trace_roll.cpp trace_roll.cpp
//
// Setup: one notch every 20 ms starting at t = 0, so the 10th notch lands at t = 180 ms
// (the whole roll spans ~200 ms). Easing window D = 300 ms, tick = 1 ms, delivery on REAPER's
// 1/256-unit grid with the remainder carried. Every call the plugin makes is counted and its
// amount printed. bend = 0.5 is the panel default.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace anim3;
static const double kU = 15.0;
static const int kNotches = 10;
static const double kGapMs = 20.0;

struct Out { double amt; int calls; };

static void trace(double Dms, double bend) {
  Params P; P.durationMs = Dms; P.shape = kSymS; P.bend = bend;
  Glide g; g.Reset();
  const double dt = 0.001, grid = 1.0 / 256.0;
  const double gap = kGapMs / 1000.0;
  double nextMsg = 0, carry = 0, total = 0;
  int fed = 0;
  const int buckets = 56;               // 0..560 ms, in 10 ms slices
  Out per[64] = {};
  double bucketSum = 0; int bucketCalls = 0; int bucketEnd = 10;

  for (int i = 0; i <= buckets * 10; ++i) {
    const double t = i * dt;
    while (fed < kNotches && nextMsg <= t + 1e-9) { g.Feed(1.0, P); ++fed; nextMsg += gap; }
    const double want = g.Tick(dt, P) * kU;
    const double tot = carry + want;
    const double m = (tot < 0) ? -std::floor(-tot / grid + 0.5) : std::floor(tot / grid + 0.5);
    const double deliv = m * grid;
    carry = tot - deliv;
    total += deliv;
    if (std::fabs(deliv) > 1e-12) { ++bucketCalls; bucketSum += deliv; }
    if (i + 1 == bucketEnd) {
      per[bucketEnd / 10 - 1].amt = bucketSum;
      per[bucketEnd / 10 - 1].calls = bucketCalls;
      bucketSum = 0; bucketCalls = 0; bucketEnd += 10;
    }
  }

  printf("=== D = %.0f ms, bend = %.1f%s ===\n", Dms, bend,
         std::fabs(bend - 0.5) < 1e-9 ? "  (panel default)" : "");
  printf("  %-12s %-10s %-14s %-14s %s\n", "time", "notch in?",
         "pieces delivered", "amount (units)", "amount (notches)");
  for (int b = 0; b < 40; ++b) {
    const int t0 = b * 10, t1 = t0 + 10;
    // notches fed in this bucket: times 0,20,...,180
    int fedHere = 0;
    for (int k = 0; k < kNotches; ++k) {
      const int tk = (int)(k * kGapMs);
      if (tk >= t0 && tk < t1) ++fedHere;
    }
    const Out &o = per[b];
    if (t0 > 400 && o.calls == 0 && o.amt == 0 && fedHere == 0) continue;
    printf("  %4d-%4d ms %-10d %-14d %-14.4f %.4f\n", t0, t1, fedHere, o.calls, o.amt,
           o.amt / kU);
  }
  printf("  TOTAL delivered = %.4f units = %.4f notches (10 went in)\n\n", total, total / kU);
}

int main() {
  printf("Roll: %d notches, one every %.0f ms (t=0 .. %.0f ms). Tick 1 ms.\n",
         kNotches, kGapMs, (kNotches - 1) * kGapMs);
  printf("'pieces' = how many separate calls the plugin makes to the action in that 10 ms.\n\n");
  trace(300.0, 0.5);
  trace(300.0, 0.0);
  return 0;
}
