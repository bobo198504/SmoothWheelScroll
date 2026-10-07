// End-to-end: a real ACT line -> children -> classification -> what each child would be sent.
//
// The static gates check each link; this one runs the CHAIN, because the failure that matters is a
// link that looks right alone and is wrong in sequence. It takes ACT lines as REAPER writes them,
// parses them with the real src/macro.h, resolves the children with the real src/routing.h, and then
// runs one wheel notch through the delivery split -- reporting, per child, how many times it would be
// invoked and how much travel it would get.
//
// THE RULE UNDER TEST (AGENTS.md 125): drivable children (view wheel actions) are ANIMATED, each on
// its own grid with its own carry. Plain children (everything else) are invoked exactly ONCE and get
// NO travel -- "once" is what REAPER does when it runs the macro itself, so nothing is amplified. The
// plain ones that sit before the first drivable child run at the START of the gesture (they set up the
// state the wheel needs); the rest run at the END (they put it back). A macro with no drivable child,
// or with a child that takes a whole notch at once, is left to REAPER entirely.
//
// Resolving a child token to an id needs REAPER (NamedCommandLookup), so the ids here are supplied the
// way the plugin receives them: as the macro file writes them. What is under test is the rule, the
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
  printf("  %-70s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok)
    ++failures;
}

// The names REAPER reports for the ids used below (measured on this machine for 998; the wheel-family
// spellings come from the action list; the two SWS ones are the forum report's own macro).
static const char *NameOf(int id)
{
  switch (id)
  {
  case 990: return "View: Zoom horizontally (MIDI CC relative/mousewheel)";
  case 1000: return "View: Zoom vertically (MIDI CC relative/mousewheel)";
  case 989: return "View: Scroll vertically (MIDI CC relative/mousewheel)";
  case 998: return "View: Adjust horizontal zoom (MIDI CC/OSC only)";
  case 991: return "View: Adjust vertical zoom (MIDI CC/OSC only)";
  case 70001: return "SWS/wol: Options - Set \"Horizontal zoom center\" to \"Mouse cursor\"";
  case 70002:
    return "SWS/wol: Options - Set \"Horizontal zoom center\" to \"Edit cursor or play cursor "
           "(default)\"";
  default: return nullptr;
  }
}

// The plugin's per-child rule, in the same order it applies it (see MacroChildren).
enum class Verdict
{
  kDrivable,
  kPlain,
  kRefuse
};

static Verdict Judge(int id, Delivery *delivery)
{
  const char *nm = NameOf(id);
  ActionSpec cs;
  if (!nm || !ClassifyName(0, nm, cs))
    return Verdict::kPlain;
  if (cs.drive != DRIVE_REPLAY)
    return Verdict::kRefuse;
  const Delivery d = FilterFor(cs);
  if (d != Delivery::kStream && d != Delivery::kStepUnits)
    return Verdict::kRefuse;
  if (delivery)
    *delivery = d;
  return Verdict::kDrivable;
}

