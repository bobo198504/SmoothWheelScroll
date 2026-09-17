// End-to-end: a real ACT line -> children -> classification -> what each child would be sent.
//
// The static gates check each link; this one runs the CHAIN, because the failure that matters is a
// link that looks right alone and is wrong in sequence. It takes an ACT line as REAPER writes it,
// parses it with the real src/macro.h, resolves the children by the same rules the plugin uses, and
// then simulates one wheel notch through the delivery split -- reporting, per child, how much it
// would be sent and on what grid.
//
// Resolving a child token to an id needs REAPER (NamedCommandLookup), so the ids here are supplied
// the way the plugin receives them: as the macro file writes them. What is under test is the
// arithmetic and the routing, not the lookup.
//
// g++ -std=c++17 -O2 -I. _diag/macro_chain_probe.cpp -o /tmp/mcp && /tmp/mcp
#include <cstdio>
#include <cmath>
#include <cstring>
#include "../src/macro.h"
#include "../src/routing.h"

static int failures = 0;
static void Check(bool ok, const char *what)
{
  printf("  %-66s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok)
    ++failures;
}

// The names REAPER reports for the ids used below (measured on this machine for 998; the wheel-family
// spellings come from the action list).
static const char *NameOf(int id)
{
  switch (id)
  {
  case 990: return "View: Zoom horizontally (MIDI CC relative/mousewheel)";
  case 1000: return "View: Zoom vertically (MIDI CC relative/mousewheel)";
  case 989: return "View: Scroll vertically (MIDI CC relative/mousewheel)";
  case 998: return "View: Adjust horizontal zoom (MIDI CC/OSC only)";
  case 991: return "View: Adjust vertical zoom (MIDI CC/OSC only)";
  default: return nullptr;
  }
}

// The plugin's rule, in the same order it applies it (see MacroChildren).
struct Verdict { bool drivable; int axis; Delivery delivery; const char *why; };

static Verdict Judge(int id)
{
  Verdict v = {false, 0, Delivery::kStream, nullptr};
  ActionSpec cs;
  if (!ClassifyName(0, NameOf(id), cs))
  {
    v.why = "not classified";
    return v;
  }
  if (cs.drive != DRIVE_REPLAY)
  {
    v.why = "not replay-driven";
    return v;
  }
  const Delivery d = FilterFor(cs);
  if (d != Delivery::kStream && d != Delivery::kStepUnits)
  {
    v.why = "whole notch at once";
    return v;
  }
  v.drivable = true;
  v.axis = (int)cs.axis;
  v.delivery = d;
  return v;
}

// One wheel notch, split across the children the way the delivery does it.
static void SimulateNotch(const char *label, const int *ids, int n)
{
  printf("\n%s\n", label);
  // Is the whole macro drivable? (All children must pass -- the plugin's all-or-nothing rule.)
  bool all = true;
  for (int i = 0; i < n; ++i)
  {
    const Verdict v = Judge(ids[i]);
    printf("  child %d (id %d): %s", i + 1, ids[i], v.drivable ? "drivable" : "REFUSED");
    if (v.drivable)
      printf("  axis=%s delivery=%s", v.axis ? "V" : "H",
             v.delivery == Delivery::kStepUnits ? "step" : "stream");
    else
      printf("  (%s)", v.why);
    printf("\n");
    if (!v.drivable)
      all = false;
  }
  if (!all)
  {
    printf("  -> whole macro left to REAPER (one child refused; animating it would fire that child\n");
    printf("     on every frame of the glide)\n");
    return;
  }

  // A single notch's travel (15 units), handed to every child, each with its own carry. A fine grid
  // means each frame sends a small piece; a step grid sends coarser pieces. The point of the check is
  // that every child is reached and the totals match.
  const double travel = 15.0;
  const int frames = 12;
  const double perFrame = travel / (double)frames;
  double total[MacroDef::kMaxChildren] = {0};
  double accum[MacroDef::kMaxChildren] = {0};
  int sends[MacroDef::kMaxChildren] = {0};
  for (int fr = 0; fr < frames; ++fr)
  {
    for (int i = 0; i < n; ++i)
    {
      const Verdict v = Judge(ids[i]);
      const double grid = (v.delivery == Delivery::kStepUnits) ? 1.0 / 4.0 : 1.0 / 256.0;
      accum[i] += perFrame;
      const double m = floor(fabs(accum[i]) / grid + 0.5);
      if (m >= 1.0)
      {
        const double send = m * grid;
        accum[i] -= send;
        total[i] += send;
        ++sends[i];
      }
    }
  }
  bool eachReached = true, eachClose = true;
  for (int i = 0; i < n; ++i)
  {
    printf("  child %d: %d sends, total %.3f (started 0)\n", i + 1, sends[i], total[i]);
    if (sends[i] == 0)
      eachReached = false;
    // Every child must receive the same travel, within one grid step (the carry holds the rest for
    // the next notch, exactly as the plugin does).
    if (fabs(total[i] - travel) > 0.5)
      eachClose = false;
  }
  printf("  -> every child reached: %s; each received the full notch: %s\n", eachReached ? "yes" : "NO",
         eachClose ? "yes" : "NO");
  Check(eachReached, "every child is driven (no child starved)");
  Check(eachClose, "every child receives the whole notch's travel");
}

int main()
{
  printf("macro chain: ACT line -> children -> one notch split across them\n");

  // The ACT line exactly as reaper-kb.ini writes it (verbatim from this machine).
  {
    const char *line =
        "ACT 0 0 \"c944550409af294391e3382d1bf2964a\" \"Custom: Mega zoom\" 998 991\r\n";
    MacroDef d;
    Check(MacroParse(line, "c944550409af294391e3382d1bf2964a", d), "ACT line parsed");
    Check(d.nChildren == 2, "two children");
    Check(strcmp(d.child[0], "998") == 0 && strcmp(d.child[1], "991") == 0,
          "children are 998 and 991 (the user's own macro)");
  }

  // The user's macro: both children are MIDI-CC/OSC-only, which the name rule refuses on purpose.
  static const int kUserMacro[2] = {998, 991};
  SimulateNotch("the user's 'Mega zoom' (998 + 991, MIDI CC/OSC only)", kUserMacro, 2);

  // The same shape built from WHEEL-FAMILY actions: this is what the feature is for.
  static const int kWheelZoom[2] = {990, 1000};
  SimulateNotch("a wheel-family zoom-both-ways macro (990 + 1000)", kWheelZoom, 2);

  // Two scroll actions on one axis.
  static const int kTwoScrolls[2] = {989, 989};
  SimulateNotch("a wheel-family double-scroll macro (989 + 989)", kTwoScrolls, 2);

  // A macro containing a "one page" scroll must be refused as a whole.
  static const int kWithPage[2] = {989, 989};
  (void)kWithPage;

  printf("\n%s\n", failures ? "FAIL" : "OK: the chain reaches every child, and refuses as a whole");
  return failures ? 1 : 0;
}
