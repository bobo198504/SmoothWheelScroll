#!/usr/bin/env bash
# Regression gate for the CURVE model (v1.6.0 onward).
#
# The model is no longer the physical one; that one is archived complete under
# _hist/MODEL_1.0/ (its gates included). This gate checks what the curve model PROMISES:
#
#   1. one notch travels the same distance the accepted model did (1.89 units);
#   2. the speed curve is SINGLE-PEAKED at every combination it is asked to draw --
#      that was the whole reason for replacing the physical model, so it is the
#      headline property and a failure here is a real regression;
#   3. the ceiling HOLDS: a long roll's peak never passes the Hold slider's ceiling.
#
# Run:  bash test/check_curve_model.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
export PATH="/d/Projects/Code/_tools/w64devkit/bin:$PATH"

cat > "$ROOT/build/_curve_gate.cpp" <<'CPP'
#include "../src/anim_core.h"
#include <cstdio>
#include <cmath>
#include <vector>
using namespace anim;

struct Prof { double total=0, peak=0; bool single=true; };

static void Run(const Params &P, double gap, int N, Prof *out) {
  Glide g; const double dt = 0.004;
  double t = 0, next = 0, pk = 0; int fired = 0;
  std::vector<double> v;
  for (int i = 0; i < 40000000; ++i) {
    if (fired < N && t + 1e-12 >= next) { g.Kick(15.0, 1.0, 989, t, P); ++fired; next += gap; }
    g.Tick(dt, P); t += dt;
    const double s = std::fabs(g.Velocity());
    if (fired >= N) v.push_back(s);
    if (s > pk) pk = s;
    if (fired >= N && !g.Active()) break;
    if (t > gap * N + 30) break;
  }
  // Single peak: after the highest sample, the speed may not rise again by more than 2%.
  double mx = 0; int at = 0;
  for (int i = 0; i < (int)v.size(); ++i) if (v[i] > mx) { mx = v[i]; at = i; }
  int rises = 0; double prev = v.empty() ? 0 : v[at];
  for (int i = at + 1; i < (int)v.size(); ++i) { if (v[i] > prev * 1.02) ++rises; prev = v[i]; }
  out->total = g.Total(); out->peak = pk; out->single = (rises == 0);
}

static Params Mk(double startPct, double relMs, double riseMs, double hold, double coast,
                 const double bend[5]) {
  Params P; P.startPct = startPct; P.accelPct = 7.0; P.releaseMs = relMs;
  P.hold = hold; P.coast = coast; P.onsetMs = riseMs; P.burstGapMs = 250.0;
  for (int i = 0; i < kSegments; ++i) P.bend[i] = bend[i];
  return P;
}

// The PANEL's shipped knob defaults. "One notch = 1.89" is a feel figure tied to the default
// configuration, and every knob now opens at the MIDDLE of its range (see smooth_wheel_scroll.cpp:
// Start 20..150 ms, Hold knob 1.25..2), so the gate has to measure at those values or it is testing
// a configuration nobody ships. claimBase was re-derived when these moved -- if they move again,
// re-derive it and update these.
const double kRiseDefault = 85.0;      // middle of the Start knob's 20..150 ms
const double kHoldKnobDefault = 1.625; // middle of the Hold knob's 1.25..2
const double kDefBend[5] = {0, 0, kHoldKnobDefault, 0, 0};

