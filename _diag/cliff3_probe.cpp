// CLIFF PROBE 3 -- how a multi-notch message is read.
//
// The speed is (this message's deltas) / (time since the previous message), so a message carrying
// more than one notch (val > 15 -- a coarse device step, or messages coalesced) is read as
// proportionally faster. Under the OLD hard switch that alone could cross the line; under the ramp
// (model::EatScale) it only fades the eat a step further. This probe shows both a lone fast notch and
// a chunky message, and reports the travel handed over between arrivals.
//
//   g++ -std=c++17 -O2 -I../src -o cliff3.exe cliff3_probe.cpp && ./cliff3.exe
#include "model.h"
#include <cstdio>
#include <cmath>

static const double kDeltasPerNotch = 120.0;
static const double kUnitsPerNotch = 15.0;
static const double kDeltasPerUnit = kDeltasPerNotch / kUnitsPerNotch;
static const double X = 100.0, RATE = 8.0, V = 1.0; // RATE = the Resistance slider, deltas/ms

struct Decision { bool spit; double eatRate, spitMul; };
// `valUnits` = the val field's units (15 = one notch). `gapMs` = time since the previous message.
static Decision Decide(double gapMs, double valUnits)
{
  const double deltas = (valUnits / kUnitsPerNotch) * kDeltasPerNotch; // notches * 120
  const double gapSec = (gapMs > 0.0) ? (gapMs / 1000.0) : -1.0;
  const double scale = model::EatScale(gapSec, deltas);
  Decision d;
  d.spit = (scale <= 0.0);
  d.eatRate = RATE * scale;
  d.spitMul = d.spit ? V : 1.0;
  return d;
}

// Run a scripted roll: a list of (gapMs, valUnits). Print travel produced between arrivals.
struct Step { double gapMs, valUnits; };

static void Run(const char *title, const Step *s, int n)
{
  printf("== %s ==\n", title);
  model::Axis g; g.Reset();
  Decision last = Decide(0.0, kUnitsPerNotch);
  const double dt = 0.0005;
  double t = 0, next = 0, total = 0;
  double atArrival[64];
  for (int i = 0; i < n; ++i) atArrival[i] = 0.0;
  int f = 0;
  while (f < n || g.Active())
  {
    if (f < n && next <= t + 1e-12)
    {
      const Decision d = Decide(f == 0 ? 0.0 : s[f].gapMs, s[f].valUnits);
      model::Params P; P.windowMs = X;
      P.eatRatePerMs = d.eatRate / kDeltasPerUnit;
      P.spitMul = d.spitMul;
      g.Feed(s[f].valUnits, P);
      atArrival[f] = total;
      last = d;
      f++;
      next += s[f - 1].gapMs / 1000.0;
    }
    model::Params P; P.windowMs = X;
    P.eatRatePerMs = last.eatRate / kDeltasPerUnit;
    P.spitMul = last.spitMul;
    total += g.Tick(dt, P) * kDeltasPerUnit;
    t += dt;
    if (t > 30.0) break;
  }
  printf("  %-5s %-8s %-6s %-6s %-14s %s\n", "#", "gap", "val", "mode", "travel", "note");
  for (int i = 0; i < n; ++i)
  {
    const Decision d = Decide(i == 0 ? 0.0 : s[i].gapMs, s[i].valUnits);
    const double prev = (i == 0) ? 0.0 : atArrival[i - 1];
    printf("  %-5d %-8.0f %-6.0f %-6s %-14.2f %s\n", i + 1, s[i].gapMs, s[i].valUnits,
           d.spit ? "SPIT" : "eat", atArrival[i] - prev,
           (s[i].valUnits != kUnitsPerNotch) ? "multi-notch message" : "");
  }
  printf("  (tail after the last: %.2f)\n\n", total - atArrival[n - 1]);
}

int main()
{
  printf("X=%.0f ms  Resistance=%.0f d/ms  V=%.2f  eat ramp: 1 at >=200 ms/notch, 0 at <=50\n\n",
         X, RATE, V);

  Step a[] = {
      {200, 15}, {200, 15}, {200, 15}, {45, 15}, {200, 15}, {200, 15}, {200, 15}, {200, 15}};
  Run("(a) slow 200 ms/notch, ONE notch at 45 ms", a, 8);

  Step b[] = {
      {180, 15}, {180, 15}, {90, 30}, {180, 15}, {180, 15}, {180, 15}};
  Run("(b) slow 180 ms/notch, ONE message carrying 2 notches (val=30)", b, 6);
  return 0;
}
