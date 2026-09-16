// Start=0.1 可行吗？在两种投递网格下实测。
//
// 新模型的慢轮行程 = Start（delta）。两个投递网格：
//   * stream 轴（水平）：网格 1/256 单位 = 1/32 delta  -> 0.1 delta 能直接发
//   * step   轴（主视图竖直）：网格 1 单位 = 8 delta   -> 0.1 delta 要攒 80 格
//
//   g++ -std=c++17 -O2 -I../src -o s01.exe start_small_probe.cpp && ./s01.exe
#include "model.h"
#include <cstdio>
#include <cmath>
#include <vector>

static const double kNotchUnits = 15.0, kDeltasPerNotch = 120.0;
static const double kDPU = kDeltasPerNotch / kNotchUnits; // 8 deltas/unit

// 慢轮固定间隔滚动，Start=小值；返回"每格发出去多少"+"多少格才发出一次"
static void 跑(const char *轴, double grid单位, double Start, int n, double gapMs)
{
  model::SpeedBudget bud;
  bud.Reset();
  double accum = 0, t = 0;
  int 空 = 0;
  std::vector<double> sends;
  for (int i = 0; i < n; ++i)
  {
    const double gap = (i == 0) ? 0.0 : gapMs;
    t += gap;
    const double msgD = 120.0;
    bud.Add(msgD, gap);
    // 慢轮：用很长间隔让预算基本不涨，逼近 Start
    const double travelD = model::Travel(msgD, (i == 0 ? 0.0 : 0.0), 1e9, Start);
    accum += travelD / kDPU; // deltas -> 单位
    const double m = floor(fabs(accum) / grid单位 + 0.5);
    if (m >= 1.0)
    {
      const double send = m * grid单位;
      accum -= send;
      sends.push_back(send);
    }
    else
    {
      sends.push_back(0.0);
      ++空;
    }
  }
  printf("  %-22s Start=%.2f: 前几格发出 ", 轴, Start);
  for (int i = 0; i < 12 && i < (int)sends.size(); ++i) printf("%.4f ", sends[i] * kDPU);
  printf("\n      %d 格里 %d 格【没发出】", n, 空);
  printf("  → %s\n", 空 > n / 2 ? "★大部分格都没动" : "基本每格都动");
}

int main()
{
  printf("慢轮（预算几乎不涨，逼近 Start）；单位=delta\n\n");
  const int n = 16;
  for (double s : {1.0, 0.5, 0.2, 0.1})
  {
    printf("Start=%.2f delta:\n", s);
    跑("水平(stream 1/32d)", 1.0 / 256.0, s, n, 300.0);
    跑("竖直(step 1 单位=8d)", 1.0, s, n, 300.0);
    printf("\n");
  }
  printf("结论：\n");
  printf("  * 水平轴网格是 1/32 delta，0.1 delta 也发得出 -> Start=0.1 成立且更细腻；\n");
  printf("  * 竖直轴网格是 8 delta，Start=0.1 时要【攒 80 格】才走 1 单位 -> 慢滚几乎不动。\n");
  return 0;
}