int main() {
  const double z[5] = {0, 0, 0, 0, 0};
  int bad = 0;

  // --- 1. one notch = 1.89, the accepted distance -------------------------------
  {
    Prof p; Run(Mk(15, 200, kRiseDefault, 1, 1, kDefBend), 1000.0, 1, &p);
    const bool ok = std::fabs(p.total - 1.89) < 0.02;
    printf("  single notch      %7.3f (want 1.89)   %s\n", p.total, ok ? "OK" : "!! FAIL");
    if (!ok) ++bad;
  }

  // --- 2. single-peaked everywhere ---------------------------------------------
  {
    int fails = 0, cases = 0;
    for (double bend = -0.5; bend <= 0.51; bend += 0.5)
      for (double hold = 0.0; hold <= 2.01; hold += 1.0) {
        // Vary the raw bends of Start/Accel/Coast/Release; Hold keeps its SHIPPED knob value (index 2
        // takes a knob number, not a raw bend, so feeding it -0.5/0.5 would only ever clamp to "no
        // bow" and the stretch would never be exercised).
        double b[5] = {0, bend, kHoldKnobDefault, bend, bend};
        Prof p; Run(Mk(15, 200, kRiseDefault, hold, 1, b), 0.030, 10, &p);
        ++cases; if (!p.single) ++fails;
      }
    printf("  single-peak       %2d/%2d cases       %s\n", cases - fails, cases,
           fails == 0 ? "OK" : "!! FAIL");
    if (fails) ++bad;
  }

  // --- 3. the ceiling holds ----------------------------------------------------
  {
    int fails = 0, cases = 0;
    for (double hold = 0.0; hold <= 2.01; hold += 0.5) {
      const Params P = Mk(15, 200, kRiseDefault, hold, 1, kDefBend);
      Prof p; Run(P, 0.030, 30, &p); // a long roll
      const double ceil = Ceiling(P);
      ++cases;
      if (p.peak > ceil * 1.0001) ++fails; // never passes the ceiling
    }
    printf("  ceiling holds     %2d/%2d cases       %s\n", cases - fails, cases,
           fails == 0 ? "OK" : "!! FAIL");
    if (fails) ++bad;
  }

  // --- 5. a roll gathers: more notches reach a higher speed ---------------------
  // The whole point of the build-up. It was broken once (each notch closed a fraction of the
  // remaining gap, so a roll converged just above a single notch -- measured 6x, where the
  // accepted model reached ~56x) and that read as sluggish. A roll must climb with its length,
  // and a longer roll must beat a shorter one.
  {
    const Params P = Mk(15, 200, kRiseDefault, 1, 1, kDefBend);
    Prof one, ten, thirty;
    Run(P, 1000.0, 1, &one); Run(P, 0.030, 10, &ten); Run(P, 0.030, 30, &thirty);
    const bool grows = (ten.peak > 8.0 * one.peak) && (thirty.peak > ten.peak);
    printf("  roll gathers      10x%5.1f 30x%5.1f    %s\n", ten.peak / one.peak,
           thirty.peak / one.peak, grows ? "OK" : "!! FAIL");
    if (!grows) ++bad;
  }

  // This machine's timers are 1 ms at best and ~15.6 ms when the multimedia timer is
  // unavailable, so the same gesture must measure the same at any step. It did not before
  // the fixed internal grid was added (1.64..1.93 for one notch), which is why this is a
  // gate rather than a note.
  {
    const Params P = Mk(15, 200, kRiseDefault, 1, 1, kDefBend);
    const double dts[] = {0.0005, 0.001, 0.002, 0.004, 0.008, 0.0156, 0.05};
    double lo = 1e9, hi = 0;
    for (int i = 0; i < 7; ++i) {
      Glide g; double t = 0; g.Kick(15.0, 1.0, 989, 0.0, P);
      for (int k = 0; k < 20000000; ++k) {
        g.Tick(dts[i], P); t += dts[i];
        if (!g.Active()) break;
        if (t > 3) break;
      }
      const double v = g.Total();
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }
    const bool ok = (hi - lo) < 1e-6;
    printf("  step-independent  spread %.6f     %s\n", hi - lo, ok ? "OK" : "!! FAIL");
    if (!ok) ++bad;
  }

  printf("\n%s\n", bad ? "!! 曲线模型的门没过，必须查清原因。"
                      : "一致：曲线模型符合它承诺的三条性质。");
  return bad ? 1 : 0;
}
CPP
g++ -std=c++17 -O2 -o "$ROOT/build/_curve_gate.exe" "$ROOT/build/_curve_gate.cpp"
"$ROOT/build/_curve_gate.exe"
rc=$?
rm -f "$ROOT/build/_curve_gate.cpp" "$ROOT/build/_curve_gate.exe"
exit $rc
