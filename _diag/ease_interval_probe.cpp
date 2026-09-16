// 量化轴的真正手感指标：两次"整量子跳"之间的【间隔】。
// 平均起伏小 ≠ 手感好；如果间隔忽长忽短（有空档再一簇），就读作"发沉/一顿一顿"。
//
//   g++ -std=c++17 -O2 -I../src -o iv.exe ease_interval_probe.cpp && ./iv.exe
#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace anim3;

struct Stat { double mean, sd, mx; int n; };

// 稳态滚，step 网格；统计相邻两次"发出整单位"之间的间隔
static Stat iv(double gapMs, double W, double ease, double grid)
{
  Glide g;
  Params P;
  P.windowMs = W;
  P.payoutEase = ease;
  g.Reset();
  const double dt = 0.0005, ms = 5000.0;
  double t = 0, next = 0, accum = 0, last = -1;
  std::vector<double> gaps;
  while (t < ms)
  {
    if (next <= t + 1e-12) { g.Feed(15.0, P); next += gapMs / 1000.0; }
    accum += g.Tick(dt, P);
    const double m = std::floor(std::fabs(accum) / grid + 0.5);
    if (m >= 1.0)
    {
      accum -= m * grid;
      if (last >= 0) gaps.push_back((t - last) * 1000.0);
      last = t;
    }
    t += dt;
  }
  const int skip = 40;
  double s = 0; int n = 0, mx = 0;
  for (int i = skip; i < (int)gaps.size(); ++i) { s += gaps[i]; ++n; if (gaps[i] > mx) mx = gaps[i]; }
  Stat st{0, 0, (double)mx, n};
  if (n > 0)
  {
    st.mean = s / n;
    double v = 0;
    for (int i = skip; i < (int)gaps.size(); ++i) v += (gaps[i] - st.mean) * (gaps[i] - st.mean);
    st.sd = std::sqrt(v / n);
  }
  return st;
}

int main()
{
  const double W = 200.0;
  printf("垂直缩放轴（整单位网格）：两次量子跳之间的间隔（ms）\n\n");
  printf("  %-7s | %-22s | %-22s\n", "每格", "ease0  平均/最大", "ease0.5 平均/最大");
  for (double g : {80, 100, 120, 140, 150, 160, 180, 200})
  {
    const Stat a = iv(g, W, 0.0, 1.0);
    const Stat b = iv(g, W, 0.5, 1.0);
    printf("  %-7.0f | 均%6.1f 最大%6.1f   | 均%6.1f 最大%6.1f   %s\n", g, a.mean, a.mx, b.mean, b.mx,
           (b.mx > a.mx * 1.15) ? "★最大间隔变长(会发沉)" : (b.mx < a.mx * 0.9 ? "最大间隔变短(更好)" : "同"));
  }
  return 0;
}
