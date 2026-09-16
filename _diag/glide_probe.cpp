// Glide length（窗口）取不同值时，慢轮的实际输出长什么样？
//
// 当前模型 = 窗口时序（anim3_core）+ Travel/SpeedBudget 决定距离（model.h）。
// "每格的量摊在 windowMs 里" —— 所以 windowMs 决定"两格之间有没有空档"。
// 定时器 = 15.6ms（kFastTimer=false 之后），窗口就按 15.6ms 一帧采样。
//
//   g++ -std=c++17 -O2 -I../src -o glide.exe glide_probe.cpp && ./glide.exe
#include "model.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <initializer_list>

static const double kDPU = 8.0;      // deltas per unit
static const double kTickMs = 15.6;  // 实际定时器周期

// 慢滚：每格 gapMs，普通鼠标 120/条；按窗口摊开，按 kTickMs 采样，返回每 20ms 的输出(deltas)
static void 剖面(double windowMs, double gapMs, double start, double ramp, int n)
{
  model::Axis g;
  g.Reset();
  model::Params P;
  P.windowMs = windowMs;
  model::SpeedBudget bud;
  bud.Reset();

  const double dt = kTickMs / 1000.0;
  double t = 0, next = 0, accum = 0;
  int f = 0;
  std::vector<double> bins;
  const double binMs = 20.0;
  double bacc = 0, bt = 0;

  const double endT = n * gapMs / 1000.0 + windowMs / 1000.0 + 0.05;
  while (t < endT)
  {
    if (f < n && next <= t + 1e-12)
    {
      const double gap = (f == 0) ? 0.0 : gapMs;
      bud.Add(120.0, gap);
      const double travelD = model::Travel(120.0, bud.Value(), ramp, start, 1.0);
      g.Feed(travelD / kDPU, P); // 单位
      ++f;
      next += gapMs / 1000.0;
    }
    const double o = g.Tick(dt, P) * kDPU;
    accum += o;
    bacc += o;
    bt += kTickMs;
    if (bt >= binMs - 1e-9) { bins.push_back(bacc); bacc = 0; bt = 0; }
    t += dt;
  }
  // 打印每 20ms 一段
  printf("  window=%-4.0fms  每 20ms 输出: ", windowMs);
  int zero = 0;
  for (int i = 0; i < 18 && i < (int)bins.size(); ++i)
  {
    printf("%4.1f ", bins[i]);
    if (bins[i] < 1e-6) ++zero;
  }
  printf("\n                 前 18 段里【空档】%d 段；总行程 %.1f deltas\n", zero, accum);
}

int main()
{
  printf("慢滚：每格 250ms（间隔 > 窗口）；Slow step=1, Ramp-up=600；定时器 %.1fms\n\n", kTickMs);
  for (double w : {100.0, 150.0, 200.0, 300.0})
    剖面(w, 250.0, 1.0, 600.0, 5);

  printf("\n更慢：每格 400ms\n\n");
  for (double w : {100.0, 150.0, 200.0, 300.0})
    剖面(w, 400.0, 1.0, 600.0, 5);

  printf("\n说明：window 越大 -> 每格的量摊得越久 -> 越容易【接上下一格】-> 空档越少、越连续。\n");
  printf("      总行程与 window 无关（守恒）。\n");
  return 0;
}
