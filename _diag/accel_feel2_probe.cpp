// 加速探针 2：用【更真实的抖动】比较几种测速方式，看哪个"既稳又跟得上手"。
//
// 手：间隔从 220ms 渐渐缩到 55ms（加速），叠加【伪随机】抖动（不是完美交替）。
// 对每种方式测两件事：
//   * 慢区抖动：开头几格（还在慢轮）行程的相对波动 std/mean —— 越小越稳；
//   * 滞后：行程涨到 60 delta 需要第几格 —— 越小越"跟得上手"（理想值最小）。
//
//   g++ -std=c++17 -O2 -I../src -o accelfeel2.exe accel_feel2_probe.cpp && ./accelfeel2.exe
#include "model.h"
#include <cstdio>
#include <cmath>
#include <vector>

static const double kDPN = 120.0;

// 确定性伪随机（同一个种子每次一样）
struct Rng
{
  unsigned s;
  double next() { s = s * 1103515245u + 12345u; return ((s >> 16) % 1001) / 1000.0; } // 0..1
};

static std::vector<double> 造间隔(int n, double g0, double g1, double jit)
{
  Rng r{12345u};
  std::vector<double> g(n);
  for (int i = 0; i < n; ++i)
  {
    const double base = g0 + (g1 - g0) * i / (double)(n - 1);
    g[i] = base + (r.next() * 2.0 - 1.0) * jit;
  }
  return g;
}

enum class Filt { kRaw, kEma, kMedian3 };

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
      if (f == Filt::kEma)
      {
        if (ema < 0.0) ema = sec;
        else ema += (sec - ema) / emaN;
        sec = ema;
      }
      else if (f == Filt::kMedian3)
      {
        double a = gaps[i - 1] / 1000.0, b = sec, c = sec;
        if (i + 1 < n) c = gaps[i + 1] / 1000.0;
        if (a > b) { double t = a; a = b; b = t; }
        if (b > c) { double t = b; b = c; c = t; }
        if (a > b) { double t = a; a = b; b = t; }
        sec = b;
      }
    }
    out[i] = model::TravelScale(sec, kDPN, slowMove) * kDPN;
  }
  return out;
}

static void 评(const char *name, const std::vector<double> &v)
{
  // 慢区：前 8 格
  double sum = 0;
  int k = 0;
  for (int i = 1; i < 8 && i < (int)v.size(); ++i) { sum += v[i]; ++k; }
  const double mean = sum / k;
  double var = 0;
  for (int i = 1; i < 8 && i < (int)v.size(); ++i) var += (v[i] - mean) * (v[i] - mean);
  const double sd = std::sqrt(var / k);
  // 滞后：第一格行程 >= 60 delta
  int lag = -1;
  for (int i = 0; i < (int)v.size(); ++i)
    if (v[i] >= 60.0) { lag = i; break; }
  printf("  %-14s 慢区 均值%6.1f 波动%5.2f  | 到60delta第%2d格\n", name, mean,
         mean > 0 ? sd / mean : 0.0, lag + 1);
}

int main()
{
  const int n = 34;
  const double slowMove = 1.0;
  const std::vector<double> gaps = 造间隔(n, 220.0, 55.0, 30.0);

  printf("Slow=%.0f，间隔 220→55ms，伪随机抖动 ±30ms，共 %d 格\n", slowMove, n);
  printf("（慢区波动越小越稳；到 60delta 的格号越小越跟得上手）\n\n");

  printf("== 各测速方式 ==\n");
  评("原始(不平滑)", 行程(gaps, Filt::kRaw, 0, slowMove));
  评("EMA2", 行程(gaps, Filt::kEma, 2.0, slowMove));
  评("EMA3", 行程(gaps, Filt::kEma, 3.0, slowMove));
  评("EMA4(当前)", 行程(gaps, Filt::kEma, 4.0, slowMove));
  评("中位数3", 行程(gaps, Filt::kMedian3, 0, slowMove));

  printf("\n== 同一组数据下，行程序列（每 2 格打一次）==\n");
  const char *names[] = {"原始", "EMA2", "EMA3", "EMA4", "中位数3"};
  std::vector<std::vector<double>> all = {
      行程(gaps, Filt::kRaw, 0, slowMove), 行程(gaps, Filt::kEma, 2.0, slowMove),
      行程(gaps, Filt::kEma, 3.0, slowMove), 行程(gaps, Filt::kEma, 4.0, slowMove),
      行程(gaps, Filt::kMedian3, 0, slowMove)};
  for (int j = 0; j < 5; ++j)
  {
    printf("  %-10s:", names[j]);
    for (int i = 0; i < n; i += 2) printf(" %5.1f", all[j][i]);
    printf("\n");
  }
  return 0;
}
