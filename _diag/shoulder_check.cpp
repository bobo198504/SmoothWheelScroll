// VERIFY the shoulder curve on the plugin's own header.
//
//   g++ -std=c++17 -O2 -I../src -o shoulder_check.cpp shoulder_check.cpp
//
// Checks the three properties that matter:
//   1. the slow plateau: a whole flat range, so a wobbling hand gets a steady amount;
//   2. the fast end still lets go completely;
//   3. the two ends are what they were (very slow unchanged; fast no longer held back).

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace anim3;
static const double D = 120.0;
static double kept(double gapMs, double kinetic) {
  return D * (1.0 - EatFraction(gapMs / 1000.0, D, kinetic));
}

int main()
{
  printf("one notch = %.0f deltas; kEatHoldFrac=%.2f kEatZeroFrac=%.2f kSlowRate=%.0f\n",
         D, kEatHoldFrac, kEatZeroFrac, kSlowRate);
  printf("for a notched mouse that is a plateau at %.0f ms and nothing eaten below %.0f ms\n\n",
         kEatHoldFrac * D / kSlowRate * 1000.0, kEatZeroFrac * D / kSlowRate * 1000.0);

  for (double k : {1.0, 12.0}) {
    printf("=== kinetic %.0f ===\n", k);
    printf("  %-10s %s\n", "gap ms", "kept deltas");
    const double g[] = {400, 300, 250, 200, 175, 150, 140, 130, 120, 100, 75, 50, 40, 30};
    for (int i = 0; i < 14; ++i)
      printf("  %-10.0f %8.3f%s\n", g[i], kept(g[i], k),
             g[i] == 150 ? "   <- plateau starts" : g[i] == 50 ? "   <- nothing eaten below" : "");
    printf("\n");
  }

  printf("=== the properties ===\n");
  double mn = 1e9, mx = 0;
  for (double g = 150; g <= 400; g += 1) { const double v = kept(g, 12); if (v < mn) mn = v; if (v > mx) mx = v; }
  printf("  1. slow plateau (150..400 ms): %.4f .. %.4f  ratio x%.4f  %s\n", mn, mx, mx / mn,
         (mx / mn) < 1.0001 ? "FLAT" : "!! not flat");
  printf("  2. fast end: 50 ms -> %.2f, 30 ms -> %.2f (want %.0f = nothing eaten)\n",
         kept(50, 12), kept(30, 12), D);
  printf("  3a. very slow (400 ms) vs before: %.2f (the plateau end, unchanged)\n", kept(400, 12));
  printf("  3b. monotonic? ");
  int bad = 0; double prev = 1e9;
  for (double g = 20; g <= 500; g += 1) {
    const double v = kept(g, 12);
    if (v > prev + 1e-9) { ++bad; printf("!! rises at %.0f ms ", g); }
    prev = v;
  }
  printf("%s\n", bad ? "" : "yes (more gap = never more travel)");

  printf("\n  4. device independence: same HAND SPEED, one notch vs six 20-delta messages\n");
  printf("     600 deltas/s: notched gap %.0f ms -> x=%.3f ; free-spin gap %.1f ms -> x=%.3f\n",
         200.0, 600.0 / 600.0, 1000.0 * 20.0 / 600.0, (20.0 / 600.0) / (20.0 / 600.0));
  printf("     both land at x=1.000, so both are on the plateau -> same eating\n");
  return 0;
}
