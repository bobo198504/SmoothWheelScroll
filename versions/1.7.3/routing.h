#ifndef SWS_ROUTING_H
#define SWS_ROUTING_H

#include <cstddef>
#include <cstring>

// ---------------------------------------------------------------------------
// ROUTING -- "where does an animated amount go, and how is it handed over?"
//
// This is the ONE place that answers both questions. It used to be spread over five spots
// (a command-id switch, the action table's own delivery column, a name-based branch, the
// delivery argument at each call site, and the consumers), which is how the rule could drift
// without anything noticing.
//
// Nothing here touches REAPER or Windows: it is pure data and string rules, so the real code can
// be compiled and diffed outside REAPER (see test/check_routes.sh).
//
// Three layers, kept apart on purpose:
//   * the TABLE      kActionsTbl[]   -- the actions we know by id
//   * the NAME RULE  ClassifyName()  -- everything else, matched on the action's own name
//   * the FILTER     FilterFor()     -- the single rule that turns (section, command, axis, kind)
//                                       into a Delivery
// ---------------------------------------------------------------------------

// How the animated amount reaches the receiver.
enum Drive
{
  DRIVE_NONE = 0,
  DRIVE_REPLAY,   // re-invoke REAPER's own action with fractional relative values
  DRIVE_MCP_WHEEL // hand the mixer a small wheel message and let REAPER's own code move it
};

// WHICH SURFACE the gesture drives.
enum class Axis
{
  kHorizontal = 0,
  kVertical
};

// WHAT the action does. The MIDI editor's vertical axis behaves differently for these two, and
// that is the only place the distinction is needed.
enum class Kind
{
  kScroll = 0,
  kZoom,
  // The action's NAME says neither Scroll nor Zoom (a "mousewheel"-marked View action of some
  // other sort). Its receiver is unknown, so it keeps the ordinary stream -- the conservative
  // default, and exactly what this plugin has always done for such an action.
  kUnknown
};

// HOW the travel is handed to the receiver.
enum class Delivery
{
  kStream,    // continuous relative values, with the normal glide (everything else)
  kStepUnits, // whole 7-bit units over time -- a receiver that steps in whole units and
              // loses anything smaller (MIDI editor vertical scroll; the arrange's own
              // vertical scroll and zoom)
  kImmediate  // the whole notch at once, no glide -- MIDI editor vertical ZOOM,
              // a fixed pixel scale, so coasting has nowhere to land
};

// ---------------------------------------------------------------------------
// The action table: the actions we know by id.
//
// We key on SECTION + COMMAND, never on a key binding, so re-bound and custom actions follow
// automatically. The delivery is NOT stored here: FilterFor() derives it from (section, command,
// axis, kind), so a row cannot disagree with the rule.
//
//   * real scrolling / zooming -> DRIVE_REPLAY. The action takes a relative value continuously
//     (a wheel notch is 15 units), so the glide's fractional travel is handed back in small
//     pieces and the action does the moving.
//   * "one page" scroll actions are deliberately NOT listed: each call jumps a whole page and
//     ignores the relative value, so animating one would either mean driving the view ourselves
//     (forbidden) or replaying it many times (many page jumps). They are inherently discrete, so
//     they pass through untouched and stay entirely REAPER's.
// ---------------------------------------------------------------------------
// Section ids used here: 0 = main, 100 = main alt-recording, 32060 = MIDI editor.
static const int kSectionMain = 0;
static const int kSectionMainAlt = 100;
static const int kSectionMidi = 32060;

struct ActionSpec
{
  int section = 0;          // the section uniqueID the action lives in
  int command = 0;          // the command id
  Axis axis = Axis::kVertical; // which axis' glide the action belongs to
  Kind kind = Kind::kScroll;   // Scroll or Zoom (only the MIDI axis separates them)
  Drive drive = DRIVE_REPLAY;  // how the destination is reached
  // True for a REAPER action whose name mentions "mousewheel". Those are the actions REAPER lets
  // a relative amount drive (mousewheel, MIDI CC, OSC), so they are admitted WITHOUT the wheel
  // latch (see the caller). The table's own rows are all false: they are the canonical ids and
  // keep the latch requirement, exactly as before.
  bool relativeAction = false;
};

