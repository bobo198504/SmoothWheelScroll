// WHY a fixed take-per-time hits the free-spinner harder: measure both devices at the same hand
// speed, and test the fix "scale the take by the message's size relative to a notch".
//
//   g++ -std=c++17 -O2 -o device_gap.cpp device_gap.cpp
//
// Both devices describe the SAME turn: 120 deltas per 200 ms (that is 1 notch per 200 ms).
//   notched  : 1 message of 120 deltas every 200 ms
//   free-spin: 6 messages of  20 deltas every 33.3 ms
// Resistance is "N deltas per 5 ms" -> N*200 deltas/s.
//
// CURRENT: take = R per second, whatever the message size.
// FIXED  : take = R * (messageDeltas/120) per second, so a sixth-size message is taken at a
//          sixth of the rate -- and it arrives six times as often, so the totals agree.

#include <cstdio>
#include <cmath>

static double run(int msgs, double eachDeltas, double gapMs, double easeMs, double resist,
                  bool scaled)
{
  const double R = resist * 200.0;                 // deltas per second
  const double dur = easeMs / 1000.0;
  const double dt = 0.001;
  const double gap = gapMs / 1000.0;
  double owed = 0, out = 0, msgTime = 0;
  int fed = 0;
  const double end = msgs * gap + 4.0;
  for (double t = 0; t < end; t += dt) {
    if (fed < msgs && msgTime <= t + 1e-9) {
      owed += eachDeltas;
      ++fed; msgTime += gap;
    }
    if (owed == 0.0) continue;
    double mag = owed;
    // the take rate, optionally scaled by this device's message size
    double rate = R;
    if (scaled) {
      const double frac = (eachDeltas / 120.0 > 1.0) ? 1.0 : eachDeltas / 120.0;
      rate = R * frac;
    }
    double take = rate * dt;
    if (take > mag) take = mag;
    mag -= take;
    double pay = (dur > 0.0) ? mag * (dt / dur) : mag;
    mag -= pay;
    if (mag < 0.004) mag = 0.0;
    owed = mag;
    out += pay;
  }
  return out;
}

int main()
{
  printf("same turn: 120 deltas / 200 ms. retained %% of what went in.\n\n");
  printf("  %-9s | %-24s | %-24s\n", "", "CURRENT (absolute take)", "FIXED (scaled by message)");
  printf("  %-9s | %-11s %-12s | %-11s %-12s\n", "resistance",
         "notched", "free-spin", "notched", "free-spin");
  const double rs[] = {1, 12, 60, 110};
  for (int i = 0; i < 4; ++i) {
    const double r = rs[i];
    // 5 turns (1 second) each
    const double n = run(5, 120.0, 200.0, 300.0, r, false);
    const double f = run(30, 20.0, 200.0 / 6, 300.0, r, false);
    const double n2 = run(5, 120.0, 200.0, 300.0, r, true);
    const double f2 = run(30, 20.0, 200.0 / 6, 300.0, r, true);
    printf("  %-9.0f | %8.1f%% %11.1f%% | %8.1f%% %11.1f%%%s\n", r,
           100 * n / 600, 100 * f / 600, 100 * n2 / 600, 100 * f2 / 600,
           (std::fabs(f2 - n2) < 2.0) ? "   <- now equal" : "");
  }
  return 0;
}
