// LIVE PROBE -- mirror the plugin's OWN accounting, to answer "why do in and out differ".
//
// This reproduces Kick -> Tick -> DeliverTravel for the two deliveries the plugin actually uses, and
// prints, per wheel message, the received deltas and the deltas handed to REAPER in between, plus the
// running totals -- exactly what the panel shows. No eat, no spit (Y=0, V=1), so the totals MUST
// match once the glide drains.
//
//   g++ -std=c++17 -O2 -I../src -o live_probe.exe live_probe.cpp && ./live_probe.exe
#include "model.h"
#include <cstdio>
#include <cmath>
#include <vector>

static const double kU = 15.0, kDPN = 120.0, kDPU = 8.0;

struct Row { double inDeltas, outDeltas; };

// kind: 0 = kStream (grid 1/256 unit), 1 = kStepUnits (grid 1 unit, with the end-of-gesture top-up)
static std::vector<Row> Run(int n, double gapMs, double X, int kind, double *totIn, double *totOut)
{
  model::Axis g;
  g.Reset();
  model::Params P;
  P.windowMs = X;
  P.eatFrac = 0.0;
  P.spitMul = 1.0;

  const double grid = (kind == 0) ? (1.0 / 256.0) : 1.0;
  const double dt = 0.001;
  const double gap = gapMs / 1000.0;

  std::vector<Row> rows;
  double accum = 0.0, outAcc = 0.0, inTot = 0.0, outTot = 0.0;
  bool sentThisBurst = false;
  bool wasActive = false;
  double t = 0, next = 0;
  int f = 0;

  for (double guard = 0; guard < 200.0; guard += dt)
  {
    if (f < n && next <= t + 1e-12)
    {
      // MonitorIn: close off the previous message's output, record this message's input.
      Row r;
      r.outDeltas = outAcc;
      r.inDeltas = kDPN;
      rows.push_back(r);
      outAcc = 0.0;
      inTot += kDPN;

      g.Feed(kU, P); // Kick
      ++f;
      next += gap;
      sentThisBurst = false;
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
        const double deltas = send * kDPU;
        outAcc += deltas;
        outTot += deltas;
        sentThisBurst = true;
      }
      if (wasActive && !g.Active() && kind == 1 && !sentThisBurst && accum != 0.0)
      {
        outAcc += 1.0 * kDPU;
        outTot += 1.0 * kDPU;
        accum = 0.0;
      }
    }
    wasActive = g.Active();
    t += dt;
    if (f >= n && !g.Active())
      break;
  }
  // final tail
  if (outAcc != 0.0)
  {
    Row r;
    r.outDeltas = outAcc;
    r.inDeltas = 0.0;
    rows.push_back(r);
  }
  *totIn = inTot;
  *totOut = outTot;
  return rows;
}

static void Show(const char *title, int n, double gapMs, int kind)
{
  double ti = 0, to = 0;
  const std::vector<Row> rows = Run(n, gapMs, 100.0, kind, &ti, &to);
  printf("== %s ==\n", title);
  printf("  last rows (in / out, deltas):\n   ");
  const int start = (int)rows.size() > 6 ? (int)rows.size() - 6 : 0;
  for (int i = start; i < (int)rows.size(); ++i)
    printf(" [%.0f/%.1f]", rows[i].inDeltas, rows[i].outDeltas);
  printf("\n  TOTALS: in %.1f  out %.1f  diff %.2f  ratio %.6f\n\n", ti, to, ti - to,
         (ti > 0.0) ? to / ti : 0.0);
}

int main()
{
  printf("Y=0 (no eat), V=1 (no spit). X=100 ms, 1 ms tick.\n");
  printf("The totals must match -- the input and output rows can differ MOMENT TO MOMENT because the\n"
         "glide spreads each message over X ms (that lag is the whole point of the plugin).\n\n");

  Show("main view vertical scroll (kStepUnits) -- 10 notches @ 60 ms", 10, 60.0, 1);
  Show("main view vertical scroll (kStepUnits) -- single notch", 1, 0.0, 1);
  Show("horizontal axis (kStream) -- 10 notches @ 60 ms", 10, 60.0, 0);
  Show("horizontal axis (kStream) -- single notch", 1, 0.0, 0);
  Show("fast roll (kStepUnits) -- 20 notches @ 16 ms", 20, 16.0, 1);
  return 0;
}
