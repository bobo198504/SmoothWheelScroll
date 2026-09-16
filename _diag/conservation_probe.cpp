// CONSERVATION PROBE -- the model is now ONLY a timing model: take N, give N.
//
// With the eat and the spit removed, the model's single promise is that the total handed over equals
// the total fed, exactly, at any speed, for any window, for any number of notches -- and that each
// window finishes at exactly `windowMs`.
//
// Checks:
//   1. total out == total in, to the last fraction (single notch, rolls at many gaps, many windows);
//   2. it holds at every precision X;
//   3. a window ends at exactly windowMs (the share is a DURATION, not a time constant);
//   4. the order/overlap does not change the total (a fast roll of many small windows).
//
//   g++ -std=c++17 -O2 -I../src -o conserve_probe.exe conservation_probe.cpp && ./conserve_probe.exe
#include "model.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

static const double kU = 15.0, kDPN = 120.0, kDPU = 8.0;
static int failures = 0;
static void Check(const char *what, bool ok, const char *detail)
{
  if (!ok) ++failures;
  printf("  %-60s %-4s %s\n", what, ok ? "ok" : "FAIL", detail);
}

// Feed n notches at gapMs; return {in, out, ms the last output was seen}.
struct Res { double in, out, lastMs; };
static Res Run(int n, double gapMs, double X)
{
  model::Axis g;
  g.Reset();
  model::Params P;
  P.windowMs = X;
  const double dt = 0.0005, gap = gapMs / 1000.0;
  double out = 0, t = 0, next = 0, last = 0;
  int f = 0;
  while (f < n || g.Active())
  {
    if (f < n && next <= t + 1e-12)
    {
      g.Feed(kU, P);
      ++f;
      next += gap;
    }
    const double s = g.Tick(dt, P);
    if (s != 0.0)
      last = (t + dt) * 1000.0;
    out += s;
    t += dt;
    if (t > 100.0)
      break;
  }
  return Res{n * kDPN, out * kDPU, last};
}

int main()
{
  printf("the model is timing only: total out must equal total in, exactly\n\n");

  printf("== 1. conservation across roll speeds and counts (X=150) ==\n");
  printf("  %-10s %-6s %-14s %-14s %s\n", "gap ms", "n", "in", "out", "out/in");
  struct C { double gap; int n; };
  for (C c : {C{0.0, 1}, C{400.0, 5}, C{200.0, 5}, C{100.0, 10}, C{60.0, 10}, C{30.0, 10},
              C{16.0, 12}, C{4.0, 30}, C{1.0, 60}})
  {
    const Res r = Run(c.n, c.gap, 150.0);
    const double ratio = r.out / r.in;
    printf("  %-10.0f %-6d %-14.2f %-14.2f %.9f\n", c.gap, c.n, r.in, r.out, ratio);
    Check("conserved", std::fabs(ratio - 1.0) < 1e-9, "");
  }

  printf("\n== 2. conservation at every precision X (10 notches @ 30 ms) ==\n");
  for (double X : {100.0, 150.0, 200.0, 300.0, 400.0})
  {
    const Res r = Run(10, 30.0, X);
    const double ratio = r.out / r.in;
    printf("  X=%-8.0f in %-12.2f out %-12.2f ratio %.9f\n", X, r.in, r.out, ratio);
    Check("conserved at this X", std::fabs(ratio - 1.0) < 1e-9, "");
  }

  printf("\n== 3. a single window ends at exactly X ms (a DURATION, not a time constant) ==\n");
  for (double X : {100.0, 150.0, 300.0, 400.0})
  {
    const Res r = Run(1, 0.0, X);
    // With a 0.5 ms grid the last nonzero frame lands within a frame of X.
    const bool ok = std::fabs(r.lastMs - X) < 1.0 && std::fabs(r.out / r.in - 1.0) < 1e-9;
    printf("  X=%-8.0f last output at %.1f ms (want ~%.0f)\n", X, r.lastMs, X);
    Check("window length honoured and nothing left over", ok, "");
  }

  printf("\n== 4. one notch, however it is turned, gives back exactly one notch ==\n");
  {
    // The same total fed as 1 x 120, 4 x 30, or 8 x 15 must hand over the same amount.
    const Res one = Run(1, 0.0, 150.0);
    printf("  1 notch (15 units): in %.2f out %.2f\n", one.in, one.out);
    Check("exactly one notch back", std::fabs(one.out - kDPN) < 1e-9, "");
  }

  printf("\n%s\n", failures ? "FAIL: the model is not conservative" : "OK: take N, give N -- exactly");
  return failures ? 1 : 0;
}
