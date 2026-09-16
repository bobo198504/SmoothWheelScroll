// WHY does a QUICK scroll look like TWO movements when the Shape bend is raised?
//
//   g++ -std=c++17 -O2 -I../src -o two_moves.exe two_moves.cpp
//
// The symmetric S-curve puts almost all of a window's movement in its MIDDLE and makes both ends
// very slow. For a quick scroll the input is over almost at once, so the output is: a long near-
// silence while the window's slow start creeps, then a fast dash, then a slow crawl to the end.
// This measures that: the DEAD TIME before anything is delivered, the shape of the delivered trace,
// and how much of the whole movement lands in the middle third of the window.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <initializer_list>

using namespace anim3;
static const double kU = 15.0; // units per notch, and also the grid is 1/256 unit

// Deliver N notches spread over `spanMs` (0 = all at once = a quick flick), then watch.
struct Trace {
  double firstMoveMs = -1; // when the FIRST delivery happened
  double maxStep = 0;      // largest single delivery, in units
  double total = 0;
  double t10 = -1, t50 = -1; // when 10% / 50% of the movement had landed
  std::vector<double> rate; // units per 20 ms, sampled every 20 ms
};

static Trace run(int n, double spanMs, double Dms, double bend) {
  Params P; P.durationMs = Dms; P.shape = kSymS; P.bend = bend;
  Glide g; g.Reset();
  const double dt = 0.001, grid = 1.0 / 256.0;
  const double span = spanMs / 1000.0;
  const double iv = (n > 1 && span > 0) ? span / (n - 1) : 0.0;
  Trace tr;
  double nextMsg = 0, carry = 0;
  int fed = 0;
  double bucket = 0;
  int bucketTick = 0;
  const int steps = (int)((span + Dms / 1000.0 + 0.05) / dt);
  for (int i = 0; i < steps; ++i) {
    const double t = i * dt;
    if (fed < n && (iv == 0 ? t <= 1e-9 : t + 1e-9 >= fed * iv)) {
      if (iv == 0) { while (fed < n) { g.Feed(1.0, P); ++fed; } }
      else { g.Feed(1.0, P); ++fed; }
    }
    const double want = g.Tick(dt, P) * kU;
    const double tot = carry + want;
    const double m = (tot < 0) ? -std::floor(-tot / grid + 0.5) : std::floor(tot / grid + 0.5);
    const double deliv = m * grid;
    carry = tot - deliv;
    if (std::fabs(deliv) > 1e-12 && tr.firstMoveMs < 0) tr.firstMoveMs = t * 1000.0;
    if (std::fabs(deliv) > tr.maxStep) tr.maxStep = std::fabs(deliv);
    tr.total += deliv;
    const double goal = n * kU;
    if (tr.t10 < 0 && tr.total >= 0.10 * goal) tr.t10 = t * 1000.0;
    if (tr.t50 < 0 && tr.total >= 0.50 * goal) tr.t50 = t * 1000.0;
    bucket += std::fabs(deliv);
    if (++bucketTick == 20) { tr.rate.push_back(bucket / 20.0 * 1000.0); bucket = 0; bucketTick = 0; }
  }
  return tr;
}

static void show(const char *what, int n, double spanMs, double bend) {
  const double D = 300.0;
  const Trace tr = run(n, spanMs, D, bend);
  printf("%-26s bend=%.1f | first step %5.1f ms | 10%% by %5.1f ms | 50%% by %5.1f ms"
         " | max step %6.3f u (%4.1f%%)\n",
         what, bend, tr.firstMoveMs, tr.t10, tr.t50, tr.maxStep, 100 * tr.maxStep / kU);
  printf("      delivered rate (units/s) every 20 ms:\n      ");
  for (size_t i = 0; i < tr.rate.size() && i < 24; ++i)
    printf("%5.0f", tr.rate[i]);
  printf("\n");
}

int main() {
  printf("one notch = %.0f units; tick 1 ms; window D = 300 ms.\n\n", kU);

  printf("=== A. ONE notch, released at once (a single quick flick) ===\n");
  show("1 notch, instant", 1, 0.0, 0.0);
  show("1 notch, instant", 1, 0.0, 0.5);
  show("1 notch, instant", 1, 0.0, 1.0);

  printf("\n=== B. TWO notches 50 ms apart (a fast two-detent flick) ===\n");
  show("2 notches, 50 ms", 2, 50.0, 0.0);
  show("2 notches, 50 ms", 2, 50.0, 0.5);
  show("2 notches, 50 ms", 2, 50.0, 1.0);

  printf("\n=== C. THREE notches over 120 ms ===\n");
  show("3 notches, 120 ms", 3, 120.0, 0.5);
  show("3 notches, 120 ms", 3, 120.0, 1.0);

  printf("\n=== D. how concentrated? fraction of the movement in each third of the window ===\n");
  for (double bend : {0.0, 0.5, 1.0}) {
    Params P; P.durationMs = 300.0; P.shape = kSymS; P.bend = bend;
    Glide g; g.Reset(); g.Feed(1.0, P);
    double third[3] = {0, 0, 0};
    const double dt = 0.001;
    for (int i = 0; i < 300; ++i) {
      const double s = std::fabs(g.Tick(dt, P) * kU);
      third[i < 100 ? 0 : i < 200 ? 1 : 2] += s;
    }
    printf("  bend=%.1f | first third %4.1f%%   middle third %4.1f%%   last third %4.1f%%\n",
           bend, 100 * third[0] / 15.0, 100 * third[1] / 15.0, 100 * third[2] / 15.0);
  }
  return 0;
}
