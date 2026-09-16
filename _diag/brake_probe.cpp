// THE PROPORTIONAL BRAKE -- does it do what the requirement says?
//
//   g++ -std=c++17 -O2 -I../src -o brake_probe.cpp -o brake_probe && ./brake_probe
//
// The requirement: at the MAXIMUM setting, on ANY device, the brake must never empty a message --
// always leave something -- and it must do that WITHOUT a "keep at least 1 delta" floor (a floor is
// device-dependent). It must also stay device-independent and frame-rate independent.

#include "model.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

static const double kNotchDeltas = 120.0;
static const double kNotchUnits = 15.0;
static const double kDeltasPerUnit = kNotchDeltas / kNotchUnits; // 8

static int failures = 0;
static void Check(const char *what, bool ok, const char *detail)
{
  if (!ok) ++failures;
  printf("  %-56s %-4s %s\n", what, ok ? "ok" : "FAIL", detail);
}

// One message of `perDeltas`, through the model with brake fraction `f` (signed). Returns deltas out.
static double One(double perDeltas, double f, double Xms)
{
  model::Params P;
  P.windowMs = Xms;
  P.eatFrac = f;
  model::Axis g;
  g.Reset();
  g.Feed(perDeltas / kDeltasPerUnit, P); // in the model's units
  const double dt = 0.001;
  double out = 0;
  for (double t = 0; t < 8.0; t += dt)
  {
    out += g.Tick(dt, P);
    if (!g.Active())
      break;
  }
  return out * kDeltasPerUnit;
}

// A gesture: `totalDeltas` over `Tsec`, delivered in messages of `per`.
static double Roll(double totalDeltas, double Tsec, double per, double f, double Xms)
{
  model::Params P;
  P.windowMs = Xms;
  P.eatFrac = f;
  model::Axis g;
  g.Reset();
  const double dt = 0.001;
  const double nm = totalDeltas / per;
  const double gap = Tsec / nm;
  double out = 0, next = 0;
  int fed = 0;
  for (double t = 0; t < Tsec + Xms / 1000.0 + 1.0; t += dt)
  {
    while (fed < (int)(nm + 0.5) && next <= t + 1e-12)
    {
      g.Feed(per / kDeltasPerUnit, P);
      ++fed;
      next += gap;
    }
    out += g.Tick(dt, P);
  }
  return out * kDeltasPerUnit;
}

int main()
{
  const double fMax = model::EatFraction(9.0); // the slider at its maximum
  printf("kBrakeMaxFrac = %.4f -> the slider's top eats %.1f%% and leaves %.1f%%\n\n",
         100.0 * fMax, 100.0 * fMax, 100.0 * (1.0 - fMax));

  printf("== 1. at the MAXIMUM, nothing is ever emptied -- for ANY device and ANY window ==\n");
  {
    printf("   %-22s %-10s %-12s %-12s\n", "message", "window", "left after", "verdict");
    const double msgs[] = {120, 20, 4, 1};  // notched mouse ... a single delta
    const double wins[] = {50, 150, 400};
    bool allPositive = true;
    for (int m = 0; m < 4; ++m)
      for (int w = 0; w < 3; ++w)
      {
        const double left = One(msgs[m], fMax, wins[w]);
        const char *v = (left > 0.0) ? "ok" : "EMPTIED";
        if (!(left > 0.0))
          allPositive = false;
        printf("   %6.0f delta          %5.0f ms  %-12.6f %s\n", msgs[m], wins[w], left, v);
      }
    Check("every message keeps something, at every window", allPositive,
          "(including a 1-delta message at 400 ms)");
  }

  printf("\n== 2. device independence: the SAME motion in different message sizes ==\n");
  {
    printf("   %-22s %-16s %-12s\n", "message size", "kept % of motion", "vs notched");
    const double per[] = {120, 60, 20, 4, 1};
    double base = 0;
    bool same = true;
    for (int i = 0; i < 5; ++i)
    {
      const double out = Roll(1200, 3.0, per[i], fMax, 150.0);
      const double frac = out / 1200.0;
      if (i == 0)
        base = frac;
      else if (std::fabs(frac - base) > 1e-9)
        same = false;
      printf("   %6.0f delta          %-16.4f %-12.4f\n", per[i], frac, base > 0 ? frac / base : 0);
    }
    Check("every message size keeps the same fraction", same, "(a fraction, not a count)");
  }

  printf("\n== 3. Y = 0 is exact; Y never eats everything for the whole slider range ==\n");
  {
    Check("Y=0 leaves the whole notch", std::fabs(One(120, 0.0, 150.0) - 120.0) < 1e-6, "");
    bool neverZero = true;
    for (double y = 0.0; y <= 9.0; y += 0.5)
      if (!(One(120, model::EatFraction(y), 150.0) > 0.0))
        neverZero = false;
    Check("no slider value empties a notch", neverZero, "");
  }

  printf("\n== 4. the spit: proportional, bounded, and it DIES with the gesture ==\n");
  {
    // A fast flick (the spit is active), then the hand stops. 6 notches @25 ms.
    (void)0; // spit is a multiplier now
    const double num = model::EatFraction(9.0);
    // in = 6*120 = 720
    double out = 0, last = 0;
    {
      model::Params P;
      P.windowMs = 200.0;
      P.spitMul = model::SpitMultiplier(2.0); // spit: a multiplier of its own
      model::Axis g;
      g.Reset();
      const double dt = 0.001, gap = 0.025;
      double next = 0;
      int fed = 0;
      for (double t = 0; t < 30.0; t += dt)
      {
        while (fed < 6 && next <= t + 1e-12)
        {
          g.Feed(kNotchUnits, P);
          ++fed;
          next += gap;
        }
        const double s = g.Tick(dt, P);
        if (s != 0.0)
          last = (t + dt) * 1000.0;
        out += s;
        if (fed >= 6 && !g.Active())
          break;
      }
    }
    printf("   6 notches @25 ms (720 in) with the spit at max -> %.1f out (%.2fx), ended at %.0f ms\n",
           out * kDeltasPerUnit, out * kDeltasPerUnit / 720.0, last);
    Check("the spit adds travel", out * kDeltasPerUnit > 720.0, "");
    Check("the spit ends with the gesture (does not self-feed)", last < 2000.0, "");
    (void)num;
  }

  printf("\n== 5. frame-rate independence (the brake keeps the window's property) ==\n");
  {
    auto run = [fMax](double dtMs) {
      model::Params P;
      P.windowMs = 200.0;
      P.eatFrac = fMax;
      model::Axis g;
      g.Reset();
      g.Feed(kNotchUnits, P);
      const double dt = dtMs / 1000.0;
      double o = 0;
      for (double t = 0; t < 8.0; t += dt)
      {
        o += g.Tick(dt, P);
        if (!g.Active())
          break;
      }
      return o * kDeltasPerUnit;
    };
    const double a = run(1.0), b = run(4.0), c = run(15.6);
    printf("   dt=1ms %.4f | dt=4ms %.4f | dt=15.6ms %.4f deltas\n", a, b, c);
    Check("the kept amount is close across frame rates",
          std::fabs(a - b) < 0.5 && std::fabs(a - c) < 2.0, "");
  }

  printf("\n%s\n", failures ? "FAIL" : "OK: a fraction of the amount, never zero, same for every device");
  return failures ? 1 : 0;
}
