// WHY the free-spinner is annihilated, and the fix: eat and pay must share the pool FAIRLY.
//
//   g++ -std=c++17 -O2 -o fair_split.cpp fair_split.cpp
//
// The bug: on each tick resistance takes R*dt from the pool, and only THEN does easing pay a share
// of what is left. With R = 22 deltas per ms, a fine device's 20-delta message is swallowed by the
// very first tick, before easing has touched it. A notched mouse's 120-delta message survives a few
// ticks, so it collects some payout -- the two devices disagree badly.
//
//   variant 0  eat first, then pay (what the DLL does now)
//   variant 1  fair split: if eat+pay needs more than the pool, scale BOTH by the same factor
//
// Device independence is the point: the same turn must retain the same share on both.

#include <cstdio>
#include <cmath>

static const double kDPU = 8.0;

// variant 0 = eat-then-pay, 1 = fair split
static double run(int msgs, double each, double gapMs, double easeMs, double resist, int variant)
{
  const double R = (resist * 200.0) / kDPU; // units per second
  const double dur = easeMs / 1000.0;
  const double dt = 0.001, gap = gapMs / 1000.0;
  double owed = 0, out = 0, msgTime = 0;
  int fed = 0;
  for (double t = 0; t < msgs * gap + 6.0; t += dt) {
    if (fed < msgs && msgTime <= t + 1e-9) { owed += each / kDPU; ++fed; msgTime += gap; }
    if (owed == 0.0) continue;
    double mag = owed;
    const double wantEat = R * dt;
    const double wantPay = (dur > 0.0) ? mag * (dt / dur) : mag;
    double eat, pay;
    if (variant == 0) {
      eat = (wantEat > mag) ? mag : wantEat;
      const double left = mag - eat;
      pay = (dur > 0.0) ? left * (dt / dur) : left;
    } else {
      const double need = wantEat + wantPay;
      const double k = (need > mag) ? (mag / need) : 1.0;
      eat = wantEat * k;
      pay = wantPay * k;
    }
    mag -= (eat + pay);
    if (mag < 0.004) mag = 0.0;
    owed = mag;
    out += pay;
  }
  return out * kDPU;
}

int main()
{
  printf("same turn: 120 deltas / 200 ms. retained %%.\n\n");
  printf("  %-9s | %-26s | %-26s\n", "", "variant 0: eat then pay", "variant 1: fair split");
  printf("  %-9s | %-11s %-14s | %-11s %-14s\n", "resist",
         "notched", "free-spin", "notched", "free-spin");
  const double rs[] = {1, 5, 12, 60, 110};
  for (int i = 0; i < 5; ++i) {
    const double r = rs[i];
    const double a = 100 * run(5, 120.0, 200.0, 10.0, r, 0) / 600;
    const double b = 100 * run(30, 20.0, 200.0 / 6, 10.0, r, 0) / 600;
    const double c = 100 * run(5, 120.0, 200.0, 10.0, r, 1) / 600;
    const double d = 100 * run(30, 20.0, 200.0 / 6, 10.0, r, 1) / 600;
    printf("  %-9.0f | %8.1f%% %11.1f%% | %8.1f%% %11.1f%%   %s\n", r, a, b, c, d,
           std::fabs(d - c) < 2.0 ? "fair split AGREES" : "");
  }

  printf("\n=== at ease 300 ms (the longer easing) ===\n");
  for (int i = 0; i < 5; ++i) {
    const double r = rs[i];
    const double a = 100 * run(5, 120.0, 200.0, 300.0, r, 0) / 600;
    const double b = 100 * run(30, 20.0, 200.0 / 6, 300.0, r, 0) / 600;
    const double c = 100 * run(5, 120.0, 200.0, 300.0, r, 1) / 600;
    const double d = 100 * run(30, 20.0, 200.0 / 6, 300.0, r, 1) / 600;
    printf("  resist %-4.0f | old %6.1f%% %6.1f%% | fair %6.1f%% %6.1f%%   %s\n",
           r, a, b, c, d, std::fabs(d - c) < 2.0 ? "AGREES" : "");
  }
  return 0;
}
