// What may a "Custom:" macro contain, and what happens to each child?
//
// THE RULE (AGENTS.md 125). A "Custom:" action is taken over when at least ONE child is a view wheel
// action the plugin can spread over time. Each child then falls into one of three classes:
//
//   DRIVABLE -- it classifies, is replay-driven, and its delivery can be spread. It is ANIMATED.
//   PLAIN    -- it does NOT classify as a view wheel action (an SWS mode toggle, a script, a nested
//               macro, "one page", "select next track", ...). It is invoked exactly ONCE, at the
//               position REAPER itself would run it: before the first drivable child that is when the
//               gesture starts, after it when the gesture ends. Once is what REAPER does when it runs
//               the macro itself, so nothing can be amplified -- which is exactly what the old
//               all-or-nothing rule existed to guarantee, and why the rule could be relaxed.
//   REFUSE   -- it classifies but takes a WHOLE NOTCH at once (Delivery::kImmediate: MIDI editor
//               vertical zoom). Its single execution cannot be reproduced as a spread, so the whole
//               macro is left to REAPER rather than half-driven. Unchanged from before.
//
// A macro with NO drivable child has no gesture to run and is left to REAPER as well.
//
// It includes the REAL src/routing.h, and re-implements NOTHING about classification: it feeds
// candidate action NAMES and sections through the same ClassifyName the plugin calls, then applies the
// same three conditions MacroChildren applies. If routing.h's exclusions ever change, this shows it.
//
// g++ -std=c++17 -O2 -I. _diag/macro_gate_probe.cpp -o /tmp/mgp && /tmp/mgp
#include <cstdio>
#include <cstring>
#include "../src/routing.h"

