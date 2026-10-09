// HWHEEL PROBE -- is the HORIZONTAL wheel a first-class stream?
//
//   g++ -std=c++17 -O2 -I../src -o hwheel_probe.exe hwheel_probe.cpp && ./hwheel_probe.exe
//
// The report (forum, MX Master 3S thumb wheel) said, and this probe originally CONFIRMED against the
// code as it then stood:
//   (a) the TABLE rows for 977/979/988/990 came back with relativeAction = false, so they demanded
//       the wheel latch, unlike the very same actions matched by NAME, which set it true from
//       "mousewheel". The thumb wheel never passes the message hook, so the latch was never armed
//       and every horizontal notch was refused.
//   (b) WM_MOUSEHWHEEL was never watched, so nothing could arm that latch anyway.
//
// Both are now FIXED, and this probe is what keeps them fixed: it asserts the table and the name
// rule AGREE about relativeAction (the disagreement was the bug), and that the hook watches the
// horizontal message. It includes the REAL src/routing.h, so it tests the shipped rule, not a copy.

#include "routing.h"

#include <cstdio>
#include <cstring>

static const char *DelName(Delivery d)
{
  switch (d)
  {
  case Delivery::kStream: return "stream";
  case Delivery::kStepUnits: return "step";
  case Delivery::kImmediate: return "immed";
  }
  return "?";
}

static int g_fail = 0;
static void Check(bool ok, const char *what)
{
  printf("  %-66s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok)
    ++g_fail;
}

struct Row { int section; int command; const char *nm; };

int main()
{
  // The four horizontal actions the report names, each with the name REAPER actually shows.
  static const Row rows[] = {
      {0, 988, "View: Scroll horizontally (MIDI CC relative/mousewheel)"},
      {0, 977, "View: Scroll horizontally reversed (MIDI CC relative/mousewheel)"},
      {0, 990, "View: Zoom horizontally (MIDI CC relative/mousewheel)"},
      {0, 979, "View: Zoom horizontally reversed (MIDI CC relative/mousewheel)"},
  };

  printf("hwheel probe: the horizontal actions, TABLE vs NAME\n\n");
  printf("  cmd | table rel | name rel | name-matched at all\n");
  printf("  ----+-----------+----------+--------------------\n");
  for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
  {
    const Row &r = rows[i];
    ActionSpec byTable;
    ActionSpec byName;
    const bool t = LookupAction(r.section, r.command, byTable);
    const bool n = ClassifyName(r.section, r.nm, byName);
    printf("  %3d |     %d     |    %d     | %s\n", r.command, t ? (byTable.relativeAction ? 1 : 0) : -1,
           n ? (byName.relativeAction ? 1 : 0) : -1, n ? "yes" : "no");
  }

  printf("\n");
  for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
  {
    const Row &r = rows[i];
    ActionSpec byTable;
    ActionSpec byName;
    const bool t = LookupAction(r.section, r.command, byTable);
    const bool n = ClassifyName(r.section, r.nm, byName);
    char buf[128];
    // (a) The fix: the TABLE row now declares itself a relative action too, so the thumb wheel's
    // action is admitted without a latch that a horizontal wheel message can never arm.
    _snprintf(buf, sizeof(buf), "(%d) table row is a relative action (no latch demanded)", r.command);
    Check(t && byTable.relativeAction, buf);
    _snprintf(buf, sizeof(buf), "(%d) the SAME action by NAME agrees (rel = 1)", r.command);
    Check(n && byName.relativeAction, buf);
  }

  // The table and the name rule must not disagree about WHERE the action goes; only about the latch.
  // Only the four horizontal ids are compared here: FilterFor keys the whole-unit delivery on the
  // main VERTICAL ids and on 988/977's ID (not on their axis), so a name-matched vertical action is
  // deliberately NOT the same delivery as the id it happens to name -- see AGENTS.md 105/106.
  printf("\n");
  for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i)
  {
    const Row &r = rows[i];
    ActionSpec byTable;
    ActionSpec byName;
    const bool t = LookupAction(r.section, r.command, byTable);
    const bool n = ClassifyName(r.section, r.nm, byName);
    char buf[128];
    _snprintf(buf, sizeof(buf), "(%d) table and name agree on axis and kind", r.command);
    Check(t && n && byTable.axis == byName.axis && byTable.kind == byName.kind, buf);
  }

  // (b) WM_MOUSEHWHEEL: is it watched anywhere? The plugin source is the evidence.
  printf("\n  message hook: is WM_MOUSEHWHEEL handled?\n");
  FILE *f = fopen("../src/smooth_wheel_scroll.cpp", "rb");
  if (!f)
    f = fopen("src/smooth_wheel_scroll.cpp", "rb");
  if (!f)
  {
    printf("  cannot open the plugin source: skipped\n");
  }
  else
  {
    static char text[4 * 1024 * 1024];
    const size_t got = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[got] = 0;
    const int h = (int)(strstr(text, "WM_MOUSEHWHEEL") ? 1 : 0);
    printf("  WM_MOUSEHWHEEL occurrences in src/smooth_wheel_scroll.cpp: %d\n", h);
    Check(h > 0, "(b) the horizontal wheel message IS watched (so its latch can be armed)");
  }

  printf("\n");
  if (g_fail == 0)
  {
    printf("OK: the horizontal wheel is a first-class stream -- the table declares its actions\n"
           "    relative, the name rule agrees, and the message hook watches WM_MOUSEHWHEEL.\n");
    return 0;
  }
  printf("FAIL: %d check(s) -- the horizontal wheel is not fully wired.\n", g_fail);
  return 1;
}
