// WHY does the proportional brake feel "slow at first, then suddenly faster"? Measure it.
//
//   g++ -std=c++17 -O2 -I../src -o brake_step.exe brake_step.cpp && ./brake_step.exe
//
// A steady roll at each speed, and the amount ONE NOTCH hands over at that speed. If the brake
// changes only its SIGN at the crossover, the delivered amount must JUMP there -- that jump is the
// "sudden" the user feels. This runs the real model per speed, so the numbers are the real ones.

#include "model.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

static const double kDeltasPerNotch = 120.0;
static const double kNotchUnits = 15.0;
static const double kDeltasPerUnit = 8.0;
static const double kEatStopSpeed = 120.0 / 50.0; // 2.4 deltas/ms

// The plugin's decision for one message (mirrors Kick).
struct B { double eatFrac, spitDeltasPerMs; bool spit; };
static B BrakeFor(double gapMs, double Yslider, double Vslider)
{
  const double speed = kDeltasPerNotch / gapMs;
  const bool spit = (speed > kEatStopSpeed);
  B b; b.spit = spit;
  b.eatFrac = spit ? 0.0 : model::EatFraction(Yslider);
  b.spitDeltasPerMs = spit ? Vslider : 0.0;
  return b;
}

// Steady roll at `gapMs`: many notches; report the deltas delivered PER NOTCH (excluding the ramp
// in and out, so this is the settled value).
static double PerNotch(double gapMs, double Yslider, double Vslider, double Xms)
{
  model::Params P;
  P.windowMs = Xms;
  const B b = BrakeFor(gapMs, Yslider, Vslider);
  P.eatFrac = b.eatFrac; P.spitPerMs = model::SpitToModelRate(b.spitDeltasPerMs, kDeltasPerUnit);
  model::Axis g;
  g.Reset();
  const double dt = 0.001, gap = gapMs / 1000.0;
  const int n = 60;
  double out = 0, next = 0;
  int fed = 0;
  const double from = 20 * gap, to = 40 * gap; // measure the settled middle
  for (double t = 0; t < 8.0; t += dt)
  {
    while (fed < n && next <= t + 1e-12)
    {
      g.Feed(kNotchUnits, P);
      ++fed;
      next += gap;
    }
    const double s = g.Tick(dt, P);
    if (t >= from && t < to)
      out += s;
    if (fed >= n && !g.Active())
      break;
  }
  return out * kDeltasPerUnit / 20.0; // 20 notches were fed in the measured window
}

int main()
{
  const double X = 150.0;
  printf("X = %.0f ms. Per notch (=120 deltas) at each hand speed, one row per brake setting.\n", X);
  printf("'gone %%' = how much of the 120 came out. Watch for a JUMP at the crossover.\n\n");

  const double gaps[] = {400, 300, 200, 150, 120, 100, 80, 60, 50, 45, 40, 35, 30, 25, 20};
  const int ng = (int)(sizeof(gaps) / sizeof(gaps[0]));

  for (double Y : {2.0, 5.0}) // V at its default 9, so the spit is uncapped below it
  {
    printf("  Y = %.1f, V = 9.0\n", Y);
    printf("  %-10s %-10s %-8s %-12s %-8s\n", "gap ms", "speed", "brake", "out/notch", "gone %");
    double prev = -1;
    bool jumped = false;
    for (int i = 0; i < ng; ++i)
    {
      const double speed = kDeltasPerNotch / gaps[i];
      const B b = BrakeFor(gaps[i], Y, 9.0);
      const double per = PerNotch(gaps[i], Y, 9.0, X);
      const bool spit = b.spit;
      printf("  %-10.0f %-10.2f %-8s %-12.1f %-8.1f\n", gaps[i], speed, spit ? "SPIT" : "eat", per,
             100.0 * per / 120.0);
      if (prev >= 0 && per > 3.0 * prev && prev > 0.0)
        jumped = true;
      prev = per;
    }
    printf("  -> a jump bigger than 3x between neighbouring speeds: %s\n\n",
           jumped ? "YES (this is the 'sudden')" : "no");
  }

  printf("  The crossover is at %.2f deltas/ms (between 50 and 45 ms per notch).\n", kEatStopSpeed);
  printf("  On the eat side the brake takes Y of every notch; on the spit side it ADDS Y.\n");
  printf("  So the delivered amount has to step from (1-Y) to (1+Y) there -- unless the brake\n");
  printf("  changes with speed smoothly instead of flipping sign.\n");
  return 0;
}
