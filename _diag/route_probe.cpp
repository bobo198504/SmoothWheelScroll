// ROUTE PROBE -- runs the REAL routing rule (src/routing.h) over the same corpus as
// _diag/legacy_rules.cpp, so the two can be diffed byte for byte.
//
//   g++ -std=c++17 -O2 -I../src -o route_probe.exe route_probe.cpp && ./route_probe.exe
//
// test/check_routes.sh compares this output with test/expected/routes_before.txt and requires them
// to be IDENTICAL. That is the proof the unification changed no decision.

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

struct Case { int section; int command; const char *nm; };

int main()
{
  // The SAME corpus as legacy_rules.cpp -- keep the two in step if either is edited.
  static const Case corpus[] = {
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
      {0, 12345, "View: Scroll vertically (mousewheel)"},
      {0, 12346, "View: Zoom vertically (mousewheel)"},
      {0, 12347, "View: Scroll horizontally (mousewheel)"},
      {0, 12348, "View: Zoom horizontally (mousewheel)"},
      {0, 12349, "View: Adjust selected track heights (MIDI CC relative/mousewheel)"},
      {0, 12350, "View: Snap to next theme-defined track height (MIDI CC relative/mousewheel)"},
      {0, 12351, "View: Modify something (MIDI CC relative/mousewheel)"},
      {0, 12352, "View: Some other thing (MIDI CC relative/mousewheel)"},
      {0, 12353, "View: Scroll view vertically one page (MIDI CC relative/mousewheel)"},
      {0, 12354, "View: Scroll view vertically one page reversed (MIDI CC relative/mousewheel)"},
      {0, 12355, "View: Scroll view horizontally one page (MIDI CC relative/mousewheel)"},
      {32060, 12360, "View: Scroll vertically (MIDI CC relative/mousewheel)"},
      {32060, 12361, "View: Zoom vertically (MIDI CC relative/mousewheel)"},
      {32060, 12362, "View: Scroll horizontally (MIDI CC relative/mousewheel)"},
      {32060, 12363, "View: Zoom horizontally (MIDI CC relative/mousewheel)"},
      {32060, 12364, "View: Some other thing (MIDI CC relative/mousewheel)"},
      {7, 989, "View: Scroll vertically (MIDI CC relative/mousewheel)"},
      {999, 12365, "View: Scroll vertically (mousewheel)"},
  };

  printf("section | command | name                                         | matched drive  horiz rel  delivery\n");
  printf("--------+---------+----------------------------------------------+--------- ------ ----- ---- --------\n");
  for (size_t i = 0; i < sizeof(corpus) / sizeof(corpus[0]); ++i)
  {
    const Case &c = corpus[i];
    ActionSpec s;
    bool matched = LookupAction(c.section, c.command, s);
    if (matched)
    {
      s.command = c.command; // the table does not carry it in the row literal form? it does; keep it
      s.command = c.command;
    }
    else
    {
      matched = ClassifyName(c.section, c.nm, s);
      if (matched)
        s.command = c.command;
    }
    if (!matched)
    {
      printf("%7d | %7d | %-44.44s | REFUSE\n", c.section, c.command, c.nm);
      continue;
    }
    const Delivery d = FilterFor(s);
    printf("%7d | %7d | %-44.44s | yes     %-6s %s   %s   %s\n", c.section, c.command, c.nm,
           DriveName(s.drive), s.axis == Axis::kHorizontal ? "H" : "V",
           s.relativeAction ? "1" : "0", DelName(d));
  }
  return 0;
}
