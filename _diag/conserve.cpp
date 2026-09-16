// THE QUESTION: does adding the Shape curve break "take how much, give how much"?
//
//   g++ -std=c++17 -O2 -I../src -o conserve.cpp conserve.cpp
//
// The answer must be YES it still conserves, because the curve only decides WHEN within a window
// the amount is handed over -- S(0)=0 and S(1)=1, so a window that runs to the end hands over
// 100% of its amount whatever the curve is. This measures it for real: through the 1/256-unit
// delivery grid, with the carry kept across frames, for many bend values, roll speeds, and a long
// run that would expose any accumulating drift.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <initializer_list>

using namespace anim3;
static const double kU = 15.0;

// Feed `n` notches every `gapMs`, tick at 1 ms, deliver on the 1/256-unit grid with carry.
// Returns (units in, units out).
static void roll(int n, double gapMs, double Dms, double bend, double *in, double *out) {
  Params P; P.durationMs = Dms; P.shape = kSymS; P.bend = bend;
  Glide g; g.Reset();
  const double dt = 0.001, grid = 1.0 / 256.0;
  const double gap = gapMs / 1000.0;
  double nextMsg = 0, carry = 0, o = 0, i = 0;
  int fed = 0;
  const double end = n * gap + Dms / 1000.0 + 0.2;
  for (double t = 0; t < end; t += dt) {
    while (fed < n && nextMsg <= t + 1e-9) { g.Feed(1.0, P); i += kU; ++fed; nextMsg += gap; }
    const double want = g.Tick(dt, P) * kU;
    const double tot = carry + want;
    const double m = (tot < 0) ? -std::floor(-tot / grid + 0.5) : std::floor(tot / grid + 0.5);
    const double deliv = m * grid;
    carry = tot - deliv;
    o += deliv;
  }
  *in = i; *out = o;
}

// One notch only, at one bend.
static void one(double Dms, double bend) {
  double i, o;
  roll(1, 0, Dms, bend, &i, &o);
  printf("  1 notch, D=%3.0f ms, bend=%.1f -> in %7.1f out %7.4f  (%+.6f%%)\n",
         Dms, bend, i, o, 100 * (o - i) / i);
}

int main() {
  printf("one notch = %.0f units; 1/256-unit delivery grid with carry; tick 1 ms.\n\n", kU);

  printf("=== A. ONE notch, every bend (the simplest case) ===\n");
  for (double b : {0.0, 0.25, 0.5, 1.0}) one(300.0, b);

  printf("\n=== B. 10 notches at three roll speeds, bend swept ===\n");
  for (double gap : {250.0, 60.0, 33.0}) {
    for (double b : {0.0, 0.5, 1.0}) {
      double i, o; roll(10, gap, 300.0, b, &i, &o);
      printf("  10 @%3.0f ms, bend=%.1f -> in %7.1f out %7.3f  (%+.5f%%)\n",
             gap, b, i, o, 100 * (o - i) / i);
    }
  }

  printf("\n=== C. a LONG run (would expose any drift) ===\n");
  for (double b : {0.0, 0.5, 1.0}) {
    double i, o; roll(2000, 20.0, 300.0, b, &i, &o);
    printf("  2000 notches @20 ms, bend=%.1f -> in %9.1f out %12.4f  (%+.6f%%)\n",
           b, i, o, 100 * (o - i) / i);
  }

  printf("\n=== D. VERY fast, windows heavily overlapped ===\n");
  for (double b : {0.0, 0.5, 1.0}) {
    double i, o; roll(500, 2.0, 300.0, b, &i, &o);
    printf("  500 notches @2 ms (500/s), bend=%.1f -> in %9.1f out %12.4f  (%+.6f%%)\n",
           b, i, o, 100 * (o - i) / i);
  }

  printf("\n=== E. one notch, tiny and huge durations ===\n");
  for (double b : {0.5, 1.0}) {
    one(10.0, b);
    one(20.0, b);
  }

  printf("\n=== F. does a roll's TOTAL grow with bend? (it must not) ===\n");
  for (double b : {0.0, 0.5, 1.0}) {
    double i, o; roll(10, 60.0, 300.0, b, &i, &o);
    printf("  bend=%.1f -> total %8.4f units (= %.4f notches; 10 went in)\n",
           b, o, o / kU);
  }
  return 0;
}
