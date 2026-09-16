// THE EAT SIDE ON ITS OWN (the acceleration is disabled). Is its response flat, or does it jump?
//
//   g++ -std=c++17 -O2 -I../src -o eat_curve.exe eat_curve.cpp && ./eat_curve.exe
//
// If eating a fixed FRACTION of every message is the rule, then every speed hands over the same
// amount PER NOTCH: 120*(1-Y). That is a FLAT response -- the output rate simply follows the hand.
// This runs the real model at each speed and prints it, plus a simulated acceleration ramp so the
// output can be read over time rather than only in steady state.

#include "model.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

static const double kDeltasPerNotch = 120.0;
static const double kNotchUnits = 15.0;
static const double kDeltasPerUnit = 8.0;

// Settled deltas handed over per notch at this hand speed (spit off: brake is +fraction).
static double PerNotch(double gapMs, double Yslider, double Xms)
{
  model::Params P;
  P.windowMs = Xms;
  P.eatFrac = model::EatFraction(Yslider);
  model::Axis g;
  g.Reset();
  const double dt = 0.001, gap = gapMs / 1000.0;
  const int n = 60;
  double out = 0, next = 0;
  int fed = 0;
  const double from = 20 * gap, to = 40 * gap;
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
  return out * kDeltasPerUnit / 20.0;
}

int main()
{
  printf("EAT ONLY (acceleration disabled). X = 150 ms.\n");
  printf("per notch out of 120, at each hand speed:\n\n");
  const double gaps[] = {400, 300, 200, 150, 120, 100, 80, 60, 50, 40, 30, 25, 20};
  const int ng = (int)(sizeof(gaps) / sizeof(gaps[0]));

  printf("  %-8s %-8s", "gap ms", "speed");
  for (double Y : {1.0, 3.0, 5.0, 9.0})
    { char h[16]; snprintf(h,sizeof(h),"Y=%.0f",Y); printf(" %10s", h); }
  printf("\n");
  for (int i = 0; i < ng; ++i)
  {
    printf("  %-8.0f %-8.2f", gaps[i], kDeltasPerNotch / gaps[i]);
    for (double Y : {1.0, 3.0, 5.0, 9.0})
      printf(" %10.1f", PerNotch(gaps[i], Y, 150.0));
    printf("\n");
  }
  printf("\n  (a FLAT row means every speed loses the same fraction -- the response follows the hand)\n");

  printf("\n== a hand ACCELERATING: gap shrinking 200 -> 20 ms, one notch each ==\n");
  printf("   output PER NOTCH as the hand speeds up (Y = 5):\n   ");
  {
    model::Params P;
    P.windowMs = 150.0;
    P.eatFrac = model::EatFraction(5.0);
    model::Axis g;
    g.Reset();
    const double dt = 0.001;
    double next = 0;
    double gapsRun[14] = {200, 170, 140, 110, 90, 75, 60, 50, 42, 35, 30, 26, 22, 20};
    int idx = 0;
    double out = 0;
    double winStart = 0;
    for (double t = 0; t < 5.0; t += dt)
    {
      if (idx < 14 && next <= t + 1e-12)
      {
        g.Feed(kNotchUnits, P);
        next += gapsRun[idx] / 1000.0;
        ++idx;
      }
      out += g.Tick(dt, P);
      if (t - winStart >= 0.05) // every 50 ms
      {
        printf("%.0f ", out * kDeltasPerUnit); // deltas in that 50 ms
        out = 0;
        winStart = t;
      }
      if (idx >= 14 && !g.Active())
        break;
    }
    printf("\n   (each number is the deltas delivered in one 50 ms window)\n");
  }

  printf("\n  If the per-notch column was flat and the ramp rises evenly, the eat side has no jump.\n");
  return 0;
}
