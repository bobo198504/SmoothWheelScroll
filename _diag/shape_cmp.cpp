// SHAPE comparison for MODEL 3.0. Which "how the window delivers" curve feels best?
//
//   g++ -std=c++17 -O2 -o shape_cmp.exe shape_cmp.cpp
//
// Self-contained (own mini-glide) so candidate shapes can be tried WITHOUT touching src/.
//
// The user's two observations:
//   "缓动不够细（太线性）"        -- the linear shape reads as mechanical.
//   "加速不够快"                  -- a roll does not build up fast enough.
//
// Both point the same way: deliver MORE of each window EARLY (an ease-OUT shape). That raises the
// roll's output rate sooner (faster build-up) AND gives a non-flat profile -- while still landing
// at zero rate, so the stop stays soft.
//
// Reported per shape: the rate profile, the largest single delivery, how much of the motion has
// landed by 95%/99%, and -- for a roll -- how fast the output rate climbs and how long the tail is.

#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

static const double kU = 15.0; // units per notch

// ---- candidate shapes: cumulative fraction delivered by progress u in [0,1] -------------------
// rate(u) = S'(u). All have S(0)=0, S(1)=1, so a full window always delivers its whole amount.
struct Shape { const char *name; double (*S)(double); };

static double sLinear(double u) { return u; }
static double sSmooth(double u) { return u * u * u * (u * (u * 6 - 15) + 10); } // smootherstep
static double sTrap(double u) {
  const double r = 0.25, p = 1.0 / (1.0 - r);
  if (u < r) return p * u * u / (2 * r);
  if (u <= 1 - r) return p * r / 2 + p * (u - r);
  const double v = 1 - u; return 1 - p * v * v / (2 * r);
}
// ease-OUT family: rate peaks at `a` at u=0, decays to 0 at u=1. a = p, S = 1-(1-u)^p.
static double sOut2(double u) { const double v = 1 - u; return 1 - v * v; }        // peak rate 2.0
static double sOut15(double u) {
  double v = 1 - u; if (v < 0) v = 0; // u can exceed 1 by a float hair; pow(neg, 1.5) = nan
  return 1 - pow(v, 1.5);
}                                                                                  // peak rate 1.5
static double sOut3(double u) { const double v = 1 - u; return 1 - v * v * v; }     // peak rate 3.0

static const Shape kShapes[] = {
    {"linear     ", sLinear}, {"smootherstep", sSmooth}, {"trapezoid  ", sTrap},
    {"easeOut 1.5", sOut15},  {"easeOut 2.0 ", sOut2},   {"easeOut 3.0 ", sOut3},
};
static const int kNumShapes = (int)(sizeof(kShapes) / sizeof(kShapes[0]));

// ---- mini glide (same structure as anim3::Glide: one window per message) ---------------------
struct Glide {
  std::vector<double> amt, age;
  double due = 0;
  void feed(double a) { amt.push_back(a); age.push_back(0); }
  double tick(double dt, double D, double (*S)(double)) {
    double out = due; due = 0;
    if (D <= 0) { amt.clear(); age.clear(); return out; }
    size_t w = 0;
    for (size_t i = 0; i < amt.size(); ++i) {
      const double u1 = age[i] / D, u2 = (age[i] + dt) / D;
      out += amt[i] * (S(u2) - S(u1));
      age[i] += dt;
      if (age[i] < D) { amt[w] = amt[i]; age[w] = age[i]; ++w; }
    }
    amt.resize(w); age.resize(w);
    return out;
  }
  size_t inFlight() const { return amt.size(); }
};

