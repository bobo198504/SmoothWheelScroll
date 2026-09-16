// MODEL 3.0: what happens when NOTCHES ARRIVE FASTER THAN THE WINDOW.
//
//   g++ -std=c++17 -O2 -I../src -o model3_overlap.exe model3_overlap.cpp
//
// The user's question: with easing D (say 200 ms), a fast roll puts notches closer together than
// D. What does the model do?
//
// The answer this checks: each message keeps its OWN window of exactly D, and the windows simply
// OVERLAP and ADD. So at any instant the number of open windows is D/interval, and the hand-over
// rate is the D-wide moving average of the message rate. Consequences to verify:
//   * the total handed over is ALWAYS exactly the total received (no amplification, no loss);
//   * the open-window count is BOUNDED at D/interval -- it never grows without limit;
//   * the hand-over rate RISES over the first D, then tracks the input exactly;
//   * the per-tick delivery STAYS SMALL (the smoothing survives a fast roll).
//
// All in UNITS (one notch = 15). Messages on an exact grid, tick 1 ms.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <initializer_list>

using namespace anim3;
static const double kU = 15.0; // units per notch

struct Sim {
  double in = 0, out = 0;
  double maxStep = 0;
  int inFlightPeak = 0;
  double rateAtD = 0;    // hand-over rate at t = D (once the ramp is done)
  double rateSteady = 0; // hand-over rate just before the spin stops
  double rampMs = 0;     // time to reach 95% of the input rate
  double tailMs = 0;
};

// n notches, one every `intervalS`, then stop. D = window. Tick 1 ms.
static Sim roll(int n, double intervalS, double D, double watchS) {
  Params P; P.durationMs = D; P.shape = kLinear;
  Glide g; g.Reset();
  const double dt = 0.001, grid = 1.0 / 256.0;
  const double stopSec = n * intervalS;
  Sim s;
  double carry = 0, msgNext = 0;
  int fed = 0;
  const double inRate = 1.0 / intervalS; // notches/s
  const int steps = (int)std::llround((stopSec + 8.0 * (D / 1000.0) + 0.1) / dt);

  for (int i = 0; i < steps; ++i) {
    const double t = i * dt;
    while (fed < n && msgNext <= t + 1e-9) { g.Feed(1.0, P); s.in += kU; ++fed; msgNext += intervalS; }
    if (g.InFlight() > s.inFlightPeak) s.inFlightPeak = g.InFlight();

    const double want = g.Tick(dt, P) * kU;
    const double rate = std::fabs(want) / dt / kU; // notches/s
    if (t >= D / 1000.0 - dt && t <= D / 1000.0 + dt && s.rateAtD == 0) s.rateAtD = rate;
    if (rate >= 0.95 * inRate && s.rampMs == 0 && fed > 1) s.rampMs = t * 1000.0;
    if (fed == n - 1 || (fed == n && t < stopSec)) s.rateSteady = rate;
    const double total = carry + want;
    const double m = (total < 0) ? -std::floor(-total / grid + 0.5) : std::floor(total / grid + 0.5);
    const double deliv = m * grid;
    carry = total - deliv;
    if (std::fabs(deliv) > s.maxStep) s.maxStep = std::fabs(deliv);
    s.out += deliv;
    if (t > stopSec && std::fabs(want) > 1e-12) s.tailMs = (t - stopSec) * 1000.0;
  }
  return s;
}

int main() {
  printf("one notch = %.0f units; tick 1 ms; shape = linear; a notch = one message.\n", kU);
  printf("Question: notches closer than the window -- how is it handled?\n\n");

  printf("=== D = 200 ms, notch interval swept from slower to much faster than D ===\n");
  printf("    %8s | %6s | %10s %10s | %8s | %8s %8s | %9s\n",
         "interval", "notch/s", "in units", "out units", "windows", "ramp ms", "tail ms", "max step");
  const double D = 200.0;
  for (double ivMs : {400.0, 250.0, 200.0, 100.0, 50.0, 25.0, 10.0, 5.0}) {
    const double iv = ivMs / 1000.0;
    const int n = 10;
    const Sim s = roll(n, iv, D, 0.0);
    printf("    %6.0f ms | %6.0f | %10.1f %10.1f | %8d | %8.0f %8.0f | %9.3f%s\n",
           ivMs, 1.0 / iv, s.in, s.out, s.inFlightPeak, s.rampMs, s.tailMs, s.maxStep,
           (ivMs < D) ? "  <- windows overlap" : "");
  }

  printf("\n=== the OVERLAP itself: D=200 ms, a notch every 50 ms (4 windows open at once) ===\n");
  {
    Params P; P.durationMs = D; P.shape = kLinear;
    Glide g; g.Reset();
    const double dt = 0.001, grid = 1.0 / 256.0;
    const double iv = 0.05;
    double carry = 0, msgNext = 0, out = 0;
    int fed = 0;
    printf("    %6s | %7s | %9s | %9s | %s\n", "t ms", "notches", "out N/s", "windows", "note");
    for (int i = 0; i <= 700; ++i) {
      const double t = i * dt;
      while (fed < 10 && msgNext <= t + 1e-9) { g.Feed(1.0, P); ++fed; msgNext += iv; }
      const double want = g.Tick(dt, P) * kU;
      const double total = carry + want;
      const double m = std::floor(std::fabs(total) / grid + 0.5);
      const double deliv = (total < 0 ? -1 : 1) * m * grid;
      carry = total - deliv; out += deliv;
      const bool mark = (i % 50 == 0) || i == 475 || i == 500 || i == 550 || i == 650;
      if (mark)
        printf("    %6d | %7d | %9.1f | %9d | %s\n", i, fed, std::fabs(want) / dt / kU,
               g.InFlight(),
               (i == 200) ? "4 open windows = 200/50 -> rate has reached 20 N/s"
               : (i == 500) ? "spin stopped here" : "");
    }
    printf("    total handed over = %.4f units = %.4f notches (10 notches went in)\n", out, out / kU);
  }

  printf("\n=== does a long window at a very fast roll ever PILE UP? (D=200 ms) ===\n");
  for (double ivMs : {50.0, 10.0, 2.0}) {
    const int n = 200;
    const Sim s = roll(n, ivMs / 1000.0, D, 0.0);
    printf("    interval %5.0f ms (%6.0f notch/s): windows peak %4d (= D/interval = %.1f), "
           "in=%.0f out=%.0f, max step %.3f u\n",
           ivMs, 1000.0 / ivMs, s.inFlightPeak, D / ivMs, s.in, s.out, s.maxStep);
  }
  printf("    -> the open-window count is D/interval, BOUNDED; it never runs away.\n");
  return 0;
}
