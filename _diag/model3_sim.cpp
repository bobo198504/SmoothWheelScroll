// Harness for MODEL 3.0. Measures the model's OWN properties -- never compares to any other model.
//
//   g++ -std=c++17 -O2 -o model3_sim.exe model3_sim.cpp
//
// The four numbers reported per run:
//   out        total given back (must equal what went in: the defining property)
//   sends      how many deliveries were made (more = finer stream)
//   maxstep    the largest single delivery, in notches (the visible jump; smaller = smoother)
//   t95/t99    when 95% / 99% of the motion had landed (the lag the user feels)
//   ripple     smallest/largest delivery rate while the device is still feeding.
//              1.00 = perfectly even output; low = the output pulses in step with the clicks.

#include "model3.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <initializer_list>

using namespace model3;

static const double kUnitsPerNotch = 15.0;

static const char *RuleName(Rule r) {
  switch (r) {
  case kLeaky: return "leaky";
  case kLinear: return "linear";
  case kSmooth: return "smooth";
  case kSmoothFast: return "smooth+";
  case kTrap: return "trapezoid";
  }
  return "?";
}

struct Result {
  double out = 0, maxstep = 0, t95 = -1, t99 = -1, ripple = 0;
  long sends = 0;
};

// Feed nMsg messages of (total/nMsg) each, spread evenly over T seconds; tick every dtMs.
static Result run(Rule rule, double total, int nMsg, double T, double durMs, double dtMs)
{
  Params P;
  P.rule = rule;
  P.durationMs = durMs;
  P.tauMs = durMs;     // leaky uses the same number so the comparison is fair
  Glide g;
  g.Reset();

  const double dt = dtMs / 1000.0;
  const double gap = (nMsg > 1) ? T / nMsg : 0.0;
  const double end = T + 12.0 * (durMs / 1000.0) + 0.5;
  double nextMsg = 0.0, carry = 0.0;
  int sent = 0;
  double rmin = 1e30, rmax = 0.0;

  Result R;
  for (double t = 0.0; t <= end; t += dt) {
    while (sent < nMsg && t + 1e-12 >= nextMsg) { g.Feed(total / nMsg, P); ++sent; nextMsg += gap; }
    const double want = g.Tick(dt, P);
    const double got = Quantize(want, &carry);
    const double a = std::fabs(got);
    if (a > R.maxstep) R.maxstep = a;
    if (a > 0.0) ++R.sends;
    R.out += got;
    if (R.t95 < 0.0 && std::fabs(R.out) >= 0.95 * std::fabs(total)) R.t95 = t;
    if (R.t99 < 0.0 && std::fabs(R.out) >= 0.99 * std::fabs(total)) R.t99 = t;
    if (sent > 1 && t > T * 0.25 && t <= T) {
      const double rate = std::fabs(want) / dt;
      if (rate < rmin) rmin = rate;
      if (rate > rmax) rmax = rate;
    }
  }
  R.ripple = (rmin > 1e29 || rmax <= 0.0) ? 0.0 : rmin / rmax;
  return R;
}

static void line(const char *label, Rule rule, double total, int nMsg, double T, double durMs,
                 double dtMs)
{
  const Result R = run(rule, total, nMsg, T, durMs, dtMs);
  printf("%-11s %-9s %2d x %7.4f T=%5.0fms D=%3.0fms | out=%9.6f (%+.3f%%) sends=%5ld "
         "maxstep=%7.4fnt=%6.3fu  t95=%5.0f t99=%5.0f  ripple=%.2f\n",
         RuleName(rule), label, nMsg, total / nMsg, T * 1000, durMs, R.out,
         100.0 * (R.out - total) / total, R.sends, R.maxstep, R.maxstep * kUnitsPerNotch,
         R.t95 * 1000, R.t99 * 1000, R.ripple);
}

