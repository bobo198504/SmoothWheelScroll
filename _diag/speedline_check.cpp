// VERIFY the straight-line speed response over the range a hand actually uses.
//
//   g++ -std=c++17 -O2 -I../src -o speedline_check.cpp speedline_check.cpp
//
// The requested behaviour, in the plugin's own terms:
//   slow roll  -> a slow notch moves `slowMove` deltas (1 delta of resolution available)
//   fast roll  -> a notch moves the whole 120 deltas (native resolution)
//   and the way between them is a STRAIGHT LINE, with no flat stretch to jump across.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace anim3;
static const double D = 120.0;
static double moved(double gapMs, double slowMove) {
  return D * TravelFraction(gapMs / 1000.0, D, slowMove);
}

int main()
{
  printf("slow end %.0f ms, fast end %.0f ms (one message = one notch).\n\n",
         kSlowGapSec * 1000, kFastGapSec * 1000);

  for (double sm : {1.0, 12.0}) {
    printf("=== slow-move = %.0f deltas ===\n", sm);
    printf("  %-10s %-14s %s\n", "gap ms", "moved (delta)", "step from line above");
    const double g[] = {500, 450, 400, 375, 350, 300, 250, 200, 150, 100, 75, 50, 30};
    double prev = 0;
    for (int i = 0; i < 13; ++i) {
      const double v = moved(g[i], sm);
      printf("  %-10.0f %-14.3f %+8.3f%s\n", g[i], v, i ? v - prev : 0.0,
             g[i] > 400 ? "  (slower than the slow end: clamped)"
             : g[i] < 50 ? "  (faster than the fast end: full notch)" : "");
      prev = v;
    }
    printf("\n");
  }

  printf("=== is it a straight line over the hand's range (400 -> 50 ms)? ===\n");
  for (double sm : {1.0, 12.0}) {
    const double step0 = moved(375, sm) - moved(400, sm);
    double maxErr = 0, prev = moved(400, sm);
    for (double g = 375; g >= 50; g -= 25) {
      const double s = moved(g, sm) - prev;
      if (std::fabs(s - step0) > maxErr) maxErr = std::fabs(s - step0);
      prev = moved(g, sm);
    }
    printf("  slow-move %.0f: step per 25 ms = %.4f, max deviation = %.6f -> %s\n",
           sm, step0, maxErr, maxErr < 1e-9 ? "STRAIGHT" : "!! not straight");
  }

  printf("\n=== the two ends ===\n");
  printf("  slow (400 ms): %.3f delta  (= slow-move, the requested slow resolution)\n", moved(400, 1.0));
  printf("  fast ( 50 ms): %.3f delta  (= the whole notch, native resolution)\n", moved(50, 1.0));
  printf("  a lone notch (gap < 0): %.3f delta\n", D * TravelFraction(-1.0, D, 1.0));

  printf("\n=== device independence: same hand speed, two devices ===\n");
  const double kn = 120.0 * TravelFraction(0.200, 120.0, 12.0);
  const double kf = 20.0 * TravelFraction(20.0 / 600.0, 20.0, 12.0);
  printf("  notched (120 d @200ms): %.4f of 120 = %.2f%%\n", kn, 100 * kn / 120);
  printf("  free-spin( 20 d @33ms): %.4f of 20  = %.2f%%\n", kf, 100 * kf / 20);
  printf("  %s\n", std::fabs(kn / 120 - kf / 20) < 1e-9 ? "same fraction -> device independent"
                                                        : "!! differs");
  return 0;
}
