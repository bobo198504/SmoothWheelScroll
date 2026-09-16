// 三种"连击/加速"驱动方式对比：无级（15/条）vs 普通（120/条），同样手速下输出多少。
//
//   A. 连击按【消息条数】：每收一条 +1
//   B. 连击按【累计 delta/15】：每累计 15 delta +1
//   C. 行程按【累计 delta】线性涨：travel = X + (cap-X)*min(cumDelta/BUDGET, 1)
//
// 关键指标：同样"手速"（delta/ms）下，两种鼠标的（输出/输入）是否接近。
//
//   g++ -std=c++17 -O2 -o combo.exe combo_probe.cpp && ./combo.exe
#include <cstdio>
#include <cmath>
#include <initializer_list>

static const double X = 5.0;      // 单次行程（绝对 delta）
static const double A_STEP = 2.0; // A/B 方案每格增量
static const double BUDGET = 600.0; // C 方案：累计 600 delta 涨到顶（= 5 个普通格）

struct Dev { double out, inTot, ratio; };

static Dev 跑(int scheme, double msgDeltas, double sp)
{
  const double gapMs = msgDeltas / sp;
  const int n = (int)(1000.0 / gapMs);
  Dev r{0, 0, 0};
  double accum = 0;
  for (int i = 1; i <= n; ++i)
  {
    accum += msgDeltas;
    r.inTot += msgDeltas;
    double travel;
    if (scheme == 0)
      travel = fmin(X + (i - 1.0) * A_STEP, msgDeltas);
    else if (scheme == 1)
      travel = fmin(X + (accum / 15.0 - 1.0) * A_STEP, msgDeltas);
    else
      travel = X + (msgDeltas - X) * fmin(accum / BUDGET, 1.0);
    r.out += travel;
  }
  r.ratio = r.inTot > 0 ? r.out / r.inTot : 0;
  return r;
}

static void 扫描(const char *标题, int scheme)
{
  printf("== %s ==\n", 标题);
  printf("  %-7s | %-8s %-8s %-6s | %-8s %-8s %-6s | %s\n", "手速", "普通输出", "普通输入", "比",
         "无级输出", "无级输入", "比", "两者差");
  for (double sp : {0.05, 0.1, 0.2, 0.4, 0.8, 1.6, 3.2})
  {
    const Dev n = 跑(scheme, 120.0, sp);
    const Dev f = 跑(scheme, 15.0, sp);
    printf("  %-7.2f | %-8.0f %-8.0f %-6.2f | %-8.0f %-8.0f %-6.2f | %.1fx\n", sp, n.out, n.inTot,
           n.ratio, f.out, f.inTot, f.ratio, n.ratio > 0.001 ? f.ratio / n.ratio : 0.0);
  }
  printf("\n");
}

int main()
{
  printf("X=%.0f delta；同样手速 = 同样 delta/ms\n\n", X);
  扫描("A. 连击按【消息条数】", 0);
  扫描("B. 连击按【累计 delta/15】", 1);
  扫描("C. 行程按【累计 delta】线性涨（600 delta 涨满）", 2);
  printf("读法：比 = 输出/输入，越近 1 越好；两者差 = 同手速下两种鼠标输出差几倍，越近 1 越平衡。\n");
  return 0;
}
