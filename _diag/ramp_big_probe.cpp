// Ramp-up 拉大（超过 1000）会怎样？两件事：第一格、以及慢速端的设备平衡。
//
//   第一格 = Start + (本格delta - Start) × (本格delta / Ramp-up)
//   Ramp-up 越大 -> u 越小 -> 第一格越接近 Start
//
//   g++ -std=c++17 -O2 -I../src -o ramp.exe ramp_big_probe.cpp && ./ramp.exe
#include "model.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

static const double START = 1.0, MUL = 1.0;

static double 第一格(double msgD, double ramp)
{
  model::SpeedBudget b;
  b.Reset();
  const double bud = b.Add(msgD, 0.0);
  return model::Travel(msgD, bud, ramp, START, MUL);
}

// 同样手速跑 ms 毫秒的总额
static double 总额(double msgD, double speed, double ramp, double ms)
{
  model::SpeedBudget b;
  b.Reset();
  const double gap = msgD / speed;
  double total = 0, t = 0;
  while (t < ms)
  {
    b.Add(msgD, (t == 0.0) ? 0.0 : gap);
    total += model::Travel(msgD, b.Value(), ramp, START, MUL);
    t += gap;
  }
  return total;
}

int main()
{
  printf("Slow step=%.0f\n\n", START);

  printf("== 一、第一格（普通 120 / 无级 15）==\n");
  printf("  %-10s %-12s %-12s %s\n", "Ramp-up", "普通第一格", "无级第一格", "还差多少到 Start");
  for (double ramp : {120.0, 600.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0})
  {
    const double a = 第一格(120.0, ramp), c = 第一格(15.0, ramp);
    printf("  %-10.0f %-12.2f %-12.2f 普通 %.2f / 无级 %.2f\n", ramp, a, c, a - START, c - START);
  }

  printf("\n== 二、慢速端的设备平衡（同样手速，5 秒总额）==\n");
  printf("  %-10s | %-22s | %-22s | %s\n", "Ramp-up", "手速 0.2 普通/无级", "手速 0.5 普通/无级", "最大差");
  for (double ramp : {120.0, 600.0, 1000.0, 2000.0, 4000.0, 8000.0})
  {
    const double a1 = 总额(120.0, 0.2, ramp, 5000.0), c1 = 总额(15.0, 0.2, ramp, 5000.0);
    const double a2 = 总额(120.0, 0.5, ramp, 5000.0), c2 = 总额(15.0, 0.5, ramp, 5000.0);
    const double r1 = std::fmax(a1, c1) / std::fmin(a1, c1);
    const double r2 = std::fmax(a2, c2) / std::fmin(a2, c2);
    printf("  %-10.0f | %-6.0f / %-6.0f %-7.2fx | %-6.0f / %-6.0f %-7.2fx | %.2fx\n", ramp, a1, c1, r1,
           a2, c2, r2, std::fmax(r1, r2));
  }

  printf("\n== 三、爬升要多久到满速（普通鼠标，每条 120，600/条 的手速）==\n");
  printf("  %-10s %s\n", "Ramp-up", "到达满速所需的 delta 累计（= Ramp-up 本身，除以手速即时间）");
  for (double ramp : {600.0, 1000.0, 2000.0, 4000.0})
  {
    // 以 0.6 delta/ms（约 5 条/秒）计
    const double ms = ramp / 0.6;
    printf("  %-10.0f 累计 %.0f delta  -> 约 %.2f 秒\n", ramp, ramp, ms / 1000.0);
  }
  return 0;
}
