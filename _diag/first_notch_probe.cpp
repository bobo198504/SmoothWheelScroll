// "第一格是不是一定 = Start 个 delta？" —— 用真模型算第一条消息的实际行程。
//
// 第一条消息：SpeedBudget 没有"上一条"，所以 gapMs=0，预算是【这条消息自己的 delta】。
//   预算 = 这条消息的 delta
//   u    = 预算 / Ramp-up
//   行程 = Start + (这条消息delta - Start) × u
//
// 所以第一格**取决于设备**（120 还是 15），而不是 Start。
//
//   g++ -std=c++17 -O2 -I../src -o first.exe first_notch_probe.cpp && ./first.exe
#include "model.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

static const double BUD = 600.0;

static void 第一格(double msgDeltas, const char *dev)
{
  printf("   %-14s（每条 %.0f delta）\n", dev, msgDeltas);
  printf("     %-10s %-12s %-14s %s\n", "Start", "预算(=本格)", "u=预算/Ramp-up", "第一格实际行程");
  for (double start : {1.0, 5.0, 10.0})
  {
    model::SpeedBudget b;
    b.Reset();
    const double bud = b.Add(msgDeltas, 0.0); // 第一条：gapMs=0
    const double u = bud / BUD;
    const double tr = model::Travel(msgDeltas, bud, BUD, start, 1.0);
    printf("     %-10.1f %-12.1f %-14.3f %.2f delta  %s\n", start, bud, u, tr,
           (std::fabs(tr - start) < 0.01) ? "= Start" : "≠ Start");
  }
  printf("\n");
}

int main()
{
  printf("Ramp-up = %.0f delta；只喂第一条消息，看第一格走多少\n\n", BUD);
  第一格(120.0, "普通鼠标");
  第一格(20.0, "无级鼠标(20)");
  第一格(15.0, "无级鼠标(15)");

  printf("说明：\n");
  printf("  * 预算是【这条消息自己的 delta】，所以 u 一开始就不是 0；\n");
  printf("  * 普通鼠标 u=120/600=0.20 -> 第一格 ≈ 0.2×120 = 24（Start 只占一小块）；\n");
  printf("  * 无级鼠标 u= 15/600=0.025 -> 第一格 ≈ 1.35，才比较接近 Start；\n");
  printf("  * 结论：第一格【不是】Start，它首先由【设备的 delta 大小】决定。\n");
  return 0;
}
