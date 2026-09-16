// THE REAL TEST for the corner problem: a hand that wobbles around the corner, and slow rolls at
// several speeds. Compare the OLD straight ramp with the NEW smooth curve.
//
//   g++ -std=c++17 -O2 -o corner_fix.cpp corner_fix.cpp
//
// Self-contained: both curves are written out here so the old and new can be compared directly.

#include <cstdio>
#include <cmath>

static const double D = 120.0, SLOWRATE = 600.0;
static const double kKin = 12.0;
static const double eatSlow = 1.0 - kKin / D;

// OLD: straight ramp (corner where it meets the flat part)
static double oldKept(double gapMs) {
  const double refSec = D / SLOWRATE;        // 0.2 s
  double f = (gapMs / 1000.0) / refSec;
  if (f > 1.0) f = 1.0;
  return D * (1.0 - eatSlow * f);
}
// NEW: smoothstep (zero slope at both ends -- no corner)
static double newKept(double gapMs) {
  const double refSec = D / SLOWRATE;
  double x = (gapMs / 1000.0) / refSec;
  double f;
  if (x >= 1.0) f = 1.0; else if (x <= 0.0) f = 0.0; else f = x * x * (3.0 - 2.0 * x);
  return D * (1.0 - eatSlow * f);
}

int main()
{
  printf("kinetic %.0f (eats %.0f%% at full slowness). one notch = %.0f deltas.\n\n",
         kKin, 100 * eatSlow, D);

  printf("=== 1. a hand that wobbles around the 200 ms corner ===\n");
  printf("    a series of notches at gaps 195/205/190/210 ms (a very steady hand)\n\n");
  const double wob[] = {200, 205, 195, 210, 190, 205, 195, 200};
  printf("  %-10s %-14s %-14s\n", "gap ms", "OLD kept", "NEW kept");
  double omin = 1e9, omax = 0, nmin = 1e9, nmax = 0;
  for (int i = 0; i < 8; ++i) {
    const double o = oldKept(wob[i]), n = newKept(wob[i]);
    if (o < omin) omin = o; if (o > omax) omax = o;
    if (n < nmin) nmin = n; if (n > nmax) nmax = n;
    printf("  %-10.0f %-14.2f %-14.2f\n", wob[i], o, n);
  }
  printf("  spread: OLD %.2f..%.2f (x%.1f)   NEW %.2f..%.2f (x%.1f)\n\n",
         omin, omax, omax / omin, nmin, nmax, nmax / nmin);

  printf("=== 2. across the whole slow range (this is what a slow roll visits) ===\n");
  printf("  %-10s %-12s %-12s\n", "gap ms", "OLD", "NEW");
  const double g[] = {300, 250, 220, 210, 200, 190, 180, 170, 160, 150};
  for (int i = 0; i < 10; ++i)
    printf("  %-10.0f %-12.2f %-12.2f\n", g[i], oldKept(g[i]), newKept(g[i]));

  printf("\n=== 3. the two ends must be unchanged ===\n");
  printf("  very slow (400 ms):  OLD %.2f  NEW %.2f  %s\n", oldKept(400), newKept(400),
         std::fabs(oldKept(400) - newKept(400)) < 1e-9 ? "(same)" : "(differs)");
  printf("  very fast ( 20 ms):  OLD %.2f  NEW %.2f  %s\n", oldKept(20), newKept(20),
         std::fabs(oldKept(20) - newKept(20)) < 1e-9 ? "(same)" : "(differs)");
  return 0;
}
