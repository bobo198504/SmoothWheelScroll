// LEGACY RULE SNAPSHOT -- the "before" side of the unification.
//
//   g++ -std=c++17 -O2 -o legacy_rules.exe legacy_rules.cpp && ./legacy_rules.exe
//
// This is a FAITHFUL transcription of the three places 1.6.1 decides delivery and routing:
//   1. kActions[]           (src/smooth_wheel_scroll.cpp:826-855) -- the known-action table
//   2. ClassifyByName       (src/smooth_wheel_scroll.cpp:917-976) -- the name-based fallback
//   3. DeliveryForCommand   (src/smooth_wheel_scroll.cpp:586-600) -- the main-section vertical rule
//
// It is frozen as test/expected/routes_before.txt and later diffed against the REAL routing.h, so
// the unification is proven to change nothing. It deliberately mirrors the quirks:
//   * the table hard-codes delivery for some rows and leaves others at kStream;
//   * DeliveryForCommand keys on the COMMAND ID only (989/978/1000/1001), not on axis or name;
//   * the name classifier keys on section + "horizontally" + Scroll/Zoom.
//
// Deterministic output only -- no timers, no pointers, no REAPER.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---- 1.6.1's enums, verbatim ------------------------------------------------------------
enum Drive
{
  DRIVE_NONE = 0,
  DRIVE_REPLAY,
  DRIVE_MCP_WHEEL
};
enum class Delivery
{
  kStream,
  kStepUnits,
  kImmediate
};

struct ActionSpec
{
  int section;
  int command;
  bool horizontal;
  Drive drive;
  bool relativeAction;
  Delivery delivery;
};

// ---- 1.6.1's kActions[], verbatim (fields: section, command, horizontal, drive, relative, delivery)
static const ActionSpec kActions[] = {
    {0, 988, true, DRIVE_REPLAY, false, Delivery::kStream},
    {0, 977, true, DRIVE_REPLAY, false, Delivery::kStream},
    {0, 989, false, DRIVE_REPLAY, false, Delivery::kStepUnits},
    {0, 978, false, DRIVE_REPLAY, false, Delivery::kStepUnits},
    {0, 990, true, DRIVE_REPLAY, false, Delivery::kStream},
    {0, 979, true, DRIVE_REPLAY, false, Delivery::kStream},
    {0, 1000, false, DRIVE_REPLAY, false, Delivery::kStepUnits},
    {0, 1001, false, DRIVE_REPLAY, false, Delivery::kStepUnits},
    {32060, 40430, false, DRIVE_REPLAY, false, Delivery::kImmediate},
    {32060, 40431, true, DRIVE_REPLAY, false, Delivery::kStream},
    {32060, 40432, false, DRIVE_REPLAY, false, Delivery::kStepUnits},
    {32060, 40433, true, DRIVE_REPLAY, false, Delivery::kStream},
    {32060, 40660, true, DRIVE_REPLAY, false, Delivery::kStream},
    {32060, 40661, false, DRIVE_REPLAY, false, Delivery::kStepUnits},
    {32060, 40662, true, DRIVE_REPLAY, false, Delivery::kStream},
    {32060, 40663, false, DRIVE_REPLAY, false, Delivery::kImmediate},
};

static bool LookupAction(int section, int command, ActionSpec &out)
{
  for (size_t i = 0; i < sizeof(kActions) / sizeof(kActions[0]); ++i)
  {
    const ActionSpec &a = kActions[i];
    const bool secMatch = (a.section == section) || (a.section == 0 && section == 100);
    if (secMatch && a.command == command)
    {
      out = a;
      return true;
    }
  }
  return false;
}

static bool StrHasI(const char *s, const char *sub)
{
  if (!s || !sub)
    return false;
  const size_t n = strlen(sub);
  if (n == 0)
    return true;
  for (; *s; ++s)
  {
    size_t i = 0;
    while (i < n && s[i] && (char)((s[i] >= 'A' && s[i] <= 'Z') ? s[i] + 32 : s[i]) ==
                                 (char)((sub[i] >= 'A' && sub[i] <= 'Z') ? sub[i] + 32 : sub[i]))
      ++i;
    if (i == n)
      return true;
  }
  return false;
}

