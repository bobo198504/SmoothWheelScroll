// 慢轮探针：慢慢滚时输出到底长什么样？"突然快一下"来自哪里？
//
// 用【真模型】跑一串滚轮，打印每格行程与输出速率剖面，并算一个"放大倍数"：
//   放大倍数 = (输出速率比) / (滚速比)
// 若 ≈1，输出与滚速成线性；若 >1，滚快一点点输出就快很多 —— 那就是"突然快一下"。
//
// 末尾比较两条路线：用"每格自己的间隔"定行程（现状） vs 用"平滑后的间隔"定行程（候选修法）。
//
//   g++ -std=c++17 -O2 -I../src -o slowroll.exe slowroll_probe.cpp && ./slowroll.exe
#include "model.h"
#include <cstdio>
#include <cmath>
#include <vector>

static const double kU = 15.0, kDPN = 120.0, kDPU = 8.0;

struct Res
{
  std::vector<double> perNotch;
  double avgRate, peakRate, duty;
};

// emaN > 0：把间隔先做指数平滑再用（消除单手抖）；0 = 直接用每格自己的间隔。
static Res 跑序列(const std::vector<double> &gapMs, double windowMs, double slowMove, bool ramp,
                 double emaN = 0.0)
{
  const int n = (int)gapMs.size();
  std::vector<double> arr(n);
  double tt = 0;
  for (int i = 0; i < n; ++i) { tt += (i == 0 ? 0.0 : gapMs[i]); arr[i] = tt; }
  const double endT = arr[n - 1] + windowMs + 10.0;

  model::Axis g;
  g.Reset();
  model::Params P;
  P.windowMs = windowMs;

  const double dt = 0.0005;
  double t = 0, out = 0, emaGap = -1.0;
  std::vector<double> atArr(n, 0.0);
  int f = 0;
  bool hasLast = false;
  const double binMs = 20.0;
  double binAcc = 0, binT = 0;
  std::vector<double> bins;

  while (t < endT)
  {
    if (f < n && arr[f] <= t + 1e-12)
    {
      double gapSec = hasLast ? (arr[f] - arr[f - 1]) / 1000.0 : -1.0;
      hasLast = true;
      if (emaN > 0.0 && gapSec >= 0.0)
      {
        emaGap = (emaGap < 0.0) ? gapSec : (gapSec + (emaN - 1.0) * emaGap) / emaN;
        gapSec = emaGap;
      }
      const double scale = ramp ? model::TravelScale(gapSec, kDPN, slowMove) : 1.0;
      g.Feed(kU * scale, P);
      atArr[f] = out;
      ++f;
    }
    const double o = g.Tick(dt, P) * kDPU;
    out += o;
    binAcc += o;
    binT += dt * 1000.0;
    if (binT >= binMs - 1e-9) { bins.push_back(binAcc); binAcc = 0; binT = 0; }
    t += dt;
    if (f >= n && !g.Active()) break;
  }

  Res r;
  for (int i = 0; i < n; ++i)
  {
    const double prev = (i == 0) ? 0.0 : atArr[i - 1];
    r.perNotch.push_back(atArr[i] - prev);
  }
  double peak = 0;
  int nz = 0;
  for (double b : bins) { if (b > peak) peak = b; if (b > 1e-9) ++nz; }
  r.peakRate = peak / binMs;
  r.duty = bins.empty() ? 0.0 : (double)nz / (double)bins.size();
  r.avgRate = (t > 0) ? out / t : 0.0;
  return r;
}

static void 固定间隔表(const char *标题, double X, double slowMove, bool ramp)
{
  printf("%s\n", 标题);
  printf("  %-8s %-12s %-11s %-11s %-9s %-9s %s\n", "间隔ms", "每格deltas", "平均速率", "峰值速率",
         "占空比", "放大倍数", "形状");
  double prevGap = 0, prevRate = 0;
  for (double gap : {300.0, 250.0, 200.0, 175.0, 150.0, 140.0, 130.0, 120.0, 100.0, 80.0, 60.0, 50.0})
  {
    std::vector<double> gaps(14, gap);
    const Res r = 跑序列(gaps, X, slowMove, ramp);
    double perN = 0;
    for (int i = (int)r.perNotch.size() - 5; i < (int)r.perNotch.size(); ++i) perN += r.perNotch[i];
    perN /= 5.0;
    double amp = 0;
    if (prevRate > 0) amp = (r.avgRate / prevRate) / (prevGap / gap);
    printf("  %-8.0f %-12.2f %-11.3f %-11.3f %-9.2f %-9.2f %s\n", gap, perN, r.avgRate, r.peakRate,
           r.duty, amp, r.duty > 0.97 ? "连续" : "脉冲");
    prevGap = gap;
    prevRate = r.avgRate;
  }
  printf("  (放大倍数 >1：滚速只快一点，输出被放大很多)\n\n");
}

int main()
{
  const double X = 150.0;
  printf("窗口 X=%.0f ms；真模型，1ms 帧\n\n", X);

  固定间隔表("== A. 有斜坡（Slow move = 20）==", X, 20.0, true);
  固定间隔表("== B. 无斜坡（Slow move = 120，纯时序）==", X, 120.0, false);

  printf("== C. 慢轮、手抖（间隔在 130~240ms 之间抖动）==\n");
  // 同一段抖动的间隔，比较"不抖动 vs 用手抖间隔 vs 平滑后用手抖间隔"
  const std::vector<double> 抖 = {240, 180, 220, 150, 210, 170, 240, 140, 200, 180, 230, 160};
  struct Case { const char *name; bool ramp; double emaN; };
  for (Case c : {Case{"有斜坡(Slow move=20)", true, 0.0},
                 Case{"有斜坡 + 间隔平滑(EMA4)", true, 4.0},
                 Case{"无斜坡(Slow move=120)", false, 0.0}})
  {
    const Res r = 跑序列(抖, X, 20.0, c.ramp, c.emaN);
    double lo = 1e30, hi = 0;
    for (int i = 2; i < (int)r.perNotch.size(); ++i)
    {
      lo = std::fmin(lo, r.perNotch[i]);
      hi = std::fmax(hi, r.perNotch[i]);
    }
    printf("  %-28s 每格:", c.name);
    for (double v : r.perNotch) printf(" %.0f", v);
    printf("\n     %-25s 波动 %.0f~%.0f，最大/最小 = %.2fx\n", "", lo, hi, hi / lo);
  }
  return 0;
}
