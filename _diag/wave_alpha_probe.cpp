// 折中形状：S(u) = (1-α)·u + α·smoothstep(u)。α=0 是现状(flat)，α=1 是纯 smoothstep。
// 目标：找一个 α 让【快滚变平】而【慢滚不变差】。
#include <cstdio>
#include <cmath>
#include <vector>
#include <initializer_list>

static double S(double a, double u)
{
  if (u <= 0) return 0;
  if (u >= 1) return 1;
  const double ss = u * u * (3 - 2 * u);
  return (1 - a) * u + a * ss;
}

static double sdPct(double a, double gapMs, double windowMs, double travelD, double *meanOut)
{
  const double dt = 0.0005, binMs = 10.0, ms = 6000.0;
  struct W { double t0, paid; };
  std::vector<W> win; std::vector<double> bins;
  double t = 0, next = 0, acc = 0, bt = 0;
  while (t < ms)
  {
    if (next <= t + 1e-12) { win.push_back({t, 0.0}); next += gapMs / 1000.0; }
    double out = 0;
    for (size_t i = 0; i < win.size(); ++i)
    {
      const double u = (t - win[i].t0) / (windowMs / 1000.0);
      const double want = S(a, u);
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
  if (meanOut) *meanOut = mean;
  return mean > 0 ? 100 * std::sqrt(v / n) / mean : 0;
}

int main()
{
  const double W = 200.0, travel = 48.6;
  const double gaps[] = {60, 80, 100, 120, 150, 180, 200, 250, 300, 400};
  printf("窗口=%.0fms；每格一列，值=标准差%%（越小越平）\n\n", W);
  printf("  %-6s", "alpha");
  for (double g : gaps) printf(" %5.0f", g);
  printf("   %s\n", "最差");
  for (double a : {0.0, 0.15, 0.25, 0.35, 0.5, 0.75, 1.0})
  {
    double worst = 0;
    printf("  %-6.2f", a);
    for (double g : gaps)
    {
      const double s = sdPct(a, g, W, travel, nullptr);
      printf(" %5.1f", s);
      if (s > worst) worst = s;
    }
    printf("   %5.1f\n", worst);
  }
  printf("\n看哪一行【每一列都不比 alpha=0 差，且快滚列明显变小】。\n");
  return 0;
}
