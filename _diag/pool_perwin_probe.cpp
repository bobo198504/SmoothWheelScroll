// POOL vs PER-WINDOW -- does storing the brake rate per window remove the jitter spike?
//
// Two brakes on the same scripted roll:
//   POOL  -- Tick(P) brakes everything in flight by P.eatRatePerMs (the LAST message's rate)
//   PERWIN-- Tick ignores P.eatRatePerMs for braking; each window is braked by the rate it was fed
//            with, so a later message cannot change an earlier window's brake
//
// The roll is slow and even except for ONE jittery message. A real hand is never exactly even, so this
// is the shape the user's "sudden jump" comes from.
//
//   g++ -std=c++17 -O2 -I../src -o pw_probe.exe pool_perwin_probe.cpp && ./pw_probe.exe
#include "model.h"
#include <cstdio>
#include <cmath>
#include <vector>

static const double kU = 15.0, kDPN = 120.0, kDPU = 8.0;

// A minimal twin of anim3::Glide, with both brake policies, so the two can be compared directly.
struct GlidePair
{
  static const int kN = 2048;
  double amt[kN], age[kN], eat[kN];
  int n = 0;
  void Reset() { n = 0; }

  void Feed(double amount, double windowMs, double eatRatePerMs, double spitMul)
  {
    if (amount == 0.0) return;
    amount *= (spitMul >= 1.0) ? spitMul : 1.0;
    if (amount == 0.0) return;
    if (n < kN) { amt[n] = amount; age[n] = 0.0; eat[n] = eatRatePerMs; ++n; }
  }

  double Tick(double dt, double windowMs, double lastEatRate, bool poolWide)
  {
    const double w = windowMs * 0.001;
    double out = 0.0;
    if (w <= 0.0) { n = 0; return 0.0; }
    const double ms = dt * 1000.0;

    if (poolWide)
    {
      if (lastEatRate > 0.0 && n > 0)
      {
        double inflight = 0.0;
        for (int i = 0; i < n; ++i) inflight += std::fabs(amt[i]);
        if (inflight > 0.0)
        {
          double e = lastEatRate * ms;
          if (e > inflight) e = inflight;
          const double s = (inflight - e) / inflight;
          for (int i = 0; i < n; ++i) amt[i] *= s;
        }
      }
    }
    else
    {
      for (int i = 0; i < n; ++i)
      {
        if (eat[i] <= 0.0) continue;
        const double mag = std::fabs(amt[i]);
        const double e = eat[i] * ms;
        if (e >= mag) amt[i] = 0.0;
        else amt[i] -= (amt[i] < 0.0) ? -e : e;
      }
    }

    int k = 0;
    for (int i = 0; i < n; ++i)
    {
      const double left = w - age[i];
      double pay = amt[i];
      if (left > dt) pay = amt[i] * (dt / left);
      out += pay;
      amt[i] -= pay;
      age[i] += dt;
      if (age[i] < w) { amt[k] = amt[i]; age[k] = age[i]; eat[k] = eat[i]; ++k; }
    }
    n = k;
    return out;
  }
};

struct Out { std::vector<double> perNotch, scale; };

static Out Run(const std::vector<double> &arrivalMs, double X, double R, double V, bool poolWide)
{
  GlidePair g;
  g.Reset();
  const double dt = 0.0005;
  const int n = (int)arrivalMs.size();
  Out o;
  o.perNotch.assign(n, 0.0);
  o.scale.assign(n, 1.0);

  double total = 0.0, t = 0.0;
  int f = 0;
  double lastEat = 0.0;
  std::vector<double> atArrival(n, 0.0);

  while (f < n || g.n > 0)
  {
    if (f < n && arrivalMs[f] / 1000.0 <= t + 1e-12)
    {
      const double gapSec = (f == 0) ? -1.0 : (arrivalMs[f] - arrivalMs[f - 1]) / 1000.0;
      const double scale = model::EatScale(gapSec, kDPN);
      lastEat = (R * scale) / kDPU;
      const double spit = (scale <= 0.0) ? V : 1.0;
      g.Feed(kU, X, lastEat, spit);
      o.scale[f] = scale;
      atArrival[f] = total;
      ++f;
    }
    total += g.Tick(dt, X, lastEat, poolWide) * kDPU;
    t += dt;
    if (t > 60.0) break;
  }
  for (int i = 0; i < n; ++i)
    o.perNotch[i] = atArrival[i] - (i == 0 ? 0.0 : atArrival[i - 1]);
  return o;
}

static void Show(const char *title, const std::vector<double> &a, const Out &o)
{
  printf("%s\n", title);
  printf("  %-6s %-8s %-8s %-12s %s\n", "notch", "gap ms", "scale", "travel", "");
  double maxStep = 0.0;
  for (int i = 0; i < (int)a.size(); ++i)
  {
    const double gap = (i == 0) ? -1.0 : (a[i] - a[i - 1]);
    if (i > 1 && o.perNotch[i - 1] > 1e-9)
      maxStep = std::fmax(maxStep, o.perNotch[i] / o.perNotch[i - 1]);
    printf("  %-6d %-8.0f %-8.3f %-12.2f\n", i + 1, gap, o.scale[i], o.perNotch[i]);
  }
  printf("  worst step between neighbouring notches: %.2fx\n\n", maxStep);
}

int main()
{
  // Overlapping case: a "slow" roll at 70 ms/notch with a 100 ms window -- three windows in flight at
  // once. One message is 15 ms early (natural jitter).
  {
    std::vector<double> a;
    double t = 0;
    for (int i = 0; i < 12; ++i)
    {
      if (i == 5) t += 45.0;
      else if (i > 0) t += 70.0;
      a.push_back(t);
    }
    printf("roll: 12 notches ~70 ms apart (window 100 ms => overlapping), EXCEPT #6 at 45 ms\n");
    printf("X=100 R=15 V=1.0\n\n");
    Show("POOL (old: one rate for the whole pool)", a, Run(a, 100.0, 15.0, 1.0, true));
    Show("PER-WINDOW (new: each window keeps its own rate)", a, Run(a, 100.0, 15.0, 1.0, false));
  }

  // Non-overlapping case for reference.
  {
    std::vector<double> a;
    double t = 0;
    for (int i = 0; i < 12; ++i)
    {
      if (i == 5) t += 90.0;
      else if (i > 0) t += 200.0;
      a.push_back(t);
    }
    printf("roll: 12 notches ~200 ms apart (window 100 ms => NOT overlapping), EXCEPT #6 at 90 ms\n\n");
    Show("POOL", a, Run(a, 100.0, 15.0, 1.0, true));
    Show("PER-WINDOW", a, Run(a, 100.0, 15.0, 1.0, false));
  }
  return 0;
}
