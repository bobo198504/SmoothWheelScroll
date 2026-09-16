// PIPELINE PROBE -- end to end: does the plugin hand over exactly what the wheel reported?
//
// Mirrors the plugin's Kick -> Tick -> DeliverTravel flow, including the delivery grid and the
// "top-up" for whole-unit receivers, and compares total delivered vs total received for a single
// notch and for rolls. No eat, no spit (Y=0, V=1), so the only thing left that can move the total is
// the delivery layer.
//
//   g++ -std=c++17 -O2 -I../src -o pipe_probe.exe pipeline_probe.cpp && ./pipe_probe.exe
#include "model.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

static const double kU = 15.0, kDPN = 120.0, kDPU = 8.0;
static const double kSub = 256.0;   // kRelSubPerUnit: finest step for a stream axis
static const double kVert = 1.0;    // kVertStepsPerUnit: for kStepUnits axes (1 unit)

// kind 0 = kStream (grid 1/256 unit), kind 1 = kStepUnits (grid 1 unit)
static double Run(int n, double gapMs, double X, int kind, bool *topupFired)
{
  model::Axis g; g.Reset();
  model::Params P; P.windowMs = X; P.eatFrac = 0.0; P.spitMul = 1.0;
  const double grid = (kind == 0) ? (1.0 / kSub) : (1.0 / kVert);
  const double dt = 0.001; // 1 ms multimedia timer
  const double gap = gapMs / 1000.0;

  double accum = 0.0, deliveredUnits = 0.0;
  double t = 0, next = 0;
  int f = 0;
  bool sentThisBurst = false;
  bool wasActive = false;

  for (double guard = 0; guard < 200.0; guard += dt)
  {
    // feed (Kick)
    while (f < n && next <= t + 1e-12)
    {
      g.Feed(kU, P);
      ++f;
      next += gap;
      sentThisBurst = false; // a message begins/continues a burst
    }
    const bool activeNow = g.Active();
    if (wasActive || activeNow)
    {
      const double step = g.Tick(dt, P);
      accum += step;
      const double m = floor(fabs(accum) / grid + 0.5);
      if (m >= 1.0)
      {
        const double send = m * grid;
        accum -= (accum < 0.0) ? -send : send;
        deliveredUnits += send;
        sentThisBurst = true;
      }
      // top-up: whole-unit axis, gesture ended, never reached a unit
      if (wasActive && !g.Active() && kind == 1 && !sentThisBurst && accum != 0.0)
      {
        deliveredUnits += 1.0;
        accum = 0.0;
        if (topupFired) *topupFired = true;
      }
    }
    wasActive = g.Active();
    t += dt;
    if (f >= n && !g.Active())
      break;
  }
  return deliveredUnits * kDPU; // back to deltas
}

int main()
{
  const double X = 100.0;
  printf("pipeline total delivered vs received (deltas); no eat, no spit; 1 ms tick\n\n");
  printf("  %-22s %-6s %-14s %-14s %s\n", "case", "n", "in", "out", "out/in");
  struct C { const char *name; double gap; int n; int kind; };
  for (C c : {C{"kStream single", 0.0, 1, 0}, C{"kStream roll 30ms", 30.0, 10, 0},
              C{"kStream roll 16ms", 16.0, 20, 0}, C{"kStepUnits single", 0.0, 1, 1},
              C{"kStepUnits roll 30ms", 30.0, 10, 1}, C{"kStepUnits roll 16ms", 16.0, 20, 1}})
  {
    bool top = false;
    const double out = Run(c.n, c.gap, X, c.kind, &top);
    const double in = c.n * kDPN;
    printf("  %-22s %-6d %-14.2f %-14.2f %.6f %s\n", c.name, c.n, in, out, out / in,
           top ? "<- top-up fired" : "");
  }
  return 0;
}
