// BEFORE / AFTER: one notch, Fineness (window) = 500 ms.
//
// The user's report: "500 ms" behaved like more than a second, with a stuttering tail.
//
// This runs the SAME one-notch feed through BOTH headers on the same engine and reports how long
// the motion actually lasts and the biggest single step it hands over. Compile twice, once per
// header, with -I pointing at the header under test:
//
//   g++ -std=c++17 -O2 -I../pool_core -o pool_verify.exe pool_verify.cpp   (the pool: my error)
//   g++ -std=c++17 -O2 -I../win_core  -o win_verify2.exe pool_verify.cpp   (the window: restored)
#include "anim3_core.h"
#include <cstdio>
#include <cmath>

using namespace anim3;

static const double kUnitsPerNotch = 15.0; // 7-bit units in one notch

// A 1 ms frame rate, which is what the plugin's multimedia timer actually delivers.
static void runOneNotch(double Dms) {
  Params P; P.durationMs = Dms;
  // (window header) eat nothing: isolate the timing
  
  Glide g; g.Reset();
  const double dt = 0.001;
#if defined(ANIM3_IS_POOL)
  g.Feed(kUnitsPerNotch);
#else
  g.Feed(kUnitsPerNotch, 0.0, P); // no previous message => fully slow, but friction is 0
#endif
  double total = 0, last = 0, maxStep = 0;
  int calls = 0;
  for (double t = 0; t < 10.0; t += dt) {
    const double s = g.Tick(dt, P);
    if (s != 0.0) {
      total += s; last = (t + dt) * 1000.0; ++calls;
      if (fabs(s) > maxStep) maxStep = fabs(s);
    }
    if (!g.Active()) break;
  }
  printf("   Fineness %5.0f ms -> motion over at %7.1f ms   total %8.4f units   %5d calls   "
         "max step %.4f\n", Dms, last, total, calls, maxStep);
}

int main() {
#ifdef ANIM3_IS_POOL
  printf("== POOL (a single total, paid out per frame at dt/d) ==\n");
#else
  printf("== WINDOW (each message its own envelope of exactly durationMs) ==\n");
#endif
  printf("   what each setting PROMISES: the motion lasts exactly that many ms, total 15.0000\n\n");
  for (double D : {10.0, 50.0, 100.0, 300.0, 500.0}) runOneNotch(D);
  return 0;
}
