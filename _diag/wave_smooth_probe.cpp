// 波浪诊断：普通鼠标匀速滚时，输出速率有多"波浪"？以及几种平滑能不能压掉它。
//
// 普通鼠标：每 150ms 一条 120 delta（匀速手滚），模型给每格一个行程，
// 行程再摊在 Glide length 的窗口里。两个地方可能产生起伏：
//   (1) 每格的行程本身随预算波动；
//   (2) 窗口摊开造成的"脉冲/叠加"。
//
//   g++ -std=c++17 -O2 -I../src -o wave.exe wave_smooth_probe.cpp && ./wave.exe
#include "model.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <initializer_list>

static const double kDPU = 8.0;

// 匀速滚：每 gapMs 一条 120；可选把每格行程做 EMA 平滑(alpha=1/N)。
// 返回：输出速率序列（deltas per 10ms 桶）
static std::vector<double> 跑(double gapMs, double windowMs, double start, double ramp, double mul,
                              int travelEmaN, double ms)
{
  model::Axis g;
  g.Reset();
  model::Params P;
  P.windowMs = windowMs;
  model::SpeedBudget bud;
  bud.Reset();

  const double dt = 0.0005; // 0.5ms 精细积分，避免把积分误差当成波
  std::vector<double> bins;
  const double binMs = 10.0;
  double acc = 0, bt = 0, t = 0, next = 0;
  int f = 0, seen = 0;
  double emaTravel = -1.0;

  while (t < ms)
  {
    if (next <= t + 1e-12)
    {
      const double gap = (seen == 0) ? 0.0 : gapMs;
      bud.Add(120.0, gap);
      double travel = model::Travel(120.0, bud.Value(), ramp, start, mul);
      if (travelEmaN > 0)
      {
        if (emaTravel < 0.0) emaTravel = travel;
        else emaTravel += (travel - emaTravel) / travelEmaN;
        travel = emaTravel;
      }
      g.Feed(travel / kDPU, P);
      next += gapMs / 1000.0;
      ++seen;
    }
    acc += g.Tick(dt, P) * kDPU;
    bt += dt * 1000.0;
    if (bt >= binMs - 1e-9) { bins.push_back(acc); acc = 0; bt = 0; }
    t += dt;
  }
  return bins;
}

static void 评(const char *name, const std::vector<double> &b)
{
  // 丢掉起步的 300ms（还没进入稳态）
  const int skip = 30;
  double sum = 0;
  int n = 0;
  for (int i = skip; i < (int)b.size(); ++i) { sum += b[i]; ++n; }
  const double mean = sum / n;
  double v = 0, lo = 1e30, hi = -1e30;
  for (int i = skip; i < (int)b.size(); ++i)
  {
    v += (b[i] - mean) * (b[i] - mean);
    if (b[i] < lo) lo = b[i];
    if (b[i] > hi) hi = b[i];
  }
  const double sd = std::sqrt(v / n);
  printf("  %-22s 均值%7.2f  标准差%6.2f (%.1f%%)  峰-谷 %6.2f~%6.2f\n", name, mean, sd,
         mean > 0 ? 100 * sd / mean : 0, lo, hi);
}

int main()
{
  const double gap = 150.0, W = 200.0, start = 1.0, ramp = 600.0, mul = 1.0;
  printf("普通鼠标匀速滚：每 %.0fms 一格 120 delta；Glide=%.0fms；Slow step=%.0f；Ramp-up=%.0f\n\n",
         gap, W, start, ramp);

  printf("== 现状 vs 平滑（每格的行程做 EMA）==\n");
  评("不平滑", 跑(gap, W, start, ramp, mul, 0, 4000.0));
  for (int N : {2, 3, 5, 8})
  {
    char nm[32];
    _snprintf(nm, sizeof(nm), "行程 EMA%d", N);
    评(nm, 跑(gap, W, start, ramp, mul, N, 4000.0));
  }

  printf("\n== 换个更慢的滚（每格 300ms，> Glide）==\n");
  const double gap2 = 300.0;
  评("不平滑", 跑(gap2, W, start, ramp, mul, 0, 6000.0));
  for (int N : {3, 5})
  {
    char nm[32];
    _snprintf(nm, sizeof(nm), "行程 EMA%d", N);
    评(nm, 跑(gap2, W, start, ramp, mul, N, 6000.0));
  }
  return 0;
}
