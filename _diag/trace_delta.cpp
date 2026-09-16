// IN THE USER'S OWN UNIT: raw wheel deltas, where one notch = 120.
//
//   g++ -std=c++17 -O2 -I../src -o trace_delta.cpp trace_delta.cpp
//
// The confusion this settles: I reported a count in the FINEST step REAPER can express, which is
// 1/256 of a 7-bit unit = 32x finer than one raw delta. That is why "10 notches" came out as
// 38400 instead of the expected 1200. Both numbers are correct, in different units:
//
//   1 notch  = 120 raw deltas = 15 of REAPER's 7-bit units = 3840 finest steps
//   10 notch = 1200 raw deltas = 150 units                  = 38400 finest steps     (32x)
//
// Below, everything is in RAW DELTAS, so "10 notches = 1200" holds. Roll: 10 notches, one every
// 20 ms (t = 0 .. 180 ms); easing D = 300 ms; tick 1 ms; bend = 0.5 (panel default).

#include "anim3_core.h"
#include <cstdio>
#include <cmath>

using namespace anim3;
static const double kDeltasPerNotch = 120.0;
static const double kUnitsPerNotch = 15.0;
static const double kDeltasPerUnit = kDeltasPerNotch / kUnitsPerNotch; // = 8
static const int kNotches = 10;
static const double kGapMs = 20.0;

static void trace(double Dms, double bend) {
  Params P; P.durationMs = Dms; P.shape = kSymS; P.bend = bend;
  Glide g; g.Reset();
  const double dt = 0.001, grid = 1.0 / 256.0; // delivery grid, in 7-bit units
  const double gap = kGapMs / 1000.0;
  double nextMsg = 0, carry = 0;
  int fed = 0;

  const int kBuckets = 50;
  double delta[kBuckets] = {0}; // raw deltas delivered per 10 ms
  int calls[kBuckets] = {0};
  int b = 0; double sumHere = 0; int callsHere = 0;
  double grand = 0; int callsTotal = 0;

  for (int i = 0; i <= kBuckets * 10; ++i) {
    const double t = i * dt;
    while (fed < kNotches && nextMsg <= t + 1e-9) { g.Feed(1.0, P); ++fed; nextMsg += gap; }
    const double want = g.Tick(dt, P) * kUnitsPerNotch; // 7-bit units
    const double tot = carry + want;
    const double m = (tot < 0) ? -std::floor(-tot / grid + 0.5) : std::floor(tot / grid + 0.5);
    const double delivU = m * grid;                     // 7-bit units
    carry = tot - delivU;
    if (std::fabs(delivU) > 1e-12) {
      sumHere += std::fabs(delivU) * kDeltasPerUnit;    // -> raw deltas
      ++callsHere;
    }
    grand += std::fabs(delivU) * kDeltasPerUnit;
    if (i + 1 == (b + 1) * 10) {
      delta[b] = sumHere; calls[b] = callsHere;
      callsTotal += callsHere;
      ++b; sumHere = 0; callsHere = 0;
    }
  }

  printf("=== D = %.0f ms, bend = %.1f%s ===\n", Dms, bend,
         std::fabs(bend - 0.5) < 1e-9 ? "  (panel default)" : "");
  printf("  %-13s %-9s %-14s %-16s %s\n", "time", "clicks", "deltas given", "whole deltas", "calls");
  double cum = 0;
  for (int i = 0; i < kBuckets; ++i) {
    const int t0 = i * 10, t1 = t0 + 10;
    int fedHere = 0;
    for (int k = 0; k < kNotches; ++k) { const int tk = k * (int)kGapMs; if (tk >= t0 && tk < t1) ++fedHere; }
    if (t0 > 470 && delta[i] == 0 && calls[i] == 0) continue;
    const double before = cum;
    cum += delta[i];
    printf("  %4d-%4d ms %-9d %-14.2f %-16.0f %d\n", t0, t1, fedHere, delta[i],
           std::floor(cum) - std::floor(before), calls[i]);
  }
  printf("  TOTAL = %.2f deltas (= %.0f expected: 10 notches x 120)   |  %d calls total\n\n",
         grand, kNotches * kDeltasPerNotch, callsTotal);
}

int main() {
  printf("ONE notch = 120 raw deltas = 15 seven-bit units = 3840 finest steps.\n");
  printf("So '10 notches' = 1200 deltas, NOT 38400 -- 38400 was the 32x-finer step count.\n");
  printf("Roll: %d notches, one every %.0f ms (t = 0 .. %.0f ms). Tick 1 ms.\n\n",
         kNotches, kGapMs, (kNotches - 1) * kGapMs);
  trace(300.0, 0.5);
  trace(300.0, 0.0);
  return 0;
}
