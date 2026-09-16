// 波浪：根源是"窗口在飞的个数在 1↔2 之间跳"。测几种**摊开形状**能否压平它。
//
//   flat     : 现有做法——窗口在 200ms 内【匀速】付清（每帧付 剩余/剩余时间）
//   raisedcos: 付速率按"升余弦"形状（两端慢、中间快），窗口边界处不会有速率台阶
//   tri      : 付速率三角形状（线性升到中点半值再降）
//   smooth3  : 位置按 smoothstep（等价于速率是 6u(1-u) 的钟形）
//
// 目标：同样匀速滚，输出速率的【标准差/均值】越小越平。
//
//   g++ -std=c++17 -O2 -o wave2.exe wave_shape_probe.cpp && ./wave2.exe
#include <cstdio>
#include <cmath>
#include <vector>
#include <initializer_list>

static const double kDPU = 8.0;

// 一个窗口的形状函数：给定归一化进度 p∈[0,1]，返回【累计付清比例】(0→1)。
// 由它算每帧的增量，就得到付速率形状。
static double 形状(const char *k, double p)
{
  if (p <= 0) return 0;
  if (p >= 1) return 1;
  if (k[0] == 'f') return p;                        // flat: 匀速 -> 付速率恒定
  if (k[0] == 't') return 1 - (1 - p) * (1 - p);    // tri: 速率线性下降...（线性斜坡升）
  if (k[0] == 's') { double u = p * p * (3 - 2 * p); return u; } // smoothstep 位置
  // raised cosine: 位置 = p - sin(2πp)/(2π)
  return p - std::sin(2 * 3.14159265358979323846 * p) / (2 * 3.14159265358979323846);
}

struct Res { double mean, sd, lo, hi; };

static Res 跑(const char *kind, double gapMs, double windowMs, double travel, double ms)
{
  // 直接模拟"窗口"层：每 gapMs 开一个窗口，窗口把 travel 按「形状」在 windowMs 内付清。
  // 用 0.5ms 步长积分；把付出去的速率按 10ms 桶统计。
  const double dt = 0.0005;
  std::vector<double> bins;
  const double binMs = 10.0;
  double acc = 0, bt = 0, t = 0, next = 0;
  std::vector<std::pair<double, double>> win; // (出生t, 已付比例)
  while (t < ms)
  {
    if (next <= t + 1e-12) { win.push_back({t, 0.0}); next += gapMs / 1000.0; }
    double out = 0;
    for (size_t i = 0; i < win.size(); ++i)
    {
      const double p = (t - win[i].first) / (windowMs / 1000.0);
      const double want = 形状(kind, p);
      const double d = (want - win[i].second) * travel;
      if (d > 0) out += d;
      win[i].second = want;
    }
    // 清掉走完的
    for (size_t i = 0; i < win.size();)
      if (win[i].second >= 1.0 && (t - win[i].first) * 1000.0 > windowMs) win.erase(win.begin() + i);
      else ++i;
    acc += out * kDPU / 1.0; // travel 已是 delta -> 单位换算掉，这里保持 delta
    acc = 0;                 // 上面那行只为说明；重算：
    (void)acc;
    bt += dt * 1000.0;
    if (bt >= binMs - 1e-9) { bins.push_back(out * kDPU); bt = 0; }
    t += dt;
  }
  return {0, 0, 0, 0}; // 占位（本函数未用，见下）
}

int main()
{
  const double gapMs = 150, windowMs = 200, travelD = 48.6, ms = 4000;
  const double dt = 0.0005, binMs = 10.0;
  printf("匀速滚：每 %.0fms 一格（行程 %.1f delta），窗口 %.0fms\n\n", gapMs, travelD, windowMs);
  for (const char *kind : {"flat", "tri", "smoothstep", "raisedcos"})
  {
    std::vector<double> bins;
    struct W { double t0, paid; };
    std::vector<W> win;
    double t = 0, next = 0, acc = 0, bt = 0;
    while (t < ms)
    {
      if (next <= t + 1e-12) { win.push_back({t, 0.0}); next += gapMs / 1000.0; }
      double out = 0;
      for (size_t i = 0; i < win.size(); ++i)
      {
        const double p = (t - win[i].t0) / (windowMs / 1000.0);
        const double want = 形状(kind, p);
        out += (want - win[i].paid) * travelD;
        win[i].paid = want;
      }
      for (size_t i = 0; i < win.size();)
        if ((t - win[i].t0) * 1000.0 > windowMs + 1e-9) win.erase(win.begin() + i); else ++i;
      acc += out;
      bt += dt * 1000.0;
      if (bt >= binMs - 1e-9) { bins.push_back(acc); acc = 0; bt = 0; }
      t += dt;
    }
    const int skip = 40;
    double sum = 0; int n = 0;
    for (int i = skip; i < (int)bins.size(); ++i) { sum += bins[i]; ++n; }
    const double mean = sum / n;
    double v = 0, lo = 1e30, hi = -1e30;
    for (int i = skip; i < (int)bins.size(); ++i)
    { v += (bins[i] - mean) * (bins[i] - mean); if (bins[i] < lo) lo = bins[i]; if (bins[i] > hi) hi = bins[i]; }
    const double sd = std::sqrt(v / n);
    printf("  %-12s 均值%7.2f  标准差%6.2f (%4.1f%%)  峰-谷 %6.2f~%6.2f\n", kind, mean, sd,
           mean > 0 ? 100 * sd / mean : 0, lo, hi);
  }
  (void)跑;
  return 0;
}