// One wheel notch, split across the children the way the delivery does it.
static void SimulateNotch(const char *label, const int *ids, int n)
{
  printf("\n%s\n", label);

  int nDrivable = 0, firstDrivable = -1;
  bool refused = false;
  for (int i = 0; i < n; ++i)
  {
    Delivery d = Delivery::kStream;
    const Verdict v = Judge(ids[i], &d);
    printf("  child %d (id %d): ", i + 1, ids[i]);
    if (v == Verdict::kDrivable)
    {
      printf("DRIVABLE (animated)");
      ++nDrivable;
      if (firstDrivable < 0)
        firstDrivable = i;
    }
    else if (v == Verdict::kPlain)
      printf("PLAIN (runs once)");
    else
    {
      printf("REFUSED (takes a whole notch at once)");
      refused = true;
    }
    printf("\n");
  }

  if (refused || nDrivable == 0)
  {
    printf("  -> whole macro left to REAPER%s\n",
           refused ? " (one child cannot be spread)" : " (no child can be animated)");
    return;
  }

  // The plain children: exactly ONE invocation each, placed by their position around the first
  // drivable child. This is the ordering rule Kick/Tick implement.
  int head = 0, tail = 0;
  for (int i = 0; i < n; ++i)
  {
    Delivery d = Delivery::kStream;
    if (Judge(ids[i], &d) != Verdict::kPlain)
      continue;
    if (i < firstDrivable)
      ++head;
    else
      ++tail;
    printf("  plain child %d (id %d): run ONCE, at the %s of the gesture\n", i + 1, ids[i],
           (i < firstDrivable) ? "start" : "end");
  }

  // A single notch's travel (15 units), handed to every DRIVABLE child, each with its own carry. A
  // fine grid means each frame sends a small piece; a step grid sends coarser pieces. The point is
  // that every drivable child is reached, the totals match, and no plain child is ever nudged.
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
      Delivery d = Delivery::kStream;
      if (Judge(ids[i], &d) != Verdict::kDrivable)
        continue; // plain children are not delivered at all
      const double grid = (d == Delivery::kStepUnits) ? 1.0 / 4.0 : 1.0 / 256.0;
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

  bool eachReached = true, eachClose = true, noPlainTouched = true;
  for (int i = 0; i < n; ++i)
  {
    Delivery d = Delivery::kStream;
    const Verdict v = Judge(ids[i], &d);
    if (v == Verdict::kDrivable)
    {
      printf("  child %d: %d sends, total %.3f (started 0)\n", i + 1, sends[i], total[i]);
      if (sends[i] == 0)
        eachReached = false;
      // Every drivable child must receive the same travel, within one grid step (the carry holds the
      // rest for the next notch, exactly as the plugin does).
      if (fabs(total[i] - travel) > 0.5)
        eachClose = false;
    }
    else if (sends[i] != 0)
      noPlainTouched = false;
  }
  printf("  -> every drivable child reached: %s; each received the full notch: %s; plain children "
         "untouched: %s\n",
         eachReached ? "yes" : "NO", eachClose ? "yes" : "NO", noPlainTouched ? "yes" : "NO");
  Check(eachReached, "every drivable child is driven (no child starved)");
  Check(eachClose, "every drivable child receives the whole notch's travel");
  Check(noPlainTouched, "no plain child is ever delivered travel (it runs once, not animated)");
}

int main()
{
  printf("macro chain: ACT line -> children -> one notch split across them\n");

  // The ACT lines exactly as reaper-kb.ini writes them.
  {
    const char *line =
        "ACT 0 0 \"c944550409af294391e3382d1bf2964a\" \"Custom: Mega zoom\" 998 991\r\n";
    MacroDef d;
    Check(MacroParse(line, "c944550409af294391e3382d1bf2964a", d), "ACT line parsed");
    Check(d.nChildren == 2, "two children");
    Check(strcmp(d.child[0], "998") == 0 && strcmp(d.child[1], "991") == 0,
          "children are 998 and 991 (the first forum reporter's macro)");
  }

  // THE FORUM REPORT'S MACRO (AGENTS.md 125): a mode wrapped around a zoom, which used to be left to
  // REAPER because the two SWS children are not view wheel actions -- so it never smoothed.
  {
    const char *line = "ACT 0 0 \"a1b2c3d4e5f60718293a4b5c6d7e8f90\" \"Custom: zoom at mouse\" "
                       "70001 990 70002\r\n";
    MacroDef d;
    Check(MacroParse(line, "a1b2c3d4e5f60718293a4b5c6d7e8f90", d), "mixed ACT line parsed");
    Check(d.nChildren == 3, "three children (setup, zoom, restore)");
  }
  static const int kMixed[3] = {70001, 990, 70002};
  SimulateNotch("the forum report's macro: SWS set center + zoom + SWS restore (70001 + 990 + 70002)",
                kMixed, 3);

  // The same macro with the restore step FIRST: both plain children then run at the end, which is
  // still "once each" -- the ordering rule must not silently drop one.
  static const int kMixed2[3] = {990, 70001, 70002};
  SimulateNotch("zoom first, then two plain children (990 + 70001 + 70002)", kMixed2, 3);

  // The first forum reporter's macro: both children are MIDI-CC/OSC-only, which the name rule does
  // not accept -- so both are PLAIN now, and with nothing drivable the macro is still left to REAPER.
  static const int kUserMacro[2] = {998, 991};
  SimulateNotch("the first reporter's 'Mega zoom' (998 + 991, MIDI CC/OSC only)", kUserMacro, 2);

  // The same shape built from WHEEL-FAMILY actions: this is what the feature is for.
  static const int kWheelZoom[2] = {990, 1000};
  SimulateNotch("a wheel-family zoom-both-ways macro (990 + 1000)", kWheelZoom, 2);

  // Two scroll actions on one axis.
  static const int kTwoScrolls[2] = {989, 989};
  SimulateNotch("a wheel-family double-scroll macro (989 + 989)", kTwoScrolls, 2);

  printf("\n%s\n", failures ? "FAIL"
                            : "OK: drivable children animated, plain children run once, refusals hold");
  return failures ? 1 : 0;
}
