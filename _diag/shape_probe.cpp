// SHAPE PROBE -- the fix, in one table: is the output LINEAR in speed (a fraction) or CONVEX (a rate)?
//
// A rate-based eat is large next to a small amount: a slow roll's amount is wiped out, a slightly
// faster one survives better, so output turns up at the fast end -- "a slow roll suddenly takes off".
// A fraction-based eat has no such coupling: out = amount * (1 - eatFrac), a straight line.
//
// This prints the per-notch output across the ramp for the REAL model, and the step between
// neighbours. On the fraction model the steps are near-constant (a straight line); a rate model would
// grow them several-fold.
//
//   g++ -std=c++17 -O2 -I../src -o shape_probe.exe shape_probe.cpp && ./shape_probe.exe
#include "model.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

static const double kU = 15.0, kDPN = 120.0, kDPU = 8.0;

static double PerNotch(double gapMs, double X, double R)
{
  model::Axis g; g.Reset();
  model::Params P; P.windowMs = X;
  const double dt = 0.0005, gap = gapMs / 1000.0;
  double out = 0, t = 0, next = 0; int f = 0, n = 200;
  while (f < n || g.Active())
  {
    if (f < n && next <= t + 1e-12)
    {
      const double gapSec = (f == 0) ? -1.0 : gap;
      P.eatFrac = model::EatFraction(R, model::SpeedFade(gapSec, kDPN));
      g.Feed(kU, P); ++f; next += gap;
    }
    out += g.Tick(dt, P); t += dt; if (t > 200.0) break;
  }
  return out * kDPU / n;
}

int main()
{
  printf("per-notch output (of 120) across the ramp -- a straight line means no sudden take-off\n\n");
  for (double R : {15.0, 10.0, 5.0})
  {
    printf("R=%.0f:\n  %-8s", R, "gap ms");
    for (double gap : {196.0, 180.0, 150.0, 120.0, 100.0, 80.0, 60.0, 50.0})
      printf(" %-7.0f", gap);
    printf("\n  %-8s", "out");
    for (double gap : {196.0, 180.0, 150.0, 120.0, 100.0, 80.0, 60.0, 50.0})
      printf(" %-7.1f", PerNotch(gap, 100.0, R));
    printf("\n  %-8s", "step");
    double prev = -1, worst = 0;
    for (double gap : {196.0, 180.0, 150.0, 120.0, 100.0, 80.0, 60.0, 50.0})
    {
      const double v = PerNotch(gap, 100.0, R);
      if (prev > 1e-9) { printf(" %-7.2f", v / prev); worst = std::fmax(worst, v / prev); }
      else printf(" %-7s", "-");
      prev = v;
    }
    printf("\n  worst step: %.2fx (a rate-based eat ran up to ~1.6x per equal step, multiplying to 4x overall)\n\n",
           worst);
  }
  return 0;
}
