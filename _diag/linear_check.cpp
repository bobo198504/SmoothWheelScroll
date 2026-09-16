// IS the bend=0 trace the "spread one notch evenly over 300 ms" one? Check it on a SINGLE notch,
// where the answer is unambiguous.
//
//   g++ -std=c++17 -O2 -I../src -o linear_check.cpp linear_check.cpp
//
// If a notch's 120 deltas are spread evenly over 300 ms, then:
//     120 / 300 ms = 0.4 delta per ms  ->  exactly 4.00 deltas per 10 ms, flat, for 30 buckets.
// This prints the per-10 ms deltas for ONE notch at bend=0 and at bend=0.5, and checks the totals.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>

using namespace anim3;
static const double kDeltasPerNotch = 120.0;
static const double kUnitsPerNotch = 15.0;
static const double kDeltasPerUnit = 8.0;

static void one(double Dms, double bend) {
  Params P; P.durationMs = Dms; P.shape = kSymS; P.bend = bend;
  Glide g; g.Reset(); g.Feed(1.0, P);   // ONE notch only
  const double dt = 0.001, grid = 1.0 / 256.0;
  double carry = 0, total = 0;
  double bucket = 0;
  int b = 0;
  printf("  ONE notch, D = %.0f ms, bend = %.1f  ->  deltas per 10 ms:\n    ", Dms, bend);
  for (int i = 0; i <= 300; ++i) {
    const double want = g.Tick(dt, P) * kUnitsPerNotch;
    const double tot = carry + want;
    const double m = (tot < 0) ? -std::floor(-tot / grid + 0.5) : std::floor(tot / grid + 0.5);
    const double deliv = m * grid;
    carry = tot - deliv;
    const double d = std::fabs(deliv) * kDeltasPerUnit;
    bucket += d; total += d;
    if (i + 1 == (b + 1) * 10) {
      printf("%5.2f", bucket);
      ++b; bucket = 0;
      if (b % 15 == 0) printf("\n    ");
    }
  }
  printf("\n    total = %.4f deltas (one notch = 120)\n\n", total);
}

int main() {
  printf("A notch delivered EVENLY over 300 ms would be a flat 120/300*10 = 4.00 deltas per 10 ms.\n\n");
  one(300.0, 0.0);   // the candidate "linear" one
  one(300.0, 0.5);   // the S-curve
  return 0;
}
