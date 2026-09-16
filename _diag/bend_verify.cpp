// Verify MODEL 3.0's new symmetric S-curve shape and its `bend` knob, using the plugin's header.
//
//   g++ -std=c++17 -O2 -I../src -o bend_verify.exe bend_verify.cpp
//
// Checks the properties the shape MUST have:
//   1. bend = 0 is EXACTLY the old linear spread (so nothing regresses at the low end);
//   2. the curve is symmetric: S(1-u) = 1 - S(u);
//   3. it is monotonic (rate never goes negative, so the action is never driven backwards);
//   4. the peak rate equals 1 + 3*bend (the number the panel shows);
//   5. a window still delivers exactly its whole amount (total conservation).

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace anim3;
static const double kU = 15.0;

static double symS(double bend, double u) { return ShapeFrac(kSymS, bend, u); }

int main() {
  printf("MODEL 3.0 symmetric S-curve. bend 0..1; peak rate = 1 + 3*bend.\n\n");

  printf("=== 1. bend = 0 must equal the old linear spread, exactly ===\n");
  double worst = 0;
  for (int i = 0; i <= 1000; ++i) {
    const double u = i / 1000.0;
    const double d = std::fabs(symS(0.0, u) - u);
    if (d > worst) worst = d;
  }
  printf("  max |S_sym(bend=0, u) - u| over 1001 samples = %.3e  %s\n\n", worst,
         worst < 1e-12 ? "OK (linear)" : "!! NOT linear");

  printf("=== 2. symmetry S(1-u) = 1 - S(u), and 3. monotonicity ===\n");
  for (double bend : {0.0, 0.25, 0.5, 0.75, 1.0}) {
    double asym = 0, minRate = 1e30, maxRate = 0, prev = 0;
    bool mono = true;
    const int N = 20000;
    for (int i = 0; i <= N; ++i) {
      const double u = (double)i / N;
      const double a = symS(bend, u), b = 1.0 - symS(bend, 1.0 - u);
      if (std::fabs(a - b) > asym) asym = std::fabs(a - b);
      if (i > 0) {
        const double rate = (a - prev) * N;
        if (rate < minRate) minRate = rate;
        if (rate > maxRate) maxRate = rate;
        if (a < prev - 1e-12) mono = false;
      }
      prev = a;
    }
    printf("  bend=%.2f | symmetry err %.2e | rate min %.4f max %.4f (peak %.4f, 1+3b=%.2f)"
           " | monotonic %s\n",
           bend, asym, minRate, maxRate, maxRate, 1.0 + 3.0 * bend, mono ? "yes" : "NO");
  }

  printf("\n=== 4. a window delivers exactly its whole amount (conservation) ===\n");
  for (double bend : {0.0, 0.5, 1.0}) {
    Params P; P.durationMs = 300.0; P.shape = kSymS; P.bend = bend;
    Glide g; g.Reset();
    g.Feed(1.0, P);
    double out = 0;
    for (double t = 0; t < 0.6; t += 0.001) out += g.Tick(0.001, P) * kU;
    printf("  bend=%.2f -> %.6f units (want 15.000000)\n", bend, out);
  }

  printf("\n=== 5. the rate profile of one notch (D = 300 ms), read at 8 points ===\n");
  for (double bend : {0.0, 0.5, 1.0}) {
    Params P; P.durationMs = 300.0; P.shape = kSymS; P.bend = bend;
    printf("  bend=%.2f |", bend);
    for (int k = 1; k <= 8; ++k) {
      Glide g; g.Reset(); g.Feed(1.0, P);
      const double mark = 300.0 / 1000.0 * k / 9.0;
      double rate = 0;
      for (double t = 0; t < mark + 1e-9; t += 0.001) {
        const double s = g.Tick(0.001, P);
        if (t >= mark - 0.0015) rate = std::fabs(s) / 0.001 / kU;
      }
      printf(" %5.2f", rate);
    }
    printf("  N/s (u = 1/9 .. 8/9)\n");
  }
  return 0;
}
