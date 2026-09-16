// RISK CHECK: the vertical receiver's grid was changed from 1 unit to 1 delta.
//
//   g++ -std=c++17 -O2 -I../src -o vert_risk.cpp vert_risk.cpp
//
// The project notes record a measurement that a coarser-than-finest grid made this axis lurch
// ("it rounds each sub-unit value UP to its own step"), and that whole units were chosen because
// "the pieces are exactly what it acts on". This change goes the OTHER way -- to 1/8 unit -- so the
// question is whether a stream of sub-unit values can now be rounded up into oversized jumps.
//
// What this measures: for a normal-speed roll, how large is each delivered piece, in units, with
// the new 1-delta grid. If pieces are far below one unit, the receiver would have to round them up
// itself -- which is the recorded lurch. This cannot be settled without REAPER; it flags the risk
// and quantifies it.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace anim3;
static const double kD = 120.0, kU = 15.0;
static const double kStepDeltas = kD / kU;      // 8
static const double kStep = 1.0 / kStepDeltas;  // 0.125 unit

struct R { double maxPiece, meanPiece; long pieces; };

static R roll(int n, double gapMs, double easeMs, double kinetic)
{
  Params P; P.durationMs = easeMs;
  Glide g; g.Reset();
  const double gap = gapMs / 1000.0;
  double accum = 0, msgTime = 0, mx = 0, sum = 0;
  long pieces = 0;
  int fed = 0;
  for (double t = 0; t < n * gap + easeMs / 1000.0 + 0.05; t += 0.001) {
    if (fed < n && msgTime <= t + 1e-9) {
      const double gapSec = (fed == 0) ? -1.0 : gap;
      const double kept = kD * (1.0 - EatFraction(gapSec, kD, kinetic));
      g.Feed(kept / kStepDeltas, P);
      ++fed; msgTime += gap;
    }
    if (g.Active()) {
      accum += g.Tick(0.001, P);
      const double m = floor(fabs(accum) / kStep + 0.5);
      if (m >= 1.0) {
        const double s = m * kStep;
        accum -= (accum < 0 ? -s : s);
        if (s > mx) mx = s;
        sum += s; ++pieces;
      }
    }
  }
  return R{mx, pieces ? sum / pieces : 0, pieces};
}

int main()
{
  printf("The vertical axis now receives pieces of ONE DELTA = %.3f unit.\n", kStep);
  printf("The notes say a sub-unit grid made it lurch. How big are normal pieces?\n\n");
  printf("  %-26s %-12s %-12s %s\n", "case", "max piece", "mean piece", "pieces");
  struct C { const char *n; int msgs; double gap, ease, kin; };
  const C cs[] = {
    {"normal roll 20ms, k12", 20, 20, 300, 12},
    {"fast roll 20ms, k12",   40, 20,  20, 12},
    {"slow roll 150ms, k12",  10, 150, 300, 12},
    {"slow roll 150ms, k1",   10, 150,  20, 1},
    {"single notch, k12",      1, 200, 300, 12},
    {"single notch, k120",     1, 200, 300, 120},
  };
  for (int i = 0; i < 6; ++i) {
    const R r = roll(cs[i].msgs, cs[i].gap, cs[i].ease, cs[i].kin);
    printf("  %-26s %-12.4f %-12.4f %ld%s\n", cs[i].n, r.maxPiece, r.meanPiece, r.pieces,
           r.maxPiece < 1.0 ? "   <-- all pieces below 1 unit" : "");
  }
  printf("\n  If pieces are below 1 unit, a receiver that acts on whole units must round them up\n");
  printf("  itself. Whether it does -- and whether that lurches -- can only be seen in REAPER.\n");
  return 0;
}
