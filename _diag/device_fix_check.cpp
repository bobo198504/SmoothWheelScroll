// VERIFY device independence at the model level, with the plugin's own header.
//
//   g++ -std=c++17 -O2 -I../src -o device_fix_check.cpp device_fix_check.cpp
//
// Same turn (120 deltas / 200 ms), described by two devices, through the real Glide.
// The notched mouse must be UNCHANGED from before (unitFrac 1), and the free-spinner must now
// retain a comparable share.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace anim3;
static const double kDPU = 8.0; // deltas per 7-bit unit; one notch = 15 units = 120 deltas

// msgs messages of `eachDeltas`, every gapMs, easeMs, resistance in "deltas per 5ms".
static double run(int msgs, double eachDeltas, double gapMs, double easeMs, double resist)
{
  Params P;
  P.durationMs = easeMs;
  P.takePerSec = (resist * 200.0) / kDPU; // deltas/5ms -> units/sec
  P.snap = (1.0 / kDPU) * 0.5;
  Glide g; g.Reset();
  const double dt = 0.001, gap = gapMs / 1000.0;
  double msgTime = 0, out = 0;
  int fed = 0;
  for (double t = 0; t < msgs * gap + 4.0; t += dt) {
    if (fed < msgs && msgTime <= t + 1e-9) {
      g.Feed(eachDeltas / kDPU, eachDeltas / 120.0); // 7-bit units, and its share of a notch
      ++fed; msgTime += gap;
    }
    out += g.Tick(dt, P);
    if (fed >= msgs && !g.Active()) break;
  }
  return out * kDPU; // deltas
}

int main()
{
  printf("same turn: 120 deltas / 200 ms. retained %%.\n\n");
  printf("  %-10s | %-14s %-14s %s\n", "resistance", "notched", "free-spin", "verdict");
  const double rs[] = {1, 12, 60, 110};
  for (int i = 0; i < 4; ++i) {
    const double r = rs[i];
    const double n = run(5, 120.0, 200.0, 300.0, r);      // 5 notches, 1 second
    const double f = run(30, 20.0, 200.0 / 6, 300.0, r);  // same turn, 6x finer
    const double pn = 100 * n / 600, pf = 100 * f / 600;
    printf("  %-10.0f | %-13.1f%% %-13.1f%% %s\n", r, pn, pf,
           std::fabs(pf - pn) < 3.0 ? "agree" : "differs");
  }

  printf("\n=== the notched mouse must be UNCHANGED by this fix ===\n");
  printf("  a whole-notch message has unitFrac 1, so the take is exactly what it was.\n");
  for (int i = 0; i < 4; ++i) {
    const double r = rs[i];
    const double n = run(1, 120.0, 2000.0, 300.0, r); // one isolated notch
    printf("  resistance %-4.0f: one notch keeps %.2f of 120 deltas (%.1f%% eaten)\n",
           r, n, 100 * (1 - n / 120));
  }

  printf("\n=== a lone fine-device message, for contrast ===\n");
  printf("  20-delta message on its own: keeps %.3f deltas (resistance 110)\n",
         run(1, 20.0, 2000.0, 300.0, 110.0));
  printf("  120-delta message on its own: keeps %.3f deltas (resistance 110)\n",
         run(1, 120.0, 2000.0, 300.0, 110.0));
  return 0;
}
