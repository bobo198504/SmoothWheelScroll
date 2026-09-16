// HEAD TO HEAD: the two mechanisms, same experiment, so the data can pick one.
//
//   g++ -std=c++17 -O2 -I../src -o ab_verify.cpp && ./ab_verify.exe   (on Windows: -o ab_verify.exe)
//
// A = "来不及吃"   (current src/anim3_core.h): the window + a FIXED-RATE leak that eats from
//                  everything in flight, proportionally. No speed is measured anywhere.
// B = "角动量"     (the §51 version): at each message, the KEEP fraction is decided from the speed
//                  (deltas per second), then the reduced amount is windowed the same way.
//
// Both use the SAME window delivery, so the only difference is HOW the reduction is decided.
//
// Axes measured:
//   1. DEVICE INDEPENDENCE  - one hand motion (fixed deltas/second), device event size 120..1
//   2. SPEED RESPONSE       - same device, hand speed slow..fast
//   3. SINGLE NOTCH         - one lone notch, each setting
//   4. CONSERVATION         - can it ever hand over MORE than arrived?
//   5. HICCUP               - a roll with a 150 ms pause in the middle: does the output dip?

#include "anim3_core.h"   // mechanism A (real)
#include <cstdio>
#include <cmath>
#include <initializer_list>
#include <string>

static const double kDeltasPerNotch = 120.0;
static const double kUnitsPerNotch = 15.0;
static double unitsOf(double deltas) { return deltas * kUnitsPerNotch / kDeltasPerNotch; }

// ============================ mechanism B (faithful §51) ============================
static const double kB_NotchDeltas = 120.0, kB_SlowGapSec = 0.200, kB_FastGapSec = 0.050;

static double B_KeepFraction(double gapSec, double receivedDeltas, double kineticDeltas) {
  double slowFrac = kineticDeltas / kB_NotchDeltas;
  if (slowFrac < 0.0) slowFrac = 0.0;
  if (slowFrac > 1.0) slowFrac = 1.0;
  if (slowFrac >= 1.0) return 1.0;
  if (gapSec < 0.0) return slowFrac;
  if (receivedDeltas <= 0.0) return slowFrac;
  const double slowRefSec = (receivedDeltas / kB_NotchDeltas) * kB_SlowGapSec;
  if (!(slowRefSec > 0.0)) return 1.0;
  const double x = gapSec / slowRefSec;
  const double fastFrac = kB_FastGapSec / kB_SlowGapSec;
  if (x >= 1.0) return slowFrac;
  if (x <= fastFrac) return 1.0;
  const double u = (1.0 - x) / (1.0 - fastFrac);
  return slowFrac + (1.0 - slowFrac) * u;
}

// B's window: identical machine, but the amount is already reduced at Feed.
struct GlideB {
  static const int kMax = 4096;
  double amt[kMax], age[kMax]; int n; double due;
  void Reset() { n = 0; due = 0; }
  void FeedReduced(double amount, double d) {
    if (amount == 0.0) return;
    if (d <= 0.0) { due += amount; return; }
    if (n < kMax) { amt[n] = amount; age[n] = 0; ++n; } else due += amount;
  }
  double Tick(double dt, double d) {
    if (dt <= 0.0) return 0.0;
    double out = due; due = 0.0;
    if (d <= 0.0) { n = 0; return out; }
    int w = 0;
    for (int i = 0; i < n; ++i) {
      const double left = d - age[i];
      double pay = amt[i];
      if (left > dt) pay = amt[i] * (dt / left);
      out += pay;
      const double keep = amt[i] - pay;
      age[i] += dt;
      if (age[i] < d && fabs(keep) > 1e-9) { amt[w] = keep; age[w] = age[i]; ++w; }
    }
    n = w; return out;
  }
  bool Active() const { return n > 0 || due != 0.0; }
};

// ============================ shared drivers ============================
static const double kDt = 0.0005; // 0.5 ms frames (finer than the 1 ms timer, so discretisation
                                  // is not the thing being measured)

