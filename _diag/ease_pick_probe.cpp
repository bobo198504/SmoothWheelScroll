// 缓动策略对比（细扫中慢速）：窗口 200ms。
// 目标：找出"中慢速(重叠区)更平"的阈值与强度，同时不伤整数倍那两档。
//
//   g++ -std=c++17 -O2 -I../src -o pick.exe ease_pick_probe.cpp && ./pick.exe
#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace anim3;

// 策略: ease 上限 + 整数倍排除阈值
static double easeFor(double gap, double W, double amt, double thr)
{
  if (!(gap > 0.0) || gap >= W) return 0.0;
  const double r = W / gap;
  const double d = std::fabs(r - std::floor(r + 0.5));
  return (d < thr) ? 0.0 : amt;
}

static double ripple(double gap, double W, double amt, double thr)
{
  Glide g; Params P; P.windowMs = W; g.Reset();
  const double dt = 0.001, ms = 4000.0;
  std::vector<double> bins; double t = 0, next = 0, acc = 0, bt = 0;
  while (t < ms)
  {
    if (next <= t + 1e-12) { P.payoutEase = easeFor(gap, W, amt, thr); g.Feed(15.0, P);
      next += gap / 1000.0; }
    acc += g.Tick(dt, P); bt += dt * 1000.0;
    if (bt >= 0.01 - 1e-9) { bins.push_back(acc); acc = 0; bt = 0; }
    t += dt;
  }
  const int skip = 30; double sum = 0; int n = 0;
  for (int i = skip; i < (int)bins.size(); ++i) { sum += bins[i]; ++n; }
  const double mean = sum / n; double v = 0;
  for (int i = skip; i < (int)bins.size(); ++i) v += (bins[i] - mean) * (bins[i] - mean);
  return mean > 0 ? 100 * std::sqrt(v / n) / mean : 0;
}

struct Pol { const char *n; double amt, thr; };

int main()
{
  const double W = 200.0;
  const Pol pols[] = {{"none", 0, 0},   {"a25t15", 0.25, 0.15}, {"a25t05", 0.25, 0.05},
                      {"a40t05", 0.4, 0.05}, {"a50t05", 0.5, 0.05}, {"a40t02", 0.4, 0.02},
                      {"a60t05", 0.6, 0.05}};
  const int NP = 7;
  printf("窗口=%.0fms；波动%%（越小越平）；间隔从 80(快) 到 190(慢)\n\n", W);
  printf("  %-7s", "每格");
  for (int p = 0; p < NP; ++p) printf(" %-8s", pols[p].n);
  printf("  W/G\n");
  const double gaps[] = {80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190, 200};
  double worst[NP] = {0};
  double worstSlow[NP] = {0}; // 只看 >=120ms（中慢速）
  for (double g : gaps)
  {
    printf("  %-7.0f", g);
    for (int p = 0; p < NP; ++p)
    {
      const double r = ripple(g, W, pols[p].amt, pols[p].thr);
      printf(" %-8.1f", r);
      if (r > worst[p]) worst[p] = r;
      if (g >= 120.0 && r > worstSlow[p]) worstSlow[p] = r;
    }
    printf("  %.2f\n", W / g);
  }
  printf("  %-7s", "最差");
  for (int p = 0; p < NP; ++p) printf(" %-8.1f", worst[p]);
  printf("\n  %-7s", "中慢最差");
  for (int p = 0; p < NP; ++p) printf(" %-8.1f", worstSlow[p]);
  printf("\n\n读法：看有没有一列【中慢最差】明显低于 none，且 100/200 两档仍≈0。\n");
  return 0;
}
