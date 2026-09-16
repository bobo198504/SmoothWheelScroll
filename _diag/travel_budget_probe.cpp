// TRAVEL + BUDGET PROBE -- the model: a slow notch moves little, a fast one moves its whole size,
// and a free-spinning wheel and a notched mouse behave ALIKE at the same hand speed.
//
// model::Travel(messageDeltas, budget, budgetDeltas, startDeltas)
//   = start + (cap - start) * min(budget / budgetDeltas, 1),   cap = this message's own deltas.
// model::SpeedBudget adds deltas and decays over time, so the budget FOLLOWS the current speed.
//
// Checks:
//   1. endpoints: budget 0 -> start; budget >= full -> the message's own size (the cap);
//   2. the cap is the MESSAGE's size, so a free-spin message tops out lower than a notched one;
//   3. the budget follows speed: it rises while turning and falls back when the gaps grow;
//   4. device independence: the SAME hand movement gives the same total travel for a notched mouse
//      and a free-spinner (this is the whole point of counting deltas);
//   5. a steady hand speed settles at a steady travel (no ratchet, no jitter-driven jumps).
//
//   g++ -std=c++17 -O2 -I../src -o travel_probe.exe travel_budget_probe.cpp && ./travel_probe.exe
#include "model.h"
#include <cstdio>
#include <cmath>

static int failures = 0;
static void Check(const char *what, bool ok, const char *detail)
{
  if (!ok) ++failures;
  printf("  %-62s %-4s %s\n", what, ok ? "ok" : "FAIL", detail);
}

int main()
{
  const double BUD = 600.0, START = 1.0;

  printf("Travel(messageDeltas, budget, budgetDeltas, startDeltas)\n\n");

  printf("== 1. endpoints ==\n");
  Check("budget 0 -> the start travel", model::Travel(120.0, 0.0, BUD, START, 1.0) == START, "");
  Check("budget full -> the message's own size", model::Travel(120.0, BUD, BUD, START, 1.0) == 120.0, "");
  Check("budget beyond full -> still the message's size", model::Travel(120.0, BUD * 3, BUD, START, 1.0) == 120.0, "");
  Check("half budget -> halfway between", std::fabs(model::Travel(120.0, BUD / 2, BUD, START, 1.0) - (START + (120.0 - START) / 2)) < 1e-9, "");

  printf("\n== 2. the cap is the MESSAGE's size, not a fixed 120 ==\n");
  Check("a free-spin message (20) tops out at 20", model::Travel(20.0, BUD * 2, BUD, START, 1.0) == 20.0, "");
  Check("a notched message (120) tops out at 120", model::Travel(120.0, BUD * 2, BUD, START, 1.0) == 120.0, "");
  printf("   (so at full budget a free-spin notch moves 20, a notched one 120 -- each its own size)\n");

  printf("\n== 3. the budget follows speed (decays when the gaps grow) ==\n");
  {
    model::SpeedBudget b;
    b.Reset();
    // Fast burst: 8 messages of 15, 30 ms apart.
    double v = 0;
    for (int i = 0; i < 8; ++i) v = b.Add(15.0, i == 0 ? 0.0 : 30.0);
    const double fast = b.Value();
    // Then slow: gaps of 400 ms -> the budget must fall.
    for (int i = 0; i < 5; ++i) v = b.Add(15.0, 400.0);
    printf("   after a fast burst: %.0f  |  after 5 slow gaps (400ms): %.0f\n", fast, v);
    Check("a slow stretch lets the budget fall back", v < fast, "");
  }

  printf("\n== 4. device independence: same hand movement -> same total travel ==\n");
  {
    // One "flick": the hand turns 960 deltas over 1000 ms.
    //   notched: 8 messages of 120, 125 ms apart
    //   free-spin: 48 messages of 20, ~20.8 ms apart
    // The two must hand over nearly the same total.
    auto total = [&](double msgDeltas, double gapMs) {
      model::SpeedBudget b;
      b.Reset();
      const int n = (int)std::lround(960.0 / msgDeltas);
      double t = 0, sum = 0;
      for (int i = 0; i < n; ++i)
      {
        const double gap = (i == 0) ? 0.0 : gapMs;
        t += gap;
        b.Add(msgDeltas, gap);
        sum += model::Travel(msgDeltas, b.Value(), BUD, START, 1.0);
      }
      return sum;
    };
    const double nt = total(120.0, 125.0);
    const double ft = total(20.0, 1000.0 / 48.0);
    printf("   notched  (8x120): total travel %.0f\n", nt);
    printf("   free-spin(48x20): total travel %.0f\n", ft);
    const double ratio = nt > 0 ? ft / nt : 0;
    printf("   ratio %.3f\n", ratio);
    Check("both devices hand over nearly the same travel", std::fabs(ratio - 1.0) < 0.15, "");
  }

  printf("\n== 5. a steady hand speed settles at a steady travel ==\n");
  {
    model::SpeedBudget b;
    b.Reset();
    double last = 0, prev = 0;
    bool settled = true;
    for (int i = 0; i < 60; ++i)
    {
      // a constant hand speed: 40 deltas per 100 ms
      b.Add(40.0, i == 0 ? 0.0 : 100.0);
      const double tr = model::Travel(40.0, b.Value(), BUD, START, 1.0);
      if (i > 20 && std::fabs(tr - last) > 0.01)
        settled = false;
      last = tr;
      prev = tr;
    }
    (void)prev;
    printf("   steady 40 delta/100ms settles at %.2f per notch\n", last);
    Check("settled (no ratchet) and below the cap (40)", settled && last < 40.0, "");
  }

  printf("\n== 6. the speed multiplier only acts PAST the wheel's own speed ==\n");
  {
    Check("mul=1: budget 2x full still stops at the message's size",
          model::Travel(120.0, BUD * 2, BUD, START, 1.0) == 120.0, "");
    Check("mul=2: budget 2x full reaches 2x the message's size",
          std::fabs(model::Travel(120.0, BUD * 2, BUD, START, 2.0) - (2 * (120.0 - START) + START)) < 1e-9, "");
    Check("mul=2: budget beyond 2x is clamped at 2x",
          std::fabs(model::Travel(120.0, BUD * 3, BUD, START, 2.0) - (2 * (120.0 - START) + START)) < 1e-9, "");
    bool sameBelow = true;
    for (double bud = 0.0; bud <= BUD; bud += BUD / 20.0)
    {
      const double a = model::Travel(120.0, bud, BUD, START, 1.0);
      const double b2 = model::Travel(120.0, bud, BUD, START, 2.0);
      if (std::fabs(a - b2) > 1e-12)
        sameBelow = false;
    }
    printf("   below the baseline (budget <= ramp): mul=1 and mul=2 identical\n");
    Check("the multiplier changes nothing below full speed", sameBelow, "");
    Check("mul below 1 is read as 1 (never slows a roll)",
          model::Travel(120.0, BUD * 3, BUD, START, 0.5) == 120.0, "");
  }

  printf("\n%s\n", failures ? "FAIL" : "OK: travel from the speed budget, device-independent");
  return failures ? 1 : 0;
}
