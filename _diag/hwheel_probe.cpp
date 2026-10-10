// HWHEEL PROBE -- the horizontal wheel's action-path wiring, and the state of the WHEEL itself.
//
//   g++ -std=c++17 -O2 -I../src -o hwheel_probe.exe hwheel_probe.cpp && ./hwheel_probe.exe
//
// The report (forum, MX Master 3S thumb wheel) said, and part (a) of this probe CONFIRMED against the
// code as it then stood:
//   (a) the TABLE rows for 977/979/988/990 came back with relativeAction = false, so they demanded
//       the wheel latch, unlike the very same actions matched by NAME, which set it true from
//       "mousewheel". That disagreement is a bug in its own right, and it is FIXED (the rows state
//       the flag now) -- this probe keeps it fixed. It is NOT part of the horizontal-wheel feature:
//       it is what lets Shift+vertical-wheel drive 977/988 at all, so it stays even though the
//       feature it was found through does not.
//   (b) WM_MOUSEHWHEEL was never watched. Watching it IS the horizontal-wheel feature: it was written
//       for 1.7.4, held out of that release because nothing was available to measure on, and restored
//       for 1.7.5 after a forum tester reported a tilt/thumb wheel working normally. This probe
//       asserts the handler EXISTS, so removing it by accident fails here.
//
// It includes the REAL src/routing.h, so it tests the shipped rule, not a copy.

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

  // (b) THE MESSAGE HOOK. This checks the SOURCE TEXT, so it must look for the thing that actually
  // WOULD watch the message -- a comparison against it -- and not merely the name appearing somewhere.
  // Two earlier versions of this check were wrong:
  //   * the first searched for the bare string, and when the feature was removed the explanatory
  //     COMMENT kept the string alive, so it reported "watched" for a build that does not watch it;
  //   * the second skipped `//` lines but still counted the DEV log's header TEXT, which legitimately
  //     names both wheel messages ("V for the vertical one, H for the horizontal one").
  // A check a comment or a help string can satisfy is not a check. What is asked now is the message
  // COMPARISON, which only real handler code contains.
  printf("\n  message hook: does any CODE compare against WM_MOUSEHWHEEL?\n");
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

    int compares = 0, mentions = 0;
    for (char *line = strtok(text, "\n"); line; line = strtok(nullptr, "\n"))
    {
      if (!strstr(line, "WM_MOUSEHWHEEL"))
        continue;
      const char *p = line;
      while (*p == ' ' || *p == '\t' || *p == '\r')
        ++p;
      if (p[0] == '/' && p[1] == '/')
        continue; // a comment explains, it does not handle
      ++mentions;
      // The handler form is a comparison: `message == WM_MOUSEHWHEEL`.
      if (strstr(line, "== WM_MOUSEHWHEEL"))
        ++compares;
    }
    printf("  non-comment mentions: %d   of which message COMPARISONS: %d\n", mentions, compares);

    // 1.7.5 SHIPS WITH THE HORIZONTAL WHEEL. The branch was written for 1.7.4, held out of that
    // release because there was no device to measure on, and put back once a forum tester confirmed a
    // tilt/thumb wheel behaves normally. This assertion is what makes its ABSENCE detectable: delete
    // the handler and the probe fails, so the feature cannot be dropped by accident.
    Check(compares > 0, "(b) a handler COMPARES against WM_MOUSEHWHEEL -- as 1.7.5 ships");
    printf("      (the check asks for the comparison, not the name: a comment or a DEV-log help\n");
    printf("       string naming the constant must not be able to satisfy it)\n");
  }

  printf("\n");
  if (g_fail == 0)
  {
    printf("OK: the horizontal wheel is a first-class stream -- the table declares its actions\n"
           "    relative, the name rule agrees, and the message hook watches WM_MOUSEHWHEEL.\n");
    return 0;
  }
  printf("FAIL: %d check(s).\n", g_fail);
  return 1;
}