// ---- 1.6.1's DeliveryForCommand(), verbatim ---------------------------------------------
static Delivery DeliveryForCommand(int section, int command)
{
  if (section != 0 && section != 100)
    return Delivery::kStream;
  switch (command)
  {
  case 989:
  case 978:
  case 1000:
  case 1001:
    return Delivery::kStepUnits;
  default:
    return Delivery::kStream;
  }
}

// ---- 1.6.1's ClassifyByName(), reduced to its pure-string decision ----------------------
// Returns false when the name is not admitted; on true, fills the spec exactly as 1.6.1 does.
static bool ClassifyName(int sid, const char *nm, ActionSpec &out)
{
  if (sid != 0 && sid != 100 && sid != 32060)
    return false;
  if (!nm || !*nm)
    return false;
  if (!strstr(nm, "View"))
    return false;
  if (strstr(nm, "one page"))
    return false;
  const bool relativeFamily = StrHasI(nm, "mousewheel");
  if (!relativeFamily)
  {
    if (!strstr(nm, "wheel"))
      return false;
    if (!strstr(nm, "Zoom") && !strstr(nm, "Scroll"))
      return false;
    if (strstr(nm, "snap to theme") || strstr(nm, "height") || strstr(nm, "Modify"))
      return false;
  }
  out.section = sid;
  out.command = -1; // the caller's command; set below
  out.horizontal = strstr(nm, "horizontally") != nullptr;
  out.drive = DRIVE_REPLAY;
  out.relativeAction = relativeFamily;
  out.delivery = Delivery::kStream;
  if ((sid == 32060) && !out.horizontal)
  {
    if (strstr(nm, "Scroll"))
      out.delivery = Delivery::kStepUnits;
    else if (strstr(nm, "Zoom"))
      out.delivery = Delivery::kImmediate;
  }
  if ((sid == 0 || sid == 100) && !out.horizontal)
    out.delivery = DeliveryForCommand(sid, out.command < 0 ? 0 : out.command);
  return true;
}

// ---- the resolver, in 1.6.1's ORDER: table first, then name -----------------------------
static bool Resolve(int section, int command, const char *nm, ActionSpec &out)
{
  if (LookupAction(section, command, out))
    return true;
  if (nm && ClassifyName(section, nm, out))
  {
    out.command = command;
    // 1.6.1 re-runs the delivery rule with the real command at the END of ClassifyByName; do the
    // same here so the result is identical (the main-section branch above used command 0).
    out.delivery = Delivery::kStream;
    if ((section == 32060) && !out.horizontal)
    {
      if (strstr(nm, "Scroll"))
        out.delivery = Delivery::kStepUnits;
      else if (strstr(nm, "Zoom"))
        out.delivery = Delivery::kImmediate;
    }
    if ((section == 0 || section == 100) && !out.horizontal)
      out.delivery = DeliveryForCommand(section, command);
    return true;
  }
  return false;
}

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
static const char *DriveName(Drive d)
{
  switch (d)
  {
  case DRIVE_NONE: return "none";
  case DRIVE_REPLAY: return "replay";
  case DRIVE_MCP_WHEEL: return "mcp";
  }
  return "?";
}

// ---- corpus: every table row, plus every name shape the classifier could see -------------
struct Case { int section; int command; const char *nm; };

