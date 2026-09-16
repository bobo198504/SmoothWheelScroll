// Symmetric S-curve ("slow at both ends, fast in the middle") with ONE strength knob.
//
//   g++ -std=c++17 -O2 -o shape_bend.exe shape_bend.cpp
//
// Candidate family: S(u) = u^p / (u^p + (1-u)^p).
//   * p = 1  -> S(u) = u : the LINEAR curve we have now.
//   * p > 1  -> symmetric S: starts flat, is steepest at the middle, ends flat.
//   * it is exactly symmetric about the centre (S(1-u) = 1-S(u)), and the PEAK RATE equals p
//     (at u = 0.5), i.e. p IS "how much faster the middle is than the average".
//   * monotonic for every p >= 1, and S(0)=0, S(1)=1, so a window always delivers its whole amount.
//
// This measures what a given p does to the feel, so the knob's range and default can be chosen
// from numbers rather than taste.

#include <cstdio>
#include <cmath>
#include <vector>

static const double kU = 15.0; // units per notch

static double pfrac(double p, double u) {
  if (u <= 0.0) return 0.0;
  if (u >= 1.0) return 1.0;
  const double a = std::pow(u, p), b = std::pow(1.0 - u, p);
  return a / (a + b);
}

struct G {
  std::vector<double> amt, age;
  void feed(double a) { amt.push_back(a); age.push_back(0); }
  double tick(double dt, double D, double p) {
    double out = 0;
    if (D <= 0) { amt.clear(); age.clear(); return out; }
    size_t w = 0;
    for (size_t i = 0; i < amt.size(); ++i) {
      const double u1 = age[i] / D, u2 = (age[i] + dt) / D;
      out += amt[i] * (pfrac(p, u2) - pfrac(p, u1));
      age[i] += dt;
      if (age[i] < D) { amt[w] = amt[i]; age[w] = age[i]; ++w; }
    }
    amt.resize(w); age.resize(w);
    return out;
  }
};

// A. one notch: the rate profile, largest step, when it finishes.
static void one(double Dms, double p) {
  const double D = Dms / 1000.0, dt = 0.001, grid = 1.0 / 256.0;
  G g; g.feed(1.0);
  double carry = 0, out = 0, maxStep = 0, t95 = -1, r10 = 0, r50 = 0, r90 = 0;
  for (int i = 0; i < (int)(D / dt) + 60; ++i) {
    const double t = i * dt;
    const double want = g.tick(dt, D, p) * kU;
    const double total = carry + want;
    const double m = std::floor(std::fabs(total) / grid + 0.5);
    const double deliv = (total < 0 ? -1 : 1) * m * grid;
    carry = total - deliv;
    if (std::fabs(deliv) > maxStep) maxStep = std::fabs(deliv);
    out += deliv;
    if (t95 < 0 && out >= 0.95 * kU) t95 = t;
    if (t >= D * 0.10 && r10 == 0) r10 = want / dt / kU;
    if (t >= D * 0.50 && r50 == 0) r50 = want / dt / kU;
    if (t >= D * 0.90 && r90 == 0) r90 = want / dt / kU;
  }
  printf("  p=%.1f | rate 10%%/50%%/90%%: %5.2f %5.2f %5.2f N/s (peak=average x%.1f)"
         " | maxstep %6.3f u (%4.1f%%) | 95%%@%4.0fms | total %.4f\n",
         p, r10, r50, r90, p, maxStep, 100 * maxStep / kU, t95 * 1000, out);
}

// B. roll 20 notches/s for 600 ms then stop: build-up time, tail, conservation.
static void roll(double Dms, double p, double R) {
  const double D = Dms / 1000.0, dt = 0.001, grid = 1.0 / 256.0;
  const double iv = 1.0 / R, stopSec = 0.6;
  G g;
  double nextMsg = 0, carry = 0, out = 0, in = 0, maxStep = 0, riseT = -1, tail = 0;
  for (int i = 0; i < (int)((stopSec + D + 0.05) / dt); ++i) {
    const double t = i * dt;
    while (nextMsg <= t + 1e-9 && nextMsg < stopSec) { g.feed(1.0); in += kU; nextMsg += iv; }
    const double want = g.tick(dt, D, p) * kU;
    const double total = carry + want;
    const double m = std::floor(std::fabs(total) / grid + 0.5);
    const double deliv = (total < 0 ? -1 : 1) * m * grid;
    carry = total - deliv;
    if (std::fabs(deliv) > maxStep) maxStep = std::fabs(deliv);
    out += deliv;
    if (riseT < 0 && t > dt && t < stopSec && std::fabs(want) / dt / kU >= 0.95 * R) riseT = t;
    if (t >= stopSec && std::fabs(want) > 1e-12) tail = t - stopSec;
  }
  printf("  p=%.1f | rise to 95%% of %2.0f N/s: %4.0f ms | tail %4.0f ms | maxstep %6.3f u (%4.1f%%)"
         " | in %.0f out %.0f (%+.3f%%)\n",
         p, R, riseT * 1000, tail * 1000, maxStep, 100 * maxStep / kU, in, out, 100 * (out - in) / in);
}

int main() {
  printf("one notch = %.0f units; tick 1 ms. p = 1 is LINEAR; larger p = stronger symmetric S.\n\n", kU);
  printf("=== A. ONE NOTCH, D = 300 ms ===\n\n");
  for (double p : {1.0, 1.5, 2.0, 2.5, 3.0, 4.0}) one(300.0, p);
  printf("\n=== A2. ONE NOTCH, D = 100 ms ===\n\n");
  for (double p : {1.0, 2.0, 3.0, 4.0}) one(100.0, p);
  printf("\n=== B. ROLL 20 notches/s, 600 ms, D = 300 ms ===\n\n");
  for (double p : {1.0, 2.0, 2.5, 3.0, 4.0}) roll(300.0, p, 20.0);
  return 0;
}
