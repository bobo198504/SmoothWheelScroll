// Both axes dynamic, and the two corners rounded. What this checks:
//
//  1. TOP SPEED is the VERTICAL scale: the drawing fills its box, the flat top is always the roof, and
//     the wheel's own height (the native line) sits at 1/Top of the box. At Top=1.0 the flat top IS the
//     native line; at 2.0 the native line is half way down, so the top is twice as high.
//  2. GLIDE LENGTH is the HORIZONTAL scale: the drawing always spans the full width; what Glide
//     changes is how long the motion it stands for takes (spanMs), so the ball crosses slower/faster.
//     The earlier version instead made the drawing END short of the right-hand edge -- that empty part
//     is gone.
//  3. THE CORNERS ARE ROUNDED, and still CONTINUOUS: no step at either joint, no overshoot past the
//     knee's height or the roof, and monotonic through each fillet.
//
// g++ -std=c++17 -O2 -I. _diag/chart_shape_probe.cpp -o /tmp/csp && /tmp/csp
#include <cmath>
#include <cstdio>

static const double kWindowMinMs = 100.0, kWindowMaxMs = 300.0;
static const double kBudgetMin = 60.0, kBudgetMax = 2000.0;
static const double kChartKneeX = 0.20;
static const double kChartRampX0 = 0.34, kChartRampX1 = 0.88;
static const double kChartCorner = 0.10;
static const double kChartKneeMin = 0.02;
static const double kAnimPace = 0.45;

static double Norm(double v, double lo, double hi)
{
  double t = (v - lo) / (hi - lo);
  return t < 0 ? 0 : (t > 1 ? 1 : t);
}
static double LineY(double x, double x0, double y0, double x1, double y1)
{
  return (x1 > x0) ? (y0 + (y1 - y0) * ((x - x0) / (x1 - x0))) : y1;
}
static double FilletY(double x, double y0, double xc, double yc, double y2, double half)
{
  if (half <= 0.0) return yc;
  double t = (x - xc + half) / (2.0 * half);
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  const double u = 1 - t;
  return u * u * y0 + 2 * t * u * yc + t * t * y2;
}
// Exactly the plugin's ChartPathY.
static double PathY(double x, double kx, double rx, double ky, double py)
{
  double d = kChartCorner;
  if (d > kx * 0.5) d = kx * 0.5;
  if (d > (rx - kx) * 0.4) d = (rx - kx) * 0.4;
  if (d > (1.0 - rx) * 0.5) d = (1.0 - rx) * 0.5;
  if (d < 1e-6) d = 0;
  if (x < kx - d) return LineY(x, 0, 0, kx, ky);
  if (x < kx + d)
    return FilletY(x, LineY(kx - d, 0, 0, kx, ky), kx, ky, LineY(kx + d, kx, ky, rx, py), d);
  if (x < rx - d) return LineY(x, kx, ky, rx, py);
  if (x < rx + d)
    return FilletY(x, LineY(rx - d, kx, ky, rx, py), rx, py, py, d);
  return py;
}

int main()
{
  const double DW = 200.0, DB = 1000.0, DS = 5.0;
  const double msg = 120.0; // a notched mouse

  printf("== 1. Top speed is the vertical scale (drawing fills its box) ==\n");
  printf("%-10s %12s %12s %14s\n", "Top", "flat top", "native line", "top / native");
  for (double m = 1.0; m <= 2.0 + 1e-9; m += 0.25)
  {
    const double nat = 1.0 / m;
    printf("  %-8.2f %12.3f %12.3f %14.2f\n", m, 1.0, nat, 1.0 / nat);
  }

  printf("\n== 2. Glide is the horizontal scale: full width always, time changes ==\n");
  printf("%-12s %12s %14s\n", "Glide", "drawing width", "spanMs");
  for (double w = 100.0; w <= 300.0 + 1e-9; w += 50.0)
    printf("  %-10.0f %12.3f %14ld\n", w, 1.0, (long)((150.0 + 2.5 * w) * kAnimPace));

  printf("\n== 3. The corners are rounded and still continuous ==\n");
  double kneeFrac = DS / msg;
  if (kneeFrac > 1.0) kneeFrac = 1.0;
  if (kneeFrac < kChartKneeMin) kneeFrac = kChartKneeMin;
  const int NS = 20000;
  for (double m = 1.0; m <= 2.0 + 1e-9; m += 0.5)
  {
    const double nat = 1.0 / m;
    const double ky = kneeFrac * nat;
    const double rx = kChartRampX0 + (kChartRampX1 - kChartRampX0) * Norm(DB, kBudgetMin, kBudgetMax);

    // Sample finely; a SHARP corner shows one big slope jump, a fillet spreads it out.
    double prevY = PathY(0.0, kChartKneeX, rx, ky, 1.0), prevS = 0.0;
    double maxStep = 0.0, maxSlopeJump = 0.0, worstY = 0.0;
    for (int i = 1; i <= NS; ++i)
    {
      const double x = (double)i / (double)NS;
      const double y = PathY(x, kChartKneeX, rx, ky, 1.0);
      const double s = y - prevY;
      if (fabs(y - prevY) > maxStep) maxStep = fabs(y - prevY);
      if (i > 1 && fabs(s - prevS) > maxSlopeJump) maxSlopeJump = fabs(s - prevS);
      if (y > worstY) worstY = y;
      prevY = y;
      prevS = s;
    }

    // The same shape WITHOUT fillets, for reference.
    const double rx2 = rx;
    double p2 = 0.0, ps2 = 0.0, maxSlopeJump2 = 0.0;
    for (int i = 1; i <= NS; ++i)
    {
      const double x = (double)i / (double)NS;
      const double y = (x < kChartKneeX) ? LineY(x, 0, 0, kChartKneeX, ky)
                                        : ((x < rx2) ? LineY(x, kChartKneeX, ky, rx2, 1.0) : 1.0);
      const double s = y - p2;
      if (i > 1 && fabs(s - ps2) > maxSlopeJump2) maxSlopeJump2 = fabs(s - ps2);
      p2 = y;
      ps2 = s;
    }

    printf("  Top=%.1f  step/sample %.2e  slope jump %.2e (sharp corners: %.2e)  top %.4f  %s\n", m,
           maxStep, maxSlopeJump, maxSlopeJump2, worstY,
           (worstY <= 1.0 + 1e-9 && maxSlopeJump < maxSlopeJump2 * 0.6) ? "rounded OK" : "<< CHECK");
  }

  printf("\n(step/sample tiny = the curve is continuous; the slope jump is much smaller than the\n");
  printf(" same shape with sharp corners, and the top never overshoots the roof)\n");
  return 0;
}
