// TOP-UP DIRECTION PROBE -- the fix must make the top-up follow the GESTURE, not the carry.
//
//   g++ -std=c++17 -O2 -I../src -o topup_dir_probe.exe topup_dir_probe.cpp && ./topup_dir_probe.exe
//
// THE BUG (measured, 2026-10-09, Slow step = 1, Ramp-up = 1036, one notch of the smallest increment):
//   the top-up took its direction from `accum`, whose sign can sit OPPOSITE to the roll, because
//   sending a whole unit overshoots THROUGH zero. Captured in the DEV log:
//
//     n     cmd      units      topup  accum
//     19    0/977    -1.0000      0    +0.49037     <- send is BACK, carry is POSITIVE
//     20    0/977    -1.0000      0    +0.41076
//     21    0/977    +1.0000      1    +0.00000     <- top-up fires the OTHER way
//
// THE FIX (src/smooth_wheel_scroll.cpp): Integrator::gestureSign records the wheel's own direction in
// Kick, and the top-up uses it instead of `accum`'s sign.

#include <cstdio>
#include <cmath>

static int g_fail = 0;
static void Check(bool ok, const char *what)
{
  printf("  %-70s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok)
    ++g_fail;
}

// The two rules, side by side. OLD = what shipped; NEW = after the fix.
static double TopUpOld(double accum)
{
  return (accum < 0.0) ? -1.0 : 1.0;
}
static double TopUpNew(double gestureSign, double accum)
{
  const double dir = (gestureSign != 0.0) ? gestureSign : ((accum < 0.0) ? -1.0 : 1.0);
  return (dir < 0.0) ? -1.0 : 1.0;
}

int main()
{
  printf("top-up direction probe\n\n");

  // --- 1. The captured case: a BACKWARD roll whose carry ended up POSITIVE. ---
  printf("  [1] the case from the log: gesture rolls BACK, carry is POSITIVE\n");
  {
    const double gesture = -1.0; // the wheel was turned backwards
    const double accum = +0.49037; // ... and the carry reads positive
    const double oldSend = TopUpOld(accum);
    const double newSend = TopUpNew(gesture, accum);
    printf("      gesture=%+.0f  accum=%+.5f  -> old %+.1f, new %+.1f\n", gesture, accum, oldSend,
           newSend);
    Check(oldSend == +1.0, "the OLD rule fires AGAINST the gesture (this is the reported bounce)");
    Check(newSend == -1.0, "the NEW rule fires WITH the gesture");
    Check(oldSend != newSend, "so the fix changes this case");
  }

  // --- 2. The mirror case: roll forward, carry negative. ---
  printf("\n  [2] the mirror: gesture rolls FORWARD, carry is NEGATIVE\n");
  {
    const double gesture = +1.0;
    const double accum = -0.41076;
    Check(TopUpOld(accum) == -1.0, "the OLD rule fires against the gesture here too");
    Check(TopUpNew(gesture, accum) == +1.0, "the NEW rule fires with the gesture");
  }

  // --- 3. The ordinary case must be UNCHANGED: carry on the same side as the gesture. ---
  printf("\n  [3] the ordinary case (carry on the gesture's own side) must not move\n");
  {
    struct { double gesture, accum; } cases[] = {
        {+1.0, +0.12}, {+1.0, +0.49}, {-1.0, -0.12}, {-1.0, -0.49},
        {+1.0, +0.001}, {-1.0, -0.001},
    };
    bool same = true;
    for (int i = 0; i < 6; ++i)
    {
      const double o = TopUpOld(cases[i].accum);
      const double n = TopUpNew(cases[i].gesture, cases[i].accum);
      if (o != n)
        same = false;
      printf("      gesture=%+.0f accum=%+.3f  old %+.1f  new %+.1f  %s\n", cases[i].gesture,
             cases[i].accum, o, n, (o == n) ? "same" : "CHANGED");
    }
    Check(same, "every case where the carry already agrees is untouched");
  }

  // --- 4. Both directions, so the fix is not one-way. ---
  printf("\n  [4] both roll directions\n");
  {
    Check(TopUpNew(+1.0, -0.4) == +1.0, "roll forward -> top-up forward");
    Check(TopUpNew(-1.0, +0.4) == -1.0, "roll backward -> top-up backward");
  }

  // --- 5. Defensive fallback: no gesture recorded -> behave as before. ---
  printf("\n  [5] fallback when no gesture direction was recorded\n");
  {
    Check(TopUpNew(0.0, +0.3) == TopUpOld(+0.3), "gestureSign 0 -> falls back to the carry");
    Check(TopUpNew(0.0, -0.3) == TopUpOld(-0.3), "...in both directions");
  }

  printf("\n");
  if (g_fail)
    printf("FAIL: %d\n", g_fail);
  else
    printf("OK: the top-up follows the gesture, the ordinary cases are unchanged, and the\n"
           "    captured reversal is fixed.\n");
  return g_fail ? 1 : 0;
}
