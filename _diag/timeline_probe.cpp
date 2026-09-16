// TIMELINE PROBE -- what does ONE fast message do to a slow roll?
//
// The eat is a RATE applied to the WHOLE in-flight pool, and that rate is set by the LAST message.
// So a single message that happens to be faster than its neighbours lowers (or zeroes) the brake for
// EVERY window still open -- including the slow ones just fed. Real slow rolls are not perfectly
// even, so this can fire on its own.
//
// Variants on the same scripted roll (slow, with one fast message in the middle):
//   POOL    -- what the plugin does now: one rate from the last message, applied to the whole pool
//   SMOOTH  -- the same, but the SCALE is a moving average of recent messages (one outlier cannot
//              swing the brake)
//
//   g++ -std=c++17 -O2 -I../src -o timeline_probe.exe timeline_probe.cpp && ./timeline_probe.exe
#include "model.h"
#include <cstdio>
#include <cmath>
#include <vector>

static const double kU = 15.0, kDPN = 120.0, kDPU = 8.0;
static const double kX = 100.0, kR = 15.0, kV = 1.0;

// One scripted roll: arrival times in ms, with one deliberately fast step.
static void Run(const char *title, const std::vector<double> &arrivalMs, bool smooth, int smoothN)
{
  model::Axis g;
  g.Reset();
  model::Params P;
  P.windowMs = kX;

  const double dt = 0.0005;
  const int n = (int)arrivalMs.size();
  std::vector<double> atArrival(n, 0.0), scales(n, 1.0);

  double total = 0.0, t = 0.0;
  int f = 0;
  double ema = -1.0;

  while (f < n || g.Active())
  {
    if (f < n && arrivalMs[f] / 1000.0 <= t + 1e-12)
    {
      const double gapSec = (f == 0) ? -1.0 : (arrivalMs[f] - arrivalMs[f - 1]) / 1000.0;
      double scale = model::EatScale(gapSec, kDPN);
      if (smooth)
      {
        const double a = 2.0 / (smoothN + 1.0);
        ema = (ema < 0.0) ? scale : (a * scale + (1.0 - a) * ema);
        scale = ema;
      }
      scales[f] = scale;
      P.eatRatePerMs = (kR * scale) / kDPU;
      P.spitMul = (scale <= 1e-9) ? kV : 1.0;
      g.Feed(kU, P);
      atArrival[f] = total;
      ++f;
    }
    total += g.Tick(dt, P) * kDPU;
    t += dt;
    if (t > 60.0)
      break;
  }

  printf("%s\n", title);
  printf("  %-6s %-10s %-8s %-12s %s\n", "notch", "gap ms", "scale", "travel", "");
  for (int i = 0; i < n; ++i)
  {
    const double gap = (i == 0) ? -1.0 : (arrivalMs[i] - arrivalMs[i - 1]);
    const double prev = (i == 0) ? 0.0 : atArrival[i - 1];
    printf("  %-6d %-10.0f %-8.3f %-12.2f\n", i + 1, gap, scales[i], atArrival[i] - prev);
  }
  printf("  tail: %.2f\n\n", total - atArrival[n - 1]);
}

int main()
{
  // A slow roll: 12 notches 200 ms apart, except #6 lies 90 ms after #5 (a natural jitter).
  std::vector<double> a;
  double t = 0;
  for (int i = 0; i < 12; ++i)
  {
    if (i == 5)
      t += 90.0; // the jittery step
    else if (i > 0)
      t += 200.0;
    a.push_back(t);
  }

  printf("roll: 12 notches ~200 ms apart, EXCEPT #6 only 90 ms after #5\n");
  printf("X=%.0f R=%.0f V=%.2f\n\n", kX, kR, kV);

  Run("POOL (current plugin)", a, false, 0);
  Run("SMOOTH (scale = EMA over ~3 messages)", a, true, 3);
  Run("SMOOTH (scale = EMA over ~5 messages)", a, true, 5);
  return 0;
}
