// VERIFY THE RESTORED WINDOW MODEL, using the plugin's own anim3_core.h.
//
//   g++ -std=c++17 -O2 -I../src -o win_verify.exe win_verify.cpp
//
// The user's complaint was that "500 ms" was really seconds, with a stuttering tail. That is what a
// POOL paid out per frame does: `durationMs` becomes a time constant. The window model promises an
// EXACT duration, frame-rate independent. This proves both:
//
//   A. a lone notch, Fineness 500, Kinetic 120: when does the motion stop, and what is the total?
//   B. the same, sampled at 1 / 4 / 15.6 ms: the answer must not depend on the frame rate.
//   C. Kinetic: a slow turn vs a fast roll, in deltas handed over per notch.

#include "anim3_core.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace anim3;

static const double kUnitsPerNotch = 15.0; // 7-bit units in one notch
static const double kDeltasPerNotch = 120.0;

// Run one gesture and report the total handed over and the last moment anything was handed over.
struct Res { double total; double lastMs; int calls; double maxStep; };

// One notch (Kinetic 120 => nothing held back), window D, sampled every dtMs.
static Res loneNotch(double Dms, double dtMs) {
  Params P; P.durationMs = Dms;
  Glide g; g.Reset();
  const double dt = dtMs / 1000.0;
  const double travel = 1.0 * kUnitsPerNotch;
  // Feed at t=0 with "no previous message" (gap -1 => fully slow). Kinetic 120 keeps everything.
  double keep = TravelFraction(-1.0, kDeltasPerNotch, kDeltasPerNotch);
  g.Feed(travel * keep, P);
  Res r{0, 0, 0, 0};
  for (double t = 0; t < 5.0; t += dt) {
    double s = g.Tick(dt, P);
    if (s != 0.0) { r.total += s; r.lastMs = (t + dt) * 1000.0; ++r.calls;
      if (fabs(s) > r.maxStep) r.maxStep = fabs(s); }
    if (!g.Active()) break;
  }
  return r;
}

// A roll: n notches, one every gapMs, window D, Kinetic k. Reports total handed over.
static double roll(int n, double gapMs, double Dms, double kinetic) {
  Params P; P.durationMs = Dms;
  Glide g; g.Reset();
  const double dt = 0.001, gap = gapMs / 1000.0;
  double out = 0, msgTime = 0, now = 0;
  int fed = 0;
  const double top = n * gap + Dms / 1000.0 + 0.2;
  for (double t = 0; t < top; t += dt) {
    now = t;
    while (fed < n && msgTime <= t + 1e-9) {
      const double gp = (fed == 0) ? -1.0 : gap;   // first message: no previous gap
      const double keep = TravelFraction(gp, kDeltasPerNotch, kinetic);
      g.Feed(1.0 * kUnitsPerNotch * keep, P);
      ++fed; msgTime += gap;
    }
    out += g.Tick(dt, P);
  }
  return out;
}

int main() {
  printf("== A. lone notch, Fineness 500 ms, Kinetic 120 (nothing held back) ==\n");
  printf("   the model promises: total %.4f units, motion over in exactly %.0f ms\n\n",
         kUnitsPerNotch, 500.0);
  for (double dt : {1.0, 4.0, 15.6}) {
    Res r = loneNotch(500.0, dt);
    printf("   dt=%5.1f ms -> total %8.4f units (%6.2f%% of a notch), last at %7.1f ms, "
           "%3d calls, max step %.4f\n",
           dt, r.total, 100.0 * r.total / kUnitsPerNotch, r.lastMs, r.calls, r.maxStep);
  }

  printf("\n== B. same, other windows (dt=1 ms) ==\n");
  for (double D : {10.0, 50.0, 100.0, 300.0, 500.0}) {
    Res r = loneNotch(D, 1.0);
    printf("   Fineness %5.0f ms -> last at %7.1f ms, %4d calls, total %.4f\n",
           D, r.lastMs, r.calls, r.total);
  }

  printf("\n== C. Kinetic: what a notch moves, slow vs fast (window 50 ms) ==\n");
  printf("   %-8s", "kinetic");
  const double gaps[] = {400, 200, 100, 50, 20};
  for (double gp : gaps) printf("  %5.0fms", gp);
  printf("\n");
  for (double k : {1.0, 12.0, 40.0, 120.0}) {
    printf("   %-8.0f", k);
    for (double gp : gaps) {
      const double keep = TravelFraction(gp / 1000.0, kDeltasPerNotch, k);
      printf("  %7.2f", kDeltasPerNotch * keep); // deltas this notch moves
    }
    printf("\n");
  }
  printf("   (cells are DELTAS moved by one notch; 120 = the whole notch)\n");
  return 0;
}
