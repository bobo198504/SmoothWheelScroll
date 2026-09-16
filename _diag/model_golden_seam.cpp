// MODEL GOLDEN -- the "before" motion numbers of 1.6.1's model, so the model seam can be proven
// not to change the feel.
//
//   g++ -std=c++17 -O2 -I../src -o model_golden.exe model_golden.cpp && ./model_golden.exe
//
// Deterministic: fixed dt, fixed timings, no clocks. Frozen as test/expected/model_before.txt and
// later diffed against a run through the model:: seam.

#include "model.h"
using namespace anim;
#include <cstdio>
#include <cmath>

using namespace anim;

// The shipped panel defaults, EXACTLY as src/smooth_wheel_scroll.cpp declares them.
static const double kStartPct = 15.0;   // kDefaultStartPct
static const double kAccelPct = 7.0;    // kDefaultAccelPct
static const double kReleaseMs = 200.0; // kDefaultReleaseMs
static const double kHold = 1.0;        // kDefaultHold (slider)
static const double kCoast = 1.0;       // kDefaultCoast
static const double kOnsetMs = 85.0;    // kDefaultOnsetMs = (20+150)/2  (Start knob)
static const double kHoldBend = 1.625;  // kDefaultHoldBend = (1.25+2)/2 (Hold knob)
static const double kBurstGapMs = 250.0;
static const double kCeilingUnit = 1605.6;

static Params P()
{
  Params p;
  p.startPct = kStartPct;
  p.accelPct = kAccelPct;
  p.releaseMs = kReleaseMs;
  p.hold = kHold;
  p.coast = kCoast;
  p.onsetMs = kOnsetMs;
  p.burstGapMs = kBurstGapMs;
  p.ceilingUnit = kCeilingUnit;
  for (int i = 0; i < kSegments; ++i)
    p.bend[i] = 0.0;
  p.bend[2] = kHoldBend; // [2] is the Hold knob
  return p;
}

struct R { double total, peak; };

// n notches every gapSec, dt seconds per frame.
static R Run(int n, double gapSec, double dtSec)
{
  const Params p = P();
  Glide g;
  g.Reset();
  double t = 0, next = 0, total = 0, peak = 0;
  int fired = 0;
  for (long i = 0; i < 40000000L; ++i)
  {
    if (fired < n && t + 1e-12 >= next)
    {
      g.Kick(15.0, +1.0, 989, t, p);
      ++fired;
      next += gapSec;
    }
    g.Tick(dtSec, p);
    t += dtSec;
    const double v = std::fabs(g.Velocity());
    if (v > peak) peak = v;
    if (fired >= n && !g.Active()) break;
    if (t > gapSec * n + 20.0) break;
  }
  total = g.Total();
  return R{total, peak};
}

int main()
{
  static const double gaps[] = {0.250, 0.120, 0.060, 0.033};
  printf("  %-16s %12s %12s\n", "gesture", "total", "peak");
  {
    R r = Run(1, 0.060, 0.0156);
    printf("  %-16s %12.6f %12.6f\n", "single", r.total, r.peak);
  }
  for (int i = 0; i < 4; ++i)
  {
    R r = Run(10, gaps[i], 0.0156);
    char lab[32];
    _snprintf(lab, sizeof(lab), "10@%.0f", gaps[i] * 1000.0);
    printf("  %-16s %12.6f %12.6f\n", lab, r.total, r.peak);
  }
  // dt-independence spot check (must be identical to the single at 15.6 ms)
  printf("  -- dt independence (single notch) --\n");
  static const double dts[] = {0.001, 0.004, 0.0156, 0.030};
  for (int i = 0; i < 4; ++i)
  {
    R r = Run(1, 0.060, dts[i]);
    char lab[32];
    _snprintf(lab, sizeof(lab), "dt=%.3f", dts[i]);
    printf("  %-16s %12.6f %12.6f\n", lab, r.total, r.peak);
  }
  return 0;
}
