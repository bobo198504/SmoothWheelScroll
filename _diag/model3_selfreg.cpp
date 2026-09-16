// Is the automatic overlap SELF-REGULATING? Same physical rotation described two ways.
//
//   g++ -std=c++17 -O2 -I../src -o model3_selfreg.exe model3_selfreg.cpp
//
// A notched mouse: 5 messages of 1.0 notch, 400 ms apart total.
// A free-spinner: 30 messages of 1/6 notch, over the same 400 ms.
// Both describe the SAME turn. With easing D, the output RATE should be the same shape,
// because rate = (notches received in the last D) / D -- and that does not depend on how
// finely the same rotation was chopped up.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>

using namespace anim3;
static const double kU = 15.0;

struct Trace { double rate[2000]; int n; };

static Trace trace(double totalNotches, int nMsg, double spanSec, double D, double *outTotal) {
  Params P; P.durationMs = D; P.shape = kLinear;
  Glide g; g.Reset();
  const double dt = 0.001, grid = 1.0 / 256.0;
  const double each = totalNotches / nMsg;
  const double iv = (nMsg > 1) ? spanSec / nMsg : 0.0;
  double msgNext = 0, carry = 0;
  int fed = 0;
  Trace tr; tr.n = 0;
  for (double t = 0.0; t < spanSec + D / 1000.0 + 0.1 && tr.n < 2000; t += dt) {
    while (fed < nMsg && msgNext <= t + 1e-9) { g.Feed(each, P); ++fed; msgNext += iv; }
    const double want = g.Tick(dt, P) * kU;
    const double total = carry + want;
    const double m = std::floor(std::fabs(total) / grid + 0.5);
    const double deliv = (total < 0 ? -1 : 1) * m * grid;
    carry = total - deliv;
    *outTotal += deliv;
    tr.rate[tr.n++] = std::fabs(want) / dt / kU; // notches/s
  }
  return tr;
}

int main() {
  const double D = 200.0, span = 0.4, total = 5.0;
  double outN = 0, outF = 0;
  const Trace a = trace(total, 5, span, D, &outN);   // notched mouse
  const Trace b = trace(total, 30, span, D, &outF);  // free-spinner

  printf("D = %.0f ms; both describe the SAME turn: 5 notches over 400 ms.\n", D);
  printf("  notched mouse : 5 msgs of 1.0000 notch (one every 80 ms)\n");
  printf("  free-spinner  : 30 msgs of 0.1667 notch (one every 13 ms)\n\n");
  printf("  total out: notched %.3f units, free-spinner %.3f units (both = %.1f)\n\n",
         outN, outF, total * kU);

  printf("  %6s | %14s %14s\n", "t ms", "notched N/s", "free N/s");
  for (int i = 0; i < 620; i += 50)
    printf("  %6d | %14.2f %14.2f\n", i, a.rate[i], b.rate[i]);
  printf("  %6d | %14.2f %14.2f\n", 600, a.rate[600], b.rate[600]);
  return 0;
}
