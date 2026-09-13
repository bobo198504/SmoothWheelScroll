// Verify ArrangeBarAt's geometry against the real measured rects and the actual wheel
// positions from the debug log, before trusting it in REAPER.
//
// Mirrors the function exactly (same constants, same order).
//
// Build/run:  g++ -O2 _diag/barcheck.cpp -o /tmp/bc && /tmp/bc
#include <windows.h>
#include <stdio.h>

enum class Bar { None, V, H };

// --- mirror of ArrangeBarAt (geometry only) ---
static Bar BarAt(RECT ar, POINT pt)
{
  if (pt.x < ar.left || pt.x >= ar.right || pt.y < ar.top || pt.y >= ar.bottom)
    return Bar::None;
  int vw = GetSystemMetrics(SM_CXVSCROLL);
  int hh = GetSystemMetrics(SM_CYHSCROLL);
  if (vw < 2) vw = 17;
  if (hh < 2) hh = 17;
  --vw;
  --hh;
  const int minLen = 8;
  const bool onV = (pt.x >= ar.right - vw) && ((ar.bottom - ar.top) >= minLen);
  const bool onH = (pt.y >= ar.bottom - hh) && ((ar.right - ar.left) >= minLen);
  if (onV) return Bar::V;
  if (onH) return Bar::H;
  return Bar::None;
}

static const char *Nm(Bar b) { return b == Bar::V ? "VERTICAL" : b == Bar::H ? "HORIZONTAL" : "none"; }

struct C { int x, y; Bar want; const char *why; };

int main(void)
{
  RECT ar = {1786, 621, 3320, 1271}; // measured: ARRANGE = (1786,621)-(3320,1271)
  printf("arrange = (%ld,%ld)-(%ld,%ld), vw=%d hh=%d (band = metric-1)\n\n", ar.left, ar.top,
         ar.right, ar.bottom, GetSystemMetrics(SM_CXVSCROLL),
         GetSystemMetrics(SM_CYHSCROLL));

  // Real positions from the debug log: the first four are ON the bars and produced NO
  // action natively; (3122,858) is arrange interior and DID produce one.
  const C cs[] = {
      {3315, 842, Bar::V, "log: right edge, 5px in -> vertical bar"},
      {3314, 968, Bar::V, "log: right edge, 6px in -> vertical bar"},
      {3306, 808, Bar::V, "log: right edge, 14px in -> still the bar"},
      {3298, 900, Bar::None, "18px in -> past the band (one-pixel-narrow band ends at 3304)"},
      {2827, 1261, Bar::H, "log: bottom edge, 10px up -> horizontal bar"},
      {2827, 1259, Bar::H, "log: bottom edge, 12px up -> still the bar"},
      {2062, 1268, Bar::H, "log: bottom edge, 3px up -> horizontal bar"},
      {3122, 858, Bar::None, "log: ARRANGE INTERIOR -> must NOT be claimed"},
      // geometry edges
      {3319, 1000, Bar::V, "last pixel column"},
      {3303, 1000, Bar::None, "one past the band on the inside"},
      {2000, 1270, Bar::H, "last pixel row"},
      {2000, 1254, Bar::None, "one past the band on the inside"},
      {3319, 1270, Bar::V, "corner belongs to the VERTICAL bar"},
      // outside the view entirely
      {3321, 1000, Bar::None, "right of the view"},
      {2000, 1272, Bar::None, "below the view"},
  };

  int bad = 0;
  for (auto &c : cs)
  {
    Bar got = BarAt(ar, POINT{c.x, c.y});
    bool ok = (got == c.want);
    if (!ok) ++bad;
    printf("%-4s (%4d,%4d) want %-10s got %-10s  %s\n", ok ? "ok" : "FAIL", c.x, c.y,
           Nm(c.want), Nm(got), c.why);
  }
  printf("\n%s\n", bad ? "FAILED" : "all geometry cases match");
  return bad ? 1 : 0;
}
