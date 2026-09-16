// AFTER: the restored WINDOW model. One notch, Fineness = 500 ms, and frame-rate independence.
#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace anim3;
static const double kU = 15.0; // 7-bit units in one notch

static void runOneNotch(double Dms, double dtMs) {
  Params P; P.durationMs = Dms; // Kinetic is the caller's concern; this isolates the timing
  Glide g; g.Reset();
  g.Feed(kU, P);
  const double dt = dtMs / 1000.0;
  double total = 0, last = 0, maxStep = 0;
  int calls = 0;
  for (double t = 0; t < 5.0; t += dt) {
    const double s = g.Tick(dt, P);
    if (s != 0.0) {
      total += s; last = (t + dt) * 1000.0; ++calls;
      if (fabs(s) > maxStep) maxStep = fabs(s);
    }
    if (!g.Active()) break;
  }
  printf("   Fineness %5.0f ms  dt=%5.1f ms -> over at %7.1f ms   total %8.4f   %5d calls   "
         "max step %.4f\n", Dms, dtMs, last, total, calls, maxStep);
}

int main() {
  printf("== WINDOW (each message its own envelope of exactly durationMs) ==\n");
  printf("   promise: motion lasts exactly the setting, total 15.0000\n\n");
  for (double D : {10.0, 50.0, 100.0, 300.0, 500.0}) runOneNotch(D, 1.0);
  printf("\n   frame-rate independence, Fineness 500 ms:\n\n");
  for (double dt : {1.0, 4.0, 15.6, 30.0}) runOneNotch(500.0, dt);
  return 0;
}
