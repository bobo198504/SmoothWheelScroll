// 3.0 窗口模型的结构诊断：慢滚时输出到底是连续还是"脉冲+空档"？
//
// 3.0 的核心：**每一个格子在 W 毫秒内摊完**（W = 精度值，默认 150ms）。
// 慢滚时，两格的间隔 T 可能大于 W → 前一个窗口已经走完、下一个还没来 → **空档**。
// 于是屏幕是"冲一下 → 停 → 冲一下 → 停"，而不是连续移动。
//
// 本探针按 20ms 一格打印输出量，直接看是不是脉冲。
//
//   g++ -std=c++17 -O2 -I../src -o win.exe window_fail_probe.cpp && ./win.exe
#include "model.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <initializer_list>

static const double kU = 15.0, kDPU = 8.0;

// 以固定周期 T 滚 n 格，W 为窗口；返回每 20ms 的输出（deltas）
static std::vector<double> 剖面(int n, double T毫秒, double W毫秒, double slowMove)
{
  model::Axis g;
  g.Reset();
  model::Params P;
  P.windowMs = W毫秒;

  const double dt = 0.0005;
  double t = 0;
  int f = 0;
  const double gap = T毫秒 / 1000.0;
  double next = 0;
  std::vector<double> bins;
  const double binMs = 20.0;
  double acc = 0, bt = 0;
  bool hasLast = false;

  const double endT = n * gap + W毫秒 / 1000.0 + 0.02;
  while (t < endT)
  {
    if (f < n && next <= t + 1e-12)
    {
      const double gapSec = hasLast ? gap : -1.0;
      hasLast = true;
      const double scale = model::TravelScale(gapSec, 120.0, slowMove);
      g.Feed(kU * scale, P);
      ++f;
      next += gap;
    }
    const double o = g.Tick(dt, P) * kDPU;
    acc += o;
    bt += dt * 1000.0;
    if (bt >= binMs - 1e-9) { bins.push_back(acc); acc = 0; bt = 0; }
    t += dt;
  }
  return bins;
}

static void 打印(const char *标题, int n, double T, double W, double slowMove)
{
  const std::vector<double> b = 剖面(n, T, W, slowMove);
  printf("%s（每格 %.0fms，窗口 %.0fms）\n  ", 标题, T, W);
  int nz = 0, gaps = 0;
  for (int i = 0; i < (int)b.size() && i < 30; ++i)
  {
    printf("%4.1f", b[i]);
    if (b[i] > 1e-6) ++nz; else ++gaps;
  }
  printf("\n  有输出的 20ms 段 %d 个，空档 %d 个 → %s\n\n", nz, gaps,
         gaps > 0 ? "★脉冲（会一顿一顿）" : "连续");
}

int main()
{
  const double W = 150.0; // 默认精度
  const double sm = 30.0; // 慢轮走 30 delta
  printf("3.0 窗口模型：一个格子在 W=%.0fms 内摊完\n\n", W);

  打印("慢滚（每格 300ms，间隔 > 窗口）", 6, 300.0, W, sm);
  打印("慢滚（每格 200ms，间隔 > 窗口）", 6, 200.0, W, sm);
  打印("中速（每格 150ms，间隔 = 窗口）", 6, 150.0, W, sm);
  打印("较快（每格 100ms，间隔 < 窗口）", 8, 100.0, W, sm);
  打印("快滚（每格 50ms，窗口重叠）", 10, 50.0, W, sm);

  printf("结论：\n");
  printf("  * 只要【每格间隔 > 窗口】，输出就是【冲一下 + 空档】，屏幕一顿一顿；\n");
  printf("  * 只有【间隔 < 窗口】时窗口才重叠、才连续。\n");
  printf("  * 而慢滚恰恰是间隔大 → 结构上必然一顿一顿。这就是 3.0 的死结。\n");
  return 0;
}
