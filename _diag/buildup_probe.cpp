// BUILDUP PROBE -- "roll a while, THEN it suddenly speeds up".
//
// The user's words: 慢慢滚，滚一会儿突然加速. That is a claim about the TIME COURSE at a CONSTANT
// speed, not about the steady-state curve. This feeds notches at a fixed gap and prints the travel of
// EACH notch, so a build-up (or a late jump) shows as numbers.
//
//   g++ -std=c++17 -O2 -I../src -o buildup_probe.exe buildup_probe.cpp && ./buildup_probe.exe
#include "model.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

static const double kU = 15.0, kDPN = 120.0, kDPU = 8.0;
static const double kV = 1.0;

static void Run(const char *title, double gapMs, double X, double R, int n)
{
  model::Axis g; g.Reset();
  model::Params P; P.windowMs = X;
  const double dt = 0.0005, gap = gapMs / 1000.0;
  double total = 0, t = 0, next = 0; int f = 0;
  double atArrival[64];
  for (int i = 0; i < 64; ++i) atArrival[i] = 0;
  while (f < n || g.Active())
  {
    if (f < n && next <= t + 1e-12)
    {
      const double gapSec = (f == 0) ? -1.0 : gap;
      const double scale = model::EatScale(gapSec, kDPN);
      P.eatRatePerMs = (R * scale) / kDPU;
      P.spitMul = (scale <= 0.0) ? kV : 1.0;
      g.Feed(kU, P);
      atArrival[f] = total;
      ++f; next += gap;
    }
    total += g.Tick(dt, P) * kDPU;
    t += dt; if (t > 200.0) break;
  }
  printf("%s\n  per-notch travel (deltas):\n ", title);
  for (int i = 0; i < n; ++i)
  {
    const double prev = (i == 0) ? 0.0 : atArrival[i - 1];
    printf(" %.1f", atArrival[i] - prev);
    if ((i + 1) % 10 == 0) printf("\n ");
  }
  printf("\n\n");
}

int main()
{
  printf("steady roll at 60 ms/notch (inside the ramp), R=15, V=1.0, 40 notches\n\n");
  Run("X=100", 60.0, 100.0, 15.0, 40);
  Run("X=300", 60.0, 300.0, 15.0, 40);
  printf("steady roll at 90 ms/notch, R=15\n\n");
  Run("X=100", 90.0, 100.0, 15.0, 40);
  Run("X=300", 90.0, 300.0, 15.0, 40);
  return 0;
}
