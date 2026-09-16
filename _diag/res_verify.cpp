// VERIFY: window mechanism (exact duration) + resistance (eats per second), using the plugin's
// own src/anim3_core.h. This is the combination the user asked for: "remove the pool, keep
// resistance (1 ms takes N deltas)".
//
//   g++ -std=c++17 -O2 -I../src -o res_verify.exe res_verify.cpp
#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>
using namespace anim3;

static const double kU = 15.0;        // 7-bit units in one notch
static const double kDeltasPerUnit = 8.0;

// One notch (15 units), window D, resistance R deltas/ms, frame dt. Reports total handed over,
// when it stops, and how much was eaten.
struct Res { double total, stopMs, eatenPct; int calls; };
static Res oneNotch(double Dms, double R, double dtMs) {
  Params P;
  P.durationMs = Dms;
  P.takePerSec = (R * 1000.0) / kDeltasPerUnit;
  P.snap = (1.0 / kDeltasPerUnit) * 0.5;
  Glide g; g.Reset();
  g.Feed(kU, P);
  const double dt = dtMs / 1000.0;
  double total = 0, stop = 0; int calls = 0;
  for (double t = 0; t < 20.0; t += dt) {
    const double s = g.Tick(dt, P);
    if (s != 0.0) { total += s; stop = (t + dt) * 1000.0; ++calls; }
    if (!g.Active()) break;
  }
  Res r{ total, stop, 100.0 * (1.0 - total / kU), calls };
  return r;
}

int main() {
  printf("== A. window exactness (R=0, no eating): 1 notch, 1 ms frames ==\n\n");
  for (double D : {50.0, 100.0, 300.0, 500.0}) {
    Res r = oneNotch(D, 0.0, 1.0);
    printf("   Fineness %5.0f ms -> stops at %7.1f ms   total %7.4f (%.2f%%)   %4d calls\n",
           D, r.stopMs, r.total, 100.0 * r.total / kU, r.calls);
  }
  printf("   frame-rate independence, Fineness 500 ms, R=0:\n");
  for (double dt : {1.0, 4.0, 15.6}) {
    Res r = oneNotch(500.0, 0.0, dt);
    printf("     dt=%5.1f ms -> stops at %7.1f ms   total %7.4f\n", dt, r.stopMs, r.total);
  }

  printf("\n== B. resistance eats, by setting (Fineness 500 ms, 1 notch, 1 ms frames) ==\n\n");
  printf("   %-10s", "resist");
  for (double R : {0.0, 1.0, 3.0, 10.0}) printf(" %8.2f", R);
  printf("\n   eaten %%   ");
  for (double R : {0.0, 1.0, 3.0, 10.0}) {
    Res r = oneNotch(500.0, R, 1.0);
    printf(" %7.1f%%", r.eatenPct);
  }
  printf("\n   stops ms  ");
  for (double R : {0.0, 1.0, 3.0, 10.0}) {
    Res r = oneNotch(500.0, R, 1.0);
    printf(" %7.1f", r.stopMs);
  }
  printf("\n");

  printf("\n== C. 'faster turn -> less eaten': resistance 3, different message gaps ==\n\n");
  // A roll of 10 notches at the given gap, Fineness 500, resistance 3. Total handed over vs fed.
  printf("   %-12s %10s %10s %8s\n", "gap", "fed(units)", "out(units)", "eaten");
  for (double gapMs : {400.0, 200.0, 100.0, 50.0, 20.0}) {
    Params P; P.durationMs = 500.0; P.takePerSec = (3.0 * 1000.0) / kDeltasPerUnit;
    P.snap = (1.0 / kDeltasPerUnit) * 0.5;
    Glide g; g.Reset();
    const double dt = 0.001, gap = gapMs / 1000.0;
    double out = 0, t = 0, msgTime = 0; int fed = 0;
    const int n = 10;
    for (double tt = 0; tt < n * gap + 3.0; tt += dt) {
      while (fed < n && msgTime <= tt + 1e-9) { g.Feed(kU, P); ++fed; msgTime += gap; }
      out += g.Tick(dt, P);
      t = tt;
    }
    const double inTot = n * kU;
    printf("   %-12.0f %10.2f %10.2f %7.1f%%\n", gapMs, inTot, out, 100.0 * (1.0 - out / inTot));
  }
  return 0;
}
