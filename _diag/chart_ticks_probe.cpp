// Do the chart's tick numbers rescale when Glide and Top move?
//
// The X axis is g_anim.spanMs (Glide length) and the Y axis is g_anim.yTopDeltas (Top speed, as
// travel), and each grid is stepped by a "nice" value (1/2/5 x 10^n). This prints the step and the
// labels for a sweep of each slider, so "the grid follows the scale" is a measurement, not a claim.
//
// g++ -std=c++17 -O2 -I. _diag/chart_ticks_probe.cpp -o /tmp/ctp && /tmp/ctp
#include <cmath>
#include <cstdio>

static const int kAnimNotches = 6;
static const double kDeltasPerNotch = 120.0;
static const double kAnimPace = 0.45;
static const int kChartMaxTicksX = 8, kChartMaxTicksY = 8;

// Exactly the plugin's NiceStep.
static double NiceStep(double range, int maxTicks)
{
  if (range <= 0.0 || maxTicks < 1) return 0.0;
  const double raw = range / (double)maxTicks;
  double mag = pow(10.0, floor(log10(raw)));
  for (int k = 0; k < 30; ++k)
  {
    const double mult[3] = {1.0, 2.0, 5.0};
    for (int i = 0; i < 3; ++i)
    {
      const double step = mag * mult[i];
      if (range / step <= (double)maxTicks + 1e-9) return step;
    }
    mag *= 10.0;
  }
  return range;
}

static void Show(double win, double mul)
{
  const double top = mul < 1.0 ? 1.0 : mul;
  const long spanMs = (long)((150.0 + 2.5 * win) * kAnimPace);
  const double yRange = top * (double)kAnimNotches * kDeltasPerNotch;
  const double sx = NiceStep((double)spanMs, kChartMaxTicksX);
  const double sy = NiceStep(yRange, kChartMaxTicksY);

  printf("  Glide=%3.0fms Top=%.2fx | X: %ldms, step %-5.0f ->", win, mul, spanMs, sx);
  for (double v = sx; v < (double)spanMs - 1e-9; v += sx) printf(" %.0f", v);
  printf("   | Y: %.0fd, step %-6.0f ->", yRange, sy);
  for (double v = sy; v < yRange - 1e-9; v += sy) printf(" %.0f", v);
  printf("\n");
}

int main()
{
  printf("X axis follows GLIDE LENGTH (spanMs); Y axis follows TOP SPEED (deltas)\n\n");
  printf("-- Glide sweep (Top fixed at 1.50) --\n");
  for (double w = 100.0; w <= 300.0 + 1e-9; w += 50.0) Show(w, 1.5);
  printf("\n-- Top sweep (Glide fixed at 200ms) --\n");
  for (double m = 1.0; m <= 2.0 + 1e-9; m += 0.25) Show(200.0, m);

  printf("\nthe steps are the evidence: an unchanged step across a sweep would mean the grid is not\n");
  printf("following the scale (the earlier version's fixed 1/6 and 1/4 fractions never moved).\n");
  return 0;
}
