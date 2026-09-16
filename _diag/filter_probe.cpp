// Regression probe for the FILTER rule, now running the REAL code.
//
// It used to hand-copy the rule, which is how it managed to pass while asserting a rule that did
// not match the plugin (AGENTS.md 70). It now includes src/routing.h and calls the same FilterFor
// the plugin calls, so it cannot drift from the shipped rule.
//
// WHAT THE RULE ACTUALLY IS:
//   * the MIDI editor's VERTICAL axis: SCROLL -> whole units, ZOOM -> immediate, HORIZONTAL -> stream
//   * the MAIN section's VERTICAL actions 989/978/1000/1001 -> whole units
//   * the MAIN section's HORIZONTAL PAN 988/977 -> whole units (a pixel pan discards sub-unit
//     pieces; measured on both a touchpad and a notched mouse -- AGENTS.md 104)
//   * everything else -- including the horizontal ZOOM (990/979) and name-only matches -> stream
//
// Build/run:  g++ -std=c++17 -O2 -I../src -o fp _diag/filter_probe.cpp && ./fp
#include "routing.h"

#include <cstdio>
#include <cstring>

static const char *DelName(Delivery d)
{
  return d == Delivery::kStream ? "stream" : d == Delivery::kStepUnits ? "step" : "immed";
}

static Axis AxisOfName(const char *nm)
{
  return strstr(nm, "horizontally") ? Axis::kHorizontal : Axis::kVertical;
}
static Kind KindOfName(const char *nm)
{
  return strstr(nm, "Zoom") ? Kind::kZoom
         : strstr(nm, "Scroll") ? Kind::kScroll
                                : Kind::kUnknown;
}

struct Case { int section; int command; const char *nm; };

// The expected filter, stated directly and independently of the implementation.
static Delivery Expect(int section, int command, Axis ax, Kind kd)
{
  if (section == kSectionMidi)
  {
    // MIDI horizontal is the fine stream (a whole-unit try was reverted -- AGENTS.md 106).
    if (ax == Axis::kHorizontal)
      return Delivery::kStream;
    return (kd == Kind::kZoom) ? Delivery::kImmediate : Delivery::kStepUnits;
  }
  // Main view: the FOUR known VERTICAL ids (scroll and zoom) and the two HORIZONTAL PAN ids are
  // whole-unit receivers. Everything else -- the horizontal ZOOM, and any action matched only by name
  // -- is the fine stream. (The vertical zoom went to the stream in AGENTS.md 109 and was REVERTED in
  // 110: a fine grid makes this axis jitter and a slow zoom not move at all.)
  if (command == 989 || command == 978 || command == 1000 || command == 1001)
    return Delivery::kStepUnits;
  if (command == 988 || command == 977)
    return Delivery::kStepUnits; // a pixel pan discards sub-unit pieces (AGENTS.md 104)
  return Delivery::kStream;
}

