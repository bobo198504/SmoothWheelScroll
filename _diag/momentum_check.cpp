// DOES THE CURRENT RESISTANCE DEPEND ON THE DEVICE? One hand motion, different device granularity.
//
//   g++ -std=c++17 -O2 -I../src -o momentum_check.exe momentum_check.cpp
//
// The hand speed is held CONSTANT (same deltas per second); only the device's event size changes
// (120-delta notched mouse ... 1-delta free-spinner). If the eaten fraction changes with the event
// size, the algorithm is device-dependent. Uses the plugin's own src/anim3_core.h.
#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>
using namespace anim3;

static const double kDeltasPerNotch = 120.0;
static const double kUnitsPerNotch = 15.0;
static double unitsOf(double deltas) { return deltas * kUnitsPerNotch / kDeltasPerNotch; }
static double takeSec(double R) { return (R * 1000.0) / 8.0; } // R deltas/ms -> units/sec

// One gesture: `totalDeltas` of hand motion spread over `Tsec`, delivered as events of `per`
// deltas each. Reports the fraction of the hand's motion that got eaten.
static double eatenFrac(double R, double Dms, double totalDeltas, double Tsec, double per) {
  Params P; P.durationMs = Dms; P.takePerSec = takeSec(R); P.snap = (1.0 / 8.0) * 0.5;
  Glide g; g.Reset();
  const double dt = 0.0005;
  const double gap = Tsec / (totalDeltas / per); // seconds between events
  const double amount = unitsOf(per);
  double out = 0, fed = 0, next = 0;
  for (double t = 0; t < Tsec; t += dt) {
    while (next <= t + 1e-12) { g.Feed(amount, P); fed += amount; next += gap; }
    out += g.Tick(dt, P);
  }
  for (double t = 0; t < 5.0 && g.Active(); t += dt) out += g.Tick(dt, P); // drain
  return 100.0 * (1.0 - out / fed);
}

int main() {
  struct Dev { double per; const char *name; };
  Dev devs[] = {{120.0, "notched 1 event = 120 delta (1 notch)"},
                {60.0,  "2 events per notch (60 delta)"},
                {20.0,  "6 events per notch (20 delta)  [free-spinner]"},
                {8.0,   "15 events per notch (8 delta)"},
                {4.0,   "30 events per notch (4 delta)"},
                {1.0,   "120 events per notch (1 delta)"}};

  // Two hand speeds, same total motion, Fineness fixed, Resistance fixed.
  struct Case { double totalDeltas, Tsec; const char *label; };
  Case cases[] = {{1200.0, 3.0, "moderate: 1200 delta over 3 s = 400 delta/s (3.3 notches/s)"},
                  {240.0, 3.0, "slow:      240 delta over 3 s =  80 delta/s (0.7 notches/s)"}};

  const double R = 1.0, D = 500.0;
  printf("阻力 = %.2f, 精细度 = %.0f ms  (同一段手部运动，只改设备粒度)\n\n", R, D);
  for (auto &c : cases) {
    printf("== %s ==\n", c.label);
    printf("   %-42s %10s\n", "device", "吃掉");
    for (auto &d : devs)
      printf("   %-42s %9.1f%%\n", d.name, eatenFrac(R, D, c.totalDeltas, c.Tsec, d.per));
    printf("\n");
  }
  return 0;
}
