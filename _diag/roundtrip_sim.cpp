// Verify the EASE=0 ROUND TRIP: decode the wheel value REAPER hands us, re-encode it the way the
// plugin replays it, and check the two are bit-identical. If they are, "intercept + replay" is
// transparent and ease 0 must feel exactly like the native wheel.
//
//   g++ -std=c++17 -O2 -o roundtrip_sim.exe roundtrip_sim.cpp
//
// The decode is the plugin's (OnAction): raw = val & 0x7f; sign; units = raw>63 ? 128-raw : raw.
// The encode is REAPER's own encode_relmode1_extended, ported verbatim from the SDK
// (third_party/reaper-sdk-git/.../csurf_osc.cpp), which is what the plugin uses too.

#include <cstdio>
#include <cmath>
#include <initializer_list>

// --- the plugin's decode (src/smooth_wheel_scroll.cpp, OnAction) -----------------------------
static void Decode(int val, double *sign, double *units) {
  const int raw = val & 0x7f;
  *sign = (raw > 63) ? -1.0 : 1.0;
  *units = (raw > 63) ? (128 - raw) : raw;
}

// --- REAPER's encode (ported verbatim; same as the plugin's EncodeRel1) ----------------------
static void Encode(double d, int *val, int *valhw) {
  int val7bit = 0, z = 0;
  if (d < 0.0) {
    const double w = std::floor(d);
    if (w >= -64.0) { val7bit = (int)w; z = (int)((d - w) * 256.0 + 0.5); }
    else val7bit = -64;
  } else if (d > 0.0) {
    const double w = std::ceil(d);
    if (w <= 63.0) { val7bit = (int)w; z = (int)((w - d) * 256.0 + 0.5); }
    else val7bit = 63;
  }
  *val = val7bit & 0x7f;
  *valhw = -1 - z;
}

int main() {
  printf("ease = 0 (one delivery). Does decode -> encode return the SAME (val, valhw)?\n");
  printf("A native wheel notch arrives as (val, valhw) below; the plugin must replay precisely that.\n\n");
  printf("  %5s %8s | %5s %8s | %s\n", "in.val", "in.valhw", "out.val", "out.valhw", "result");

  const int inVals[] = {1, 2, 3, 7, 15, 16, 20, 30, 63, 65, 113, 120, 121, 126, 127};
  int bad = 0;
  for (int iv : inVals) {
    double sign, units;
    Decode(iv, &sign, &units);
    // ease 0: the whole amount is delivered at once, so what is replayed is sign*units exactly.
    const double travel = sign * units;
    int ov = 0, ohw = 0;
    Encode(travel, &ov, &ohw);
    const bool ok = (ov == iv) && (ohw == -1);
    if (!ok) ++bad;
    printf("  %5d %8d | %5d %8d | %s  (units=%.0f, travel=%.1f)\n", iv, -1, ov, ohw,
           ok ? "SAME" : "!! DIFFER", units, travel);
  }

  printf("\nAlso: the plugin delivers on a 1/256-unit grid. At ease 0 the amount is exact, so the\n");
  printf("grid must return it unchanged (accum=15 -> 3840 sub-steps -> 15.0):\n");
  for (double u : {1.0, 15.0, 30.0, 63.0}) {
    const double grid = 1.0 / 256.0;
    const double m = std::floor(std::fabs(u) / grid + 0.5);
    const double sent = m * grid;
    printf("   accum=%6.1f -> m=%6.0f -> sent=%6.1f  %s\n", u, m, sent,
           (std::fabs(sent - u) < 1e-9) ? "exact" : "!! off");
    if (std::fabs(sent - u) >= 1e-9) ++bad;
  }

  printf("\n%s\n", bad ? "!! NOT a clean round trip -- ease 0 would differ from native."
                      : "CLEAN: ease 0 replays exactly what the wheel reported (val,valhw) = (n,-1).");
  return bad ? 1 : 0;
}
