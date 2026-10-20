// Parity check for the C# port of model.h -- WheelTravel.cs (Travel + SpeedBudget).
//
// WHY THIS ONE MATTERS MORE THAN THE OTHERS: Travel() is where "how far does one notch go" is decided,
// and it is also where the FREE-SPINNING wheel is made to behave like a notched one (the budget counts
// DELTAS, not messages). A transcription error here would not crash anything -- it would just make the
// two apps feel different at some speeds, which is the hardest kind of difference to notice.
//
// The transcription is mechanical: same expression order, same clamping order.
#include <cstdio>
#include <cmath>
#include <initializer_list>
#include "model.h"

// ---------------------------------------------------------------------------
// The C# WheelTravel / WheelTravel.SpeedBudget, transcribed.
// ---------------------------------------------------------------------------
namespace cs {

static double Travel(double messageDeltas, double budget, double budgetDeltas, double startDeltas,
                     double speedMul)
{
  double cap = messageDeltas;
  if (cap < 0.0) cap = 0.0;
  double start = startDeltas;
  if (start < 0.0) start = 0.0;
  if (start > cap) start = cap;
  if (!(budgetDeltas > 0.0)) return cap;
  double mul = speedMul > 1.0 ? speedMul : 1.0;
  double u = budget / budgetDeltas;
  if (u < 0.0) u = 0.0;
  if (u > mul) u = mul;
  return start + (cap - start) * u;
}

class SpeedBudget
{
public:
  static double DecayMs() { return 300.0; }
  void Reset() { budget_ = 0.0; }
  double Add(double messageDeltas, double gapMs)
  {
    double tau = DecayMs();
    if (gapMs <= 0.0) budget_ += messageDeltas;
    else if (gapMs >= tau) budget_ = messageDeltas;
    else budget_ = budget_ * (1.0 - gapMs / tau) + messageDeltas;
    return budget_;
  }
  double Value() const { return budget_; }
private:
  double budget_ = 0.0;
};

} // namespace cs

static int g_fail = 0;
static void Check(bool ok, const char *what, double a = 0, double b = 0)
{
  if (ok) std::printf("  ok   %s\n", what);
  else { std::printf("  FAIL %s  (%.17g vs %.17g)\n", what, a, b); ++g_fail; }
}

