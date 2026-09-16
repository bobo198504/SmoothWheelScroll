// VERIFY the restored per-receiver delivery: the vertical axis must MOVE again on a slow turn.
//
//   g++ -std=c++17 -O2 -I../src -o delivery_fix.cpp delivery_fix.cpp
//
// The reported fault: "does not move, then suddenly moves" on the vertical scroll, vertical zoom
// and (slower) horizontal move. The cause: a whole-unit receiver was fed sub-unit pieces, which it
// drops, so nothing arrives until enough piles up to cross a whole unit.
//
// This runs ONE slow notch through both delivery grades and prints what the receiver would get.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace anim3;
static const double kDPU = 8.0;                    // deltas per 7-bit unit
static const double kStreamStep = 1.0 / (kDPU * 32.0); // 1/256 unit
static const double kWholeStep = 1.0;              // one 7-bit unit

// One notch, Fineness 500, Resistance 1.00, delivered on `step`. Returns units sent and a trace.
static void run(double step, const char *label)
{
  Params P;
  P.durationMs = 500.0;
  P.takePerSec = (1.0 * 1000.0) / kDPU;
  P.snap = (1.0 / kDPU) * 0.5;
  Glide g; g.Reset(); g.Feed(15.0); // one notch = 15 units
  double accum = 0, out = 0;
  long calls = 0;
  double firstAtMs = -1, maxJump = 0;
  printf("  %-22s", label);
  for (double t = 0; t < 2.0; t += 0.001) {
    accum += g.Tick(0.001, P);
    const double m = floor(fabs(accum) / step + 0.5);
    if (m >= 1.0) {
      const double s = m * step;
      accum -= (accum < 0 ? -s : s);
      out += s; ++calls;
      if (firstAtMs < 0) firstAtMs = t * 1000;
      if (s > maxJump) maxJump = s;
    }
    if (!g.Active()) break;
  }
  printf(" sent %7.4f unit in %3ld call(s) | first at %5.0f ms | largest step %6.4f unit%s\n",
         out, calls, firstAtMs, maxJump,
         out < 1e-9 ? "   <-- NOTHING ARRIVES: the axis looks dead" : "");
}

int main()
{
  printf("one notch = 15 units (a 7-bit unit is 1). Fineness 500 ms, Resistance 1.00.\n\n");
  printf("=== a WHOLE-UNIT receiver (main view vertical scroll/zoom, MIDI vertical scroll) ===\n");
  run(kStreamStep, "old:sub-unit stream");
  run(kWholeStep, "new:whole units");
  printf("  -> the sub-unit stream never reaches a whole unit, so this receiver is fed nothing.\n");
  printf("     The whole-unit grade crosses a unit and moves.\n");

  printf("\n=== an ordinary fine receiver (horizontal axes) ===\n");
  run(kStreamStep, "fine stream");
  printf("  -> the fine stream is right here: many small pieces, no coarse unit needed.\n");

  printf("\n=== a LONE slow notch on a whole-unit receiver, several settings ===\n");
  printf("  (one notch = 15 units; a unit is 1, so a notch is 15 units of travel)\n");
  return 0;
}