// The known-id rows. axis + kind are stated; the delivery is NOT (FilterFor derives it).
static const ActionSpec kActionsTbl[] = {
    // ---- main: scrolling ----
    {kSectionMain, 988, Axis::kHorizontal, Kind::kScroll, DRIVE_REPLAY}, // Scroll horizontally
    {kSectionMain, 977, Axis::kHorizontal, Kind::kScroll, DRIVE_REPLAY}, // Scroll horizontally rev
    {kSectionMain, 989, Axis::kVertical, Kind::kScroll, DRIVE_REPLAY},   // Scroll vertically
    {kSectionMain, 978, Axis::kVertical, Kind::kScroll, DRIVE_REPLAY},   // Scroll vertically rev
    // ---- main: zooming ----
    {kSectionMain, 990, Axis::kHorizontal, Kind::kZoom, DRIVE_REPLAY},   // Zoom horizontally
    {kSectionMain, 979, Axis::kHorizontal, Kind::kZoom, DRIVE_REPLAY},   // Zoom horizontally rev
    {kSectionMain, 1000, Axis::kVertical, Kind::kZoom, DRIVE_REPLAY},    // Zoom vertically
    {kSectionMain, 1001, Axis::kVertical, Kind::kZoom, DRIVE_REPLAY},    // Zoom vertically rev
    // ---- MIDI editor: scroll + zoom through the section's own actions ----
    {kSectionMidi, 40430, Axis::kVertical, Kind::kZoom, DRIVE_REPLAY},   // Zoom vertically
    {kSectionMidi, 40431, Axis::kHorizontal, Kind::kZoom, DRIVE_REPLAY}, // Zoom horizontally
    {kSectionMidi, 40432, Axis::kVertical, Kind::kScroll, DRIVE_REPLAY}, // Scroll vertically
    {kSectionMidi, 40433, Axis::kHorizontal, Kind::kScroll, DRIVE_REPLAY}, // Scroll horizontally
    {kSectionMidi, 40660, Axis::kHorizontal, Kind::kScroll, DRIVE_REPLAY}, // Scroll horiz rev
    {kSectionMidi, 40661, Axis::kVertical, Kind::kScroll, DRIVE_REPLAY},   // Scroll vert rev
    {kSectionMidi, 40662, Axis::kHorizontal, Kind::kZoom, DRIVE_REPLAY},   // Zoom horiz rev
    {kSectionMidi, 40663, Axis::kVertical, Kind::kZoom, DRIVE_REPLAY},     // Zoom vert rev
};

// Look the action up by section + command. Section 100 (main alt-recording) shares the main table.
inline bool LookupAction(int section, int command, ActionSpec &out)
{
  for (size_t i = 0; i < sizeof(kActionsTbl) / sizeof(kActionsTbl[0]); ++i)
  {
    const ActionSpec &a = kActionsTbl[i];
    const bool secMatch = (a.section == section) ||
                          (a.section == kSectionMain && section == kSectionMainAlt);
    if (secMatch && a.command == command)
    {
      out = a;
      return true;
    }
  }
  return false;
}

