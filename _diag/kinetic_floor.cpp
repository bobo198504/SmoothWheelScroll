// THE USER'S WORRY: with a PROPORTION, can a small message be eaten down to nothing?
//
//   g++ -std=c++17 -O2 -I../src -o kinetic_floor.cpp kinetic_floor.cpp
//
// The kept amount is  received x (kinetic/120). So a 120-delta message keeps exactly `kinetic`
// deltas, but a 20-delta message keeps only a sixth of that -- at kinetic = 1 that is 0.17 delta.
// The question is whether such a tiny amount can still reach REAPER at all.
//
// The delivery grid is the smallest step REAPER can express: 1/256 of a 7-bit unit, and one delta
// is 1/8 unit, so the grid is 1/32 delta. Anything that rounds below half a grid step is carried
// to the next frame -- but if the gesture ends first, it is never sent.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace anim3;
static const double kDeltasPerUnit = 8.0;
static const double kGridDeltas = 1.0 / 32.0; // finest step REAPER's relative form can carry

int main() {
  printf("one notch = 120 deltas. delivery grid = 1/256 unit = %.5f delta.\n", kGridDeltas);
  printf("kept(deltas) = received x kinetic/120, at a fully slow turn.\n\n");

  printf("=== 1. the literal question: at kinetic = 1, what does a 20-delta message keep? ===\n");
  for (double rec : {120.0, 40.0, 20.0, 10.0, 2.0, 1.0}) {
    const double kept = rec * (1.0 - EatFraction(-1.0, rec, 1.0));
    printf("   received %6.1f delta -> keeps %7.4f delta = %7.4f unit = %8.2f grid steps%s\n",
           rec, kept, kept / kDeltasPerUnit, kept / kGridDeltas,
           (kept / kGridDeltas) < 0.5 ? "   <-- BELOW half a step: never sent on its own" : "");
  }

  printf("\n=== 2. same turn (120 deltas of travel), chopped differently, at kinetic = 1 ===\n");
  for (int n : {1, 2, 4, 6, 12}) {
    const double rec = 120.0 / n;
    const double keptEach = rec * (1.0 - EatFraction(-1.0, rec, 1.0));
    printf("   %2d message(s) of %5.1f delta -> %7.4f kept each, %7.4f kept in total\n",
           n, rec, keptEach, keptEach * n);
  }
  printf("   (the TOTAL is the same every way -- that is the device independence)\n");

  printf("\n=== 3. where does a message stop being delivered on its own? ===\n");
  printf("   a message is sent only if its kept amount rounds to >= half a grid step.\n");
  printf("   %-9s %-14s %s\n", "kinetic", "smallest recv", "that still reaches the grid");
  for (double k : {1.0, 2.0, 6.0, 12.0, 30.0, 60.0, 120.0}) {
    // need rec * k/120 >= 1/64  ->  rec >= 120/(64k)
    const double need = 120.0 / (64.0 * k);
    printf("   %-9.0f %-14.3f %s\n", k, need,
           need <= 1.0 ? "any message (>=1 delta) is fine" : "messages below this move nothing");
  }

  printf("\n=== 4. a 1-delta message (a very fine free-spinner) at each kinetic ===\n");
  for (double k : {1.0, 12.0, 60.0, 120.0}) {
    const double kept = 1.0 * (1.0 - EatFraction(-1.0, 1.0, k));
    printf("   kinetic %3.0f -> keeps %.5f delta = %6.2f grid steps  %s\n", k, kept,
           kept / kGridDeltas, (kept / kGridDeltas) >= 0.5 ? "(sent)" : "(carried, may never send)");
  }
  return 0;
}
