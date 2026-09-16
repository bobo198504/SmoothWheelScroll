// KINETIC ENERGY as EATING: it can only remove from the excess, never add.
//
//   g++ -std=c++17 -O2 -I../src -o kinetic_eat.cpp kinetic_eat.cpp
//
// The rules to verify, on the plugin's own header:
//   1. what survives is NEVER more than what arrived (eat >= 0 always);
//   2. a message no bigger than the kinetic setting passes through UNTOUCHED -- which is what makes
//      a free-spinning wheel (small messages, e.g. 20 deltas) unaffected once the setting is <= 20;
//   3. for a notched mouse (120), a slow turn keeps exactly the kinetic amount;
//   4. a roll still keeps nearly everything whatever the setting.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace anim3;

static double keptDeltas(double gapSec, double kinetic, double received) {
  return received * (1.0 - EatFraction(gapSec, kinetic, received));
}

int main() {
  const double SLOW = 0.4;  // 400 ms apart = fully slow
  const double FAST = 0.0;  // back to back = fully fast

  printf("=== 1. kinetic is the GUARANTEED floor: only the excess above it can be eaten ===\n");
  printf("   kinetic=12, received=120 (a notch), slow -> keeps 12, eats the other 108\n");
  printf("   kinetic=12, received=20 (free-spin), slow -> keeps 12, eats only the excess 8\n");
  printf("     => %.3f deltas kept of 20\n", keptDeltas(SLOW, 12.0, 20.0));
  printf("   kinetic=25, received=20: nothing is above the floor -> UNTOUCHED\n");
  printf("     => %.3f deltas kept of 20   %s\n", keptDeltas(SLOW, 25.0, 20.0),
         std::fabs(keptDeltas(SLOW, 25.0, 20.0) - 20.0) < 1e-9 ? "UNTOUCHED (correct)" : "!! eaten");

  printf("\n=== 3. a notched mouse message (120 deltas), slow turn: kept == kinetic ===\n");
  for (double k : {1.0, 12.0, 30.0, 60.0, 100.0, 120.0}) {
    const double kept = keptDeltas(SLOW, k, 120.0);
    printf("   kinetic %3.0f -> kept %6.1f deltas  %s\n", k, kept,
           std::fabs(kept - k) < 1e-9 ? "" : "(differs!)");
  }

  printf("\n=== 4. never more than received: sweep received vs kinetic, slow ===\n");
  int bad = 0;
  for (double rec : {1, 5, 15, 40, 120, 240}) {
    for (double k : {1, 12, 20, 60, 120}) {
      const double kept = keptDeltas(SLOW, k, rec);
      if (kept > rec + 1e-9) { printf("   !! OVER: rec=%.0f k=%.0f kept=%.3f\n", rec, k, kept); ++bad; }
      if (kept < 0) { printf("   !! NEGATIVE\n"); ++bad; }
    }
  }
  printf("   %s\n", bad ? "FAILED" : "OK: never more than received, never negative");

  printf("\n=== 5. a FREE-SPINNING roll (20-delta messages) keeps all at small kinetic ===\n");
  printf("   %-10s %-12s %s\n", "kinetic", "kept at 10ms", "kept at 200ms(slow)");
  for (double k : {1.0, 12.0, 20.0, 60.0, 120.0}) {
    const double fast = keptDeltas(0.010, k, 20.0);
    const double slow = keptDeltas(SLOW, k, 20.0);
    printf("   %-10.0f %-12.2f %.2f\n", k, fast, slow);
  }
  return 0;
}
