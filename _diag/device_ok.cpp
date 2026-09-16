// VERIFY device independence with the REAL header: 120-delta messages vs 20-delta messages, same
// hand speed. This is the property the user needs (a notched mouse at resistance 5 must feel like
// a free-spinner at resistance 5).
//
//   g++ -std=c++17 -O2 -I../src -o device_ok.cpp device_ok.cpp

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace anim3;
static const double kDPU = 8.0;   // deltas per 7-bit unit
static const double kNotchD = 120.0;

// `msgs` messages of `each` deltas, every gapMs. Returns deltas handed over.
static double run(int msgs, double each, double gapMs, double easeMs, double resist)
{
  Params P;
  P.durationMs = easeMs;
  P.takePerSec = (resist * 20.0) / kDPU; // 20 deltas/s per step
  P.snap = (1.0 / kDPU) * 0.5;
  Glide g; g.Reset();
  const double dt = 0.001, gap = gapMs / 1000.0;
  double msgTime = 0, out = 0, last = -1;
  int fed = 0;
  for (double t = 0; t < msgs * gap + 6.0; t += dt) {
    if (fed < msgs && msgTime <= t + 1e-9) {
      const double elapsed = (last < 0.0) ? 0.2 : (t - last);
      last = t;
      g.Feed(each / kDPU, elapsed, P);
      ++fed; msgTime += gap;
    }
    out += g.Tick(dt, P);
    if (fed >= msgs && !g.Active()) break;
  }
  return out * kDPU;
}

int main()
{
  printf("same turn: 120 deltas / 200 ms. retained %%.\n\n");
  printf("  %-10s | %-14s %-14s %s\n", "resistance", "notched", "free-spin", "verdict");
  const double rs[] = {1, 5, 12, 60, 110};
  for (int i = 0; i < 5; ++i) {
    const double r = rs[i];
    const double n = run(5, 120.0, 200.0, 300.0, r);
    const double f = run(30, 20.0, 200.0 / 6, 300.0, r);
    const double pn = 100 * n / 600, pf = 100 * f / 600;
    printf("  %-10.0f | %-13.2f%% %-13.2f%% %s\n", r, pn, pf,
           std::fabs(pf - pn) < 1.5 ? "AGREE" : "differs");
  }

  printf("\n=== a faster roll keeps more (the wanted behaviour) ===\n");
  printf("  notched, resistance 60, ease 300: retained %%\n");
  const double gaps[] = {400, 200, 100, 50, 20};
  for (int i = 0; i < 5; ++i) {
    const int n = 10;
    const double keep = run(n, 120.0, gaps[i], 300.0, 60.0);
    printf("    %.0f ms/notch -> %.1f%%\n", gaps[i], 100 * keep / (120.0 * n));
  }

  printf("\n=== a lone notch (the slow case), ease 300 ===\n");
  for (int i = 0; i < 5; ++i) {
    const double r = rs[i];
    const double k = run(1, 120.0, 2000.0, 300.0, r);
    printf("  resistance %-4.0f: keeps %.1f of 120 deltas (%.1f%%)\n", r, k, 100 * k / 120);
  }
  return 0;
}
