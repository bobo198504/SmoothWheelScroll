// CANDIDATE "eating vs speed" curves, measured side by side.
//
//   g++ -std=c++17 -O2 -o curves.cpp curves.cpp
//
// The eating decides how much of each notch survives, based on how fast the wheel turns. Everyone
// tested here shares the two ends that must not move:
//   very slow  -> eat the full amount (a deliberate notch stays soft)
//   very fast  -> eat nothing       (a flick is direct)
// The candidates differ only in HOW they get from one to the other.
//
// Two numbers matter for comfort:
//   WOBBLE: how much the per-notch amount changes across the gaps a slow roll actually visits
//           (150..300 ms here). Near 1.0 = a steady hand gets a steady amount.
//   DIRECT: how much is left of a fast notch (50..60 ms). Near 1.0 = a flick is not held back.

#include <cstdio>
#include <cmath>

static const double D = 120.0;          // deltas per notch
static const double EAT = 0.9;          // kinetic 12 = eat 90% at full slowness
static const double REF_MS = 200.0;     // the reference the models below are built on

// kept deltas for a given "eaten fraction" f
static double kept(double f) { return D * (1.0 - EAT * f); }

static double sstep(double u) { return (u <= 0) ? 0 : (u >= 1) ? 1 : u * u * (3 - 2 * u); }

// --- 1. straight ramp (the ORIGINAL) ---
static double fLinear(double gapMs) {
  double x = gapMs / REF_MS; return x > 1 ? 1 : x;
}
// --- 2. smoothstep on x (CURRENT) ---
static double fSmooth(double gapMs) { return sstep(gapMs / REF_MS); }
// --- 3. smoothstep with a LATER reference (the ramp starts further into fast rolling) ---
static double fSmoothLate(double gapMs, double refMs) { return sstep(gapMs / refMs); }
// --- 4. power 1-(1-x)^3: flat near the slow end, steep near the fast end ---
static double fPow3(double gapMs) {
  double x = gapMs / REF_MS; if (x > 1) x = 1; if (x < 0) x = 0;
  double v = 1.0 - x; return 1.0 - v * v * v;
}
// --- 5. SHOULDER: fully eaten above `hi` ms, nothing eaten below `lo` ms, smooth between ---
static double fShoulder(double gapMs, double loMs, double hiMs) {
  if (gapMs >= hiMs) return 1.0;
  if (gapMs <= loMs) return 0.0;
  return sstep((gapMs - loMs) / (hiMs - loMs));
}

static void row(const char *name, double (*f)(double)) {
  const double gaps[] = {400, 300, 250, 200, 175, 150, 125, 100, 75, 60, 50, 30};
  printf("  %-24s", name);
  for (int i = 0; i < 12; ++i) printf(" %6.1f", kept(f(gaps[i])));
  // wobble over 150..300, direct at 50..60
  double mn = 1e9, mx = 0;
  for (double g = 150; g <= 300; g += 5) { const double k = kept(f(g)); if (k < mn) mn = k; if (k > mx) mx = k; }
  const double wobble = mx / mn;
  const double direct = kept(f(50)) / D;
  printf("   | wobble x%.2f | direct %.2f\n", wobble, direct);
}

int main() {
  printf("kinetic 12 (eats 90%% at full slowness). Row = kept deltas per notch.\n");
  printf("Columns are the gap between notches: 400 300 250 200 175 150 125 100 75 60 50 30 ms\n\n");
  row("1 linear (original)", fLinear);
  row("2 smoothstep (current)", fSmooth);
  printf("\n");
  {
    const double refs[] = {250, 300, 400};
    for (int i = 0; i < 3; ++i) {
      const double r = refs[i];
      const double gaps[] = {400, 300, 250, 200, 175, 150, 125, 100, 75, 60, 50, 30};
      char b[48]; snprintf(b, sizeof(b), "3 smoothstep ref %.0f", r);
      printf("  %-24s", b);
      for (int j = 0; j < 12; ++j) printf(" %6.1f", kept(fSmoothLate(gaps[j], r)));
      double mn = 1e9, mx = 0;
      for (double g = 150; g <= 300; g += 5) { const double k = kept(fSmoothLate(g, r)); if (k < mn) mn = k; if (k > mx) mx = k; }
      printf("   | wobble x%.2f | direct %.2f\n", mx / mn, kept(fSmoothLate(50, r)) / D);
    }
  }
  row("4 power 1-(1-x)^3", fPow3);
  printf("\n");
  {
    const double lo[] = {40, 50, 50}, hi[] = {120, 150, 200};
    for (int i = 0; i < 3; ++i) {
      const double L = lo[i], H = hi[i];
      const double gaps[] = {400, 300, 250, 200, 175, 150, 125, 100, 75, 60, 50, 30};
      char b[48]; snprintf(b, sizeof(b), "5 shoulder %.0f-%.0f ms", L, H);
      printf("  %-24s", b);
      for (int j = 0; j < 12; ++j) printf(" %6.1f", kept(fShoulder(gaps[j], L, H)));
      double mn = 1e9, mx = 0;
      for (double g = 150; g <= 300; g += 5) { const double k = kept(fShoulder(g, L, H)); if (k < mn) mn = k; if (k > mx) mx = k; }
      printf("   | wobble x%.2f | direct %.2f\n", mx / mn, kept(fShoulder(50, L, H)) / D);
    }
  }
  printf("\nwobble = max/min over the slow range 150..300 ms (near x1.00 = steady hand, steady output)\n");
  printf("direct = how much of a fast notch survives (near 1.00 = a flick is not held back)\n");
  return 0;
}
