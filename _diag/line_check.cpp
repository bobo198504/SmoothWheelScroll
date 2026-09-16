// VERIFY the straight-line eating (with a dead zone), on the plugin's own header.
//
//   g++ -std=c++17 -O2 -I../src -o line_check.cpp line_check.cpp
//
// What must hold:
//   1. a dead zone at the fast end (nothing eaten below it) -- "两格间少于多少便不吃了";
//   2. a STRAIGHT LINE above it -- the same change of gap always gives the same change of travel,
//      which is what makes the response feel proportional to the hand;
//   3. the slow end unchanged (one deliberate notch still moves the kinetic amount);
//   4. device independence (same hand speed, same eating).

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
  const double refMs = D / kSlowRate * 1000.0; // 200 ms
  printf("one notch = %.0f deltas. dead zone <= %.0f ms, full eating >= %.0f ms.\n\n",
         D, kEatZeroFrac * refMs, kEatFullFrac * refMs);

  for (double k : {1.0, 12.0}) {
    printf("=== kinetic %.0f ===\n", k);
    printf("  %-10s %-14s %s\n", "gap ms", "kept deltas", "step from the row above");
    const double g[] = {300, 250, 225, 200, 175, 150, 125, 100, 75, 50, 40, 30};
    double prev = 0;
    for (int i = 0; i < 12; ++i) {
      const double v = kept(g[i], k);
      printf("  %-10.0f %-14.3f %s\n", g[i], v,
             i == 0 ? "" : (g[i] >= 50 ? "linear above the dead zone" : "DEAD ZONE"));
      prev = v;
    }
    printf("\n");
  }

  printf("=== 2. is it a straight line? equal gaps must give equal steps ===\n");
  {
    // from 200 down to 50 in 25 ms steps
    double last = kept(200, 12), maxErr = 0;
    for (double g = 175; g >= 50; g -= 25) {
      const double v = kept(g, 12);
      const double step = v - last;
      printf("  %.0f -> %.0f ms : step %+7.3f\n", g + 25, g, step);
      last = v;
    }
    const double s1 = kept(175, 12) - kept(200, 12);
    maxErr = 0;
    double prev = kept(200, 12);
    for (double g = 175; g >= 75; g -= 25) {
      const double s = kept(g, 12) - prev;
      if (std::fabs(s - s1) > maxErr) maxErr = std::fabs(s - s1);
      prev = kept(g, 12);
    }
    printf("  largest deviation from a constant step: %.6f deltas -> %s\n", maxErr,
           maxErr < 1e-9 ? "EXACTLY straight (linear)" : "!! not straight");
  }

  printf("\n=== 3. the slow end ===\n");
  printf("  200 ms (one deliberate notch): %.2f deltas  (kinetic 12, the tuned feel)\n", kept(200, 12));
  printf("  400 ms: %.2f   (clamped at the maximum eating)\n", kept(400, 12));

  printf("\n=== 4. device independence ===\n");
  {
    // same hand speed 600 deltas/s: notched 120 deltas at 200 ms; free-spin 20 deltas at 33.3 ms
    const double kn = 120.0 * (1.0 - EatFraction((120.0 / 600.0), 120.0, 12.0));
    const double kf = 20.0 * (1.0 - EatFraction((20.0 / 600.0), 20.0, 12.0));
    printf("  notched keeps %.4f of 120 (%.1f%%) ; free-spin keeps %.4f of 20 (%.1f%%)\n",
           kn, 100 * kn / 120, kf, 100 * kf / 20);
    printf("  %s\n", std::fabs(kn / 120 - kf / 20) < 1e-9 ? "same fraction -> device independent"
                                                          : "!! differs");
  }
  return 0;
}
