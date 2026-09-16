// 形状平滑是否【跨滚速稳】？扫 gap 从 60ms 到 400ms，比 flat vs smoothstep 的标准差。
#include <cstdio>
#include <cmath>
#include <vector>
#include <initializer_list>

static double S(const char *k, double u)
{
  if (u <= 0) return 0;
  if (u >= 1) return 1;
  if (k[0] == 'f') return u;
  return u * u * (3 - 2 * u);
}

static double sdPct(const char *kind, double gapMs, double windowMs, double travelD)
{
  const double dt = 0.0005, binMs = 10.0, ms = 6000.0;
  struct W { double t0, paid; };
  std::vector<W> win;
  std::vector<double> bins;
  double t = 0, next = 0, acc = 0, bt = 0;
  while (t < ms)
  {
    if (next <= t + 1e-12) { win.push_back({t, 0.0}); next += gapMs / 1000.0; }
    double out = 0;
    for (size_t i = 0; i < win.size(); ++i)
    {
      const double u = (t - win[i].t0) / (windowMs / 1000.0);
      const double want = S(kind, u);
      out += (want - win[i].paid) * travelD;
      win[i].paid = want;
    }
    for (size_t i = 0; i < win.size();)
      if ((t - win[i].t0) * 1000.0 > windowMs + 1e-9) win.erase(win.begin() + i); else ++i;
    acc += out; bt += dt * 1000.0;
    if (bt >= binMs - 1e-9) { bins.push_back(acc); acc = 0; bt = 0; }
    t += dt;
  }
  const int skip = 40;
  double sum = 0; int n = 0;
  for (int i = skip; i < (int)bins.size(); ++i) { sum += bins[i]; ++n; }
  const double mean = sum / n;
  double v = 0;
  for (int i = skip; i < (int)bins.size(); ++i) v += (bins[i] - mean) * (bins[i] - mean);
  return mean > 0 ? 100 * std::sqrt(v / n) / mean : 0;
}

int main()
{
  const double W = 200.0, travel = 48.6;
  printf("窗口=%.0fms；下表=输出速率的标准差%%（越小越平）\n\n", W);
  printf("  %-8s %-10s %-10s %s\n", "每格ms", "flat", "smoothstep", "改善");
  for (double gap : {60.0, 80.0, 100.0, 120.0, 150.0, 180.0, 200.0, 250.0, 300.0, 400.0})
  {
    const double a = sdPct("flat", gap, W, travel);
    const double b = sdPct("smooth", gap, W, travel);
    printf("  %-8.0f %-10.1f %-10.1f %s\n", gap, a, b,
           b < a * 0.8 ? "★好" : (b < a * 1.1 ? "≈同" : "差"));
  }
  return 0;
}
