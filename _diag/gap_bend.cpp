// DECISIVE TEST: does a HIGH bend EXPOSE a gap in the notch arrival that a LOW bend smooths over?
//
//   g++ -std=c++17 -O2 -I../src -o gap_bend.cpp gap_bend.cpp
//
// A window's rate is spread over the WHOLE 300 ms at bend=0, but concentrated in ~66 ms at bend=1.
// So a gap in the arrival can be filled in by the long tails at bend=0, yet left open at bend=1.
// Same input each row; only the bend changes.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <initializer_list>

using namespace anim3;

static void run(const char *name, const std::vector<double> &times, double D, double bend) {
  Params P; P.durationMs = D; P.shape = kSymS; P.bend = bend;
  Glide g; g.Reset();
  const double dt = 0.001;
  const double end = (times.empty() ? 0.0 : times.back()) + D / 1000.0 + 0.05;
  size_t k = 0;
  std::vector<double> rate; double acc = 0; int cnt = 0;
  for (double t = 0; t < end; t += dt) {
    while (k < times.size() && times[k] <= t + 1e-9) { g.Feed(1.0, P); ++k; }
    acc += g.Tick(dt, P);
    if (++cnt == 5) { rate.push_back(acc / 0.005); acc = 0; cnt = 0; }
  }
  double mx = 0; for (double r : rate) if (r > mx) mx = r;
  printf("  %-22s bend=%.1f | peak %5.1f N/s | rate per 20 ms:\n    ", name, bend, mx);
  // print averaged per 20 ms (= every 4 samples) to keep it readable
  for (size_t i = 0; i + 3 < rate.size(); i += 4)
    printf("%5.0f", (rate[i] + rate[i + 1] + rate[i + 2] + rate[i + 3]) / 4.0);
  printf("\n");
}

int main() {
  printf("one number per 20 ms = the delivered rate (notches/s). Same input per block; bend varies.\n\n");

  {
    std::vector<double> two;
    for (int i = 0; i < 8; ++i) two.push_back(i * 60.0 / 7 / 1000.0);
    for (int i = 0; i < 8; ++i) two.push_back((200.0 + i * 60.0 / 7) / 1000.0);
    printf("=== INPUT A: 8 notches, then a 200 ms GAP, then 8 notches ===\n");
    for (double b : {0.0, 0.5, 1.0}) run("two bursts", two, 300, b);
  }
  {
    std::vector<double> one;
    for (int i = 0; i < 16; ++i) one.push_back(i * 150.0 / 15 / 1000.0);
    printf("\n=== INPUT B: 16 notches evenly over 150 ms (a single fast burst) ===\n");
    for (double b : {0.0, 0.5, 1.0}) run("one fast burst", one, 300, b);
  }
  {
    std::vector<double> hole;
    for (int i = 0; i < 8; ++i) hole.push_back(i * 50.0 / 7 / 1000.0);
    for (int i = 0; i < 8; ++i) hole.push_back((150.0 + i * 50.0 / 7) / 1000.0);
    printf("\n=== INPUT C: 8 notches, a ~100 ms hole, 8 notches (a hesitant flick) ===\n");
    for (double b : {0.0, 0.5, 1.0}) run("hole in middle", hole, 300, b);
  }
  return 0;
}