int main()
{
  std::printf("=== model.h parity: WheelTravel.cs (transcribed) ===\n\n");

  // [1] Travel() over a dense sweep of every input, including the degenerate corners.
  std::printf("[1] Travel() over a sweep of all inputs\n");
  {
    double worst = 0.0;
    int cases = 0;
    const double deltas[] = {0, 1, 15, 20, 30, 60, 120};
    const double budgetDs[] = {0, 1, 60, 200, 1000, 2000};
    const double starts[] = {1, 5, 10};
    const double muls[] = {1.0, 1.2, 2.0, 0.5, 0.0};
    for (double d : deltas)
      for (double bd : budgetDs)
        for (double st : starts)
          for (double mu : muls)
            for (double budget = 0.0; budget <= 2200.0; budget += 37.0)
            {
              double a = model::Travel(d, budget, bd, st, mu);
              double b = cs::Travel(d, budget, bd, st, mu);
              double diff = std::fabs(a - b);
              if (diff > worst) worst = diff;
              ++cases;
            }
    char buf[140];
    std::snprintf(buf, sizeof(buf), "%d (delta, budget, ramp, start, mul) combinations, worst diff %.3g",
                  cases, worst);
    Check(worst == 0.0, buf, worst, 0.0);
  }

  // [2] SpeedBudget over a random gap stream.
  std::printf("\n[2] SpeedBudget over a random gap stream\n");
  {
    model::SpeedBudget cpp;
    cs::SpeedBudget csharp;
    unsigned seed = 999;
    double worst = 0.0;
    for (int i = 0; i < 100000; ++i)
    {
      seed = seed * 1103515245u + 12345u;
      const double gap = (double)((seed >> 16) % 900);   // 0..899 ms, spans tau from below to above
      const double amt = (double)(((seed >> 8) % 120) + 1);
      const double a = cpp.Add(amt, gap);
      const double b = csharp.Add(amt, gap);
      const double d = std::fabs(a - b);
      if (d > worst) worst = d;
    }
    Check(worst == 0.0, "100000 random additions, identical", worst, 0.0);
  }

  // [3] ★ DEVICE INDEPENDENCE -- the whole reason the budget counts DELTAS.
  //     A notched mouse and a free-spinning wheel turned at the SAME HAND SPEED must sit at the same
  //     point on the ramp. That point is `u`, not the travel in deltas.
  //
  //     WHY NOT COMPARE TRAVEL DIRECTLY (the first version of this test did, and looked like a
  //     failure): travel = start + (cap - start) * u, and `cap` is THIS DEVICE'S OWN message size --
  //     120 for the mouse, 15 for the free-spinner. That is by design ("each device caps at what it
  //     actually reports"), so comparing raw travel is comparing two different quantities. Multiplying
  //     the free-spinner's travel by 8 to "make it a notch" is wrong for the same reason: it scales the
  //     constant `start` term too, which is not what eight messages do.
  //
  //     The invariant that must hold is: SAME HAND SPEED => SAME u.
  std::printf("\n[3] device independence: same hand speed => same point on the ramp\n");
  {
    // Notched: one message every 125 ms (the measured notched gap), 120 deltas each.
    cs::SpeedBudget notched;
    double nb = 0.0;
    for (int i = 0; i < 8; ++i)
      nb = notched.Add(120.0, i == 0 ? 0.0 : 125.0);

    // Free-spin: the same 120 deltas per 125 ms, delivered as eight messages of 15.
    cs::SpeedBudget freeSpin;
    double fb = 0.0;
    for (int i = 0; i < 64; ++i)
      fb = freeSpin.Add(15.0, i == 0 ? 0.0 : 125.0 / 8.0);

    const double budgetDeltas = 1000.0, startDeltas = 5.0;

    // Recover u from the travel: travel = start + (cap - start) * u.
    const double tNotched = cs::Travel(120, nb, budgetDeltas, startDeltas, 1.0);
    const double tFree = cs::Travel(15, fb, budgetDeltas, startDeltas, 1.0);
    const double uNotched = (tNotched - startDeltas) / (120.0 - startDeltas);
    const double uFree = (tFree - startDeltas) / (15.0 - startDeltas);

    std::printf("       notched   budget=%.2f  -> u = %.4f\n", nb, uNotched);
    std::printf("       free-spin budget=%.2f  -> u = %.4f\n", fb, uFree);
    std::printf("       (raw travel differs because each device caps at its own message size:\n");
    std::printf("        %.3f of 120  vs  %.3f of 15 -- that is the design, not a defect)\n",
                tNotched, tFree);
    Check(std::fabs(uNotched - uFree) / uNotched < 0.05,
          "the two devices sit within 5% of the same point on the ramp", uNotched, uFree);
  }

  // [4] The decay's steady state is rate*tau whatever the gap -- the reason Euler is used over an
  //     exponential of the gap. Stated as a check so a "simplification" to exp() is caught.
  std::printf("\n[4] steady state is rate*tau, independent of the gap\n");
  {
    const double tau = cs::SpeedBudget::DecayMs();
    for (double gap : {10.0, 50.0, 125.0, 250.0})
    {
      // A steady stream that delivers `rate` deltas per ms: amount = rate*gap per message.
      const double rate = 1.0; // deltas per ms
      cs::SpeedBudget sb;
      double v = 0.0;
      for (int i = 0; i < 4000; ++i)
        v = sb.Add(rate * gap, gap);
      char buf[130];
      std::snprintf(buf, sizeof(buf), "gap=%5.0fms settles at %.1f (rate*tau = %.1f)", gap, v,
                    rate * tau);
      Check(std::fabs(v - rate * tau) < 1.0, buf, v, rate * tau);
    }
  }

  std::printf("\n%s\n", g_fail ? "*** FAILURES ***" : "ALL CHECKS PASSED");
  return g_fail ? 1 : 0;
}
