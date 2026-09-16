// 具体场景对比：同样"手转了多少"，两种鼠标各走多远。
//
// 场景：手在一秒内**物理滚了 480 delta**（= 4 个普通格），方向相同。
//   * 普通鼠标：一条消息 120 delta  → 发 4 条
//   * 无级鼠标：一条消息  15 delta  → 发 32 条
// **两者代表的是同一段物理滚动**，理想情况下画面应该走一样远。
//
// 模型（你的方案）：单次 = X(5)，每"格"线性涨，封顶 = 这条消息自己的 delta。
// 只有"涨"的驱动量不同：
//   方案A：按【消息条数】涨（你的想法）
//   方案C：按【累计 delta】涨（每 600 delta 涨满）
//
//   g++ -std=c++17 -O2 -o combo2.exe combo_scenario_probe.cpp && ./combo2.exe
#include <cstdio>
#include <cmath>

static const double X = 5.0;        // 单次（第 1 格）行程，绝对 delta
static const double BUDGET = 600.0; // 方案C：累计 600 delta 涨到封顶

// 走一段：返回总行程，并打印每条的明细
static double 明细(const char *name, double msgDeltas, double totalDeltas, int scheme, bool print)
{
  const int n = (int)std::lround(totalDeltas / msgDeltas);
  double accum = 0, out = 0, prev = X;
  if (print)
    printf("  %s（每条 %.0f delta，共 %d 条）\n    %-6s %-10s %-10s %-12s %s\n", name, msgDeltas, n,
           "第几条", "累计delta", "本格上限", "本格行程", "说明");
  for (int i = 1; i <= n; ++i)
  {
    accum += msgDeltas;
    double cap, travel, driver;
    if (scheme == 0) // A: 按消息条数
    {
      driver = (double)i;
      travel = fmin(X + (driver - 1.0) * 2.0, msgDeltas);
    }
    else // C: 按累计 delta
    {
      const double u = fmin(accum / BUDGET, 1.0);
      travel = X + (msgDeltas - X) * u;
    }
    cap = msgDeltas;
    out += travel;
    if (print && (i <= 4 || i == n || travel >= cap - 1e-9))
      printf("    %-6d %-10.0f %-10.0f %-12.2f %s\n", i, accum, cap, travel,
             (travel >= cap - 1e-9) ? "★到顶" : "");
    if (print && i == 5 && n > 6)
      printf("    ...（中间略）\n");
    (void)prev;
  }
  if (print)
    printf("    %s总行程 = %.1f delta\n\n", "", out);
  return out;
}

int main()
{
  printf("场景：手在 1 秒内**物理滚了 480 delta**\n");
  printf("      普通鼠标发 4 条（每条 120）；无级鼠标发 32 条（每条 15）。\n");
  printf("      同一段物理滚动，若模型设备无关，两者画面前进应接近。\n\n");

  printf("========== 方案 A：按【消息条数】涨 ==========\n");
  const double aN = 明细("普通鼠标", 120.0, 480.0, 0, true);
  const double aF = 明细("无级鼠标", 15.0, 480.0, 0, true);
  printf("  ★ 结果：普通走 %.0f，无级走 %.0f  → 无级是普通的 %.1f 倍\n", aN, aF, aF / aN);
  printf("  （问题：普通只发了 4 条，连击数只到 4，还没加速就结束了；\n");
  printf("    无级发了 32 条，连击数早早涨到顶。同样一段手滚，差 %.0f 倍。）\n\n", aF / aN);

  printf("========== 方案 C：按【累计 delta】涨（600 delta 涨满）==========\n");
  const double cN = 明细("普通鼠标", 120.0, 480.0, 1, true);
  const double cF = 明细("无级鼠标", 15.0, 480.0, 1, true);
  printf("  ★ 结果：普通走 %.0f，无级走 %.0f  → 无级是普通的 %.2f 倍\n", cN, cF, cF / cN);
  printf("  （两者接近 1 倍 = 平衡。因为累积的 delta 是物理量，两者实际转的量一样。）\n\n");

  return 0;
}
