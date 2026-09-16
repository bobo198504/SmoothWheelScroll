// Correct comparison: how the shape changes when the SLOW end of the ramp is moved out.
//
//   g++ -std=c++17 -O2 -o ramp_shift.cpp ramp_shift.cpp
//
// travel(gap) = slowMove + (notch - slowMove) * u,  u = (1 - x) / (1 - fastFrac)
//   x = gap / slowGap   (so x = 1 at the slow end, x = fastFrac at the fast end)
// This is a straight line in the GAP: every extra ms between notches removes the same travel.

#include <cstdio>
#include <cmath>
#include <initializer_list>

static const double D = 120.0;

static double travel(double gapMs, double slowMove, double slowGapMs, double fastGapMs)
{
  const double x = gapMs / slowGapMs;
  const double fastFrac = fastGapMs / slowGapMs;
  if (x >= 1.0) return slowMove;              // slower than the slow end: clamped
  if (x <= fastFrac) return D;                // faster than the fast end: full notch
  const double u = (1.0 - x) / (1.0 - fastFrac);
  return slowMove + (D - slowMove) * u;
}

static void table(const char *name, double slowMove, double slowGap, double fastGap)
{
  printf("  %-34s |", name);
  const double g[] = {500, 400, 350, 300, 250, 200, 150, 100, 75, 50, 30};
  for (int i = 0; i < 11; ++i) printf(" %6.1f", travel(g[i], slowMove, slowGap, fastGap));
  printf("\n");
}

int main()
{
  printf("travel per notch (deltas). Columns are the gap: 500 400 350 300 250 200 150 100 75 50 30 ms\n\n");
  printf("slowMove = 1 delta\n");
  table("slow end 200 (today)", 1.0, 200.0, 50.0);
  table("slow end 300", 1.0, 300.0, 50.0);
  table("slow end 400", 1.0, 400.0, 50.0);
  table("slow end 500", 1.0, 500.0, 50.0);
  printf("\nslowMove = 12 delta\n");
  table("slow end 200 (today)", 12.0, 200.0, 50.0);
  table("slow end 300", 12.0, 300.0, 50.0);
  table("slow end 400", 12.0, 400.0, 50.0);
  table("slow end 500", 12.0, 500.0, 50.0);

  printf("\n=== is each row straight? (equal gaps must give equal steps) ===\n");
  for (double sm : {1.0, 12.0}) {
    for (double sg : {200.0, 400.0}) {
      double prev = travel(300, sm, sg, 50.0), d1 = -1, maxErr = 0;
      int n = 0;
      for (double g = 275; g >= 50; g -= 25) {
        const double v = travel(g, sm, sg, 50.0);
        const double step = v - prev;
        if (n == 0) d1 = step; else if (std::fabs(step - d1) > maxErr) maxErr = std::fabs(step - d1);
        ++n; prev = v;
      }
      printf("  slowMove %.0f, slow end %.0f: step %.3f, max deviation %.6f -> %s\n",
             sm, sg, d1, maxErr, maxErr < 1e-9 ? "straight" : "NOT straight");
    }
  }
  return 0;
}
