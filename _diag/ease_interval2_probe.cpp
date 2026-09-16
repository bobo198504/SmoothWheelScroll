// 用【真实 PayoutEaseFor】扫：窗口(Glide) × 每格间隔，在整单位轴上的"最大间隔"(发沉指标)。
//
//   g++ -std=c++17 -O2 -I../src -o iv2.exe ease_interval2_probe.cpp && ./iv2.exe
#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace anim3;

static double maxGap(double gapMs, double W, double grid, bool useEase)
{
  Glide g;
  Params P;
  P.windowMs = W;
  g.Reset();
  const double dt = 0.0005, ms = 5000.0;
  double t = 0, next = 0, accum = 0, last = -1, mx = 0;
  int f = 0;
  while (t < ms)
  {
    if (next <= t + 1e-12)
    {
      const double realGap = (f == 0) ? 0.0 : gapMs;
      P.payoutEase = useEase ? PayoutEaseFor(realGap, W) : 0.0;
      g.Feed(15.0, P);
      next += gapMs / 1000.0;
      ++f;
    }
    accum += g.Tick(dt, P);
    const double m = std::floor(std::fabs(accum) / grid + 0.5);
    if (m >= 1.0)
    {
      accum -= m * grid;
      if (last >= 0 && t * 1000.0 > 400.0)
      {
        const double d = (t - last) * 1000.0;
        if (d > mx) mx = d;
      }
      last = t;
    }
    t += dt;
  }
  return mx;
}

int main()
{
  printf("整单位轴(垂直缩放/竖直滚动)：两次量子跳的【最大间隔】(ms)。越小越不发沉。\n\n");
  for (double W : {150.0, 200.0, 300.0})
  {
    printf("== 窗口 Glide = %.0fms ==\n", W);
    printf("  %-8s %-12s %-12s %s\n", "每格", "ease0", "ease(真判据)", "判定");
    for (double g : {60, 80, 100, 120, 140, 150, 160, 180, 200, 250, 300})
    {
      const double a = maxGap(g, W, 1.0, false);
      const double b = maxGap(g, W, 1.0, true);
      printf("  %-8.0f %-12.1f %-12.1f %s\n", g, a, b,
             (b > a + 1.0) ? "★变沉" : (b < a - 1.0 ? "变好" : "同"));
    }
    printf("\n");
  }
  return 0;
}
