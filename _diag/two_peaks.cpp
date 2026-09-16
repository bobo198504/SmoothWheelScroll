// WHY does a fast ~16-notch flick with Shape at MAX (bend=1.0) feel like TWO peaks?
//
//   g++ -std=c++17 -O2 -I../src -o two_peaks.cpp two_peaks.cpp
//
// Output rate = the sum of every open window's rate. Each window's rate is single-peaked, so two
// peaks in the output must come from HOW THE NOTCHES ARRIVED. This sweeps arrival patterns (speed
// and evenness) and prints the delivered rate, marking local maxima, so the cause is visible.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <initializer_list>

using namespace anim3;
static const double kPI = 3.14159265358979323846;

// Notch arrival times for a hand whose turning speed follows v(x), x in 0..1 over Tms.
static std::vector<double> fromVel(double Tms, int n, double (*v)(double)) {
  const int NS = 20000;
  std::vector<double> V(NS);
  double sum = 0;
  for (int i = 0; i < NS; ++i) { V[i] = v(i / (double)(NS - 1)); sum += V[i]; }
  std::vector<double> out;
  double cum = 0;
  int next = 1;
  for (int i = 0; i < NS; ++i) {
    cum += V[i] / sum;
    while (next <= n && cum >= next / (double)n - 1e-12) {
      out.push_back(Tms / 1000.0 * i / (NS - 1));
      ++next;
    }
  }
  return out;
}
static double velSine(double x) { return std::sin(kPI * x); }        // slow - fast - slow
static double velDecel(double x) { return std::exp(-2.2 * x); }      // fast at first, slowing
static double velAccel(double x) { return std::exp(-2.2 * (1 - x)); }// slow at first, faster

static void run(const char *name, const std::vector<double> &times, double Dms, double bend) {
  Params P;
  P.durationMs = Dms; P.shape = kSymS; P.bend = bend;
  Glide g; g.Reset();
  const double dt = 0.001;
  const double end = (times.empty() ? 0.0 : times.back()) + Dms / 1000.0 + 0.05;
  size_t k = 0;
  std::vector<double> rate;
  double acc = 0; int cnt = 0;
  for (double t = 0; t < end; t += dt) {
    while (k < times.size() && times[k] <= t + 1e-9) { g.Feed(1.0, P); ++k; }
    acc += g.Tick(dt, P);
    if (++cnt == 5) { rate.push_back(acc / 0.005); acc = 0; cnt = 0; } // notches/s
  }
  double mx = 0;
  for (double r : rate) if (r > mx) mx = r;
  printf("=== %s  | D=%.0f ms, bend=%.1f, notches span %.0f ms ===\n", name, Dms, bend,
         times.empty() ? 0.0 : times.back() * 1000.0);
  printf("  delivered rate (notches/s), one number per 5 ms:\n   ");
  for (size_t i = 0; i < rate.size(); ++i) {
    printf("%6.1f", rate[i]);
    if ((i + 1) % 12 == 0 && i + 1 < rate.size()) printf("\n   ");
  }
  printf("\n  local maxima above 5%% of peak: ");
  int np = 0;
  for (size_t i = 1; i + 1 < rate.size(); ++i)
    if (rate[i] >= rate[i - 1] && rate[i] > rate[i + 1] && rate[i] > 0.05 * mx) {
      printf("%.0fms=%.0f   ", i * 5.0, rate[i]);
      ++np;
    }
  printf("[%d peak(s)]\n\n", np);
}

int main() {
  printf("~16 notches, quickly; Shape at max (bend = 1.0, peak rate 4x). Where are the peaks?\n\n");

  { std::vector<double> t; for (int i = 0; i < 16; ++i) t.push_back(i * 150.0 / 15 / 1000.0);
    run("A  uniform, all 16 within 150 ms", t, 300, 1.0); }
  { std::vector<double> t; for (int i = 0; i < 16; ++i) t.push_back(i * 250.0 / 15 / 1000.0);
    run("B  uniform, all 16 within 250 ms", t, 300, 1.0); }
  run("C  hand: slow-fast-slow (250 ms)", fromVel(250, 16, velSine), 300, 1.0);
  run("D  hand: fast at first, slowing (250 ms)", fromVel(250, 16, velDecel), 300, 1.0);
  run("E  hand: slow at first, faster (250 ms)", fromVel(250, 16, velAccel), 300, 1.0);

  // A control: what DOES make two peaks? Two separate bursts.
  { std::vector<double> t;
    for (int i = 0; i < 8; ++i) t.push_back(i * 60.0 / 7 / 1000.0);
    for (int i = 0; i < 8; ++i) t.push_back((200.0 + i * 60.0 / 7) / 1000.0);
    run("F  CONTROL: two bursts (8 + 8, 200 ms apart)", t, 300, 1.0); }
  return 0;
}