int main()
{
  static const Case corpus[] = {
      // main section, by id
      {0, 988, "View: Scroll horizontally (MIDI CC relative/mousewheel)"},
      {0, 977, "View: Scroll horizontally reversed (MIDI CC relative/mousewheel)"},
      {0, 989, "View: Scroll vertically (MIDI CC relative/mousewheel)"},
      {0, 978, "View: Scroll vertically reversed (MIDI CC relative/mousewheel)"},
      {0, 990, "View: Zoom horizontally (MIDI CC relative/mousewheel)"},
      {0, 979, "View: Zoom horizontally reversed (MIDI CC relative/mousewheel)"},
      {0, 1000, "View: Zoom vertically (MIDI CC relative/mousewheel)"},
      {0, 1001, "View: Zoom vertically reversed (MIDI CC relative/mousewheel)"},
      // main section, by name (id outside the four)
      {0, 12345, "View: Scroll vertically (mousewheel)"},
      {0, 12346, "View: Zoom vertically (mousewheel)"},
      {0, 12347, "View: Scroll horizontally (mousewheel)"},
      {0, 12348, "View: Zoom horizontally (mousewheel)"},
      {0, 12349, "View: Adjust selected track heights (MIDI CC relative/mousewheel)"},
      {0, 12350, "View: Snap to next theme-defined track height (MIDI CC relative/mousewheel)"},
      {0, 12352, "View: Some other thing (MIDI CC relative/mousewheel)"},
      // MIDI editor
      {32060, 40430, "View: Zoom vertically (MIDI CC relative/mousewheel)"},
      {32060, 40431, "View: Zoom horizontally (MIDI CC relative/mousewheel)"},
      {32060, 40432, "View: Scroll vertically (MIDI CC relative/mousewheel)"},
      {32060, 40433, "View: Scroll horizontally (MIDI CC relative/mousewheel)"},
      {32060, 40660, "View: Scroll horizontally reversed (MIDI CC relative/mousewheel)"},
      {32060, 40661, "View: Scroll vertically reversed (MIDI CC relative/mousewheel)"},
      {32060, 40662, "View: Zoom horizontally reversed (MIDI CC relative/mousewheel)"},
      {32060, 40663, "View: Zoom vertically reversed (MIDI CC relative/mousewheel)"},
      // main alt-recording section shares the main table
      {100, 989, "View: Scroll vertically (MIDI CC relative/mousewheel)"},
      {100, 1000, "View: Zoom vertically (MIDI CC relative/mousewheel)"},
  };

  int bad = 0;
  printf("%-7s | %-22s | %s\n", "section", "command / name", "filter");
  for (size_t i = 0; i < sizeof(corpus) / sizeof(corpus[0]); ++i)
  {
    const Case &c = corpus[i];
    const Axis ax = AxisOfName(c.nm);
    const Kind kd = KindOfName(c.nm);
    const Delivery after = FilterFor(c.section, c.command, ax, kd);
    const Delivery expect = Expect(c.section, c.command, ax, kd);
    const bool ok = (after == expect);
    if (!ok) ++bad;
    printf("%-7d | %-22.22s | %-9s %s\n", c.section, c.nm, DelName(after), ok ? "" : "<- WRONG");
  }

  // Cross-checks stated as invariants (not tied to a specific id list).
  if (FilterFor(kSectionMain, 989, Axis::kVertical, Kind::kScroll) != Delivery::kStepUnits) ++bad;
  // The vertical zoom is whole units TOO (as 1.6.1 had it; a fine stream was tried and reverted).
  if (FilterFor(kSectionMain, 1000, Axis::kVertical, Kind::kZoom) != Delivery::kStepUnits) ++bad;
  if (FilterFor(kSectionMain, 1001, Axis::kVertical, Kind::kZoom) != Delivery::kStepUnits) ++bad;
  // ★ THE HORIZONTAL PAN IS WHOLE UNITS, the horizontal ZOOM is the fine stream (AGENTS.md 104):
  //   a pixel pan discards sub-unit pieces, a zoom shows them.
  if (FilterFor(kSectionMain, 988, Axis::kHorizontal, Kind::kScroll) != Delivery::kStepUnits) ++bad;
  if (FilterFor(kSectionMain, 977, Axis::kHorizontal, Kind::kScroll) != Delivery::kStepUnits) ++bad;
  if (FilterFor(kSectionMain, 990, Axis::kHorizontal, Kind::kZoom) != Delivery::kStream) ++bad;
  if (FilterFor(kSectionMain, 979, Axis::kHorizontal, Kind::kZoom) != Delivery::kStream) ++bad;
  // The MIDI horizontal axis is the fine stream, BOTH scroll and zoom (a whole-unit try for the
  // pan was reverted -- AGENTS.md 106).
  if (FilterFor(kSectionMidi, 40433, Axis::kHorizontal, Kind::kScroll) != Delivery::kStream) ++bad;
  if (FilterFor(kSectionMidi, 40660, Axis::kHorizontal, Kind::kScroll) != Delivery::kStream) ++bad;
  if (FilterFor(kSectionMidi, 40431, Axis::kHorizontal, Kind::kZoom) != Delivery::kStream) ++bad;
  // A vertical action matched only by NAME (not one of the four ids) keeps the fine stream.
  if (FilterFor(kSectionMain, 12345, Axis::kVertical, Kind::kScroll) != Delivery::kStream) ++bad;
  // The track panel and the MIDI vertical scroll share ONE filter; only MIDI vertical zoom differs.
  if (FilterFor(kSectionMain, 989, Axis::kVertical, Kind::kScroll) !=
      FilterFor(kSectionMidi, 40432, Axis::kVertical, Kind::kScroll))
    ++bad;

  // ★ FULL TRAVEL: only the MAIN view's VERTICAL ZOOM hands over the whole notch (AGENTS.md 110).
  if (!FullTravelFor(kSectionMain, 1000, Axis::kVertical, Kind::kZoom)) ++bad;
  if (!FullTravelFor(kSectionMain, 1001, Axis::kVertical, Kind::kZoom)) ++bad;
  if (!FullTravelFor(kSectionMainAlt, 1000, Axis::kVertical, Kind::kZoom)) ++bad;
  // A name-matched vertical zoom (not one of the ids) gets it too -- the rule is about the axis/kind.
  if (!FullTravelFor(kSectionMain, 12346, Axis::kVertical, Kind::kZoom)) ++bad;
  // No other axis/kind, and no other section:
  if (FullTravelFor(kSectionMain, 990, Axis::kHorizontal, Kind::kZoom)) ++bad;
  if (FullTravelFor(kSectionMain, 989, Axis::kVertical, Kind::kScroll)) ++bad;
  if (FullTravelFor(kSectionMain, 1000, Axis::kVertical, Kind::kScroll)) ++bad;
  if (FullTravelFor(kSectionMidi, 40430, Axis::kVertical, Kind::kZoom)) ++bad;
  if (FullTravelFor(kSectionMain, 1000, Axis::kHorizontal, Kind::kZoom)) ++bad;

  printf("\n%s\n", bad ? "FAIL: the filter rule does not match the intended one"
                       : "OK: main vertical (the four ids) and the main horizontal pan = whole "
                         "units; horizontal zoom, and name-only matches "
                         "= the fine stream (the MIDI pan's DRIVE is separate -- AGENTS.md 106); "
                         "full travel only for the main vertical zoom (110)");
  return bad ? 1 : 0;
}