// ---- A. single notch: the rate profile, and the step size -----------------------------------
static void singleNotch(double Dms, double (*S)(double), const char *name) {
  const double D = Dms / 1000.0, dt = 0.001, grid = 1.0 / 256.0;
  Glide g; g.feed(1.0);
  double carry = 0, out = 0, maxStep = 0, t95 = -1, t99 = -1;
  double p10 = 0, p50 = 0, p90 = 0;
  for (int i = 0; i < (int)(D / dt) + 50; ++i) {
    const double t = i * dt;
    const double want = g.tick(dt, D, S) * kU;
    const double total = carry + want;
    const double m = std::floor(std::fabs(total) / grid + 0.5);
    const double deliv = (total < 0 ? -1 : 1) * m * grid;
    carry = total - deliv;
    if (std::fabs(deliv) > maxStep) maxStep = std::fabs(deliv);
    out += deliv;
    if (t95 < 0 && out >= 0.95 * kU) t95 = t;
    if (t99 < 0 && out >= 0.99 * kU) t99 = t;
    if (t >= D * 0.10 && p10 == 0) p10 = want / dt / kU;
    if (t >= D * 0.50 && p50 == 0) p50 = want / dt / kU;
    if (t >= D * 0.90 && p90 == 0) p90 = want / dt / kU;
  }
  printf("  %s | rate at 10%%/50%%/90%%: %5.2f %5.2f %5.2f N/s | maxstep %6.3f u (%4.1f%%)"
         " | 95%%@%4.0fms 99%%@%4.0fms | total %.4f\n",
         name, p10, p50, p90, maxStep, 100 * maxStep / kU, t95 * 1000, t99 * 1000, out);
}

// ---- B. roll then stop: build-up speed, tail, conservation -----------------------------------
static void roll(double Dms, double (*S)(double), const char *name, double R, double spinMs) {
  const double D = Dms / 1000.0, dt = 0.001, grid = 1.0 / 256.0;
  const double iv = 1.0 / R, stopSec = spinMs / 1000.0;
  Glide g;
  double nextMsg = 0, carry = 0, out = 0, in = 0, maxStep = 0, riseT = -1, tail = 0;
  const int steps = (int)((stopSec + D + 0.05) / dt);
  for (int i = 0; i < steps; ++i) {
    const double t = i * dt;
    while (nextMsg <= t + 1e-9 && nextMsg < stopSec) { g.feed(1.0); in += kU; nextMsg += iv; }
    const double want = g.tick(dt, D, S) * kU;
    const double total = carry + want;
    const double m = std::floor(std::fabs(total) / grid + 0.5);
    const double deliv = (total < 0 ? -1 : 1) * m * grid;
    carry = total - deliv;
    if (std::fabs(deliv) > maxStep) maxStep = std::fabs(deliv);
    out += deliv;
    if (riseT < 0 && t > dt && t < stopSec && std::fabs(want) / dt / kU >= 0.95 * R) riseT = t;
    if (t >= stopSec && std::fabs(want) > 1e-12) tail = t - stopSec;
  }
  printf("  %s | rise to 95%% of %4.0f N/s: %4.0f ms | tail %4.0f ms | maxstep %6.3f u (%4.1f%%)"
         " | in %.0f out %.0f (%+.3f%%) | peak windows %zu\n",
         name, R, riseT * 1000, tail * 1000, maxStep, 100 * maxStep / kU, in, out,
         100 * (out - in) / in, g.inFlight());
}

int main() {
  printf("one notch = %.0f units; tick 1 ms; a notch = one message; window D = 200 ms.\n\n", kU);

  printf("=== A. ONE NOTCH (D = 200 ms). 'rate' is what the action is fed, in notches/s. ===\n");
  printf("    linear is a flat 5 N/s for 200 ms; an ease-OUT starts high and settles.\n\n");
  for (int i = 0; i < kNumShapes; ++i) singleNotch(200.0, kShapes[i].S, kShapes[i].name);

  printf("\n=== B. ROLL 20 notches/s for 600 ms, then STOP (D = 200 ms). ===\n");
  printf("    'rise' = how long before the output reaches the input rate (smaller = snappier).\n\n");
  for (int i = 0; i < kNumShapes; ++i) roll(200.0, kShapes[i].S, kShapes[i].name, 20.0, 600.0);

  printf("\n=== C. the same roll at 40 notches/s (a fast flick). ===\n\n");
  for (int i = 0; i < kNumShapes; ++i) roll(200.0, kShapes[i].S, kShapes[i].name, 40.0, 600.0);
  return 0;
}
