// CLIFF PROBE -- did the speed ramp (model::EatScale) remove the sudden take-off?
//
// It mirrors Kick's per-message decision EXACTLY (see smooth_wheel_scroll.cpp Kick):
//   scale   = model::EatScale(gapSec, 120)        (1 at >=200 ms/notch, 0 at <=50 ms/notch)
//   eatRate = R * scale                           (the slider's rate, faded by speed)
//   spitMul = V when scale == 0, else 1
// then feeds notches into the real model's Glide and measures the travel handed over.
//
// Run it with the OLD hard-switch rule and the NEW ramp to compare: the old one jumps by ~10x across
// one millisecond of gap; the ramp must not.
//
//   g++ -std=c++17 -O2 -I../src -o cliff_probe.exe cliff_probe.cpp && ./cliff_probe.exe
#include "model.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

static const double kDeltasPerNotch = 120.0;
static const double kUnitsPerNotch = 15.0;
static const double kDeltasPerUnit = kDeltasPerNotch / kUnitsPerNotch; // 8

struct Decision { bool spit; double eatRate; double spitMul; };

// The rule under test: `hard` = the old on/off switch, otherwise the CONTINUOUS ramp.
static Decision Decide(double gapMs, double Rrate, double V, bool hard)
{
  Decision d;
  if (hard)
  {
    const double speed = (gapMs > 0.0) ? (kDeltasPerNotch / gapMs) : 0.0;
    d.spit = (speed > 120.0 / 50.0);
    d.eatRate = d.spit ? 0.0 : Rrate;
    d.spitMul = d.spit ? V : 1.0;
  }
  else
  {
    const double gapSec = (gapMs > 0.0) ? (gapMs / 1000.0) : -1.0;
    const double scale = model::EatScale(gapSec, kDeltasPerNotch);
    d.spit = (scale <= 0.0);
    d.eatRate = Rrate * scale;
    d.spitMul = d.spit ? V : 1.0;
  }
  return d;
}

// Feed `n` notches at a fixed gap; return the travel actually handed over, in deltas.
// Mirrors Kick: the decision made by the LAST message is what Tick uses for the whole in-flight pool.
static double Travel(int n, double gapMs, double Xms, double Rrate, double V, bool hard){
  model::Axis g;
  g.Reset();
  const double dt = 0.0005; // 0.5 ms frames
  double outUnits = 0.0, next = 0.0;
  int f = 0;
  Decision cur = Decide(0.0, Rrate, V, hard); // first message of a gesture has no previous one: slow
  for (double t = 0.0; t < 30.0; t += dt)
  {
    while (f < n && next <= t + 1e-12)
    {
      cur = Decide(f == 0 ? 0.0 : gapMs, Rrate, V, hard);
      model::Params P;
      P.windowMs = Xms;
      P.eatRatePerMs = cur.eatRate / kDeltasPerUnit;
      P.spitMul = cur.spitMul;
      g.Feed(kUnitsPerNotch, P);
      ++f;
      next += gapMs / 1000.0;
    }
    model::Params Pt;
    Pt.windowMs = Xms;
    Pt.eatRatePerMs = cur.eatRate / kDeltasPerUnit;
    Pt.spitMul = cur.spitMul;
    outUnits += g.Tick(dt, Pt);
    if (f >= n && !g.Active())
      break;
  }
  return outUnits * kDeltasPerUnit;
}

int main()
{
  const double X = 100.0, R = 15.0, V = 1.0;
  printf("X=%.0f ms  Resistance R=%.0f deltas/ms (the slider IS the rate)  V=%.2f\n", X, R, V);
  printf("eat ramp: 1 at >=200 ms/notch, 0 at <=50 ms/notch, continuous in between\n\n");

  printf("== per-notch travel as the roll speeds up, OLD hard switch vs NEW ramp ==\n");
  printf("  %-8s %-8s %-16s %-16s %s\n", "gap ms", "speed", "travel/notch OLD", "travel/notch NEW",
         "mode(NEW)");
  for (double gap : {200.0, 150.0, 120.0, 100.0, 80.0, 60.0, 55.0, 51.0, 50.0, 49.0, 45.0, 40.0})
  {
    const double oldTr = Travel(20, gap, X, R, V, true) / 20.0;
    const double newTr = Travel(20, gap, X, R, V, false) / 20.0;
    const Decision d = Decide(gap, R, V, false);
    printf("  %-8.0f %-8.2f %-16.2f %-16.2f %s\n", gap, kDeltasPerNotch / gap, oldTr, newTr,
           d.spit ? "SPIT" : "eat");
  }

  printf("\n== the step across the old crossover, OLD vs NEW (X=100, per R) ==\n");
  printf("  %-8s %-12s %-12s %-10s %-12s %-12s %s\n", "R d/ms", "51ms OLD", "49ms OLD", "stepOLD",
         "51ms NEW", "49ms NEW", "stepNEW");
  for (double Rv : {2.0, 5.0, 8.0, 11.0, 15.0})
  {
    const double oa = Travel(20, 51.0, X, Rv, V, true) / 20.0;
    const double ob = Travel(20, 49.0, X, Rv, V, true) / 20.0;
    const double na = Travel(20, 51.0, X, Rv, V, false) / 20.0;
    const double nb = Travel(20, 49.0, X, Rv, V, false) / 20.0;
    printf("  %-8.0f %-12.2f %-12.2f %-10.1fx %-12.2f %-12.2f %.2fx\n", Rv, oa, ob,
           (oa > 0.0) ? ob / oa : 0.0, na, nb, (na > 0.0) ? nb / na : 0.0);
  }
  return 0;
}
