// ---------------------------------------------------------------------------
// anim_sim.cpp -- exercise the glide animation standalone, as data.
//
// One notch = 1.0 unit of travel, so Start of 15% moves 0.15 unit. The checks are
// the ones that must hold, not a hand-feel impression:
//   1. AMOUNT   : N notches in must deliver about N notches out, and the total
//                 must not depend on how fast the wheel is turned.
//   2. CLOCK    : the result must not depend on the timer granularity.
//   3. DIRECTION: up and down must be opposite.
//   4. FLOW     : a roll must not stall between notches.
// ---------------------------------------------------------------------------
#include "../src/anim_core.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <cstdlib>
#include <initializer_list>

using namespace anim;

static Params g_P;
static const double DT = 0.001;

static void Sparkline(const std::vector<double> &v) {
  static const char *ramp = " .:-=+*#%@";
  if (v.empty()) return;
  double mx = 0.0;
  for (double x : v) if (x > mx) mx = x;
  if (mx <= 0.0) { printf("  (flat)\n"); return; }
  std::string line;
  for (double x : v) { int i = (int)(x / mx * 9.999); if (i < 0) i = 0; if (i > 9) i = 9; line += ramp[i]; }
  printf("  |%s|\n", line.c_str());
}

static double Single(double dt, std::vector<double> *prof = nullptr, double *settle = nullptr) {
  Glide g; g.Kick(1.0, +1.0, 989, 0.0, g_P);
  double t = 0.0;
  for (int i = 0; i < 40000000; ++i) {
    double s = g.Tick(dt, g_P); t += dt;
    if (prof) prof->push_back(std::fabs(s) / dt);
    if (!g.Active()) break;
    if (t > 30.0) break;
  }
  if (settle) *settle = t;
  return g.Total();
}

static double Roll(double gap, int N) {
  Glide g; double t = 0, next = 0; int fired = 0;
  for (int i = 0; i < 40000000; ++i) {
    if (fired < N && t + 1e-12 >= next) { g.Kick(1.0, +1.0, 989, t, g_P); fired++; next += gap; }
    g.Tick(DT, g_P); t += DT;
    if (fired >= N && !g.Active()) break;
    if (t > gap * N + 10) break;
  }
  return g.Total();
}

static double Intended(int N) {
  double s = 0; for (int k = 1; k <= N; ++k) s += NotchFrac(g_P, k);
  return s;
}

int main(int argc, char **argv) {
  if (argc > 1) g_P.startPct = atof(argv[1]);
  if (argc > 2) g_P.accelPct = atof(argv[2]);
  if (argc > 3) g_P.releaseMs = atof(argv[3]);

  printf("PARAMS start=%.2f%% accel=%.2f%% release=%.0fms p=%.2f\n\n",
         g_P.startPct, g_P.accelPct, g_P.releaseMs, g_P.frictionPow);

  printf("== SINGLE NOTCH ==\n");
  std::vector<double> prof; double settle = 0;
  double travel = Single(DT, &prof, &settle);
  Sparkline(prof);
  printf("  travel=%.5f (want %.5f, err %+.2f%%)  settle=%.0fms\n\n",
         travel, g_P.startPct / 100.0, (travel / (g_P.startPct / 100.0) - 1) * 100, settle * 1000);

  printf("== AMOUNT (N notches in -> total out; must not depend on the gap) ==\n");
  const int N = 10;
  printf("   gap(ms)   total   vs intended(%.3f)\n", Intended(N));
  for (double gap : {0.400, 0.250, 0.120, 0.080, 0.060, 0.033}) {
    double tot = Roll(gap, N);
    printf("   %6.0f  %7.3f   %+6.2f%%\n", gap * 1000, tot, (tot / Intended(N) - 1) * 100);
  }
  printf("\n");

  printf("== CLOCK INDEPENDENCE (same notch, different tick length) ==\n");
  printf("   tick(ms)   travel    settle(ms)\n");
  for (double d : {0.016, 0.010, 0.005, 0.001}) {
    double s2 = 0, tr = Single(d, nullptr, &s2);
    printf("   %6.1f   %7.5f   %8.0f\n", d * 1000, tr, s2 * 1000);
  }
  printf("\n");

  printf("== DIRECTION (sign must be preserved) ==\n");
  for (double sg : {+1.0, -1.0}) {
    Glide g; double t = 0; g.Kick(1.0, sg, 989, 0.0, g_P);
    int pos = 0, neg = 0;
    for (int i = 0; i < 40000000; ++i) {
      double s = g.Tick(DT, g_P); t += DT;
      if (s > 0) ++pos; else if (s < 0) ++neg;
      if (!g.Active()) break;
      if (t > 30) break;
    }
    double tot = g.Total();
    printf("   sign=%+.0f -> total %+.4f  (steps %d pos / %d neg) %s\n", sg, tot, pos, neg,
           ((sg > 0 && tot > 0 && neg == 0) || (sg < 0 && tot < 0 && pos == 0)) ? "OK" : "!! WRONG");
  }
  printf("\n");

  printf("== ROLL FLOW (speed must not collapse between notches) ==\n");
  printf("   gap(ms)  peak   sag(min/peak)\n");
  for (double gap : {0.250, 0.120, 0.080, 0.060, 0.033}) {
    Glide g; double t = 0, next = 0; int fired = 0;
    double mn = 1e9, mx = 0;
    for (int i = 0; i < 40000000; ++i) {
      if (fired < 8 && t + 1e-12 >= next) { g.Kick(1.0, +1.0, 989, t, g_P); fired++; next += gap; }
      g.Tick(DT, g_P); t += DT;
      double v = std::fabs(g.Velocity());
      if (fired >= 2 && fired < 8) { if (v < mn) mn = v; if (v > mx) mx = v; }
      if (fired >= 8 && !g.Active()) break;
      if (t > gap * 8 + 10) break;
    }
    double sag = mx > 0 ? mn / mx : 0;
    printf("   %6.0f  %6.2f   %6.2f  %s\n", gap * 1000, mx, sag,
           sag > 0.28 ? "OK" : "!! stalls");
  }
  return 0;
}
