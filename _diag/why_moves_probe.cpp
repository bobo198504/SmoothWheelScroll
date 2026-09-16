// 为什么 Start=1 在竖直轴也会动？—— 用【真模型】算实际行程，不用手算。
//
// 关键：SpeedBudget 每条消息都加上【这条消息自己的 delta】，所以
//   u = budget / budgetDeltas  有一个【下限】：一条消息的 delta / budgetDeltas
//   普通鼠标：120/600 = 0.20
//   无级鼠标： 15/600 = 0.025
// 于是实际行程 = Start + (cap - Start) * u  ≈ Start + cap*0.2，**Start 只占很小一块**。
//
//   g++ -std=c++17 -O2 -I../src -o why.exe why_moves_probe.cpp && ./why.exe
#include "model.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

static const double kDPU = 120.0 / 15.0; // 8 deltas per unit
static const double BUD = 600.0;

// 慢滚：固定间隔 gapMs，普通鼠标 120/条
static void 一条(double Start, double gapMs)
{
  model::SpeedBudget b;
  b.Reset();
  b.Add(120.0, 0.0); // 第一条
  const double bud = b.Value();
  const double travel = model::Travel(120.0, bud, BUD, Start);
  printf("  间隔 %4.0fms  预算=%6.1f  u=%.3f  行程=%6.2f delta (=%.2f 单位)  %s\n", gapMs, bud,
         bud / BUD, travel, travel / kDPU,
         travel / kDPU >= 1.0 ? "→ 竖直轴一次就动 1 单位 ✓" : "→ 竖直轴要攒");
}

int main()
{
  printf("普通鼠标（每条 120 delta），上下限由真模型算出\n\n");
  for (double Start : {1.0, 0.1})
  {
    printf("== Slow step = %.1f ==\n", Start);
    for (double gap : {400.0, 300.0, 240.0, 120.0, 60.0})
      一条(Start, gap);
    printf("\n");
  }

  printf("对照：u 的下限（一条消息就到的预算）\n");
  printf("  普通鼠标 120/条：u = 120/600 = %.3f  → 行程至少 = Start + (120-Start)*0.2\n", 120.0 / BUD);
  printf("  无级鼠标  15/条：u =  15/600 = %.3f  → 行程至少 = Start + ( 15-Start)*0.025\n", 15.0 / BUD);
  printf("\n结论：Start=1 之所以竖直轴也动，是因为【预算项】占了 ~20%%（普通鼠标），\n");
  printf("      不是在拿 Start 那 1 个 delta 去攒。Start 只影响很小一截。\n");
  return 0;
}
