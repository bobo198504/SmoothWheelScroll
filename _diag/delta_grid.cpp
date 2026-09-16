// Focused: is a 1-DELTA output grid enough? Measure the ACTUAL cost of coarsening the output grid,
// counting only the time WHILE the window is still running (the post-window tail is not a stall).
//
//   g++ -std=c++17 -O2 -I../src -o delta_grid.cpp delta_grid.cpp
//
// Grids compared (in 7-bit units): 1/256 (= 1/32 delta, today), 1/8 (= 1 delta), 1.0 (= 1 unit).
// Reported: the largest single step, and the longest run of sends that carried NOTHING while the
// window was still delivering (that is the only "stall" the eye could see).

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace anim3;
static const double kU = 15.0;      // units per notch
static const double kDelta = 0.125; // one raw delta, in units

struct R { double maxStepDelta; long sends; double maxGapMs; double jitterDelta; };

static R sim(double Dms, double grid, double tickMs, double inNotches, double inGapMs) {
  Params P; P.durationMs = Dms; P.shape = kSymS; P.bend = 0.0;
  Glide g; g.Reset();
  const double dt = tickMs / 1000.0, gap = inGapMs / 1000.0;
  double carry = 0, nextMsg = 0, out = 0;
  int fed = 0;
  R r{0, 0, 0, 0};
  double quiet = 0, expectedStep = 0;
  // expected per-send amount = (notches in flight) * dt; approximate steady value for jitter
  for (double t = 0; t < inNotches * gap + Dms / 1000.0 + 0.05; t += dt) {
    while (fed < (int)inNotches && nextMsg <= t + 1e-9) { g.Feed(1.0, P); ++fed; nextMsg += gap; }
    const bool windowLive = (t < (inNotches - 1) * gap + Dms / 1000.0);
    const double want = g.Tick(dt, P) * kU;
    const double tot = carry + want;
    const double m = (tot < 0) ? -std::floor(-tot / grid + 0.5) : std::floor(tot / grid + 0.5);
    const double deliv = m * grid;
    carry = tot - deliv;
    out += deliv;
    const double mag = std::fabs(deliv);
    if (mag > 1e-12) {
      ++r.sends;
      if (mag > r.maxStepDelta) r.maxStepDelta = mag;
      quiet = 0;
    } else if (windowLive) {
      quiet += tickMs;
      if (quiet > r.maxGapMs) r.maxGapMs = quiet;
    }
  }
  r.maxStepDelta /= kDelta; // convert to deltas
  return r;
}

static const char *GridName(double g_) {
  if (std::fabs(g_ - 1.0 / 256.0) < 1e-12) return "1/256 u (1/32 d)";
  if (std::fabs(g_ - 1.0 / 8.0) < 1e-12) return "1/8 u   (1 delta) ";
  return "1 u     (8 delta)   ";
}

static void row(const char *what, double D, double inN, double inGap) {
  printf("  %s  (D=%.0f ms, %s)\n", what, D,
         inN == 1 ? "single notch" : "a roll of notches");
  for (double grid : {1.0 / 256.0, 1.0 / 8.0, 1.0}) {
    const R r = sim(D, grid, 1.0, inN, inGap);
    printf("    grid %s | largest step %7.3f delta | sends %5ld | longest quiet gap %5.1f ms\n",
           GridName(grid), r.maxStepDelta, r.sends, r.maxGapMs);
  }
}

int main() {
  printf("1 notch = 120 deltas = 15 units.  1 delta = 0.125 unit.\n");
  printf("Tick = 1 ms. 'quiet gap' = longest stretch, WHILE A WINDOW IS STILL RUNNING, in which\n");
  printf("the plugin handed over nothing at all (that is the only stall the eye could catch).\n\n");

  printf("=== A. what a single notch looks like at each output grid ===\n\n");
  row("one notch, eased", 300.0, 1, 0);
  row("one notch, eased", 100.0, 1, 0);
  row("one notch, eased", 10.0, 1, 0);

  printf("\n=== B. a real fast roll: 10 notches in 200 ms (one every 20 ms) ===\n\n");
  row("10 notches / 200 ms", 300.0, 10, 20);
  row("10 notches / 200 ms", 100.0, 10, 20);

  printf("\n=== C. a slow creep: 1 notch per 300 ms ===\n\n");
  row("slow creep", 300.0, 1, 0);

  return 0;
}
