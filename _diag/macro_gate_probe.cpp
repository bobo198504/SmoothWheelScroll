// Does the macro path actually classify the children it will later drive?
//
// The end-to-end rule is: a "Custom:" action is taken over ONLY when EVERY child passes the ordinary
// classification (ClassifyName / the table) AND is replay-driven on ONE axis with a delivery that can
// be spread over time. That rule is what keeps a macro containing a "one page" scroll -- which jumps a
// whole page per call and ignores the value handed in -- from being animated into a burst of page
// jumps (AGENTS.md 19.8), and (far worse) a macro containing "select next item" from being fired
// dozens of times.
//
// It includes the REAL src/routing.h, and re-implements NOTHING: it feeds candidate action NAMES and
// ids through the same ClassifyName the plugin calls, then applies the same four extra conditions the
// macro path applies. If routing.h's exclusions ever change, this shows it.
//
// g++ -std=c++17 -O2 -I. _diag/macro_gate_probe.cpp -o /tmp/mgp && /tmp/mgp
#include <cstdio>
#include <cstring>
#include "../src/routing.h"

static int failures = 0;
static void Check(bool ok, const char *what)
{
  printf("  %-66s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok)
    ++failures;
}

// Exactly the four conditions MacroChildren applies, on top of ClassifyName.
static const char *WhyRejected(int section, const char *name, ActionSpec &out)
{
  if (!ClassifyName(section, name, out))
    return "not classified at all";
  if (out.drive != DRIVE_REPLAY)
    return "not replay-driven";
  const Delivery d = FilterFor(out);
  if (d != Delivery::kStream && d != Delivery::kStepUnits)
    return "takes a whole notch at once";
  return nullptr; // accepted as a child
}

static void Child(const char *label, int section, const char *name, bool wantAccepted)
{
  ActionSpec out;
  const char *why = WhyRejected(section, name, out);
  const bool accepted = (why == nullptr);
  char line[200];
  if (accepted)
    _snprintf(line, sizeof(line), "%s -> accepted (axis=%d kind=%d)", label, (int)out.axis,
              (int)out.kind);
  else
    _snprintf(line, sizeof(line), "%s -> REJECTED (%s)", label, why);
  Check(accepted == wantAccepted, line);
}

int main()
{
  printf("macro gate: which children a macro may contain, via the ORDINARY classification\n\n");

  printf("-- children the macro path ACCEPTS (a scroll/zoom it can spread out) --\n");
  // The names are the REAL ones REAPER reports: the main view's wheel-driven actions carry the
  // "(MIDI CC relative/mousewheel)" marker, and that marker is what the name rule keys on. (An
  // earlier version of this probe used the SHORT names -- "View: Scroll vertically" -- and they were
  // correctly REFUSED, because those plain actions are the table's, not the name rule's. The probe
  // was wrong, the rule was right.)
  Child("View: Scroll vertically (MIDI CC relative/mousewheel)", 0,
        "View: Scroll vertically (MIDI CC relative/mousewheel)", true);
  Child("View: Zoom horizontally (MIDI CC relative/mousewheel)", 0,
        "View: Zoom horizontally (MIDI CC relative/mousewheel)", true);
  Child("View: Scroll horizontally (MIDI CC relative/mousewheel)", 0,
        "View: Scroll horizontally (MIDI CC relative/mousewheel)", true);
  Child("MIDI editor: Scroll vertically (mousewheel)", 32060,
        "View: Scroll vertically (MIDI CC relative/mousewheel)", true);

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

  printf("-- children it MUST REFUSE (the whole macro is then left to REAPER) --\n");
  Child("View: Scroll view vertically one page (MIDI CC relative/mousewheel)", 0,
        "View: Scroll view vertically one page (MIDI CC relative/mousewheel)", false);
  Child("View: Scroll view vertically one page", 0,
        "View: Scroll view vertically one page", false);
  Child("View: Toggle snap to theme", 0, "View: Toggle snap to theme", false);
  Child("Track: Select next track", 0, "Track: Select next track", false);
  Child("a script / custom name", 0, "Custom: Mega zoom", false);
  Child("View: Zoom vertically (Modify)", 0, "View: Zoom vertically (Modify)", false);
  Child("something with no View", 0, "Item: Nudge left", false);
  Child("a section we do not drive", 9999,
        "View: Scroll vertically (MIDI CC relative/mousewheel)", false);
  // A plain "View:" action with no wheel marker: the table's domain, not the name rule's. A macro
  // made of these cannot be driven by name, so it must be refused rather than silently mis-driven.
  Child("plain View: Scroll vertically (no wheel marker)", 0, "View: Scroll vertically", false);

  printf("\nthe two the user's own macro contains (998, 991): their NAMES are not known here, and\n");
  printf("guessing them is how the wrong rule gets written -- the DEV build logs them instead.\n");
  {
    ActionSpec a;
    // A MEASURED fact, not an assumption: neither id is in the table, so both must come through
    // the name rule, and are therefore refused unless REAPER's own name for them carries the
    // mousewheel marker. (An earlier session note claimed 998 was in the table; it is not.)
    Check(!LookupAction(0, 998, a), "998 is NOT in the table (so it needs the name rule)");
    Check(!LookupAction(0, 991, a), "991 is NOT in the table (so it needs the name rule)");
  }

  printf("\n%s\n", failures ? "FAIL" : "OK: the exclusions (one page, scripts, non-View) hold");
  return failures ? 1 : 0;
}
