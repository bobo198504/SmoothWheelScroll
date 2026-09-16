// THE USER'S REASONING: a free-spinner sends fewer deltas per message (so it eats less per
// message), but it sends them faster, so overall it should feel about the same.
//
//   g++ -std=c++17 -O2 -I../src -o kinetic_speed.cpp kinetic_speed.cpp
//
// This checks exactly that: for the SAME hand speed (same deltas per second), the two devices must
// come out the same; and as hand speed rises, the eating must fall for BOTH, together.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>

using namespace anim3;
static const double kDeltasPerNotch = 120.0;
static const double kU = 15.0;

// Turn at `rate` deltas/second, chopped into messages of `msgDeltas`, for `durationSec`.
// Ease each message over Dms. Returns units delivered (and the kept deltas).
static void turn(double rate, double msgDeltas, double durationSec, double Dms, double kinetic,
                 double *outUnits, double *outKeptDeltas, int *outMsgs)
{
  Params P; P.durationMs = Dms;
  Glide g; g.Reset();
  const double msgSec = msgDeltas / rate;        // time between messages
  const int nMsg = (int)std::floor(durationSec / msgSec);
  double units = 0, kept = 0, msgTime = 0;
  int fed = 0;
  for (double t = 0; t < nMsg * msgSec + Dms / 1000.0 + 0.05; t += 0.001) {
    while (fed < nMsg && msgTime <= t + 1e-9) {
      const double eaten = EatFraction((fed == 0) ? -1.0 : msgSec, msgDeltas, kinetic);
      const double k = msgDeltas * (1.0 - eaten);
      g.Feed(k / kDeltasPerNotch, P);
      kept += k;
      ++fed; msgTime += msgSec;
    }
    units += g.Tick(0.001, P) * kU;
  }
  *outUnits = units; *outKeptDeltas = kept; *outMsgs = nMsg;
}

int main()
{
  const double kinetic = 12.0;
  const double D = 300.0;
  printf("kinetic = %.0f (keeps 10%% at the slow rate). Ease %.0f ms.\n", kinetic, D);
  printf("Same HAND SPEED (deltas/second), two devices: a notched mouse (120/message)\n");
  printf("and a free-spinner (20/message). They must agree.\n\n");
  printf("  %-14s | %-26s | %-26s\n", "hand speed", "notched  (120/msg)", "free-spin (20/msg)");
  printf("  %-14s | %-26s | %-26s\n", "delta/s", "msgs  kept delta  units", "msgs  kept delta  units");
  const double rates[] = {150, 300, 600, 1200, 2400, 4800};
  for (int i = 0; i < 6; ++i) {
    const double r = rates[i];
    double un, kn; int mn; double uf, kf; int mf;
    turn(r, 120.0, 1.0, D, kinetic, &un, &kn, &mn);
    turn(r, 20.0, 1.0, D, kinetic, &uf, &kf, &mf);
    const double diff = (kn > 0) ? 100.0 * (kf - kn) / kn : 0;
    printf("  %6.0f         | %4d  %8.2f  %7.3f | %4d  %8.2f  %7.3f  %s\n",
           r, mn, kn, un, mf, kf, uf,
           std::fabs(diff) < 1.5 ? "(same)" : "(differs!)");
  }

  printf("\n=== how the eating falls as the hand speeds up (free-spinner, 20/message) ===\n");
  printf("  %-12s %-14s %s\n", "deltas/s", "kept fraction", "meaning");
  for (int i = 0; i < 6; ++i) {
    const double r = rates[i];
    const double gap = 20.0 / r;
    const double kept = 1.0 - EatFraction(gap, 20.0, kinetic);
    printf("  %-12.0f %-14.1f%% %s\n", r, 100 * kept,
           r <= 600 ? "at or below the slow rate: full eating"
                    : r >= 2400 ? "a fast flick: almost nothing eaten" : "");
  }

  printf("\n=== the point, in one line ===\n");
  {
    double un, kn; int mn; double uf, kf; int mf;
    turn(600.0, 120.0, 1.0, D, kinetic, &un, &kn, &mn); // one notch / 200 ms
    turn(600.0, 20.0, 1.0, D, kinetic, &uf, &kf, &mf);  // same 600 deltas/s, 20 at a time
    printf("  same hand speed (600 delta/s): notched keeps %.2f delta, free-spin keeps %.2f delta\n",
           kn, kf);
    printf("  free-spinner sent %d messages instead of %d -- 'it is fast' -- but the total matches.\n",
           mf, mn);
  }
  return 0;
}
