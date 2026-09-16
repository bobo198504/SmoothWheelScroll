// SWEEP: does a SINGLE ~16-notch gesture ever produce TWO peaks, and at which settings?
//
//   g++ -std=c++17 -O2 -I../src -o peak_sweep.cpp peak_sweep.cpp
//
// Method: for each (arrival pattern, total span, bend), build the delivered-rate trace, then ask
// the specific question "after the main peak, does the rate dip and then rise to a real SECOND
// peak?" A second peak counts only if it climbs back above 40% of the main peak, with a valley
// at least 15% below it in between (so a mere plateau does not count).

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <initializer_list>
#include <algorithm>

using namespace anim3;
static const double kPI = 3.14159265358979323846;

static std::vector<double> uniformTimes(double Tms, int n) {
  std::vector<double> t;
  for (int i = 0; i < n; ++i) t.push_back((Tms / 1000.0) * i / (double)(n - 1));
  return t;
}
static std::vector<double> fromVel(double Tms, int n, double (*v)(double)) {
  const int NS = 20000;
  std::vector<double> V(NS); double sum = 0;
  for (int i = 0; i < NS; ++i) { V[i] = v(i / (double)(NS - 1)); sum += V[i]; }
  std::vector<double> out; double cum = 0; int next = 1;
  for (int i = 0; i < NS; ++i) {
    cum += V[i] / sum;
    while (next <= n && cum >= next / (double)n - 1e-12) {
      out.push_back(Tms / 1000.0 * i / (NS - 1)); ++next;
    }
  }
  return out;
}
static double vSine(double x) { return std::sin(kPI * x); }
static double vDecel(double x) { return std::exp(-2.5 * x); }
static double vAccel(double x) { return std::exp(-2.5 * (1 - x)); }

// Returns the count of real peaks, and fills valley depth (%) and the two peak times (ms).
struct Info { int peaks; double valleyPct; double t1, t2, t3; bool two; };

static Info analyse(const std::vector<double> &times, double Dms, double bend) {
  Params P; P.durationMs = Dms; P.shape = kSymS; P.bend = bend;
  Glide g; g.Reset();
  const double dt = 0.001;
  const double end = (times.empty() ? 0.0 : times.back()) + Dms / 1000.0 + 0.05;
  size_t k = 0;
  std::vector<double> rate; double acc = 0; int cnt = 0;
  for (double t = 0; t < end; t += dt) {
    while (k < times.size() && times[k] <= t + 1e-9) { g.Feed(1.0, P); ++k; }
    acc += g.Tick(dt, P);
    if (++cnt == 5) { rate.push_back(acc / 0.005); acc = 0; cnt = 0; }
  }
  Info info{0, 0, 0, 0, 0, false};
  if (rate.size() < 6) return info;
  const size_t M = (size_t)(std::max_element(rate.begin(), rate.end()) - rate.begin());
  const double peak = rate[M];
  // after the main peak: find the deepest valley before the rate settles low, then the next max
  double valley = peak; size_t V = M;
  for (size_t i = M; i < rate.size(); ++i) {
    if (rate[i] < valley) { valley = rate[i]; V = i; }
    if (rate[i] < 0.20 * peak) break;
  }
  size_t H = V;
  for (size_t i = V + 1; i < rate.size(); ++i) if (rate[i] > rate[H]) H = i;
  info.peaks = 1;
  info.t1 = M * 5.0 / 1000.0 * 1000.0;
  info.t1 = M * 5.0;
  if (H > V && rate[H] > 0.40 * peak && valley < 0.85 * rate[H]) {
    info.peaks = 2; info.two = true;
    info.valleyPct = 100.0 * valley / peak;
    info.t2 = H * 5.0;
    info.t3 = peak;
  } else {
    info.valleyPct = (valley < peak) ? 100.0 * valley / peak : 100.0;
  }
  return info;
}

static void sweep(const char *name, std::vector<std::vector<double>> times[]) { (void)times; }

int main() {
  printf("~16 notches, single gesture. Does the delivered rate ever have TWO real peaks?\n");
  printf("(a second peak must climb above 40%% of the main peak, with a >=15%% valley between)\n\n");

  struct Pat { const char *name; std::vector<double> (*make)(double, int); };
  const double spans[] = {150, 200, 250, 300, 350, 400, 500, 600, 800};
  const double bends[] = {0.0, 0.5, 1.0};

  printf("%-10s %6s %6s | %-7s %-9s %s\n", "pattern", "span", "bend", "peaks", "valley%", "peak times (ms)");
  for (int pi = 0; pi < 4; ++pi) {
    const char *pname = pi == 0 ? "uniform" : pi == 1 ? "sine" : pi == 2 ? "decel" : "accel";
    for (double sp : spans) {
      std::vector<double> times;
      if (pi == 0) times = uniformTimes(sp, 16);
      else if (pi == 1) times = fromVel(sp, 16, vSine);
      else if (pi == 2) times = fromVel(sp, 16, vDecel);
      else times = fromVel(sp, 16, vAccel);
      for (double bd : bends) {
        const Info in = analyse(times, 300.0, bd);
        if (in.peaks >= 2)
          printf("%-10s %6.0f %6.1f | !! 2      %-9.1f %.0f  then  %.0f\n",
                 pname, sp, bd, in.valleyPct, in.t1, in.t2);
      }
    }
  }
  printf("\n(only TWO-peak cases are listed; everything else came out as ONE peak)\n");
  return 0;
}