// Run one gesture: `totalDeltas` of hand motion over `Tsec`, events of `perDeltas` each.
// Returns {handDeltas fed, delivered units} for whichever mechanism.
struct Out { double fed, delivered; };
static Out runA(double R, double finenessMs, double totalDeltas, double Tsec, double perDeltas) {
  anim3::Params P; P.durationMs = finenessMs; P.takePerSec = (R * 1000.0) / 8.0;
  P.snap = (1.0 / 8.0) * 0.5;
  anim3::Glide g; g.Reset();
  const double d = finenessMs / 1000.0;
  const double gap = Tsec / (totalDeltas / perDeltas);
  const double amt = unitsOf(perDeltas);
  double out = 0, fed = 0, next = 0;
  for (double t = 0; t < Tsec; t += kDt) {
    while (next <= t + 1e-12) { g.Feed(amt, P); fed += amt; next += gap; }
    out += g.Tick(kDt, P);
  }
  for (double t = 0; t < 8.0 && g.Active(); t += kDt) out += g.Tick(kDt, P);
  return {fed, out};
}
static Out runB(double kinetic, double finenessMs, double totalDeltas, double Tsec, double perDeltas) {
  GlideB g; g.Reset();
  const double d = finenessMs / 1000.0;
  const double gap = Tsec / (totalDeltas / perDeltas);
  const double amt = unitsOf(perDeltas);
  double out = 0, fed = 0, next = 0, lastT = -1;
  for (double t = 0; t < Tsec; t += kDt) {
    while (next <= t + 1e-12) {
      const double gp = (lastT < 0) ? -1.0 : (t - lastT);
      const double keep = B_KeepFraction(gp, perDeltas, kinetic);
      g.FeedReduced(amt * keep, d); fed += amt; lastT = t; next += gap;
    }
    out += g.Tick(kDt, d);
  }
  for (double t = 0; t < 8.0 && g.Active(); t += kDt) out += g.Tick(kDt, d);
  return {fed, out};
}
static double eatenPct(const Out &o) { return 100.0 * (1.0 - o.delivered / o.fed); }

