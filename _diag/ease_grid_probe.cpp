// 缓动对两种【投递粒度】的效果：stream(1/256单位) vs step(1单位)。
//
// 假设：缓动只对【细流】轴有好处——接收方看得见亚单位形状，形状平滑就是平滑。
// 对【整单位】轴，接收方只按整量子跳，缓动只是把"什么时候跨过一个量子"改了时间，
// 可能反而让整量子跳变得更不均匀 → 读作"发沉/顿"。
// 垂直缩放正是【整单位】轴。
//
//   g++ -std=c++17 -O2 -I../src -o grid.exe ease_grid_probe.cpp && ./grid.exe
#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace anim3;

// 稳态滚一段，按 grid 累积投递，返回每 10ms 【实际投递出去的单位数】的波动%
static double ripple(double gapMs, double W, double ease, double grid, double *sentPerNotch)
{
  Glide g;
  Params P;
  P.windowMs = W;
  g.Reset();
  const double dt = 0.001, ms = 5000.0;
  std::vector<double> bins;
  double t = 0, next = 0, acc = 0, bt = 0, accum = 0, sent = 0;
  int f = 0;
  while (t < ms)
  {
    if (next <= t + 1e-12)
    {
      P.payoutEase = ease;
      g.Feed(15.0, P); // 一格 = 15 七位单位
      next += gapMs / 1000.0;
      ++f;
    }
    accum += g.Tick(dt, P); // 单位
    const double m = std::floor(std::fabs(accum) / grid + 0.5);
    if (m >= 1.0) { const double s = m * grid; accum -= s; sent += s; }
    bt += dt * 1000.0;
    if (bt >= 10.0 - 1e-9) { bins.push_back(sent); sent = 0; bt = 0; }
    t += dt;
  }
  if (sentPerNotch) *sentPerNotch = sent;
  const int skip = 40;
  double sum = 0; int n = 0;
  for (int i = skip; i < (int)bins.size(); ++i) { sum += bins[i]; ++n; }
  const double mean = sum / n;
  double v = 0;
  for (int i = skip; i < (int)bins.size(); ++i) v += (bins[i] - mean) * (bins[i] - mean);
  return mean > 0 ? 100 * std::sqrt(v / n) / mean : 0;
}

int main()
{
  const double W = 200.0;
  const double gaps[] = {80, 100, 120, 140, 150, 160, 180, 200};
  printf("窗口=%.0fms；投递量的波动%%（越小越平）\n\n", W);
  printf("== 细流轴 stream 网格 1/256 单位 ==\n");
  printf("  %-7s %-10s %-10s %s\n", "每格", "ease0", "ease0.5", "判定");
  for (double g : gaps)
  {
    const double a = ripple(g, W, 0.0, 1.0 / 256.0, nullptr);
    const double b = ripple(g, W, 0.5, 1.0 / 256.0, nullptr);
    printf("  %-7.0f %-10.1f %-10.1f %s\n", g, a, b, b < a - 0.5 ? "好" : (b > a + 0.5 ? "★差" : "同"));
  }
  printf("\n== 整单位轴 step 网格 1 单位（= 垂直缩放/竖直滚动）==\n");
  printf("  %-7s %-10s %-10s %s\n", "每格", "ease0", "ease0.5", "判定");
  for (double g : gaps)
  {
    const double a = ripple(g, W, 0.0, 1.0, nullptr);
    const double b = ripple(g, W, 0.5, 1.0, nullptr);
    printf("  %-7.0f %-10.1f %-10.1f %s\n", g, a, b, b < a - 0.5 ? "好" : (b > a + 0.5 ? "★差" : "同"));
  }
  return 0;
}
