// 扫"离最近整数多远"(d) 从 0.01 到 0.25，对 n=1,2,3（=floor(W/G)）分别看缓动的效果。
// 目标：找出"d 小于多少时缓动反而添乱"。
#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace anim3;

static double sd(double gapMs, double W, bool ease)
{
  Glide g; Params P; P.windowMs = W; g.Reset();
  const double dt = 0.0005, ms = 6000.0;
  double t = 0, next = 0, accum = 0, last = -1; std::vector<double> gs; int f = 0;
  while (t < ms)
  {
    if (next <= t + 1e-12)
    { P.payoutEase = ease ? PayoutEaseFor(f == 0 ? 0.0 : gapMs, W) : 0.0;
      g.Feed(15.0, P); next += gapMs / 1000.0; ++f; }
    accum += g.Tick(dt, P);
    const double m = std::floor(std::fabs(accum) + 0.5);
    if (m >= 1.0) { accum -= m; if (last >= 0 && t > 0.5) gs.push_back((t - last) * 1000.0); last = t; }
    t += dt;
  }
  if (gs.empty()) return 0;
  double mu = 0; for (double x : gs) mu += x; mu /= gs.size();
  double v = 0; for (double x : gs) v += (x - mu) * (x - mu);
  return std::sqrt(v / gs.size());
}

int main()
{
  const double W = 200.0;
  printf("窗口=%.0fms；整单位轴；量间隔标准差。d = |W/G - 最近整数|\n\n", W);
  for (int n = 1; n <= 3; ++n)
  {
    printf("== n=%d（%.0f~%.0fms 每格）==\n", n, W / (n + 1), W / n);
    printf("  %-7s %-9s %-9s %s\n", "d", "ease0", "ease0.5", "判定");
    for (double d : {0.02, 0.04, 0.05, 0.06, 0.08, 0.10, 0.12, 0.15, 0.20, 0.25})
    {
      const double ratio = (d < 0.5) ? (n + d) : (n + 1 - d);
      const double gap = W / ratio;
      const double a = sd(gap, W, false), b = sd(gap, W, true);
      printf("  %-7.2f %-9.2f %-9.2f %s\n", d, a, b,
             b > a * 1.05 ? "★缓动更差" : (b < a * 0.95 ? "缓动更好" : "接近"));
    }
    printf("\n");
  }
  return 0;
}