int main(int argc, char **argv)
{
  const bool all = (argc > 1 && std::strcmp(argv[1], "all") == 0);
  printf("one notch = %.1f units = raw delta 120; finest reachable step = 1/3840 notch (%.4f unit)\n\n",
         kUnitsPerNotch, kUnitsPerNotch / 3840.0);

  printf("=== 1. THE DEFINING PROPERTY: same rotation, four device reporting styles ===\n");
  printf("    (1x5.0 / 5x1.0 / 30x0.167 / 120x0.0417 all describe ONE 5-notch turn)\n\n");
  for (Rule r : {(Rule)kSmooth}) {
    line("1 msg", r, 5.0, 1, 0.0, 150, 4);
    line("5 msgs", r, 5.0, 5, 0.4, 150, 4);
    line("30 msgs", r, 5.0, 30, 0.4, 150, 4);
    line("120 msgs", r, 5.0, 120, 0.4, 150, 4);
  }
  printf("\n    all four give back the SAME total -> the model cannot tell the devices apart.\n");

  printf("\n=== 2. ALL FIVE RULES SIDE BY SIDE (single notch, D/tau = 150ms, tick 4ms) ===\n\n");
  for (Rule r : {(Rule)kLeaky, (Rule)kLinear, (Rule)kSmooth, (Rule)kSmoothFast, (Rule)kTrap})
    line("1 notch", r, 1.0, 1, 0.0, 150, 4);
  printf("\n=== 2b. the same five at the SHIPPED tick rate (15.6ms) -- does the shape still help? ===\n\n");
  for (Rule r : {(Rule)kLinear, (Rule)kSmooth, (Rule)kTrap})
    line("1 notch", r, 1.0, 1, 0.0, 150, 15.6);

  printf("\n=== 3. THE ONE KNOB: duration/tau sweep, single notch, tick 4ms ===\n\n");
  for (double D : {40, 60, 80, 100, 150, 200, 300}) {
    line("1 notch", kSmooth, 1.0, 1, 0.0, D, 4);
  }
  printf("\n");
  for (double D : {60, 100, 150, 200, 300}) {
    line("1 notch", kLeaky, 1.0, 1, 0.0, D, 4);
  }

  printf("\n=== 4. ROLL SPEED: same 5-notch turn, turned slowly vs flicked (D=150ms) ===\n\n");
  for (double T : {0.2, 0.4, 1.0, 2.0}) {
    char b[32];
    snprintf(b, sizeof(b), "T=%.0fms", T * 1000);
    line(b, kSmooth, 5.0, 5, T, 150, 4);
  }
  printf("\n    total is the same at every speed: no build-up, no energy. Take = give.\n");

  printf("\n=== 5. THE NOTCHED MOUSE'S CLICK RIPPLE (5 clicks / 400ms) ===\n\n");
  printf("    can a longer duration smooth the click-pulses away?\n");
  for (double D : {60, 100, 150, 200, 300, 400}) {
    line("5 clicks", kSmooth, 5.0, 5, 0.4, D, 4);
  }
  printf("\n");
  for (double D : {40, 60, 100, 150}) {
    line("30 fine", kSmooth, 5.0, 30, 0.4, D, 4);
  }

  printf("\n=== 6. TICK RATE: changes granularity, never the total ===\n\n");
  for (double dt : {15.6, 5.0, 4.0, 1.0})
    line("1 notch", kSmooth, 1.0, 1, 0.0, 150, dt);

  printf("\n=== 7. STRESS: a long fast free-spin (200 msgs over 400ms), does anything break? ===\n\n");
  for (Rule r : {(Rule)kSmooth, (Rule)kSmoothFast, (Rule)kLeaky})
    line("200 msgs", r, 20.0, 200, 0.4, 150, 4);

  if (all) {
    printf("\n=== 8. RIPPLE vs DURATION vs SPEED (the fine-grained table) ===\n\n");
    for (double T : {0.2, 0.4, 0.8}) {
      for (double D : {60, 100, 150, 200, 300}) {
        char b[32];
        snprintf(b, sizeof(b), "T=%.0fms", T * 1000);
        line(b, kSmooth, 5.0, 5, T, D, 4);
      }
      printf("\n");
    }
  }
  return 0;
}