// Case-insensitive substring test. REAPER names the same idea both "mousewheel" and "Mousewheel",
// so the wheel marker must be matched without regard to case.
inline bool StrHasI(const char *s, const char *sub)
{
  if (!s || !sub)
    return false;
  const size_t n = strlen(sub);
  if (n == 0)
    return true;
  for (; *s; ++s)
  {
    size_t i = 0;
    while (i < n && s[i] &&
           (char)((s[i] >= 'A' && s[i] <= 'Z') ? s[i] + 32 : s[i]) ==
               (char)((sub[i] >= 'A' && sub[i] <= 'Z') ? sub[i] + 32 : sub[i]))
      ++i;
    if (i == n)
      return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// The NAME RULE: everything not in the table is matched on the action's own name.
//
// The action NAME is what we key on, never a key binding, so re-bound and custom actions follow
// automatically.
//
// A second, narrower family is also accepted as a fallback: a plain action whose name mentions the
// wheel and is a Zoom or a Scroll. Three kinds are deliberately excluded from THAT family, because
// each call jumps a whole step regardless of the value handed in, so feeding it many small values
// would multiply the jump:
//   - "snap to theme-defined sizes": each call snaps one size step;
//   - track/envelope "height" adjusters: likewise step-based.
//
// "one page" is excluded from BOTH families: its name carries the mousewheel marker like the
// relative actions do, but that marker only says what may DRIVE it; the page actions still jump a
// whole page per call and ignore the value handed in.
//
// On success, `out` gets section/axis/kind/drive/relativeAction. The caller owns `command`.
// ---------------------------------------------------------------------------
inline bool ClassifyName(int section, const char *nm, ActionSpec &out)
{
  // Only sections whose relative replay path we know how to dispatch (main, and the MIDI editor --
  // the only other section with wheel-driven view actions).
  if (section != kSectionMain && section != kSectionMainAlt && section != kSectionMidi)
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

  out.section = section;
  out.axis = strstr(nm, "horizontally") ? Axis::kHorizontal : Axis::kVertical;
  out.kind = strstr(nm, "Zoom") ? Kind::kZoom
             : strstr(nm, "Scroll") ? Kind::kScroll
                                    : Kind::kUnknown;
  out.drive = DRIVE_REPLAY; // reproduce exactly what the action does
  out.relativeAction = relativeFamily;
  return true;
}

// ---------------------------------------------------------------------------
// THE FILTER -- the ONE rule for how a receiver is fed. It is about HOW the travel is handed
// over, never about HOW MUCH there is (the model owns the amount).
//
// It reproduces this plugin's measured behaviour exactly:
//
//   * the MIDI editor's VERTICAL axis is a discrete receiver -- its SCROLL takes whole units, its
//     ZOOM takes the whole amount at once (a fixed pixel scale with nowhere to coast); its
//     horizontal axes are ordinary fine axes.
//
//   * the MAIN view's VERTICAL SCROLL and ZOOM take whole units. That is keyed on the COMMAND ID
//     (989/978/1000/1001) -- the four the plugin knows -- and NOT on the axis name, which is
//     deliberate: a name-matched "vertical" action that is not one of those four keeps the
//     ordinary stream, exactly as before.
//
//   * the MAIN view's HORIZONTAL PAN (988/977 "Scroll horizontally") takes whole units TOO, and for
//     the same reason. Measured (touchpad AND notched mouse, AGENTS.md 104): at a slow roll the
//     per-frame piece is a fraction of a unit, and this action DISCARDS it -- a single notch moved
//     nothing at all until the roll sped up enough that a piece reached a whole unit. It is a
//     pixel-displacement action, so it cannot act on less than a unit, exactly like the vertical
//     axis. The HORIZONTAL ZOOM (990/979) is NOT changed: a zoom is continuous and shows
//     sub-unit steps, and it was measured to behave correctly on the stream.
//
//   * the MIDI editor's HORIZONTAL axis is the fine stream, both SCROLL and ZOOM. A whole-unit
//     version of the horizontal PAN was tried and REVERTED: this receiver's unit step is large, so
//     one notch (about 3 units) became 2-3 discrete whole-unit jumps -- accurate, but the motion read
//     as stuttering rather than gliding, and the user preferred the continuous (stream) form. See
//     AGENTS.md 106. (The main view's horizontal pan is DIFFERENT: its unit is small, so whole units
//     there both move correctly AND stay fine -- that change is kept.)
//
//   * everything else is the fine stream.
// ---------------------------------------------------------------------------
inline Delivery FilterFor(int section, int command, Axis axis, Kind kind)
{
  if (section == kSectionMidi)
  {
    if (axis == Axis::kHorizontal)
      return Delivery::kStream;
    if (kind == Kind::kZoom)
      return Delivery::kImmediate;
    if (kind == Kind::kScroll)
      return Delivery::kStepUnits;
    return Delivery::kStream; // unknown kind on the MIDI vertical axis
  }
  if (section == kSectionMain || section == kSectionMainAlt)
  {
    switch (command)
    {
    case 989: // View: Scroll vertically
    case 978: // View: Scroll vertically reversed
      return Delivery::kStepUnits;
    // 1000/1001 (vertical ZOOM) is WHOLE UNITS, exactly as 1.6.1 had it and as the user confirmed is
    // correct. A fine stream was tried here (AGENTS.md 109) and REVERTED (AGENTS.md 110): the finest
    // grid makes this axis JITTER -- the receiver cannot act on a sub-unit step, so consecutive values
    // cancel against each other and a slow zoom simply does not move (AGENTS.md 21 measured this same
    // thing years earlier; 109 was a rediscovery of the bug). Its travel is also made constant
    // (FullTravelFor / kZoomStepDeltas), so it never lumps or stalls.
    case 1000: // View: Zoom vertically
    case 1001: // View: Zoom vertically reversed
      return Delivery::kStepUnits;
    case 988: // View: Scroll horizontally
    case 977: // View: Scroll horizontally reversed
      return Delivery::kStepUnits; // a pixel pan: discards sub-unit pieces (see the note above)
    default:
      return Delivery::kStream; // includes horizontal ZOOM (990/979), which is continuous
    }
  }
  return Delivery::kStream;
}

// Convenience: the filter for a resolved spec.
inline Delivery FilterFor(const ActionSpec &s)
{
  return FilterFor(s.section, s.command, s.axis, s.kind);
}

// ---------------------------------------------------------------------------
// FULL TRAVEL -- whether a gesture hands over the WHOLE notch, with no speed attenuation.
//
// The MAIN view's VERTICAL ZOOM does. Every other gesture (all scrolling, and both horizontal axes)
// keeps the normal travel model, where a slow turn moves only a little.
//
// WHY THIS AXIS IS DIFFERENT (measured, AGENTS.md 110): a zoom is a MULTIPLIER, so 1:1 is its natural
// scale -- one notch of wheel, one notch of zoom. The travel model was built to make a slow SCROLL
// feel fine, and on this axis it backfired: a slowly turned notch travelled only ~3 units, spread
// over the ~200 ms window, i.e. ~0.2 units per timer frame. The vertical-zoom receiver cannot act on
// a piece that small (it rounds sub-unit pieces up -- the same finding as AGENTS.md 21), so it went
// "stuck, then rushed through" while a fast roll (which travels the whole notch anyway) was smooth.
//
// Pure function of (section, axis, kind), like FilterFor, so a name-matched vertical zoom is covered
// too and there is ONE place to read the rule.
// ---------------------------------------------------------------------------
inline bool FullTravelFor(int section, int command, Axis axis, Kind kind)
{
  (void)command;
  if (section == kSectionMain || section == kSectionMainAlt)
    return (axis == Axis::kVertical && kind == Kind::kZoom);
  return false;
}

inline bool FullTravelFor(const ActionSpec &s)
{
  return FullTravelFor(s.section, s.command, s.axis, s.kind);
}

#endif // SWS_ROUTING_H
