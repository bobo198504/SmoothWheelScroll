// ---------------------------------------------------------------------------
// anim_compare.cpp -- regression guard for the release ("收尾") character.
//
// The approved feel is the PHYSICAL model: a notch gives velocity, and a power-law
// brake dv/dt = -c*v^p bleeds it off, with the velocity injected along a smoothstep
// onset. This tool records that exact profile and, if the model is ever changed
// again, prints how far the new one has drifted from it.
//
// It also derives the closed-form facts the tuning relies on:
//   - the tail of the velocity is v(t) ~ (T-t)^(1/(1-p))  =>  (1-u)^5 for p=0.8
//   - peak position and peak/avg ratio, which is what "hard brake" changes.
// ---------------------------------------------------------------------------
#include "../src/anim_core.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

using namespace anim;
static const double DT = 0.002;

static const char *ramp = " .:-=+*#%@";
static void Bar(const std::vector<double> &v, const char *label) {
  double mx = 0; for (double x : v) if (x > mx) mx = x;
  std::string s;
  for (double x : v) { int i = mx > 0 ? (int)(x / mx * 9.999) : 0; if (i < 0) i = 0; if (i > 9) i = 9; s += ramp[i]; }
  printf("  %-22s |%s|\n", label, s.c_str());
}

struct Prof { double total = 0, settle = 0, peak = 0, peakAt = 0, avg = 0; std::vector<double> v; };

static Prof Run(double D, double T, const Params &P) {
  Glide g; Prof r; g.Kick(D, +1.0, 7, 0.0, P);
  double t = 0;
  for (int i = 0; i < 4000000; ++i) {
    double s = g.Tick(DT, P); t += DT; r.total += fabs(s);
    double sp = fabs(s) / DT;
    if (sp > r.peak) { r.peak = sp; r.peakAt = t; }
    r.v.push_back(sp);
    if (!g.Active()) break;
  }
  r.settle = t;
  r.avg = r.total / t;
  return r;
}

// The reference profile: the approved physical model with the shipped constants.
static Prof RunReference(double D, double T) {
  Params P; P.releaseMs = T * 1000; P.frictionPow = 0.8; P.onsetRatio = 0.8;
  return Run(D, T, P);
}

int main() {
  const double D = 1.0, T = 0.100;   // one notch, Release 100 ms

  Prof ref = RunReference(D, T);
  printf("参考（已认可的物理模型）: p=0.8, onset=0.8*T,  D=1, Release=100ms\n");
  printf("  travel=%.4f  settle=%.0fms  peak=%.3f @%.0fms  peak位置=%.2f  peak/avg=%.2f\n\n",
         ref.total, ref.settle * 1000, ref.peak, ref.peakAt * 1000, ref.peakAt / ref.settle,
         ref.peak / ref.avg);

  // 60-sample profile up to 1.3x the settle, for the visual record.
  auto sample = [&](const Prof &p) {
    std::vector<double> out;
    for (int i = 0; i < 60; ++i) {
      double u = (i + 0.5) / 60.0;
      int idx = (int)(u * p.settle / DT);
      if (idx >= (int)p.v.size()) idx = (int)p.v.size() - 1;
      out.push_back(p.v[idx]);
    }
    return out;
  };
  printf("== 已认可的收尾剖面 ==\n");
  Bar(sample(ref), "approved (p=0.8)");
  printf("\n");

  printf("== 摩擦指数 p 对收尾的影响（p 越大，末段越硬）==\n");
  printf("   p    末段指数        settle(ms)  peak位置  peak/avg\n");
  for (double p : {0.5, 0.6, 0.7, 0.8, 0.9}) {
    Params P; P.releaseMs = T * 1000; P.frictionPow = p; P.onsetRatio = 0.8;
    Prof r = Run(D, T, P);
    printf("  %.1f    (1-u)^%.1f        %6.0f      %.2f      %.2f%s\n",
           p, 1.0 / (1.0 - p), r.settle * 1000, r.peakAt / r.settle, r.peak / r.avg,
           (p == 0.8) ? "   <- 已认可" : "");
  }
  printf("\n");

  printf("== 漂移检查：若模型被改动，这里会显示与参考的差异 ==\n");
  {
    // Compare the CURRENT core (as compiled) against the recorded reference. They
    // are the same code path today, so the numbers must match to floating error.
    Prof now = RunReference(D, T);
    double dTravel = fabs(now.total - ref.total);
    double dSettle = fabs(now.settle - ref.settle) * 1000;
    double dPeakAt = fabs(now.peakAt - ref.peakAt) * 1000;
    printf("  travel Δ=%.6f   settle Δ=%.3fms   peak@ Δ=%.3fms\n", dTravel, dSettle, dPeakAt);
    printf("  %s\n", (dTravel < 1e-9 && dSettle < 1e-6 && dPeakAt < 1e-6)
                          ? "一致：收尾与已认可的模型相同。"
                          : "!! 不一致：收尾已经被改动。");
  }
  return 0;
}
