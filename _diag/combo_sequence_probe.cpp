// 用户指定的场景：一次手指滑动 = 960 delta
//   普通鼠标：8 格，每格 120 delta  → 8 条消息
//   无级鼠标：48 格，每格 20 delta  → 48 条消息   （20×48 = 960，与普通同量）
//
// 把这一组代进两种方案，打印每格实际走多少 delta 的【序列】。
//
//   A：加速按【消息条数】涨   travel = min(X + (第几条-1)*a, 本格delta)
//   C：加速按【累计delta】涨   travel = X + (本格delta - X) * min(累计/预算, 1)
//
//   g++ -std=c++17 -O2 -o seq.exe combo_sequence_probe.cpp && ./seq.exe
#include <cstdio>
#include <cmath>
#include <vector>

static const double X = 5.0;        // 单次（第 1 格）
static const double A_STEP = 2.0;   // A：每格增量
static const double BUDGET = 600.0; // C：累计 600 delta 涨到顶

struct Row { int idx; double accum, cap, travel; };

static std::vector<Row> 跑(int scheme, double msgDeltas, int n)
{
  std::vector<Row> v;
  double accum = 0;
  for (int i = 1; i <= n; ++i)
  {
    accum += msgDeltas;
    double travel;
    if (scheme == 0)
      travel = fmin(X + (i - 1.0) * A_STEP, msgDeltas);
    else
      travel = X + (msgDeltas - X) * fmin(accum / BUDGET, 1.0);
    v.push_back({i, accum, msgDeltas, travel});
  }
  return v;
}

static double 总和(const std::vector<Row> &v) { double s = 0; for (auto &r : v) s += r.travel; return s; }

static void 打印(const char *标题, int scheme)
{
  const std::vector<Row> N = 跑(scheme, 120.0, 8);  // 普通
  const std::vector<Row> F = 跑(scheme, 20.0, 48);  // 无级
  printf("========== %s ==========\n\n", 标题);

  printf("  普通鼠标（8 条，每条 120）：\n    ");
  for (auto &r : N) printf("%.0f ", r.travel);
  printf("\n    合计 %.0f delta（输入 960）\n\n", 总和(N));

  printf("  无级鼠标（48 条，每条 20）：\n    ");
  for (size_t i = 0; i < F.size(); ++i)
  {
    printf("%.1f ", F[i].travel);
    if ((i + 1) % 12 == 0) printf("\n    ");
  }
  printf("\n    合计 %.0f delta（输入 960）\n\n", 总和(F));

  printf("  >> 同一段手滚（960 delta）：普通 %.0f，无级 %.0f  → 差 %.2f 倍\n\n",
         总和(N), 总和(F), 总和(N) > 0 ? 总和(F) / 总和(N) : 0.0);
}

int main()
{
  printf("场景：一次手指滑动 = 960 delta\n");
  printf("      普通鼠标：8 格 x120；无级鼠标：48 格 x20\n");
  printf("      X=%.0f（单次），下面每个数 = 这一格实际走了多少 delta\n\n", X);

  打印("方案 A：加速按【消息条数】涨", 0);
  打印("方案 C：加速按【累计 delta】涨（600 涨满）", 1);

  printf("读法：两行的【合计】越接近 = 两种鼠标越一致（理想两者都是 960 的某个相同比例）。\n");
  return 0;
}
