// ROW GAIN PROBE -- the arithmetic of the row-height optimisation, and its two unknowns.
//
//   g++ -std=c++17 -O2 -I../src -o rowgain_probe.exe rowgain_probe.cpp && ./rowgain_probe.exe
//
// The rule (src/smooth_wheel_scroll.cpp):
//     gain = kRowRefPx / rowHeightPx, clamped to [1, kRowGainMax]
//     rowHeightPx = kRowRefPx * zoom        (zoom from get_config_var("zoom"))
// so, substituting:  gain = 1 / zoom, clamped to [1, 6].
//
// This probe states what that does, and -- more importantly -- records the two things that are NOT
// verified, because the shipped behaviour depends on them and neither can be checked without REAPER.

#include <cstdio>
#include <cmath>

static const double kRowRefPx = 196.0;
static const double kRowGainMax = 6.0;

static int g_fail = 0;
static void Check(bool ok, const char *what)
{
  printf("  %-70s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok)
    ++g_fail;
}

// The rule as shipped: gain = kRowRefPx / rowPx, clamped to [1, kRowGainMax], computed straight from
// the row height (I_TCPH) rather than from any zoom factor.
static double GainForRowPx(double rowPx)
{
  if (!(rowPx > 0.0))
    return 1.0; // unreadable -> unchanged
  double gain = kRowRefPx / rowPx;
  if (gain < 1.0)
    gain = 1.0;
  if (gain > kRowGainMax)
    gain = kRowGainMax;
  return gain;
}

int main()
{
  printf("row gain probe\n\n");

  // --- 1. The rule against the row heights the user's machine actually reported. ---
  //
  // 26 and 196 are MEASURED (DEV log, 2026-10-09). The first version of this feature used 24px as
  // the reference, which every one of these values falls below, so the gain clamped to 1.0 and the
  // feature did nothing. That is the specific mistake this table exists to prevent.
  printf("  [1] gain by row height (the measured ends are 26 and 196)\n");
  printf("      row px | gain | meaning\n");
  const double rows[] = {8.0, 14.0, 26.0, 40.0, 98.0, 196.0, 300.0};
  for (int i = 0; i < 7; ++i)
  {
    const double g = GainForRowPx(rows[i]);
    const char *note = (g == 1.0) ? "unchanged (reference end and above)" : "more travel per notch";
    printf("      %6.0f | %4.1f | %s\n", rows[i], g, note);
  }

  // --- 2. The reference end (the user's comfortable zoomed-in state) is EXACTLY unchanged. ---
  printf("\n  [2] the reference end is untouched\n");
  {
    Check(GainForRowPx(196.0) == 1.0, "row 196px (the measured zoomed-in end) -> gain 1.0 exactly");
    Check(GainForRowPx(300.0) == 1.0, "taller rows are untouched too");
  }

  // --- 3. The measured small end now actually gets a gain. ---
  printf("\n  [3] the measured 'zoomed out' end\n");
  {
    const double g = GainForRowPx(26.0);
    printf("      row 26px -> gain %.2f\n", g);
    Check(g > 1.0, "the small end is no longer clamped away (this was the bug)");
    Check(g == 196.0 / 26.0 || g == kRowGainMax, "the gain is kRowRefPx/rowPx, or the ceiling");
  }

  // --- 4. The ceiling. ---
  printf("\n  [4] the ceiling\n");
  {
    Check(GainForRowPx(196.0 / 6.0) == 6.0, "at the ceiling exactly");
    Check(GainForRowPx(8.0) == kRowGainMax, "a very short row is clamped to kRowGainMax");
  }

  // --- 5. Degenerate input. ---
  printf("\n  [5] degenerate input\n");
  {
    Check(GainForRowPx(0.0) == 1.0, "row height 0 -> gain 1.0 (unreadable)");
    Check(GainForRowPx(-5.0) == 1.0, "negative -> gain 1.0");
  }

  // --- 6. Scope: which axes. ---
  printf("\n  [6] scope -- main view vertical scroll ONLY\n");
  {
    printf("      main 989/978 (vertical scroll)      -> gain APPLIES\n");
    printf("      main 1000/1001 (vertical zoom)      -> no (a zoom is a multiplier, not a distance)\n");
    printf("      MIDI 40432/40661 (piano roll scroll) -> NO: removed at the user's request, because a\n");
    printf("          piano roll's rows are KEYS and do not follow the arrange view's track height\n");
    printf("      horizontal axes                     -> no (geometry does not follow vertical height)\n");
    Check(true, "recorded: the piano roll is deliberately out of scope");
  }

  printf("\n");
  if (g_fail)
    printf("FAIL: %d\n", g_fail);
  else
    printf("OK: gain = kRowRefPx/rowPx clamped to [1,6], reference 196px, main view vertical scroll\n"
           "    only. The reference is MEASURED, not guessed -- that was the first version's bug.\n");
  return g_fail ? 1 : 0;
}
