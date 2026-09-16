// 用户场景：一次手指滑动 = 960 delta，X=1
//   普通鼠标：8 格，每格 120  → 8 条
//   无级鼠标：48 格，每格 20  → 48 条     (20 x 48 = 960，与普通同量)
//
// 方案 C（加速按【累计 delta】涨）：
//   travel = X + (本格delta - X) * min(累计delta / 预算, 1)
//
// 用户的期望：普通在第 4~5 格加满、无级在第 30 格加满。
//   -> 普通 120x5=600，无级 20x30=600  =>  预算 = 600 delta
// 本探针把这个预算代进去，打印每格的行程序列。
//
//   g++ -std=c++17 -O2 -o seq.exe combo_seq2_probe.cpp && ./seq.exe
#include <cstdio>
#include <cmath>
#include <vector>

static const double X = 1.0;        // 单次（第 1 格）= 1 delta
static const double BUDGET = 600.0; // 涨满所需累计 delta

struct Row { int i; double accum, travel; };

static std::vector<Row> 跑(double msgDeltas, int n)
{
  std::vector<Row> v;
  double accum = 0;
  for (int i = 1; i <= n; ++i)
  {
    accum += msgDeltas;
    const double u = fmin(accum / BUDGET, 1.0);
    const double travel = X + (msgDeltas - X) * u;
    v.push_back({i, accum, travel});
  }
  return v;
}

static double 总和(const std::vector<Row> &v) { double s = 0; for (auto &r : v) s += r.travel; return s; }

static int 加满于(const std::vector<Row> &v, double cap)
{
  for (auto &r : v)
    if (r.travel >= cap - 1e-9) return r.i;
  return -1;
}

int main()
{
  printf("场景：一次手指滑动 = 960 delta；X=%.0f；预算（涨满）= %.0f delta\n\n", X, BUDGET);

  const std::vector<Row> N = 跑(120.0, 8);  // 普通
  const std::vector<Row> F = 跑(20.0, 48);  // 无级

  printf("普通鼠标（8 格，每格 120）—— 每格行程序列：\n  ");
  for (auto &r : N) printf("%.1f ", r.travel);
  printf("\n  第 %d 格加满(120)；合计 %.0f delta（输入 960）\n\n", 加满于(N, 120.0), 总和(N));

  printf("无级鼠标（48 格，每格 20）—— 每格行程序列：\n  ");
  for (size_t i = 0; i < F.size(); ++i)
  {
    printf("%.2f ", F[i].travel);
    if ((i + 1) % 12 == 0) printf("\n  ");
  }
  printf("\n  第 %d 格加满(20)；合计 %.0f delta（输入 960）\n\n", 加满于(F, 20.0), 总和(F));

  printf(">> 同一段手滚：普通 %.0f，无级 %.0f  → 差 %.2f 倍\n\n", 总和(N), 总和(F),
         总和(N) > 0 ? 总和(F) / 总和(N) : 0.0);

  printf("对照用户的期望：\n");
  printf("  普通 \"第 4~5 格加满\"  ->  实际第 %d 格\n", 加满于(N, 120.0));
  printf("  无级 \"第 30 格加满\"   ->  实际第 %d 格\n", 加满于(F, 20.0));
  printf("  => 预算 = 600 delta 正好落在这两个点上（120x5 = 20x30 = 600）。\n");
  return 0;
}
