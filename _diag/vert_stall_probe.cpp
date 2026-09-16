// 垂直缩放（整单位投递）的【发送时间线】：慢滚 + 小行程时，是不是"突然一簇 + 然后长时间不发"？
//
// 复刻插件路径：SpeedBudget → Travel → Glide(带缓动) → kStepUnits 网格(1 单位，四舍五入)
// 打印：每次【真的发出 1 单位】的时刻，以及"最长不发间隔"。
//
//   g++ -std=c++17 -O2 -I../src -o diag.exe vert_stall_probe.cpp && ./diag.exe
#include "model.h"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace model;

// 一段稳定慢滚：每 gapMs 一条 120 delta 的消息（普通鼠标）
struct Res { double maxGapMs; int sends; double firstBurstMs; };

static Res run(double gapMs, double ramp, double start, double mul, double W, bool ease, bool verbose)
{
  SpeedBudget bud;
  bud.Reset();
  Axis g;
  g.Reset();
  Params P;
  P.windowMs = W;

  const double dt = 0.001, ms = 4000.0;
  double t = 0, next = 0, accum = 0, lastSend = -1, maxGap = 0;
  int sends = 0, f = 0;
  std::vector<double> sendTimes;

  while (t < ms)
  {
    if (next <= t + 1e-12)
    {
      const double gap = (f == 0) ? 0.0 : gapMs;
      bud.Add(120.0, gap);
      const double travel = Travel(120.0, bud.Value(), ramp, start, mul);
      P.payoutEase = ease ? PayoutEaseFor(gap, W) : 0.0;
      g.Feed(travel / 8.0, P); // deltas -> 7bit units
      next += gapMs / 1000.0;
      ++f;
    }
    accum += g.Tick(dt, P);
    const double m = std::floor(std::fabs(accum) / 1.0 + 0.5); // 网格 = 1 单位
    if (m >= 1.0)
    {
      accum -= m;
      ++sends;
      sendTimes.push_back(t * 1000.0);
      if (lastSend >= 0)
      {
        const double d = (t - lastSend) * 1000.0;
        if (d > maxGap) maxGap = d;
      }
      lastSend = t;
    }
    t += dt;
  }
  if (verbose)
  {
    printf("  发出时刻(ms): ");
    for (size_t i = 0; i < sendTimes.size() && i < 60; ++i) printf("%.0f ", sendTimes[i]);
    printf("...\n");
  }
  Res r;
  r.maxGapMs = maxGap;
  r.sends = sends;
  r.firstBurstMs = 0;
  return r;
}

int main()
{
  const double start = 1.0, mul = 1.0;
  printf("垂直缩放（整单位网格 1 单位 = 8 delta）；稳定慢滚\n\n");
  printf("  %-8s %-8s %-8s | %-10s %-10s %s\n", "每格ms", "Ramp-up", "缓动", "发送次数", "最长间隔ms",
         "判定");
  for (double gap : {200.0, 150.0, 120.0, 100.0})
    for (double ramp : {600.0, 60.0})
      for (int ez = 0; ez < 2; ++ez)
      {
        const Res r = run(gap, ramp, start, mul, 200.0, ez != 0, false);
        printf("  %-8.0f %-8.0f %-8s | %-10d %-10.1f %s\n", gap, ramp, ez ? "on" : "off", r.sends,
               r.maxGapMs, (r.maxGapMs > 60.0) ? "★卡住(长时间不发)" : "");
      }

  printf("\n== 时间线（每格 150ms，Ramp-up=600）==\n");
  printf(" 缓动 off:"); run(150.0, 600.0, start, mul, 200.0, false, true);
  printf(" 缓动 on :"); run(150.0, 600.0, start, mul, 200.0, true, true);
  printf("\n== 时间线（每格 150ms，Ramp-up=60）==\n");
  printf(" 缓动 off:"); run(150.0, 60.0, start, mul, 200.0, false, true);
  return 0;
}
