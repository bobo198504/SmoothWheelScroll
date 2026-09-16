// 同样手速下，两种鼠标的【总量】是否一致？（第一格不是 Start 的连带后果）
#include "model.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

static const double RAMP = 600.0, START = 1.0, MUL = 1.0;

static double 跑(double msgDeltas, double speed, double ms)
{
  model::SpeedBudget b;
  b.Reset();
  const double gap = msgDeltas / speed; // ms per message
  double total = 0, t = 0;
  while (t < ms)
  {
    b.Add(msgDeltas, (t == 0.0) ? 0.0 : gap);
    total += model::Travel(msgDeltas, b.Value(), RAMP, START, MUL);
    t += gap;
  }
  return total;
}

int main()
{
  printf("同样手速（delta/ms）、同样 5 秒，两种鼠标滚出的总行程：\n");
  printf("Ramp-up=%.0f  Slow step=%.0f\n\n", RAMP, START);
  printf("  %-10s | %-9s %-9s %-9s | %s\n", "手速", "普通(120)", "无级(20)", "无级(15)", "最大/最小");
  for (double s : {0.2, 0.4, 0.8, 1.6, 3.2})
  {
    const double a = 跑(120.0, s, 5000.0);
    const double c = 跑(20.0, s, 5000.0);
    const double e = 跑(15.0, s, 5000.0);
    const double hi = std::fmax(a, std::fmax(c, e));
    const double lo = std::fmin(a, std::fmin(c, e));
    printf("  %-10.1f | %-9.0f %-9.0f %-9.0f | %.2fx  %s\n", s, a, c, e, hi / lo,
           hi / lo < 1.05 ? "平衡" : (hi / lo < 1.30 ? "尚可" : "★差得多"));
  }
  return 0;
}
