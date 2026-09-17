// The macro parser: does it find a Custom: action's children, and does it refuse everything else?
//
// This matters because the children ARE the feature: they are what decides whether a macro may be
// driven by the plugin (all of them must be actions it knows) or must be left to REAPER. A parser
// that found the wrong children, or that matched the wrong line, would make the plugin treat an
// unrelated action as a macro -- and if such a macro contained, say, "select next item", animating it
// would fire that action dozens of times.
//
// It includes the REAL src/macro.h, not a copy (the project's rule), and uses lines copied verbatim
// from this machine's reaper-kb.ini, including the ones with named children and the MIDI-editor one.
//
// g++ -std=c++17 -O2 -I. _diag/macro_parse_probe.cpp -o /tmp/mpp && /tmp/mpp
#include <cstdio>
#include <cstring>
#include "../src/macro.h"

static int failures = 0;
static void Check(bool ok, const char *what)
{
  printf("  %-64s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok)
    ++failures;
}

// Verbatim from reaper-kb.ini on this machine.
static const char *kIni =
    "SCR 4 0 RS1ec9ab1b4ff08bc6f116768e9e126fc3fe67a812 \"Custom: mpl_Align takes.lua\" \"MPL Scripts/Various/mpl_Align takes.lua\"\r\n"
    "ACT 0 0 \"16dcba7b806c9d4c804482ceaf7e479c\" \"Custom: play item and select next\" 41558 _XENAKIOS_TIMERTEST1 _SWS_ITEMCUSTCOL2 40290 _SWS_SELNEXTITEM 40285\r\n"
    "ACT 0 32060 \"9a6fbf67a30cfa47a21f281f2ebc4b78\" \"Custom: show CC lanes and set height\" _FNG_ME_SHOW_USED_CC_LANES _BR_ME_SET_CC_LANES_HEIGHT_110\r\n"
    "ACT 0 0 \"c944550409af294391e3382d1bf2964a\" \"Custom: Mega zoom\" 998 991\r\n"
    "KEY 255 14576 _c944550409af294391e3382d1bf2964a 0\t\t # Main : Win+Mousewheel : Custom: Mega zoom\r\n";

int main()
{
  printf("macro parser: finds the right line, the right children, and nothing else\n\n");

  // --- the two-child scroll/zoom macro (the one the user reported) ---
  {
    MacroDef d;
    Check(MacroParse(kIni, "c944550409af294391e3382d1bf2964a", d), "found the Mega zoom macro");
    Check(d.found, "reported as found");
    Check(d.section == 0, "section read as 0 (main)");
    Check(d.nChildren == 2, "two children");
    Check(d.nChildren == 2 && strcmp(d.child[0], "998") == 0, "child 0 is 998");
    Check(d.nChildren == 2 && strcmp(d.child[1], "991") == 0, "child 1 is 991");
    Check(!d.overflow, "no overflow");
  }

  // --- named children and a different section ---
  {
    MacroDef d;
    Check(MacroParse(kIni, "16dcba7b806c9d4c804482ceaf7e479c", d), "found the item macro");
    Check(d.nChildren == 6, "six children");
    Check(strcmp(d.child[0], "41558") == 0, "child 0 is 41558");
    Check(strcmp(d.child[1], "_XENAKIOS_TIMERTEST1") == 0, "child 1 kept as a named token");
    Check(strcmp(d.child[5], "40285") == 0, "last child is 40285");

    MacroDef m;
    Check(MacroParse(kIni, "9a6fbf67a30cfa47a21f281f2ebc4b78", m), "found the MIDI macro");
    Check(m.section == 32060, "MIDI section read as 32060");
    Check(m.nChildren == 2, "MIDI macro has two children");
  }

  // --- must NOT match: a wrong guid, a random guid, empty ---
  {
    MacroDef d;
    Check(!MacroParse(kIni, "00000000000000000000000000000000", d), "an unknown guid matches nothing");
    Check(!d.found, "and is reported as not found");
    MacroDef e;
    Check(!MacroParse(kIni, "", e), "an empty guid matches nothing");
    MacroDef f;
    Check(!MacroParse("", "c944550409af294391e3382d1bf2964a", f), "empty text matches nothing");

    // A SCR line mentions a guid-shaped string and a "Custom:" name; it must not be taken for an ACT.
    MacroDef g;
    Check(!MacroParse("SCR 4 0 RS1ec9ab1b4ff08bc6f116768e9e126fc3fe67a812 \"x\" \"y\"\r\n",
                      "1ec9ab1b4ff08bc6f116768e9e126fc3fe67a812", g),
          "a SCR line is not a macro");
  }

  // --- case-insensitive guid, and CRLF handling ---
  {
    MacroDef d;
    Check(MacroParse(kIni, "C944550409AF294391E3382D1BF2964A", d), "guid compared case-insensitively");
  }

  // --- the last line of a file with no trailing newline still parses ---
  {
    const char *noEol = "ACT 0 0 \"abcdef\" \"Custom: x\" 989";
    MacroDef d;
    Check(MacroParse(noEol, "abcdef", d), "a final line with no newline parses");
    Check(d.nChildren == 1 && strcmp(d.child[0], "989") == 0, "its single child is read");
  }

  // --- a line with too many children is flagged, not silently truncated into a wrong macro ---
  {
    const char *many = "ACT 0 0 \"abcdef\" \"Custom: big\" 1 2 3 4 5 6 7 8 9 10";
    MacroDef d;
    Check(MacroParse(many, "abcdef", d), "the over-long macro is still found");
    Check(d.overflow, "overflow flagged (so the caller can refuse it)");
    Check(d.nChildren == 10, "the true child count is reported");
  }

  printf("\n%s\n", failures ? "FAIL" : "OK: parses the right children and refuses everything else");
  return failures ? 1 : 0;
}