static int failures = 0;
static void Check(bool ok, const char *what)
{
  printf("  %-70s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok)
    ++failures;
}

enum class Verdict
{
  kDrivable,
  kPlain,
  kRefuse
};

static const char *VerdictName(Verdict v)
{
  switch (v)
  {
  case Verdict::kDrivable: return "DRIVABLE (animated)";
  case Verdict::kPlain: return "PLAIN (run once)";
  default: return "REFUSED";
  }
}

// Exactly what MacroChildren decides per child, on top of ClassifyName.
static Verdict JudgeChild(int section, const char *name, ActionSpec &out, const char **why)
{
  if (why)
    *why = nullptr;
  if (!ClassifyName(section, name, out))
  {
    if (why)
      *why = "not a view wheel action";
    return Verdict::kPlain;
  }
  if (out.drive != DRIVE_REPLAY)
  {
    if (why)
      *why = "not replay-driven";
    return Verdict::kRefuse;
  }
  const Delivery d = FilterFor(out);
  if (d != Delivery::kStream && d != Delivery::kStepUnits)
  {
    if (why)
      *why = "takes a whole notch at once";
    return Verdict::kRefuse;
  }
  return Verdict::kDrivable;
}

// The macro-level rule: refused if any child refuses, or if nothing is drivable.
static bool MacroAccepted(const Verdict *v, int n)
{
  int drivable = 0;
  for (int i = 0; i < n; ++i)
  {
    if (v[i] == Verdict::kRefuse)
      return false;
    if (v[i] == Verdict::kDrivable)
      ++drivable;
  }
  return drivable > 0;
}

static void Child(const char *label, int section, const char *name, Verdict want)
{
  ActionSpec out;
  const char *why = nullptr;
  const Verdict got = JudgeChild(section, name, out, &why);
  char line[300];
  if (got == Verdict::kDrivable)
    _snprintf(line, sizeof(line), "%s -> DRIVABLE (axis=%d kind=%d)", label, (int)out.axis,
              (int)out.kind);
  else
    _snprintf(line, sizeof(line), "%s -> %s (%s)", label, VerdictName(got), why ? why : "?");
  Check(got == want, line);
}

int main()
{
  printf("macro gate: what each child of a macro becomes, via the ORDINARY classification\n\n");

  printf("-- children the macro path ANIMATES (a scroll/zoom it can spread out) --\n");
  // The names are the REAL ones REAPER reports: the main view's wheel-driven actions carry the
  // "(MIDI CC relative/mousewheel)" marker, and that marker is what the name rule keys on. (An
  // earlier version of this probe used the SHORT names -- "View: Scroll vertically" -- and they were
  // correctly refused as drivable, because those plain actions are the table's, not the name rule's.)
  Child("View: Scroll vertically (MIDI CC relative/mousewheel)", 0,
        "View: Scroll vertically (MIDI CC relative/mousewheel)", Verdict::kDrivable);
  Child("View: Zoom horizontally (MIDI CC relative/mousewheel)", 0,
        "View: Zoom horizontally (MIDI CC relative/mousewheel)", Verdict::kDrivable);
  Child("View: Scroll horizontally (MIDI CC relative/mousewheel)", 0,
        "View: Scroll horizontally (MIDI CC relative/mousewheel)", Verdict::kDrivable);
  Child("MIDI editor: Scroll vertically (mousewheel)", 32060,
        "View: Scroll vertically (MIDI CC relative/mousewheel)", Verdict::kDrivable);

  // CROSS-AXIS IS FINE, and must stay fine: "zoom both ways in one action" is the most natural macro
  // shape there is. The axis only picks which glide runs the gesture; each child carries its own
  // section+command, and its own delivery grid is resolved per child.
  printf("  -- a macro mixing the two axes (the common 'zoom both ways' shape) --\n");
  {
    ActionSpec h, v;
    const bool okH = ClassifyName(0, "View: Zoom horizontally (MIDI CC relative/mousewheel)", h);
    const bool okV = ClassifyName(0, "View: Zoom vertically (MIDI CC relative/mousewheel)", v);
    Check(okH && okV, "both a horizontal and a vertical zoom child classify");
    Check(okH && okV && h.axis != v.axis, "they really are on different axes (so this is a real test)");
  }

  printf("-- children that become PLAIN: run exactly ONCE, never animated --\n");
  // This is the class the forum report needed (AGENTS.md 125): a mode toggle wrapped around a zoom.
  // None of these is refused any more -- refusing them was what left such a macro unsmoothed.
  Child("SWS/wol: set horizontal zoom center to Mouse cursor", 0,
        "SWS/wol: Options - Set \"Horizontal zoom center\" to \"Mouse cursor\"", Verdict::kPlain);
  Child("Track: Select next track", 0, "Track: Select next track", Verdict::kPlain);
  Child("View: Scroll view vertically one page (wheel-marked)", 0,
        "View: Scroll view vertically one page (MIDI CC relative/mousewheel)", Verdict::kPlain);
  Child("View: Scroll view vertically one page", 0, "View: Scroll view vertically one page",
        Verdict::kPlain);
  Child("View: Toggle snap to theme", 0, "View: Toggle snap to theme", Verdict::kPlain);
  Child("a script / custom name", 0, "Custom: Mega zoom", Verdict::kPlain);
  Child("View: Zoom vertically (Modify)", 0, "View: Zoom vertically (Modify)", Verdict::kPlain);
  Child("something with no View", 0, "Item: Nudge left", Verdict::kPlain);
  // A plain "View:" action with no wheel marker: the table's domain, not the name rule's. Not
  // drivable by name, so it must not be animated.
  Child("plain View: Scroll vertically (no wheel marker)", 0, "View: Scroll vertically",
        Verdict::kPlain);
  {
    // A section we do not drive: moot in practice (a macro's children live in the macro's own
    // section, which is checked before any child is looked at), but it must never come back DRIVABLE.
    ActionSpec out;
    const char *why = nullptr;
    const Verdict v = JudgeChild(9999, "View: Scroll vertically (MIDI CC relative/mousewheel)", out,
                                 &why);
    Check(v != Verdict::kDrivable, "a child in a section we do not drive is never DRIVABLE");
  }

  printf("-- children that still REFUSE the whole macro (cannot be reproduced as a spread) --\n");
  Child("MIDI editor: Zoom vertically (kImmediate)", 32060,
        "View: Zoom vertically (MIDI CC relative/mousewheel)", Verdict::kRefuse);

  printf("-- the macro-level rule --\n");
  {
    // one page + a real zoom: accepted now, with the page scroll running once.
    const Verdict mixed[2] = {Verdict::kPlain, Verdict::kDrivable};
    Check(MacroAccepted(mixed, 2), "plain + drivable -> macro accepted (the forum report's shape)");
    const Verdict onlyPlain[2] = {Verdict::kPlain, Verdict::kPlain};
    Check(!MacroAccepted(onlyPlain, 2), "no drivable child -> macro left to REAPER");
    const Verdict withRefuse[2] = {Verdict::kDrivable, Verdict::kRefuse};
    Check(!MacroAccepted(withRefuse, 2), "one refusing child -> the whole macro is left to REAPER");
    const Verdict allDrive[2] = {Verdict::kDrivable, Verdict::kDrivable};
    Check(MacroAccepted(allDrive, 2), "all drivable -> accepted, exactly as before");
  }

  printf("\nthe user's first macro (998, 991): their NAMES are not known here, and\n");
  printf("guessing them is how the wrong rule gets written -- the DEV build logs them instead.\n");
  {
    ActionSpec a;
    // A MEASURED fact, not an assumption: neither id is in the table, so both must come through
    // the name rule. (An earlier session note claimed 998 was in the table; it is not.)
    Check(!LookupAction(0, 998, a), "998 is NOT in the table (so it needs the name rule)");
    Check(!LookupAction(0, 991, a), "991 is NOT in the table (so it needs the name rule)");
  }

  printf("\n%s\n", failures ? "FAIL" : "OK: drivable runs animated, plain runs once, refuse still refuses");
  return failures ? 1 : 0;
}
