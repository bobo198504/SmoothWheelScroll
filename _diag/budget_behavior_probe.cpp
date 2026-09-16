// 涨满预算的两种行为：一次手势里"涨上去之后"会怎样？
//
// 场景：一次手指滑动，但手速变化：慢 → 快 → 慢（比如慢滚，然后加速，再慢下来）。
// 设备：普通鼠标，每条 120 delta。
//
//   A. 单调累积：u = 累计delta / 预算，只增不减 → 涨上去就一直满
//   B. 跟随当前速度：累计量会随时间衰减 → 慢下来时行程也跟着降回去
//
// 打印每条消息的行程（deltas），看两种行为在"慢→快→慢"下的区别。
//
//   g++ -std=c++17 -O2 -o budget.exe budget_behavior_probe.cpp && ./budget.exe
#include <cstdio>
#include <cmath>
#include <functional>

static const double X = 1.0;        // 单次
static const double BUDGET = 600.0; // 涨满预算（累计 delta）
static const double TAU = 300.0;    // B 方案的衰减时间常数(ms)

int main()
{
  const double msgDeltas = 120.0; // 普通鼠标
  // 手速剖面：0-2s 慢 0.1，2-4s 快 1.5，4-6s 慢 0.1（delta/ms）
  auto speedAt = [](double ms) {
    if (ms < 2000.0) return 0.1;
    if (ms < 4000.0) return 1.5;
    return 0.1;
  };

  printf("普通鼠标(120/条)，X=%.0f，预算=%.0f delta，B方案时间常数=%.0fms\n", X, BUDGET, TAU);
  printf("手速：0-2s 慢(0.1) → 2-4s 快(1.5) → 4-6s 慢(0.1)  [delta/ms]\n");
  printf("(每条 = 一格 120 delta；打印每格的行程，deltas)\n\n");

  for (int scheme = 0; scheme < 2; ++scheme)
  {
    printf("========== %s ==========\n", scheme == 0 ? "A. 单调累积（只增不减）"
                                                    : "B. 跟随当前速度（慢下来会降回去）");
    double t = 0, accum = 0, leak = 0;
    int i = 0;
    printf("  t(s)  手速   本格行程\n");
    while (t < 6000.0 && i < 200)
    {
      const double s = speedAt(t);
      const double gap = msgDeltas / s; // ms
      // 衰减（B）
      leak *= std::exp(-gap / TAU);
      // 累积
      accum += msgDeltas;
      leak += msgDeltas;
      const double driver = (scheme == 0) ? accum : leak;
      const double u = std::fmin(driver / BUDGET, 1.0);
      const double travel = X + (msgDeltas - X) * u;
      if (i % 3 == 0 || (t < 4200 && t > 3800))
        printf("  %4.1f  %5.2f  %8.1f   %s\n", t / 1000.0, s, travel,
               (t >= 1900 && t < 2100) ? "<-下面开始快" : (t >= 3900 && t < 4100) ? "<-下面开始慢" : "");
      t += gap;
      ++i;
    }
    printf("\n");
  }

  printf("读法：\n");
  printf("  * 看 2~4s（快速段）：两者都应涨到接近满格 120；\n");
  printf("  * 看 4~6s（又慢下来）：A 会保持 120 不动；B 会降回去（慢滚又变小）。\n");
  return 0;
}