int main() {
  const double Dms = 500.0;      // fineness (B ignores it for the reduction, but the window uses it)
  const double R = 1.0;          // A: resistance 1.00
  const double K = 12.0;         // B: kinetic 12 (slow notch keeps 12/120 = 10%)

  printf("A = 来不及吃 (resistance %.2f)   B = 角动量 (kinetic %.0f)   精细度 %.0f ms\n", R, K, Dms);

  printf("\n========= 1. 设备无关性：同一段手部运动，只改设备每次发多少 delta =========\n");
  printf("   (同一手速：3 秒内 1200 delta = 400 delta/s ≈ 3.3 格/秒)\n\n");
  printf("   %-34s %12s %12s\n", "设备（每次发的 delta）", "A 吃掉", "B 吃掉");
  const double devs[] = {120, 60, 20, 8, 4, 1};
  const char *names[] = {"普通鼠标 1 格 = 120", "60 delta/条", "无级鼠标 20 delta/条",
                         "8 delta/条", "4 delta/条", "1 delta/条（最细）"};
  for (int i = 0; i < 6; ++i) {
    Out a = runA(R, Dms, 1200, 3.0, devs[i]);
    Out b = runB(K, Dms, 1200, 3.0, devs[i]);
    printf("   %-34s %11.1f%% %11.1f%%\n", names[i], eatenPct(a), eatenPct(b));
  }

  printf("\n========= 2. 快慢响应：同一设备(120)，手速从慢到快 =========\n\n");
  printf("   %-22s %12s %12s\n", "手速", "A 吃掉", "B 吃掉");
  const double speeds[] = {100, 200, 400, 800, 1600}; // deltas/sec
  for (int i = 0; i < 5; ++i) {
    const double total = speeds[i] * 3.0;
    Out a = runA(R, Dms, total, 3.0, 120);
    Out b = runB(K, Dms, total, 3.0, 120);
    char lab[64]; snprintf(lab, sizeof(lab), "%4.0f delta/s (%.0f 格/s)", speeds[i], speeds[i] / 120.0);
    printf("   %-22s %11.1f%% %11.1f%%\n", lab, eatenPct(a), eatenPct(b));
  }

  printf("\n========= 3. 孤立一格（只滚一格，停住） =========\n\n");
  for (double per : {120.0, 20.0}) {
    Out a = runA(R, Dms, per, 0.001, per);   // one event
    Out b = runB(K, Dms, per, 0.001, per);
    printf("   一条 %3.0f delta -> A 吃掉 %5.1f%% 付 %6.4f 单位 | B 吃掉 %5.1f%% 付 %6.4f 单位\n",
           per, eatenPct(a), a.delivered, eatenPct(b), b.delivered);
  }

  printf("\n========= 4. 守恒：会不会多给？(最慢 + 最快 + 各设备) =========\n\n");
  double worstA = 0, worstB = 0;
  for (double per : devs) for (double sp : speeds) {
    const double total = sp * 3.0;
    Out a = runA(R, Dms, total, 3.0, per);
    Out b = runB(K, Dms, total, 3.0, per);
    if (a.delivered / a.fed > worstA) worstA = a.delivered / a.fed;
    if (b.delivered / b.fed > worstB) worstB = b.delivered / b.fed;
  }
  printf("   实测最大 交付/收到的比：A = %.4f   B = %.4f   (1.0000 = 永不多给)\n", worstA, worstB);
  printf("   %s\n", (worstA <= 1.0001 && worstB <= 1.0001) ? "两者都守恒" : "有超发！");

  printf("\n========= 5. 顿挫：一次连滚中间卡 150ms，输出会不会掉坑 =========\n\n");
  // 12 notches at 120ms, then a 150 ms pause, then 12 more. Look at the delivery rate around it.
  for (int mech = 0; mech < 2; ++mech) {
    anim3::Glide ga; ga.Reset();
    anim3::Params P; P.durationMs = Dms; P.takePerSec = (R * 1000.0) / 8.0; P.snap = (1.0 / 8.0) * 0.5;
    GlideB gb; gb.Reset();
    double out = 0, lastT = -1, next = 0;
    int fed = 0;
    // schedule: 12 events at 0.12s, pause 0.15s, 12 events at 0.12s
    double times[24]; double t = 0;
    for (int i = 0; i < 12; ++i) { times[i] = t; t += 0.12; }
    t += 0.15;
    for (int i = 12; i < 24; ++i) { times[i] = t; t += 0.12; }
    const double amt = unitsOf(120);
    double bucket = 0; int seg = 0; double segMin = 1e9, segMax = 0;
    double midMin = 1e9, midMax = 0;
    for (double tt = 0; tt <= times[23] + 1.0; tt += kDt) {
      while (fed < 24 && times[fed] <= tt + 1e-12) {
        if (mech == 0) ga.Feed(amt, P);
        else { const double gp = (lastT < 0) ? -1.0 : (times[fed] - lastT); gb.FeedReduced(amt * B_KeepFraction(gp, 120, K), Dms / 1000.0); lastT = times[fed]; }
        ++fed;
      }
      const double s = (mech == 0) ? ga.Tick(kDt, P) : gb.Tick(kDt, Dms / 1000.0);
      bucket += s;
      if (fmod(tt, 0.06) < kDt) {
        // sample every 60 ms
        if (tt > 0.6 && tt < 1.5) { if (bucket < midMin) midMin = bucket; if (bucket > midMax) midMax = bucket; }
        bucket = 0;
      }
    }
    printf("   %s：卡顿窗口(0.6~1.5s)内每 60ms 交付 最小 %.4f / 最大 %.4f  比 %.2f\n",
           mech == 0 ? "A 来不及吃" : "B 角动量  ", midMin, midMax, midMax > 0 ? midMin / midMax : 0);
  }
  return 0;
}
