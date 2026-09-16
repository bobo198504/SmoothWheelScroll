// CLIFF PROBE 2 -- the pool is shared: does one fast notch move the whole in-flight pool?
//
// In Kick the brake is stored per gesture (`g.eatRate`), and Tick applies THAT rate to the WHOLE
// in-flight pool. So the LAST message's speed governs every window still open -- including ones
// opened by slow notches a moment earlier. This probe rolls slow (60 ms/notch, eaten), slips in a
// single fast notch (40 ms, no eat), and reports the travel handed over between notch arrivals.
//
// (This sharing is BY DESIGN in the model; the speed ramp -- model::EatScale -- is what keeps a
// slightly-fast message from switching it off outright.)
//
//   g++ -std=c++17 -O2 -I../src -o cliff2.exe cliff2_probe.cpp && ./cliff2.exe
#include "model.h"
#include <cstdio>
#include <cmath>

static const double kDeltasPerNotch = 120.0;
static const double kUnitsPerNotch = 15.0;
static const double kDeltasPerUnit = kDeltasPerNotch / kUnitsPerNotch;
static const double X = 100.0, RATE = 8.0, V = 1.0; // RATE = the Resistance slider, deltas/ms

struct Decision { bool spit; double eatRate, spitMul; };
static Decision Decide(double gapMs)
{
  const double gapSec = (gapMs > 0.0) ? (gapMs / 1000.0) : -1.0;
  const double scale = model::EatScale(gapSec, kDeltasPerNotch);
  Decision d;
  d.spit = (scale <= 0.0);
  d.eatRate = RATE * scale;
  d.spitMul = d.spit ? V : 1.0;
  return d;
}

int main()
{
  printf("X=%.0f ms  Resistance=%.0f deltas/ms  V=%.2f   eat ramp: 1 at >=200 ms/notch, 0 at <=50\n",
         X, RATE, V);
  printf("(a lone slow notch is heavily eaten; a 40 ms notch is past the ramp and not eaten at all)\n\n");

  const int kTotal = 16;
  const int kFast = 7; // 0-based: notch #8 is 40 ms, the rest 60 ms
  const double dt = 0.0005;

  model::Axis g;
  g.Reset();
  int f = 0;
  double next = 0.0, t = 0.0, total = 0.0;
  double atArrival[64];
  for (int i = 0; i < kTotal; ++i) atArrival[i] = 0.0;
  Decision last = Decide(0.0);

  while (f < kTotal || g.Active())
  {
    if (f < kTotal && next <= t + 1e-12)
    {
      const double gap = (f == kFast) ? 40.0 : 60.0;
      const Decision d = Decide(f == 0 ? 0.0 : gap);
      model::Params P; P.windowMs = X;
      P.eatRatePerMs = d.eatRate / kDeltasPerUnit;
      P.spitMul = d.spitMul;
      g.Feed(kUnitsPerNotch, P);
      atArrival[f] = total; // output produced up to the instant this notch arrived
      last = d;
      ++f;
      next += gap / 1000.0;
    }
    model::Params P; P.windowMs = X;
    P.eatRatePerMs = last.eatRate / kDeltasPerUnit; // the LAST message governs the whole pool
    P.spitMul = last.spitMul;
    total += g.Tick(dt, P) * kDeltasPerUnit;
    t += dt;
    if (t > 20.0) break;
  }

  printf("  %-6s %-8s %-6s %-16s %s\n", "notch", "gap ms", "mode", "travel in window", "note");
  for (int i = 0; i < kTotal; ++i)
  {
    const double gap = (i == kFast) ? 40.0 : 60.0;
    const Decision d = Decide(i == 0 ? 0.0 : gap);
    const double prev = (i == 0) ? 0.0 : atArrival[i - 1];
    printf("  %-6d %-8.0f %-6s %-16.2f %s\n", i + 1, gap, d.spit ? "SPIT" : "eat",
           atArrival[i] - prev, i == kFast ? "<-- the one fast notch" : "");
  }
  printf("  (tail after the last notch: %.2f)\n", total - atArrival[kTotal - 1]);
  return 0;
}
