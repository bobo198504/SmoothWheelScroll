// WHY does a slow roll "start gentle, then suddenly speed up" at a SHORT easing?
//
//   g++ -std=c++17 -O2 -I../src -o slow_jump.cpp slow_jump.cpp
//
// Faithful copy of the plugin's delivery path for ONE axis:
//   Kick       : eat (anim3::EatFraction), Feed a window of `easeMs`
//   Tick 1 ms  : TickIntegrator -> DeliverTravel, then the whole-unit TOP-UP
//   the timer only runs while the glide is active (so an isolated window really is isolated)
//
// DeliverTravel has two grades, and they behave very differently:
//   kStream    : grid = 1/256 unit, so a small amount goes out smoothly on its own
//   kStepUnits : grid = ONE 7-bit unit = 8 deltas, so anything under half a unit waits -- and the
//                TOP-UP flushes a whole unit when the gesture ends (see Tick)

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

using namespace anim3;
static const double kD = 120.0; // deltas per notch
static const double kU = 15.0;  // units per notch

enum Filter { kStream, kStepUnits };

struct Axis {
  Glide glide;
  double accum = 0, last = 0, unitsOut = 0;
  bool sentThisBurst = false;
  long streamSends = 0, unitSends = 0, topUps = 0;
};

static void deliver(Axis &a, double step, Filter f, double ms, bool verbose)
{
  if (step == 0.0) return;
  a.accum += step;
  if (f == kStepUnits) {
    const double m = floor(fabs(a.accum) / 1.0 + 0.5);
    if (m >= 1.0) {
      const double send = (a.accum < 0.0) ? -m : m;
      a.accum -= send; a.sentThisBurst = true; a.unitsOut += send; ++a.unitSends;
      if (verbose) printf("   t=%7.1f  UNIT  %+5.0f   (accum now %.4f)\n", ms, send, a.accum);
    }
  } else {
    const double grid = 1.0 / 256.0;
    const double m = floor(fabs(a.accum) / grid + 0.5);
    if (m >= 1.0) {
      const double send = ((a.accum < 0.0) ? -m : m) * grid;
      a.accum -= send; a.unitsOut += send; ++a.streamSends;
      if (verbose) printf("   t=%7.1f  stream %+8.5f unit\n", ms, send);
    }
  }
}

static void run(const char *label, double easeMs, double kinetic, double gapMs, int n, Filter f,
                bool verbose)
{
  Axis a;
  Params P; P.durationMs = easeMs;
  const double gap = gapMs / 1000.0;
  double msgTime = 0; int fed = 0;
  const double end = n * gap + easeMs / 1000.0 + 0.05;
  printf("=== %s ===\n", label);
  for (double t = 0; t < end; t += 0.001) {
    if (fed < n && msgTime <= t + 1e-9) {
      const bool wasActive = a.glide.Active();
      if (!wasActive) a.sentThisBurst = false;
      a.last = t;
      const double gapSec = (fed == 0) ? -1.0 : gap; // the plugin's rule: first message = slowest
      const double kept = kD * (1.0 - EatFraction(gapSec, kD, kinetic));
      a.glide.Feed(kept / kD, P);
      if (verbose)
        printf("   t=%7.1f  MSG   keeps %6.3f delta = %6.4f unit\n", t * 1000, kept, kept / 8.0);
      ++fed; msgTime += gap;
    }
    if (a.glide.Active()) {
      double dt = t - a.last; a.last = t;
      if (dt < 0) dt = 0;
      if (dt > 0.25) dt = 0.25;
      const bool wasActive = a.glide.Active();
      const double step = a.glide.Tick(dt, P);
      deliver(a, step, f, t * 1000, verbose);
      if (wasActive && !a.glide.Active() && f == kStepUnits && !a.sentThisBurst && a.accum != 0.0) {
        const double send = (a.accum < 0.0) ? -1.0 : 1.0;
        a.accum = 0.0; a.unitsOut += send; ++a.unitSends; ++a.topUps;
        if (verbose) printf("   t=%7.1f  TOP-UP %+5.0f unit  (leftover flushed)\n", t * 1000, send);
      }
    }
  }
  printf("   TOTAL %7.3f units = %7.3f deltas   | sends %ld | whole-unit sends %ld (top-ups %ld)\n\n",
         a.unitsOut, a.unitsOut * 8.0, a.streamSends + a.unitSends, a.unitSends, a.topUps);
}

int main()
{
  printf("Slow roll: one notch every 150 ms, 10 notches. kinetic 1 and 12, easing 20 and 300 ms.\n");
  printf("Intended output for kinetic=1 is 1 delta per notch = 10 deltas in total.\n\n");

  run("STREAM (fine)  easing 20ms  kinetic 1", 20, 1, 150, 10, kStream, false);
  run("STREAM (fine)  easing 300ms kinetic 1", 300, 1, 150, 10, kStream, false);
  run("WHOLE-UNIT     easing 300ms kinetic 1", 300, 1, 150, 10, kStepUnits, false);
  run("WHOLE-UNIT     easing 20ms  kinetic 1", 20, 1, 150, 10, kStepUnits, true);

  printf("=== for contrast, the tuned value: WHOLE-UNIT, easing 20ms, kinetic 12 ===\n");
  run("contrast", 20, 12, 150, 10, kStepUnits, false);
  return 0;
}
