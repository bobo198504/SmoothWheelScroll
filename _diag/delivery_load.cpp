// The DELIVERY LAYER's current numbers: how often does the plugin actually call the action?
//
//   g++ -std=c++17 -O2 -I../src -o delivery_load.cpp delivery_load.cpp
//
// Chain: Kick -> Glide::Feed (total) -> 1 ms tick -> resistance eats -> easing pays ->
//        DeliverTravel quantises to ONE DELTA with a carry -> ReplayAction.
//
// The delivery can only send whole deltas. If the model asks for less than half a delta in a frame,
// nothing goes out that frame; the amount is carried. So the real call rate is lower than 1000/s,
// and it depends on the roll speed and on Fineness.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace anim3;
static const double kDPU = 8.0;   // deltas per 7-bit unit
static const double kStep = 1.0 / kDPU; // one delta, in units

struct R { long calls; double deltasOut, deltasIn; double maxQuietMs, gaps; };

// n notches, one every gapMs; Fineness = easeMs; Resistance = resist (deltas per 1 ms).
static R run(int n, double gapMs, double easeMs, double resist)
{
  Params P;
  P.durationMs = easeMs;
  P.takePerSec = (resist * 1000.0) / kDPU;
  P.snap = (1.0 / kDPU) * 0.5;
  Glide g; g.Reset();
  const double dt = 0.001, gap = gapMs / 1000.0;
  double accum = 0, out = 0, in = 0, msgTime = 0, quiet = 0;
  int fed = 0; long calls = 0;
  R r{0, 0, 0, 0, 0};
  for (double t = 0; t < n * gap + 3.0; t += dt) {
    if (fed < n && msgTime <= t + 1e-9) { g.Feed(15.0); in += 120.0; ++fed; msgTime += gap; }
    if (!g.Active() && fed >= n) break;
    accum += g.Tick(dt, P);
    const double m = floor(fabs(accum) / kStep + 0.5);
    if (m >= 1.0) {
      const double s = m * kStep;
      accum -= (accum < 0 ? -s : s);
      out += s * kDPU; ++calls; quiet = 0;
    } else {
      quiet += 1.0;
      if (quiet > r.maxQuietMs) r.maxQuietMs = quiet;
    }
  }
  r.calls = calls; r.deltasOut = out; r.deltasIn = in;
  return r;
}

int main()
{
  printf("one notch = 120 deltas. delivery step = 1 delta. tick = 1 ms.\n");
  printf("'calls' is how many times the action is invoked.\n\n");

  printf("=== calls per second, one notch every `gap` ms, 20 notches, Fineness 500 / Resistance 1 ===\n");
  printf("  %-14s %-10s %-12s %-12s %s\n", "gap/notch", "notches/s", "calls", "calls/s", "max quiet");
  const double gaps[] = {500, 300, 200, 100, 50, 20, 10};
  for (int i = 0; i < 7; ++i) {
    const int n = 20;
    const R r = run(n, gaps[i], 500.0, 1.0);
    const double span = n * gaps[i] / 1000.0;
    printf("  %-14.0f %-10.1f %-12ld %-12.0f %.0f ms\n", gaps[i], 1000.0 / gaps[i], r.calls,
           r.calls / span, r.maxQuietMs);
  }

  printf("\n=== the same at Fineness 50 (the blunt end) ===\n");
  for (int i = 0; i < 7; ++i) {
    const int n = 20;
    const R r = run(n, gaps[i], 50.0, 1.0);
    const double span = n * gaps[i] / 1000.0;
    printf("  %-14.0f %-10.1f %-12ld %-12.0f %.0f ms\n", gaps[i], 1000.0 / gaps[i], r.calls,
           r.calls / span, r.maxQuietMs);
  }

  printf("\n=== and the call rate for ONE slow notch, over time (Fineness 500) ===\n");
  {
    const R r = run(1, 5000.0, 500.0, 1.0);
    printf("  one notch sent in %ld calls (a native wheel sends it in 1)\n", r.calls);
    printf("  .bss/.text aside, that is %ld deliveries for one click.\n", r.calls);
  }
  return 0;
}
