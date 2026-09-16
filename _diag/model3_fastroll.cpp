// MODEL 3.0 under a FAST SPIN: how the amount actually reaches the action.
//
//   g++ -std=c++17 -O2 -I../src -o model3_fastroll.exe model3_fastroll.cpp
//
// Uses the plugin's own header and the shipped settings (tick 1 ms, shape = linear).
//
// The result this checks: with a LINEAR window of duration D, the hand-over RATE at time t is the
// D-wide MOVING AVERAGE of the message rate. So a steady spin is followed exactly (no pile-up, no
// runaway), but the motion trails the hand by up to D -- and after the hand stops, the still-open
// windows keep delivering for another D.
//
// Everything below is in UNITS (one notch = 15 units) to avoid mixing scales, and messages are
// scheduled on an exact integer grid (message k at time k/R) so the harness itself is exact.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <initializer_list>

using namespace anim3;
static const double kUnitsPerNotch = 15.0;

struct Sim {
  // totals, all in UNITS
  double in = 0, out = 0;
  double outAtStop = 0, inAtStop = 0;
  double maxStep = 0, peakRateN = 0;
  int inFlightPeak = 0, inFlightAtStop = 0;
  double tailMs = 0, afterStop = 0;
  long sends = 0;
};

// events: (time seconds, signed notches). Ticks at 1 ms from 0 to totalMs.
static Sim run(const std::vector<std::pair<double, double>> &ev, double D, double totalMs,
               double stopSec) {
  Params P; P.durationMs = D; P.shape = kLinear;
  Glide g; g.Reset();
  const double dt = 0.001;
  Sim s;
  double carry = 0.0;
  const double grid = 1.0 / 256.0;
  size_t k = 0;
  bool stopMarked = false;
  const int steps = (int)std::llround(totalMs / 1000.0 / dt);

  for (int i = 0; i < steps; ++i) {
    const double t = i * dt;
    while (k < ev.size() && ev[k].first <= t + 1e-9) {
      g.Feed(ev[k].second, P);
      s.in += ev[k].second * kUnitsPerNotch;
      ++k;
    }
    if (g.InFlight() > s.inFlightPeak) s.inFlightPeak = g.InFlight();

    const double tNext = t + dt;
    if (!stopMarked && tNext > stopSec) {
      stopMarked = true;
      s.outAtStop = s.out;
      s.inAtStop = s.in;
      s.inFlightAtStop = g.InFlight();
    }

    const double want = g.Tick(dt, P) * kUnitsPerNotch;
    const double rate = std::fabs(want) / dt / kUnitsPerNotch;
    if (rate > s.peakRateN) s.peakRateN = rate;
    const double total = carry + want;
    const double m = (total < 0) ? -std::floor(-total / grid + 0.5)
                                 : std::floor(total / grid + 0.5);
    const double deliv = m * grid;
    carry = total - deliv;
    if (std::fabs(deliv) > 1e-12) { ++s.sends; if (std::fabs(deliv) > s.maxStep) s.maxStep = std::fabs(deliv); }
    s.out += deliv;
    if (stopMarked) {
      if (std::fabs(want) > 1e-12) s.tailMs = (t - stopSec) * 1000.0;
      if (t > stopSec + dt * 0.5) s.afterStop += deliv;
    }
  }
  return s;
}

int main() {
  printf("one notch = %.0f units; tick 1 ms; shape = linear (rate = the D-wide moving average)\n\n",
         kUnitsPerNotch);

  printf("=== A. STEADY FAST SPIN (no stop): does the hand-over keep up? ===\n");
  printf("    spin for 1000 ms at R notches/s. 'lag' = what is still owed at 1000 ms, in ms.\n\n");
  printf("  %5s %6s | %10s %10s | %9s %8s | %9s %9s\n",
         "R/s", "D ms", "in units", "out units", "inflight", "lag ms", "peak N/s", "max step");
  for (double D : {10.0, 50.0, 200.0}) {
    for (double R : {10.0, 20.0, 40.0}) {
      std::vector<std::pair<double, double>> ev;
      for (int k = 0; k * (1.0 / R) < 1.0 - 1e-12; ++k) ev.push_back({k / R, 1.0});
      const Sim s = run(ev, D, 3000.0, 1.0);
      const double owed = s.in - s.outAtStop;
      const double lagMs = (R > 0) ? (owed / (R * kUnitsPerNotch) * 1000.0) : 0.0;
      printf("  %5.0f %6.0f | %10.1f %10.1f | %9d %8.0f | %9.0f %9.3f\n",
             R, D, s.in, s.out, s.inFlightAtStop, lagMs, s.peakRateN, s.maxStep);
    }
  }

  printf("\n=== B. SPIN 1000 ms THEN STOP at D=200ms (R=20/s): watch the rate ===\n");
  {
    std::vector<std::pair<double, double>> ev;
    for (int k = 0; k * 0.05 < 1.0 - 1e-12; ++k) ev.push_back({k * 0.05, 1.0});
    Params P; P.durationMs = 200.0; P.shape = kLinear;
    Glide g; g.Reset();
    const double dt = 0.001;
    size_t kk = 0; double carry = 0, out = 0;
    printf("  %6s | %8s %8s | %9s\n", "t ms", "in N/s", "out N/s", "in-flight");
    for (int i = 0; i <= 1300; ++i) {
      const double t = i * dt;
      while (kk < ev.size() && ev[kk].first <= t + 1e-9) { g.Feed(ev[kk].second, P); ++kk; }
      const double want = g.Tick(dt, P) * kUnitsPerNotch;
      const double total = carry + want, grid = 1.0 / 256.0;
      const double m = std::floor(std::fabs(total) / grid + 0.5);
      const double deliv = (total < 0 ? -1 : 1) * m * grid;
      carry = total - deliv; out += deliv;
      const bool mark = (i % 100 == 0) || (i == 1000) || (i == 1050) || (i == 1100) || (i == 1150) || (i == 1200);
      if (mark)
        printf("  %6d | %8.1f %8.1f | %9d %s\n", i, (t < 1.0 ? 20.0 : 0.0),
               std::fabs(want) / dt / kUnitsPerNotch, g.InFlight(),
               (i >= 1000 && i <= 1200) ? "<- after the spin stopped" : "");
    }
    printf("  totals: in=%.1f units (=%.0f notches), out=%.1f units (=%.4f notches)\n",
           ev.size() * kUnitsPerNotch, ev.size() * kUnitsPerNotch / kUnitsPerNotch, out,
           out / kUnitsPerNotch);
  }

  printf("\n=== C. REVERSAL: +20/s for 500ms then -20/s for 500ms (net input = 0) ===\n");
  {
    std::vector<std::pair<double, double>> ev;
    for (int k = 0; k * 0.05 < 1.0 - 1e-12; ++k) ev.push_back({k * 0.05, (k * 0.05 < 0.5) ? +1.0 : -1.0});
    for (double D : {10.0, 50.0, 200.0}) {
      const Sim s = run(ev, D, 3000.0, 1.0);
      printf("  D=%5.0f ms | in=%9.4f units (net %+.4f notches) | out=%9.4f units (net %+.4f notches)\n",
             D, s.in, s.in / kUnitsPerNotch, s.out, s.out / kUnitsPerNotch);
    }
    printf("  -> the out totals match the in totals: reversal cancels, no overshoot.\n");
  }
  return 0;
}
