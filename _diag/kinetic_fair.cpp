// FAIR comparison: the SAME TOTAL input over the SAME time, chopped two ways.
//
//   g++ -std=c++17 -O2 -I../src -o kinetic_fair.cpp kinetic_fair.cpp
//
// The previous sweep compared "same nominal rate", but a notched mouse cannot send 150 deltas --
// it sends 120 or 240. So the input totals differed and the comparison was not fair. Here the
// input is fixed exactly: `totalDeltas` over `durationSec`, chopped as N equal messages.
//
// This also isolates the one thing that DOES differ: the very first message of a gesture has no
// previous one, so it is treated as "as slow as it gets". That is deliberate (a lone notch must be
// eaten), but it means the first message is eaten differently on each device. The test shows how
// much that costs, and how it fades as the gesture gets longer.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>

using namespace anim3;
static const double kDeltasPerNotch = 120.0;
static const double kU = 15.0;

static double turn(double totalDeltas, int nMsg, double durationSec, double Dms, double kinetic,
                   double *keptOut)
{
  Params P; P.durationMs = Dms;
  Glide g; g.Reset();
  const double each = totalDeltas / nMsg;
  const double gap = durationSec / nMsg;
  double units = 0, kept = 0, msgTime = 0;
  int fed = 0;
  for (double t = 0; t < durationSec + Dms / 1000.0 + 0.05; t += 0.001) {
    while (fed < nMsg && msgTime <= t + 1e-9) {
      // The plugin's rule: the first message of a gesture counts as "as slow as it gets".
      const double eaten = EatFraction((fed == 0) ? -1.0 : gap, each, kinetic);
      const double k = each * (1.0 - eaten);
      g.Feed(k / kDeltasPerNotch, P);
      kept += k;
      ++fed; msgTime += gap;
    }
    units += g.Tick(0.001, P) * kU;
  }
  *keptOut = kept;
  return units;
}

// Same turn but with the first-message rule removed (first message uses the steady gap), to prove
// that the rule is the ONLY source of the difference.
static double turnNoFirstRule(double totalDeltas, int nMsg, double durationSec, double Dms,
                              double kinetic)
{
  Params P; P.durationMs = Dms;
  Glide g; g.Reset();
  const double each = totalDeltas / nMsg;
  const double gap = durationSec / nMsg;
  double units = 0, msgTime = 0;
  int fed = 0;
  for (double t = 0; t < durationSec + Dms / 1000.0 + 0.05; t += 0.001) {
    while (fed < nMsg && msgTime <= t + 1e-9) {
      const double eaten = EatFraction(gap, each, kinetic); // no special first message
      g.Feed(each * (1.0 - eaten) / kDeltasPerNotch, P);
      ++fed; msgTime += gap;
    }
    units += g.Tick(0.001, P) * kU;
  }
  return units;
}

int main()
{
  const double kinetic = 12.0, D = 300.0;
  printf("kinetic %.0f, ease %.0f ms. FIXED input: 1200 deltas (10 notches) over the stated time.\n\n",
         kinetic, D);

  printf("=== A. 1200 deltas over 1 s, chopped as 10 x 120 vs 60 x 20 ===\n");
  {
    double kn, kf;
    const double un = turn(1200, 10, 1.0, D, kinetic, &kn);
    const double uf = turn(1200, 60, 1.0, D, kinetic, &kf);
    printf("   notched (10 msgs): kept %8.2f delta, %.3f units\n", kn, un);
    printf("   free-spin(60 msgs): kept %8.2f delta, %.3f units\n", kf, uf);
    printf("   difference: %+.2f%%   (the first message of each gesture is treated as slowest)\n",
           100.0 * (kf - kn) / kn);
    const double un2 = turnNoFirstRule(1200, 10, 1.0, D, kinetic);
    const double uf2 = turnNoFirstRule(1200, 60, 1.0, D, kinetic);
    printf("   with the first-message rule removed: %.3f vs %.3f  -> %+.3f%%\n", un2, uf2,
           100.0 * (uf2 - un2) / un2);
  }

  printf("\n=== B. the SAME turn, but longer gestures: does the difference fade? ===\n");
  printf("  %-10s %-14s %-14s %s\n", "duration", "notched kept", "free-spin kept", "diff");
  const double durs[] = {0.5, 1.0, 2.0, 5.0};
  for (int i = 0; i < 4; ++i) {
    const double dur = durs[i];
    double kn, kf;
    turn(1200.0 * dur, (int)(10 * dur), dur, D, kinetic, &kn);
    turn(1200.0 * dur, (int)(60 * dur), dur, D, kinetic, &kf);
    printf("  %5.1f s    %-14.2f %-14.2f %+.2f%%\n", dur, kn, kf, 100.0 * (kf - kn) / kn);
  }

  printf("\n=== C. steady state only (skip the first message): must be identical ===\n");
  {
    // Compare everything AFTER the first message: keep fraction in steady state.
    const double gapN = 1.0 / 10.0, gapF = 1.0 / 60.0; // 1200 deltas/s either way
    const double keepN = 1.0 - EatFraction(gapN, 120.0, kinetic);
    const double keepF = 1.0 - EatFraction(gapF, 20.0, kinetic);
    printf("   notched steady keep: %.2f%% of each 120-delta message = %.1f delta\n",
           100 * keepN, 120 * keepN);
    printf("   free-spin steady keep: %.2f%% of each 20-delta message = %.1f delta\n",
           100 * keepF, 20 * keepF);
    printf("   per-delta: %.4f vs %.4f  -> %s\n", keepN, keepF,
           std::fabs(keepN - keepF) < 1e-9 ? "IDENTICAL per delta" : "differs");
  }
  return 0;
}
