// What precision does the main view's HORIZONTAL ZOOM actually get?
//
//   g++ -std=c++17 -O2 -I../src -o hzoom_precision.exe hzoom_precision.cpp && ./hzoom_precision.exe
//
// It runs the REAL model (via the seam) for one notch of a horizontal zoom and feeds the travel
// through the SAME two steps the plugin uses:
//   * the delivery grid for that receiver (kStream -> 1/256 of a 7-bit unit), and
//   * the relative encoder EncodeRel1 (val + valhw),
// then decodes every message back into wheel DELTAS so the answer is in the unit the question uses.
//
// Reference: one wheel notch = 120 deltas = 15 seven-bit units, so 1 unit = 8 deltas.

#include "model.h"
#include <cstdio>
#include <cmath>

static const double kNotchUnits = 15.0;
static const double kDeltasPerNotch = 120.0;
static const double kDeltasPerUnit = kDeltasPerNotch / kNotchUnits; // 8
static const double kRelSubPerUnit = 256.0; // the relative form's fractional resolution
static const double kVertStepsPerUnit = 1.0; // the whole-unit receivers' grid

// 1.6.1's encoder, verbatim.
static void EncodeRel1(double d, int *val, int *valhw)
{
  int val7bit = 0, z = 0;
  if (d < 0.0)
  {
    const double w = floor(d);
    if (w >= -64.0)
    {
      val7bit = (int)w;
      z = (int)((d - w) * 256.0 + 0.5);
    }
    else
      val7bit = -64;
  }
  else if (d > 0.0)
  {
    const double w = ceil(d);
    if (w <= 63.0)
    {
      val7bit = (int)w;
      z = (int)((w - d) * 256.0 + 0.5);
    }
    else
      val7bit = 63;
  }
  *val = val7bit & 0x7f;
  *valhw = -1 - (z > 255 ? 255 : z);
}

// Decode a sent (val, valhw) back into a signed float in 7-bit units, per the SDK's documented
// relative form: float = val + (val<0 ? z/256 : val>0 ? -z/256 : 0), valhw = -1-z.
static double DecodeRel1(int val, int valhw)
{
  const int v = (val & 0x40) ? (val - 128) : val; // sign-extend the 7-bit field
  const int z = -1 - valhw;
  if (v < 0)
    return v + z / 256.0;
  if (v > 0)
    return v - z / 256.0;
  return 0.0;
}

// The shipped defaults.
static model::Params P()
{
  model::Params p;
  p.startPct = 15.0;
  p.accelPct = 7.0;
  p.releaseMs = 200.0;
  p.hold = 1.0;
  p.coast = 1.0;
  p.onsetMs = 85.0;
  p.burstGapMs = 250.0;
  p.ceilingUnit = 1605.6;
  for (int i = 0; i < model::kSegments; ++i)
    p.bend[i] = 0.0;
  p.bend[2] = 1.625;
  return p;
}

// Run one notch through the model and the given grid; report the messages and the smallest step.
static void Report(const char *label, double grid)
{
  const model::Params p = P();
  model::Axis g;
  g.Reset();
  g.Kick(kNotchUnits, +1.0, 990, 0.0, p); // one notch, the horizontal-zoom action's id

  const double dt = 0.001;
  double accum = 0, sent = 0, smallest = 1e30;
  int msgs = 0, belowHalfDelta = 0;
  printf("  %s\n", label);
  for (double t = 0; t < 6.0; t += dt)
  {
    if (!g.Active() && t > 0)
      break;
    accum += g.Tick(dt, p);
    const double m = floor(fabs(accum) / grid + 0.5);
    if (m >= 1.0)
    {
      const double d = m * grid;
      const double sign = (accum < 0.0) ? -1.0 : 1.0;
      accum -= sign * d;
      int val = 0, valhw = 0;
      EncodeRel1(sign * d, &val, &valhw);
      const double got = DecodeRel1(val, valhw); // units, signed
      const double deltas = fabs(got) * kDeltasPerUnit;
      ++msgs;
      sent += fabs(deltas);
      if (deltas > 0.0 && deltas < smallest)
        smallest = deltas;
      if (deltas < 0.5)
        ++belowHalfDelta;
    }
    if (t > 0 && !g.Active())
      break;
  }
  printf("    messages = %d   total sent = %.4f delta   smallest non-zero message = %.6f delta\n",
         msgs, sent, smallest);
  // Compare against ONE delta, which is the unit the question is asked in.
  const char *verdict = (smallest < 0.999)
                            ? "finer than 1 delta"
                            : (fabs(smallest - 1.0) < 0.01 ? "one whole delta"
                                                           : "coarser than 1 delta");
  printf("    smallest step = %.6f delta = %.4f of a 7-bit unit  ->  %s\n",
         smallest, smallest / kDeltasPerUnit, verdict);
  printf("\n");
}

int main()
{
  printf("one notch = %.0f deltas = %.0f seven-bit units  ->  1 unit = %.0f deltas\n\n",
         kDeltasPerNotch, kNotchUnits, kDeltasPerUnit);

  const double streamGrid = 1.0 / kRelSubPerUnit;           // 1/256 unit
  const double stepGrid = 1.0 / kVertStepsPerUnit;          // 1 unit

  printf("The relative form's finest step is 1/256 unit = %.6f delta.\n\n", kDeltasPerUnit / 256.0);

  Report("MAIN HORIZONTAL ZOOM  (990/979)  ->  kStream  grid = 1/256 unit",
         streamGrid);
  Report("MAIN VERTICAL ZOOM    (1000/1001) -> kStepUnits grid = 1 unit",
         stepGrid);

  printf("So: horizontal = 1/256 unit = 1/32 delta grid  (NOT 8, NOT 1).\n");
  printf("    vertical   = 1 unit     = 8 deltas grid.\n");
  return 0;
}
