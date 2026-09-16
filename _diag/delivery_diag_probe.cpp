// 投递层诊断：新模型的输出，经过"整单位网格"(kStepUnits) 会变成什么样？
//
// 主视图竖直轴（滚动 989/978、缩放 1000/1001）走的是 Delivery::kStepUnits，
// 网格 = 1/kVertStepsPerUnit = 1 个单位（kVertStepsPerUnit = 1.0）。
// 也就是说：**攒够 1 个单位（= 15 deltas）才发一次**。
//
// 新模型的慢轮行程很小（Start=1 delta = 0.125 单位），所以：
//   攒够 1 单位需要 8 个 slow 格 → 前 8 格画面不动，第 8 格突然跳 1 单位。
//
// 本探针用【真模型】跑一串滚轮，模拟这个网格，打印每次真正发出去了多少。
//
//   g++ -std=c++17 -O2 -I../src -o deliv.exe delivery_diag_probe.cpp && ./deliv.exe
#include "model.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <initializer_list>

static const double kDeltasPerNotch = 120.0;
static const double kNotchUnits = 15.0;
static const double kDPU = kDeltasPerNotch / kNotchUnits; // 8 deltas per unit

// 跑一段固定间隔的滚轮，走 kStepUnits 网格，返回每次真正发出的单位数
static std::vector<double> 跑网格(int n, double gapMs, double startD, double budgetD, double grid)
{
  model::SpeedBudget bud;
  bud.Reset();
  std::vector<double> sends;
  double accum = 0, t = 0;
  for (int i = 0; i < n; ++i)
  {
    const double gap = (i == 0) ? 0.0 : gapMs;
    t += gap;
    const double msgD = 120.0; // 普通鼠标
    bud.Add(msgD, gap);
    const double travelD = model::Travel(msgD, bud.Value(), budgetD, startD);
    accum += travelD / kDPU; // deltas -> units
    const double m = floor(fabs(accum) / grid + 0.5);
    if (m >= 1.0)
    {
      const double send = m * grid;
      accum -= send;
      sends.push_back(send);
    }
    else
    {
      sends.push_back(0.0); // 这一格什么都没发出去
    }
  }
  return sends;
}

static void 打印(const char *标题, int n, double gapMs, double startD, double grid)
{
  const std::vector<double> s = 跑网格(n, gapMs, startD, 600.0, grid);
  printf("%s\n  （每格 %0.fms，Start=%.0f delta，网格=%.3f 单位）\n  发出: ", 标题, gapMs, startD, grid);
  int zero = 0;
  for (double v : s)
  {
    printf("%.1f ", v);
    if (v == 0.0) ++zero;
  }
  printf("\n  → %d 格里有 %d 格【什么都没发】（画面没动）\n\n", (int)s.size(), zero);
}

int main()
{
  printf("主视图竖直轴 = 整单位网格（kVertStepsPerUnit = 1.0 单位 = 15 deltas）\n\n");

  printf("========== 慢轮：每格 300ms ==========\n");
  打印("普通鼠标(120/格)", 16, 300.0, 1.0, 1.0);
  printf("========== 中速：每格 150ms ==========\n");
  打印("普通鼠标(120/格)", 16, 150.0, 1.0, 1.0);
  printf("========== 快轮：每格 60ms ==========\n");
  打印("普通鼠标(120/格)", 16, 60.0, 1.0, 1.0);

  printf("========== 对照：如果网格是 1/8 单位（更细）==========\n");
  打印("慢轮 300ms", 16, 300.0, 1.0, 0.125);
  打印("中速 150ms", 16, 150.0, 1.0, 0.125);

  printf("读法：只要看到连续多个 0.0，就是【攒着不动】—— 画面发涩/一顿一顿。\n");
  return 0;
}
