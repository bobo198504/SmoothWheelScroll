// DEVICE INDEPENDENCE: the same physical turn must come out the same however it was chopped up.
//
//   g++ -std=c++17 -O2 -I../src -o kinetic_universal.cpp kinetic_universal.cpp
//
// Every row is the SAME hand motion: 120 deltas of wheel per 200 ms (600 deltas/s, the slow rate).
// The only thing that varies is the message size. With the deltas-per-second speed measure the
// eating must be identical, so the kept total must be identical too.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace anim3;
static const double kU = 15.0;            // units per notch
static const double kDeltasPerNotch = 120.0;

// Turn `totalDeltas` over `spanSec`, chopped into `nMsg` equal messages, each eased over Dms.
static double turn(double totalDeltas, int nMsg, double spanSec, double Dms, double kinetic) {
  Params P; P.durationMs = Dms;
  Glide g; g.Reset();
  const double each = totalDeltas / nMsg;
  const double gap = spanSec / nMsg;
  double out = 0, msgTime = 0;
  int fed = 0;
  for (double t = 0; t < spanSec + Dms / 1000.0 + 0.05; t += 0.001) {
    while (fed < nMsg && msgTime <= t + 1e-9) {
      const double k = (fed == 0) ? -1.0 : gap; // the plugin's own rule: first message = slowest
      const double eaten = EatFraction(k, each, kinetic);
      g.Feed((each / kDeltasPerNotch) * (1.0 - eaten), P);
      ++fed; msgTime += gap;
    }
    out += g.Tick(0.001, P) * kU;
  }
  return out;
}

int main() {
  printf("Same TURN: 120 deltas per 200 ms (600 deltas/s = the slow rate). Chop varies.\n");
  printf("one notch = 120 deltas; 15 units.  Kinetic = 12 means a fully slow turn keeps 12/120.\n\n");
  printf("  %-8s | %-11s %-11s %-11s %-11s %s\n", "kinetic", "1x120", "2x60", "6x20", "12x10", "verdict");
  for (double k : {1.0, 12.0, 30.0, 60.0, 120.0}) {
    const double a = turn(120.0, 1, 0.2, 300.0, k);
    const double b = turn(120.0, 2, 0.2, 300.0, k);
    const double c = turn(120.0, 6, 0.2, 300.0, k);
    const double d = turn(120.0, 12, 0.2, 300.0, k);
    const double mn = std::fmin(std::fmin(a, b), std::fmin(c, d));
    const double mx = std::fmax(std::fmax(a, b), std::fmax(c, d));
    printf("  %-8.0f | %-11.4f %-11.4f %-11.4f %-11.4f %s\n", k, a, b, c, d,
           (mx - mn) < 0.02 * mx ? "SAME" : "!! DIFFERS");
  }

  printf("\n=== the notched mouse is UNCHANGED: 120-delta messages, gap sweep ===\n");
  printf("   (this is the feel that was tuned by ear; it must not move)\n");
  for (double gapMs : {200, 100, 60, 40, 20}) {
    const double eaten = EatFraction(gapMs / 1000.0, 120.0, 12.0);
    const double oldF = (gapMs / 1000.0) / 0.2; const double oldEat = 0.9 * (oldF > 1 ? 1 : oldF);
    printf("   gap %4.0f ms: eats %5.1f%%   (old gap-only rule would eat %5.1f%%)  %s\n",
           gapMs, 100 * eaten, 100 * oldEat,
           std::fabs(eaten - oldEat) < 1e-9 ? "identical" : "DIFFERS");
  }

  printf("\n=== never more than received, small messages included ===\n");
  int bad = 0;
  for (double rec : {1, 5, 15, 20, 40, 120, 240})
    for (double k : {1, 12, 20, 60, 120}) {
      const double kept = rec * (1.0 - EatFraction(0.4, rec, k));
      if (kept > rec + 1e-9 || kept < 0) { printf("   !! rec=%.0f k=%.0f kept=%.3f\n", rec, k, kept); ++bad; }
    }
  printf("   %s\n", bad ? "FAILED" : "OK (35 combos)");

  printf("\n=== kinetic 120 = eat nothing (exact conservation) ===\n");
  for (double span : {0.25, 0.06, 0.033}) {
    const double u = turn(1200.0, 10, span * 10, 300.0, 120.0);
    printf("   10 notches over %.0f ms/notch: %.4f units (want 150.0000) %s\n", span * 1000, u,
           std::fabs(u - 150.0) < 0.01 ? "OK" : "!!");
  }
  return 0;
}
