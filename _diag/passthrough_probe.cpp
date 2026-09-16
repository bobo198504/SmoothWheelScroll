// PASSTHROUGH PROBE -- with no eat and no spit, does the model give back exactly what it took?
//
// Y = 0 (nothing eaten) and V = 1 (no spit) must be the IDENTITY in total travel: every delta the
// wheel reports is handed over, no more and no less. This sums output vs input for a single notch and
// for rolls at many gaps, and prints the ratio. Anything other than exactly 1 (up to the last window
// still draining) is a real loss or gain.
//
//   g++ -std=c++17 -O2 -I../src -o pt_probe.exe passthrough_probe.cpp && ./pt_probe.exe
#include "model.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

static const double kU = 15.0, kDPN = 120.0, kDPU = 8.0;

// Feed n notches at gapMs with no eat / no spit; return total in and out (deltas).
struct Sum { double in, out; };
static Sum Roll(int n, double gapMs, double X)
{
  model::Axis g; g.Reset();
  model::Params P; P.windowMs = X; P.eatFrac = 0.0; P.spitMul = 1.0;
  const double dt = 0.0005, gap = gapMs / 1000.0;
  double out = 0, t = 0, next = 0; int f = 0;
  while (f < n || g.Active())
  {
    if (f < n && next <= t + 1e-12) { g.Feed(kU, P); ++f; next += gap; }
    out += g.Tick(dt, P);
    t += dt;
    if (t > 60.0) break;
  }
  return Sum{n * kDPN, out * kDPU};
}

int main()
{
  const double X = 100.0;
  printf("no eat (Y=0), no spit (V=1) -- total out must equal total in\n");
  printf("X=%.0f ms, dt=0.5 ms\n\n", X);

  printf("  %-10s %-6s %-14s %-14s %s\n", "gap ms", "n", "in", "out", "out/in");
  struct C { double gap; int n; };
  for (C c : {C{0.0, 1}, C{400.0, 5}, C{200.0, 5}, C{100.0, 10}, C{60.0, 10}, C{30.0, 10},
              C{16.0, 12}, C{4.0, 30}, C{1.0, 60}})
  {
    const Sum s = Roll(c.n, c.gap, X);
    printf("  %-10.0f %-6d %-14.2f %-14.2f %.6f\n", c.gap, c.n, s.in, s.out, s.out / s.in);
  }

  printf("\n== across the precision setting X (10 notches @30 ms) ==\n");
  printf("  %-10s %-14s %-14s %s\n", "X ms", "in", "out", "out/in");
  for (double Xv : {100.0, 150.0, 200.0, 300.0, 400.0})
  {
    const Sum s = Roll(10, 30.0, Xv);
    printf("  %-10.0f %-14.2f %-14.2f %.6f\n", Xv, s.in, s.out, s.out / s.in);
  }
  return 0;
}
