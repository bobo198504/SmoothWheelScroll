// Does each slider move its OWN visual channel, and only its own?
//
// This replicates the panel's chart geometry (src/smooth_wheel_scroll.cpp, BuildAnimCurves) for a
// grid of slider settings and reports the four things the user asked to be able to read:
//
//   * Glide    -> spanFrac  (the WIDTH of the picture)
//   * Slow step-> kneeY     (the HEIGHT of the first segment's end, vs the NATIVE LINE)
//   * Ramp-up  -> reachX    (how FAR ALONG the climb reaches the top; smaller = steeper)
//   * Top speed-> peakY     (the flat top's height, vs the native line)
//
// For each slider it sweeps that slider alone and prints the other three channels alongside, so any
// cross-talk is visible rather than argued about.
//
// The knee is checked against the DEVICE, because that was the correction the user called for: on a
// notched mouse (120 delta per message) a slow step of 10 is 10/120 of a notch, and on a free-spinner
// (15 delta messages) the same 10 is 10/15. Those must be different, and the notched one must be low.
//
// g++ -std=c++17 -O2 -I. _diag/chart_channels_probe.cpp -o /tmp/ccp && /tmp/ccp
#include <cmath>
#include <cstdio>

static const double kDeltasPerNotch = 120.0;

static const double kWindowMinMs = 100.0, kWindowMaxMs = 300.0;
static const double kBudgetMin = 60.0, kBudgetMax = 2000.0;

static const double kChartKneeMin = 0.02;
static const double kChartKneeX = 0.20;
static const double kChartRampX0 = 0.34, kChartRampX1 = 0.88;
static const double kChartNatTop = 0.5;
static const double kAnimPace = 0.45;

struct Ch
{
  double width;  // spanFrac: Glide
  double knee;   // kneeY, in box units
  double reach;  // reachX: Ramp-up
  double peak;   // peakY, in box units
  long spanMs;
};

static double Norm(double v, double lo, double hi)
{
  double t = (v - lo) / (hi - lo);
  return t < 0 ? 0 : (t > 1 ? 1 : t);
}

static Ch Measure(double win, double slow, double budget, double mul, double msgDeltas)
{
  Ch c;
  const double top = mul < 1.0 ? 1.0 : mul;
  double kneeFrac = slow / msgDeltas;
  if (kneeFrac > 1.0) kneeFrac = 1.0;
  if (kneeFrac < kChartKneeMin) kneeFrac = kChartKneeMin;

  c.width = 0.34 + 0.66 * Norm(win, kWindowMinMs, kWindowMaxMs);
  c.spanMs = (long)((150.0 + 2.5 * win) * kAnimPace);
  c.reach = kChartRampX0 + (kChartRampX1 - kChartRampX0) * Norm(budget, kBudgetMin, kBudgetMax);
  c.knee = kneeFrac * kChartNatTop;
  c.peak = top * kChartNatTop;
  return c;
}

static void Row(const char *label, double sw, double ss, double sb, double sm, double msg)
{
  const Ch c = Measure(sw, ss, sb, sm, msg);
  printf("%-22s %7.3f %7.3f %7.3f %7.3f %7ld\n", label, c.width, c.knee, c.reach, c.peak,
         c.spanMs);
}

int main()
{
  static const double DW = 200.0, DS = 5.0, DB = 1000.0, DM = 1.5; // the new defaults
  const double notched = kDeltasPerNotch;          // 120
  const double freeSpin = kDeltasPerNotch / 8.0;   // 15

  printf("defaults: Glide %.0fms, Slow %.0fd, Ramp-up %.0fd, Top %.2fx\n", DW, DS, DB, DM);
  printf("the native line is fixed at %.2f of the box; 'peak x native' is the readable form\n\n",
         kChartNatTop);
  printf("%-22s %7s %7s %7s %7s %7s\n", "setting", "width", "knee", "reach", "peak", "span ms");
  printf("%-22s %7s %7s %7s %7s %7s\n", "", "(Glide)", "(Slow)", "(Ramp)", "(Top)", "");
  Row("DEFAULT notched", DW, DS, DB, DM, notched);
  Row("DEFAULT free-spin", DW, DS, DB, DM, freeSpin);
  printf("\n");

  // Slow step, both devices: the knee must track start/message, and be MUCH lower on a notched mouse.
  printf("SLOW STEP alone -- knee as a fraction of the NATIVE line (0.50 of the box):\n");
  for (double v = 1.0; v <= 10.0 + 1e-9; v += 3.0)
  {
    char a[40], b[40];
    _snprintf(a, sizeof(a), "Slow=%.0fd notched", v);
    _snprintf(b, sizeof(b), "Slow=%.0fd free-spin", v);
    const Ch n = Measure(DW, v, DB, DM, notched);
    const Ch f = Measure(DW, v, DB, DM, freeSpin);
    printf("  %-20s knee %5.1f%% of native   |   %-20s knee %5.1f%% of native\n", a,
           100.0 * n.knee / kChartNatTop, b, 100.0 * f.knee / kChartNatTop);
  }
  printf("\n");

  // Glide alone: width (and span) must move; the other three must not.
  for (double v = 100.0; v <= 300.0 + 1e-9; v += 100.0)
  {
    char lab[32];
    _snprintf(lab, sizeof(lab), "Glide=%.0fms", v);
    Row(lab, v, DS, DB, DM, notched);
  }
  printf("\n");
  // Ramp-up alone: reach must move; the other three must not.
  for (double v = 60.0; v <= 2000.0 + 1e-9; v *= 5.8)
  {
    char lab[32];
    _snprintf(lab, sizeof(lab), "Ramp=%.0fd", v);
    Row(lab, DW, DS, v, DM, notched);
  }
  printf("\n");
  // Top speed alone: the peak moves, and the native line stays put (it is fixed at half the box).
  for (double v = 1.0; v <= 2.0 + 1e-9; v += 0.5)
  {
    const Ch c = Measure(DW, DS, DB, v, notched);
    printf("Top=%.1fx -> peak %.2f of the box = %.2fx the native line (which is fixed at %.2f)\n", v,
           c.peak, c.peak / kChartNatTop, kChartNatTop);
  }
  return 0;
}
