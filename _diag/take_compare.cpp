// Compare three ways to apply a fixed take-per-second, on two devices describing the SAME turn.
//
//   g++ -std=c++17 -O2 -o take_compare.cpp take_compare.cpp
//
//   A pool        : every tick, take T*dt from the total (the version now in the DLL)
//   B pool*frac   : same, but the take is scaled by the message's share of a notch
//   C on-arrival  : when a message arrives, take T * (time since the previous message) from it,
//                   then add the rest to the total. "As soon as a parameter arrives, eat by time".
//
// C should be exactly device-independent: the same real seconds pass for both devices, so the same
// amount is taken per second, no matter how the turn was chopped up.

#include <cstdio>
#include <cmath>

static const double kDeltasPerUnit = 8.0;

// mode: 0 = pool, 1 = pool*frac, 2 = on-arrival. gapMsFirst = elapsed assumed before the 1st message.
static double run(int msgs, double eachDeltas, double gapMs, double easeMs, double resist,
                  int mode, double firstGapMs)
{
  const double T = resist * 200.0;   // deltas per second
  const double dur = easeMs / 1000.0;
  const double dt = 0.001, gap = gapMs / 1000.0;
  double owed = 0, out = 0, msgTime = 0, lastMsg = -1;
  int fed = 0;
  for (double t = 0; t < msgs * gap + 6.0; t += dt) {
    if (fed < msgs && msgTime <= t + 1e-9) {
      const double elapsed = (lastMsg < 0.0) ? (firstGapMs / 1000.0) : (t - lastMsg);
      lastMsg = t;
      if (mode == 2) {
        double take = T * elapsed;
        if (take > eachDeltas) take = eachDeltas;
        owed += eachDeltas - take;      // the rest is what is owed
      } else {
        owed += eachDeltas;
      }
      ++fed; msgTime += gap;
    }
    if (owed == 0.0) continue;
    double mag = owed;
    if (mode != 2 && T > 0.0) {
      double rate = T;
      if (mode == 1) rate = T * (eachDeltas / 120.0);
      double take = rate * dt;
      if (take > mag) take = mag;
      mag -= take;
    }
    const double pay = (dur > 0.0) ? mag * (dt / dur) : mag;
    mag -= pay;
    if (mag < 0.004) mag = 0.0;
    owed = mag; out += pay;
  }
  return out;
}

int main()
{
  printf("same turn: 120 deltas / 200 ms. retained %%.\n");
  printf("(first message of a gesture is assumed to have followed a 200 ms gap)\n\n");
  printf("  %-10s | %-24s | %-24s | %-24s\n", "", "A pool", "B pool x frac", "C on-arrival");
  printf("  %-10s | %-11s %-12s | %-11s %-12s | %-11s %-12s\n", "resistance",
         "notched", "free", "notched", "free", "notched", "free");
  const double rs[] = {1, 12, 60, 110};
  for (int i = 0; i < 4; ++i) {
    const double r = rs[i];
    double v[3][2];
    for (int m = 0; m < 3; ++m) {
      v[m][0] = 100 * run(5, 120.0, 200.0, 300.0, r, m, 200.0) / 600;
      v[m][1] = 100 * run(30, 20.0, 200.0 / 6, 300.0, r, m, 200.0) / 600;
    }
    printf("  %-10.0f | %8.1f%% %10.1f%% | %8.1f%% %10.1f%% | %8.1f%% %10.1f%%\n",
           r, v[0][0], v[0][1], v[1][0], v[1][1], v[2][0], v[2][1]);
  }
  printf("\n  'notched' and 'free' should read the same in a good column.\n");
  return 0;
}
