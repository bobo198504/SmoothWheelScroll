// What the panel's motion track shows, in numbers rather than by eye.
//
// NOTE (2026-09-16): the chart NO LONGER runs the model. It is a schematic whose four sliders each
// own one visual channel -- see AGENTS.md 114, and chart_channels_probe.cpp for the channel grid.
// What this probe covers is the ONE part that survives from the model-run version and is still a
// real measurement: the GREY STAIRCASE is the wheel's own path, and how many steps it has depends on
// the DEVICE -- a notched mouse sends one 120-delta message per notch, a free-spinner eight 15-delta
// ones, so the same roll is a coarse staircase or a fine one. That is the "coarse grid / fine grid"
// difference the user asked to be able to see.
//
// It replicates the nStep arithmetic in src/smooth_wheel_scroll.cpp (BuildAnimCurves) exactly.
//
// g++ -std=c++17 -O2 -I. _diag/motion_track_probe.cpp -o /tmp/mtp && /tmp/mtp
#include <cstdio>

static const int    kAnimNotches    = 6;
static const double kDeltasPerNotch = 120.0;

// THE NATIVE LINE IS AT A FIXED HALF OF THE BOX (kChartNatTop in the plugin). The native height is
// therefore always 1.00x itself; what Top speed scales is the shaped curve's FLAT TOP, which is `top`
// times the native line. The staircase always tops out AT the native line.
static const double kChartNatTop = 0.5;

static double PeakOf(double speedMul)
{
  const double top = (speedMul < 1.0) ? 1.0 : speedMul;
  return top * kChartNatTop;
}

static double KneeOf(double startDeltas, double msgDeltas)
{
  double f = startDeltas / msgDeltas;
  if (f > 1.0) f = 1.0;
  if (f < 0.02) f = 0.02;
  return f * kChartNatTop;
}

int main()
{
  printf("the scripted roll: %d notches, %.0f delta each = %.0f deltas\n\n", kAnimNotches,
         kDeltasPerNotch, kAnimNotches * kDeltasPerNotch);

  printf("%-12s %8s %10s %12s %14s\n", "device", "per msg", "steps", "step height", "knee @ Slow=10");
  struct Dev { const char *name; double q; };
  const Dev devs[2] = {{"notched", 120.0}, {"free-spin", 15.0}};
  for (int i = 0; i < 2; ++i)
  {
    const double q = devs[i].q;
    const int nSub = (int)(kDeltasPerNotch / q + 0.5);
    const int nStep = kAnimNotches * nSub;
    printf("%-12s %8.0f %10d %12.3f %14.1f%%\n", devs[i].name, q, nStep, kChartNatTop / nStep,
           100.0 * KneeOf(10.0, q) / kChartNatTop);
  }

  printf("\nTop speed, in these terms -- the native line never moves (fixed at %.2f of the box):\n",
         kChartNatTop);
  for (double m = 1.0; m <= 2.0 + 1e-9; m += 0.25)
    printf("  Top=%.2fx -> the flat top sits at %.2f of the box = %.2fx the native line\n", m,
           PeakOf(m), PeakOf(m) / kChartNatTop);

  printf("\nthe user's rule: at Top=1.0 the flat top IS the native line, at Top=2.0 it is twice as high\n");
  printf("  Top=1.0 -> %.2fx native;  Top=2.0 -> %.2fx native\n", PeakOf(1.0) / kChartNatTop,
         PeakOf(2.0) / kChartNatTop);
  return 0;
}
