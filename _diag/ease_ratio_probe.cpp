// 细扫 ratio=W/G 在 1.0~1.6（只 1~2 个窗口重叠）区间：缓动到底是帮还是添乱？
// 指标：最大量子间隔(ms) 与 "间隔的标准差"。
#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace anim3;

struct R { double mx, sd, mean; };

static R iv(double gapMs, double W, double grid, bool useEase)
{
  Glide g; Params P; P.windowMs = W; g.Reset();
  const double dt = 0.0005, ms = 6000.0;
  double t = 0, next = 0, accum = 0, last = -1;
  std::vector<double> gs; int f = 0;
  while (t < ms)
  {
    if (next <= t + 1e-12)
    {
      P.payoutEase = useEase ? PayoutEaseFor(f == 0 ? 0.0 : gapMs, W) : 0.0;
      g.Feed(15.0, P); next += gapMs / 1000.0; ++f;
    }
    accum += g.Tick(dt, P);
    const double m = std::floor(std::fabs(accum) / grid + 0.5);
    if (m >= 1.0) { accum -= m * grid; if (last >= 0 && t > 0.5) gs.push_back((t - last) * 1000.0);
      last = t; }
    t += dt;
  }
  R r{0, 0, 0};
  for (double x : gs) { r.mean += x; if (x > r.mx) r.mx = x; }
  if (!gs.empty())
  {
    r.mean /= gs.size();
    double v = 0; for (double x : gs) v += (x - r.mean) * (x - r.mean);
    r.sd = std::sqrt(v / gs.size());
  }
  return r;
}

int main()
{
  printf("只 1~2 个窗口重叠的区间（ratio = 窗口/每格）\n\n");
  printf("  %-6s %-7s | %-16s | %-16s | %s\n", "W", "每格", "ease0 最大/标准差", "ease 最大/标准差", "判定");
  for (double W : {150.0, 200.0})
    for (double ratio : {1.05, 1.10, 1.15, 1.20, 1.25, 1.30, 1.40, 1.50, 1.60})
    {
      const double gap = W / ratio;
      const R a = iv(gap, W, 1.0, false);
      const R b = iv(gap, W, 1.0, true);
      printf("  %-6.0f %-7.1f | %6.1f %6.2f    | %6.1f %6.2f    | %s\n", W, gap, a.mx, a.sd, b.mx, b.sd,
             (b.sd > a.sd * 1.10) ? "★缓动更差" : (b.sd < a.sd * 0.9 ? "缓动更好" : "接近"));
    }
  return 0;
}
