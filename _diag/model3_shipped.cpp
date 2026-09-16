// Confirm MODEL 3.0's actual behaviour at the SHIPPED settings: ease 10..200 ms, tick 1 ms,
// shape = linear ("evenly across the window"). Uses the plugin's own header.
//
//   g++ -std=c++17 -O2 -I../src -o model3_shipped.exe model3_shipped.cpp
//
// Answers three questions:
//   1. one notch: is the amount handed over EVENLY across the window, and all of it?
//   2. a roll: do the windows overlap and sum exactly to the total the wheel reported?
//   3. the shortest window (10 ms): how many deliveries does it become, and how big is each?

#include "anim3_core.h"
#include <cstdio>
#include <initializer_list>

using namespace anim3;

static const double kUnitsPerNotch = 15.0;

struct R {
  double out = 0, maxstep = 0, tEnd = -1;
  long sends = 0;
};

// One notch at `easeMs`, ticked every `tickMs`. The 1/256-unit delivery grid is modelled too, so
// "sends" is the number of actual calls to the action (including the carry).
static R oneNotch(double easeMs, double tickMs, Shape shape) {
  Params P; P.durationMs = easeMs; P.shape = shape;
  Glide g; g.Reset();
  g.Feed(1.0, P);
  const double dt = tickMs / 1000.0, step = 1.0 / 256.0;
  double carry = 0.0;
  R r;
  for (double t = 0.0; t < 1.0; t += dt) {
    const double want = g.Tick(dt, P) * kUnitsPerNotch;      // notches -> 7-bit units
    const double total = carry + want;
    const double m = (total < 0) ? -__builtin_floor(-total / step + 0.5)
                                 : __builtin_floor(total / step + 0.5);
    const double sent = m * step;
    carry = total - sent;
    if (__builtin_fabs(sent) > 1e-12) { ++r.sends; if (__builtin_fabs(sent) > r.maxstep) r.maxstep = __builtin_fabs(sent); }
    r.out += sent;
    if (r.tEnd < 0 && !g.Active()) { r.tEnd = t; break; }
  }
  return r;
}

// A roll: n messages of 1 notch each, gapMs apart. Windows overlap.
static R roll(double easeMs, double tickMs, int n, double gapMs) {
  Params P; P.durationMs = easeMs; P.shape = kEaseOut;
  Glide g; g.Reset();
  const double dt = tickMs / 1000.0;
  const double gap = gapMs / 1000.0;
  double next = 0.0; int fed = 0;
  R r;
  for (double t = 0.0; t < 5.0; t += dt) {
    while (fed < n && t + 1e-12 >= next) { g.Feed(1.0, P); ++fed; next += gap; }
    const double s = g.Tick(dt, P) * kUnitsPerNotch;
    if (__builtin_fabs(s) > 1e-12) { ++r.sends; if (__builtin_fabs(s) > r.maxstep) r.maxstep = __builtin_fabs(s); }
    r.out += s;
    if (r.tEnd < 0 && fed == n && !g.Active()) { r.tEnd = t; break; }
  }
  return r;
}

int main() {
  printf("SHIPPED model 3.0: tick 1 ms, shape = linear (even across the window).\n");
  printf("one notch = %.0f units; the action speaks 7-bit units, delivered on a 1/256 grid.\n\n",
         kUnitsPerNotch);

  printf("--- one notch: total handed over, deliveries, largest delivery ---\n");
  for (double e : {10.0, 20.0, 50.0, 100.0, 200.0}) {
    R r = oneNotch(e, 1.0, kLinear);
    printf("  ease=%5.0f ms | total=%7.4f units (%+.3f%%) calls=%3ld biggest=%6.3f units "
           "(%.1f%% of a notch) done by %4.0f ms\n",
           e, r.out, 100.0 * (r.out - kUnitsPerNotch) / kUnitsPerNotch, r.sends, r.maxstep,
           100.0 * r.maxstep / kUnitsPerNotch, r.tEnd * 1000);
  }

  printf("\n--- 'evenly' check at ease=10ms: the per-ms slice must be ~constant ---\n");
  {
    Params P; P.durationMs = 10.0; P.shape = kEaseOut;
    Glide g; g.Reset(); g.Feed(1.0, P);
    printf("  tick   slice (units)\n");
    for (int i = 0; i < 12; ++i) {
      const double s = g.Tick(0.001, P) * kUnitsPerNotch;
      printf("  %2d ms  %7.4f%s\n", (i + 1), s, (s < -1e-9) ? "  <- past the window" : "");
    }
  }

  printf("\n--- a roll: 5 notches, 60ms apart (windows overlap). total must be 5 notches ---\n");
  for (double e : {10.0, 50.0, 200.0}) {
    R r = roll(e, 1.0, 5, 60.0);
    printf("  ease=%5.0f ms | total=%8.4f units  (5 notches = %.1f)  %+.3f%% | calls=%3ld "
           "biggest=%6.3f units | done by %.0f ms\n",
           e, r.out, 5 * kUnitsPerNotch, 100.0 * (r.out - 5 * kUnitsPerNotch) / (5 * kUnitsPerNotch),
           r.sends, r.maxstep, r.tEnd * 1000);
  }

  printf("\n--- the 15.6 ms clock (what the shipped 2.0 used) at ease=10ms, for contrast ---\n");
  {
    R r = oneNotch(10.0, 15.6, kLinear);
    printf("  total=%7.4f calls=%3ld biggest=%6.3f units (%.1f%% of a notch) done by %4.0f ms\n",
           r.out, r.sends, r.maxstep, 100.0 * r.maxstep / kUnitsPerNotch, r.tEnd * 1000);
    printf("  -> a 15.6ms clock hands the whole 10ms window over LATE and in one lump.\n");
  }
  return 0;
}