int main()
{
  static const Case corpus[] = {
      // --- the table's own rows, with the names REAPER gives them ---
      {0, 988, "View: Scroll horizontally (MIDI CC relative/mousewheel)"},
      {0, 977, "View: Scroll horizontally reversed (MIDI CC relative/mousewheel)"},
      {0, 989, "View: Scroll vertically (MIDI CC relative/mousewheel)"},
      {0, 978, "View: Scroll vertically reversed (MIDI CC relative/mousewheel)"},
      {0, 990, "View: Zoom horizontally (MIDI CC relative/mousewheel)"},
      {0, 979, "View: Zoom horizontally reversed (MIDI CC relative/mousewheel)"},
      {0, 1000, "View: Zoom vertically (MIDI CC relative/mousewheel)"},
      {0, 1001, "View: Zoom vertically reversed (MIDI CC relative/mousewheel)"},
      {100, 989, "View: Scroll vertically (MIDI CC relative/mousewheel)"},
      {100, 1000, "View: Zoom vertically (MIDI CC relative/mousewheel)"},
      {32060, 40430, "View: Zoom vertically (MIDI CC relative/mousewheel)"},
      {32060, 40431, "View: Zoom horizontally (MIDI CC relative/mousewheel)"},
      {32060, 40432, "View: Scroll vertically (MIDI CC relative/mousewheel)"},
      {32060, 40433, "View: Scroll horizontally (MIDI CC relative/mousewheel)"},
      {32060, 40660, "View: Scroll horizontally reversed (MIDI CC relative/mousewheel)"},
      {32060, 40661, "View: Scroll vertically reversed (MIDI CC relative/mousewheel)"},
      {32060, 40662, "View: Zoom horizontally reversed (MIDI CC relative/mousewheel)"},
      {32060, 40663, "View: Zoom vertically reversed (MIDI CC relative/mousewheel)"},
      // --- main section, by NAME, ids NOT in the table (the classifier's job) ---
      {0, 12345, "View: Scroll vertically (mousewheel)"},
      {0, 12346, "View: Zoom vertically (mousewheel)"},
      {0, 12347, "View: Scroll horizontally (mousewheel)"},
      {0, 12348, "View: Zoom horizontally (mousewheel)"},
      {0, 12349, "View: Adjust selected track heights (MIDI CC relative/mousewheel)"},
      {0, 12350, "View: Snap to next theme-defined track height (MIDI CC relative/mousewheel)"},
      {0, 12351, "View: Modify something (MIDI CC relative/mousewheel)"},
      {0, 12352, "View: Some other thing (MIDI CC relative/mousewheel)"},
      // --- "one page" must be refused from both families ---
      {0, 12353, "View: Scroll view vertically one page (MIDI CC relative/mousewheel)"},
      {0, 12354, "View: Scroll view vertically one page reversed (MIDI CC relative/mousewheel)"},
      {0, 12355, "View: Scroll view horizontally one page (MIDI CC relative/mousewheel)"},
      // --- MIDI editor, by name, ids not in the table ---
      {32060, 12360, "View: Scroll vertically (MIDI CC relative/mousewheel)"},
      {32060, 12361, "View: Zoom vertically (MIDI CC relative/mousewheel)"},
      {32060, 12362, "View: Scroll horizontally (MIDI CC relative/mousewheel)"},
      {32060, 12363, "View: Zoom horizontally (MIDI CC relative/mousewheel)"},
      {32060, 12364, "View: Some other thing (MIDI CC relative/mousewheel)"},
      // --- other sections must be refused entirely ---
      {7, 989, "View: Scroll vertically (MIDI CC relative/mousewheel)"},
      {999, 12365, "View: Scroll vertically (mousewheel)"},
  };

  printf("section | command | name                                         | matched drive  horiz rel  delivery\n");
  printf("--------+---------+----------------------------------------------+--------- ------ ----- ---- --------\n");
  for (size_t i = 0; i < sizeof(corpus) / sizeof(corpus[0]); ++i)
  {
    const Case &c = corpus[i];
    ActionSpec s;
    memset(&s, 0, sizeof(s));
    const bool matched = Resolve(c.section, c.command, c.nm, s);
    if (!matched)
    {
      printf("%7d | %7d | %-44.44s | REFUSE\n", c.section, c.command, c.nm);
      continue;
    }
    printf("%7d | %7d | %-44.44s | yes     %-6s %s   %s   %s\n", c.section, c.command, c.nm,
           DriveName(s.drive), s.horizontal ? "H" : "V", s.relativeAction ? "1" : "0",
           DelName(s.delivery));
  }
  return 0;
}
