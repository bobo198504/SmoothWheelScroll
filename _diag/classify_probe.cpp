// Regression probe for the name-based classifier's "one page" exclusion.
//
// The classifier in src/smooth_wheel_scroll.cpp needs REAPER's API, so the string rule
// is mirrored here -- twice: as it was BEFORE the fix and as it is AFTER. Both are run
// over a corpus of action names and the results are diffed.
//
// The claim being tested is narrow and checkable: the fix changes the decision for
// "one page" actions and for NOTHING else. Anything else showing up as a difference is a
// regression against the documented binding rules (AGENTS.md section 2).
//
// Build/run:  g++ -std=c++17 -O2 -I../src -o cp _diag/classify_probe.cpp && ./cp
#include "routing.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <vector>
#include <string>

// The BEFORE copy needs its own case-insensitive test; the AFTER path uses the one in routing.h.
static bool StrHasIOld(const char *s, const char *sub)
{
  const size_t n = strlen(sub);
  for (; *s; ++s)
  {
    size_t i = 0;
    while (i < n && s[i] && tolower((unsigned char)s[i]) == tolower((unsigned char)sub[i]))
      ++i;
    if (i == n)
      return true;
  }
  return false;
}

// BEFORE the fix: "one page" was excluded only in the non-mousewheel family, so the
// mousewheel-marked page actions were driven.
static bool InScopeOld(const char *nm)
{
  if (!nm || !*nm) return false;
  if (!strstr(nm, "View")) return false;
  const bool relativeFamily = StrHasIOld(nm, "mousewheel");
  if (!relativeFamily)
  {
    if (!strstr(nm, "wheel")) return false;
    if (!strstr(nm, "Zoom") && !strstr(nm, "Scroll")) return false;
    if (strstr(nm, "one page") || strstr(nm, "snap to theme") ||
        strstr(nm, "height") || strstr(nm, "Modify"))
      return false;
  }
  return true;
}

// AFTER the fix: "one page" is excluded from both families, up front.
//
// This now delegates to the REAL rule in src/routing.h (the same ClassifyName the plugin calls),
// so the probe cannot drift from the shipped behaviour -- which is how an earlier hand-copied
// mirror managed to pass while asserting the wrong thing (AGENTS.md 70).
static bool InScopeNew(const char *nm)
{
  ActionSpec out;
  // Section 0 (main) is representative here: the probe only exercises the name rule, and the
  // name rule's admission decision does not depend on which admitted section it is asked about.
  return ClassifyName(kSectionMain, nm, out);
}

int main()
{
  // Real REAPER action names covering every branch of the rule: the mousewheel family,
  // the page/snap/height/modify step actions, the fallback family, and names that are
  // out of scope. Both spellings of the marker appear because REAPER uses both.
  const char *corpus[] = {
    // mousewheel family -- continuous, must stay driven
    "View: Scroll vertically (MIDI CC relative/mousewheel)",
    "View: Scroll vertically reversed (MIDI CC relative/mousewheel)",
    "View: Zoom vertically (MIDI CC relative/mousewheel)",
    "View: Zoom vertically reversed (MIDI CC relative/mousewheel)",
    "View: Scroll horizontally (MIDI CC relative/mousewheel)",
    "View: Scroll horizontally reversed (MIDI CC relative/mousewheel)",
    "View: Zoom horizontally (MIDI CC relative/mousewheel)",
    "View: Zoom horizontally reversed (MIDI CC relative/mousewheel)",
    "View: Scroll vertically (mousewheel)",
    "View: Zoom horizontally (Mousewheel)",
    // the reported bug: must flip from driven to passed through
    "View: Scroll view vertically one page (MIDI CC relative/mousewheel)",
    "View: Scroll view vertically one page reversed (MIDI CC relative/mousewheel)",
    "View: Scroll view horizontally one page (MIDI CC relative/mousewheel)",
    // step actions WITHOUT the marker -- excluded before and after
    "View: Scroll view vertically one page",
    "View: Scroll view vertically one page reversed",
    "View: Snap to theme-defined track heights",
    "View: Adjust selected track heights",
    // the mousewheel-marked variants of those step actions: documented as bound by
    // design (AGENTS.md section 2), so they must NOT change
    "View: Adjust selected track heights (MIDI CC relative/mousewheel)",
    "View: Snap to next theme-defined track height (MIDI CC relative/mousewheel)",
    // out of scope
    "Transport: Play",
    "View: Show docker",
    "Track: Solo track",
    "View: Toggle track zoom to maximum height",
    "",
  };

  int diffs = 0, unexpected = 0;
  printf("%-7s %-7s  %s\n", "before", "after", "action name");
  for (size_t i = 0; i < sizeof(corpus) / sizeof(corpus[0]); ++i)
  {
    const char *nm = corpus[i];
    const bool a = InScopeOld(nm), b = InScopeNew(nm);
    if (a == b)
      continue;
    ++diffs;
    const char *kind = strstr(nm, "one page") ? "expected (one page)" : "UNEXPECTED";
    if (!strstr(nm, "one page"))
      ++unexpected;
    printf("%-7s %-7s  [%s] %s\n", a ? "drive" : "pass", b ? "drive" : "pass", kind,
           *nm ? nm : "(empty)");
  }

  printf("\ndifferences: %d\n", diffs);
  if (unexpected)
  {
    printf("FAIL: %d difference(s) outside \"one page\" -- the fix is too broad\n",
           unexpected);
    return 1;
  }
  printf("OK: the fix changes \"one page\" actions only\n");

  // The headline case, asserted directly so the probe fails loudly if it ever regresses.
  const char *bug = "View: Scroll view vertically one page (MIDI CC relative/mousewheel)";
  if (InScopeNew(bug))
  {
    printf("FAIL: the reported action is still driven\n");
    return 1;
  }
  printf("OK: \"%s\" is left to REAPER\n", bug);
  return 0;
}
