// KNEE PROBE -- where does a SLOW roll actually jump?
//
// The speed ramp (model::EatScale) made the EAT RATE continuous in speed. But the eat is a rate on a
// SHARED POOL, and the output is whatever survives it -- so the OUTPUT RATE does not have to be smooth
// just because the eat rate is. This measures the steady-state output rate across a fine sweep of
// notch gaps, and prints the step between neighbours, so a knee (or a real discontinuity) shows up as
// a number instead of a feeling.
//
//   g++ -std=c++17 -O2 -I../src -o knee_probe.exe knee_probe.cpp && ./knee_probe.exe
#include "model.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

static const double kU = 15.0, kDPN = 120.0, kDPU = 8.0;

struct Res { double inRate, outRate, perNotch; };

// One steady roll at a fixed gap; returns rates in wheel deltas per millisecond.
static Res Steady(double gapMs, double X, double R, double V, int n)
{
  model::Params P;
  P.windowMs = X;
  P.eatRatePerMs = 0.0;
  P.spitMul = 1.0;
  model::Axis g;
  g.Reset();
  const double dt = 0.0005, gap = gapMs / 1000.0;
  double out = 0, next = 0, t = 0;
  int f = 0;
  while (f < n || g.Active())
  {
    if (f < n && next <= t + 1e-12)
    {
      const double gapSec = (f == 0) ? -1.0 : gap; // first message: no previous one
      const double scale = model::EatScale(gapSec, kDPN);
      const bool spit = (scale <= 0.0);
      P.eatRatePerMs = (R * scale) / kDPU;
      P.spitMul = spit ? V : 1.0;
      g.Feed(kU, P);
      ++f;
      next += gap;
    }
    out += g.Tick(dt, P);
    t += dt;
    if (t > 200.0)
      break;
  }
  Res r;
  r.inRate = kDPN / gapMs;
  r.outRate = out * kDPU / (n * gapMs);
  r.perNotch = out * kDPU / n;
  return r;
}

int main()
{
  for (double R : {15.0, 8.0, 4.0})
  {
    const double X = 100.0, V = 1.0;
    printf("\n===== R = %.0f deltas/ms  (X=%.0f ms, V=%.2f) =====\n", R, X, V);
    printf("  %-8s %-8s %-8s %-10s %-10s %-10s %-8s %s\n", "gap ms", "n/s", "scale", "inRate",
           "outRate", "perNotch", "step", "note");
    double prev = -1.0;
    for (double gap = 240.0; gap >= 40.0 - 1e-9; gap -= 2.0)
    {
      const Res r = Steady(gap, X, R, V, 300);
      const double step = (prev > 0.0) ? r.outRate / prev : 0.0;
      const char *note = "";
      if (step > 1.6 && prev > 0.0)
        note = "<<< steep";
      printf("  %-8.0f %-8.1f %-8.3f %-10.3f %-10.4f %-10.2f %-8.2f %s\n", gap, 1000.0 / gap,
             model::EatScale(gap / 1000.0, kDPN), r.inRate, r.outRate, r.perNotch, step, note);
      prev = r.outRate;
    }
  }
  return 0;
}
