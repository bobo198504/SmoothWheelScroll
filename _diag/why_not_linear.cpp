// WHERE the current curve is NOT linear, measured in the range a hand actually rolls in.
//
//   g++ -std=c++17 -O2 -o why_not_linear.cpp why_not_linear.cpp
//
// The current shape (kEatZeroFrac = 0.25, kEatFullFrac = 1.0, reference 200 ms) is a straight line
// between 50 ms and 200 ms, then it is CLAMPED. So everything slower than 200 ms is a flat line --
// and that is exactly where a deliberate, slow roll lives. Rolling a little slower changes nothing,
// which is the "not linear" that was reported.

#include <cstdio>
#include <cmath>

static const double D = 120.0;

static double travel(double gapMs, double slowMoveDeltas)
{
  const double refMs = D / 600.0 * 1000.0;      // 200 ms for a full notch
  const double x = gapMs / refMs;
  const double full = 1.0, zero = 0.25;
  if (x >= full) return slowMoveDeltas;                       // clamped: FLAT
  if (x <= zero) return D;                                    // clamped: flat (native)
  const double u = (x - zero) / (full - zero);
  return slowMoveDeltas + (D - slowMoveDeltas) * u;
}

int main()
{
  printf("current shape, slow-move = 1 delta (the value asked for).\n");
  printf("gap ms   travel(delta)   change per 25 ms\n");
  const double g[] = {500, 450, 400, 350, 300, 250, 225, 200, 175, 150, 125, 100, 75, 50};
  double prev = 0;
  for (int i = 0; i < 14; ++i) {
    const double v = travel(g[i], 1.0);
    printf("%6.0f   %10.2f   %+8.2f%s\n", g[i], v, i ? v - prev : 0.0,
           (g[i] >= 200) ? "   <- FLAT (this is where slow rolling lives)" : "");
    prev = v;
  }
  printf("\n=> everything from 200 ms up is a flat line at 1 delta: a slow roll cannot answer the hand.\n");
  return 0;
}
