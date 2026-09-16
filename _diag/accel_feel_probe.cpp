// 加速探针：从慢轮渐渐加速到快轮，行程怎么变？"跟不上手 / 没有线性提速"来自哪里？
//
// 模拟一只手：间隔从 220ms 逐渐缩到 55ms（约 2.5 秒），并带 ±25ms 的手抖。
// 对每种"测速方式"打印每格行程（deltas），并算一个"滞后指数"：
//   滞后 = (平滑后行程到达最终值 90% 的那一格编号) - (理想行程到达 90% 的那一格编号)
// 滞后越大越"跟不上手"。
//
//   g++ -std=c++17 -O2 -I../src -o accelfeel.exe accel_feel_probe.cpp && ./accelfeel.exe
#include "model.h"
#include <cstdio>
#include <cmath>
#include <vector>

static const double kDPN = 120.0;

// 手：间隔从 g0 线性降到 g1，带 ±jit 的抖动
static std::vector<double> 造间隔(int n, double g0, double g1, double jit)
{
  std::vector<double> g(n);
  for (int i = 0; i < n; ++i)
  {
    const double base = g0 + (g1 - g0) * i / (double)(n - 1);
    const double j = ((i % 2) ? +jit : -jit);
    g[i] = base + j;
  }
  return g;
}

// 用某种滤波后的间隔算每格行程
enum class Filt { kRaw, kEmaN, kMedian3, kMin3 };

static std::vector<double> 行程(const std::vector<double> &gaps, Filt f, double emaN, double slowMove)
{
  const int n = (int)gaps.size();
  std::vector<double> out(n, 0.0);
  double ema = -1.0;
  for (int i = 0; i < n; ++i)
  {
    double sec = (i == 0) ? -1.0 : gaps[i] / 1000.0;
    if (i > 0)
    {
      if (f == Filt::kEmaN)
      {
        if (ema < 0.0) ema = sec;
        else ema += (sec - ema) / emaN;
        sec = ema;
      }
      else if (f == Filt::kMedian3)
      {
        double a = gaps[i - 1] / 1000.0, b = sec, c = (i + 1 < n) ? gaps[i + 1] / 1000.0 : sec;
        if (a > b) { double t = a; a = b; b = t; }
        if (b > c) { double t = b; b = c; c = t; }
        if (a > b) { double t = a; a = b; b = t; }
        sec = b;
      }
      else if (f == Filt::kMin3)
      {
        double a = gaps[i - 1] / 1000.0, b = sec;
        double c = (i + 1 < n) ? gaps[i + 1] / 1000.0 : sec;
        sec = std::fmin(a, std::fmin(b, c));
      }
    }
    out[i] = model::TravelScale(sec, kDPN, slowMove) * kDPN;
  }
  return out;
}

// 理想（无抖动、间隔就是基准）的行程
static std::vector<double> 理想行程(int n, double g0, double g1, double slowMove)
{
  std::vector<double> out(n, 0.0);
  for (int i = 0; i < n; ++i)
  {
    const double base = g0 + (g1 - g0) * i / (double)(n - 1);
    out[i] = model::TravelScale(base / 1000.0, kDPN, slowMove) * kDPN;
  }
  return out;
}

static int 到达90(const std::vector<double> &v)
{
  double mx = 0;
  for (double x : v) mx = std::fmax(mx, x);
  for (int i = 0; i < (int)v.size(); ++i)
    if (v[i] >= 0.9 * mx) return i;
  return (int)v.size();
}

static void 一行(const char *name, const std::vector<double> &v, const std::vector<double> &ideal)
{
  printf("  %-16s 行程:", name);
  for (int i = 0; i < (int)v.size(); i += 3) printf(" %5.1f", v[i]);
  printf("\n");
  printf("  %-16s 滞后 %d 格\n", "", 到达90(v) - 到达90(ideal));
}

int main()
{
  const int n = 30;
  const double slowMove = 1.0; // 用户当前设置
  const std::vector<double> gaps = 造间隔(n, 220.0, 55.0, 25.0);
  const std::vector<double> ideal = 理想行程(n, 220.0, 55.0, slowMove);

  printf("Slow=%0.f，间隔 220→55ms（每格减 ~%.0fms），手抖 ±25ms，共 %d 格\n",
         slowMove, (220.0 - 55.0) / (n - 1), n);
  printf("（每 2 格打印一次；滞后 = 到达最终行程 90%% 比理想晚几格）\n\n");

  printf("== 理想（无抖动，基准速度）==\n");
  一行("理想", ideal, ideal);

  printf("\n== 当前（EMA 窗口 4）==\n");
  一行("EMA4", 行程(gaps, Filt::kEmaN, 4.0, slowMove), ideal);

  printf("\n== 备选 ==\n");
  一行("原始(不平滑)", 行程(gaps, Filt::kRaw, 0, slowMove), ideal);
  一行("EMA2", 行程(gaps, Filt::kEmaN, 2.0, slowMove), ideal);
  一行("中位数3", 行程(gaps, Filt::kMedian3, 0, slowMove), ideal);
  一行("最小3", 行程(gaps, Filt::kMin3, 0, slowMove), ideal);

  printf("\n（理想行程在开头很小、末尾到 120；看哪一行的形状最贴近它）\n");
  return 0;
}
