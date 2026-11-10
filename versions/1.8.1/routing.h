#ifndef SWS_ROUTING_H
#define SWS_ROUTING_H

#include <cstddef>
#include <cstring>
#include <cmath>

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

// ---------------------------------------------------------------------------
// PER-SURFACE SPEED: eight knobs, one per (view, direction, kind), multiplying the travel before it
// enters the model (forum request: "even at quickest settings the vertical scroll still feels very
// sluggish").
//
// WHICH EIGHT, and why exactly these:
//
//   1. Main view  PAN  vertical    (scroll: 989/978)
//   2. Main view  PAN  horizontal  (scroll: 988/977)
//   3. Main view  ZOOM vertical    (1000/1001)
//   4. Main view  ZOOM horizontal  (990/979)
//   5. MIDI editor PAN  horizontal  (40433/40660)
//   6. MIDI editor ZOOM vertical    (40430/40663)
//   7. MIDI editor ZOOM horizontal  (40431/40662)
//   8. Mixer PAN                    (DRIVE_MCP_WHEEL -- the TCP/MCP surfaces)
//
// WHY SEPARATE BY VIEW: PAN and ZOOM are different receivers (one moves the view, the other changes a
// multiplier), and the main view and the MIDI editor are used at different scales -- one number
// cannot serve both.
//
// WHY SEPARATE BY DIRECTION: the AXIS is what a receiver quantizes, and the two axes of one view are
// different receivers. A vertical pan steps whole track rows and a horizontal one steps pixels (see
// FilterFor), so a notch of each moves a different distance and feels differently sluggish.
//
// ★ WHY MIDI VERTICAL PAN HAS NO KNOB (40432/40661). This is a measured conclusion, not an oversight.
// The axis delivers in whole 7-bit units (Delivery::kStepUnits), so a bigger factor does not produce
// more sends -- it produces the SAME number of sends, each one BIGGER. Measured with the knob at
// 3.00x: amount rose from about 8 units per notch to 26, and the log shows single sends of val=-7 to
// -14 (7 to 14 whole units at once), i.e. half a pitch row per call. REAPER's MIDI scroll is
// quantized to a whole pitch row (AGENTS 17), so it does move further -- but it moves in fewer,
// coarser strides rather than faster, which reads as "the knob does nothing". A knob that cannot do
// what its label promises is worse than no knob, so the axis is left alone at its 1.0x behaviour.
// The correct fix would be a finer delivery on that axis, which AGENTS 17 already measured as causing
// stutter; do not retry it casually.
//
// WHERE THE FACTOR IS APPLIED: to the TRAVEL, before the window model, NOT to the delivered
// values. That matters -- it means
//   * the glide shape is unchanged (a factor scales the motion, it does not re-time it),
//   * everything downstream keeps working: the row-height gain, both delivery grids, the
//     end-of-gesture top-up, macros,
//   * a factor of exactly 1.0 leaves every number bit-for-bit as it was, so the defaults are
//     the behaviour this plugin has always shipped.
enum class Surface
{
  kMainPanV = 0,
  kMainPanH,
  kMainZoomV,
  kMainZoomH,
  kMidiPanH, // NOT kMidiPanV: see the note above on why MIDI vertical pan has no knob
  kMidiZoomV,
  kMidiZoomH,
  kMixerPan,
  kCount
};

// The knob range, and the fact that 1.0x sits at the MIDDLE of the dial.
//
// The two are in tension: 0.5..3.0 does not have 1.0 in the middle linearly (that would be 1.75), and
// the user wants both -- the full 0.5..3.0 range AND the default at twelve o'clock, so the dial reads
// as "centred = no change". The solution is a PIECEWISE LOGARITHMIC dial position: the left half of
// the travel runs 0.5x -> 1.0x and the right half runs 1.0x -> 3.0x. Each half is logarithmic, so
// equal movements of the knob are equal RATIOS, and the two halves are independent (the left spans a
// factor of 2, the right a factor of 3) -- which is exactly what the values asked for imply.
//
// The value itself stays a plain multiplicative factor: only the MAPPING between knob position and
// value is nonlinear. Everything downstream sees the same 0.5..3.0 number it always would.
//
// Clamped, never rejected: a stored value outside the range is pulled to the nearest end (the panel
// and the loader both go through ClampSurfaceSpeed).
static const double kSurfaceSpeedMin = 0.5;
static const double kSurfaceSpeedMax = 3.0;
static const double kSurfaceSpeedDefault = 1.0;
static const int kSurfaceCount = (int)Surface::kCount;

inline double ClampSurfaceSpeed(double v)
{
  if (!(v == v)) // NaN
    return kSurfaceSpeedDefault;
  if (v < kSurfaceSpeedMin)
    return kSurfaceSpeedMin;
  if (v > kSurfaceSpeedMax)
    return kSurfaceSpeedMax;
  return v;
}

// ★ POSITION <-> VALUE for the surface dials, with kSurfaceSpeedDefault at exactly 500 -- twelve
// o'clock, the pointer straight up.
//
// THE DIAL IS PIECEWISE LOGARITHMIC. The left half of the travel runs 0.5x -> 1.0x and the right half
// runs 1.0x -> 3.0x, each half logarithmic so equal knob movements are equal RATIOS. That is what
// makes both things the user asked for true at once: the range is the full 0.5..3.0, and the default
// sits in the middle. A linear dial cannot do both -- with 0.5..3.0 linear, 1.0 is at 20% of the
// travel, not the middle.
//
// The VALUE stays a plain factor; only this mapping is nonlinear.
inline int SurfaceKnobPosFromValue(double v)
{
  v = ClampSurfaceSpeed(v);
  double p;
  if (v >= kSurfaceSpeedDefault)
  {
    const double span = kSurfaceSpeedMax / kSurfaceSpeedDefault; // 3.0 over 500..1000
    p = 500.0 + 500.0 * (log(v / kSurfaceSpeedDefault) / log(span));
  }
  else
  {
    const double span = kSurfaceSpeedDefault / kSurfaceSpeedMin; // 2.0 over 0..500
    p = 500.0 - 500.0 * (log(kSurfaceSpeedDefault / v) / log(span));
  }
  if (p < 0.0) p = 0.0;
  if (p > 1000.0) p = 1000.0;
  return (int)(p + 0.5);
}

inline double SurfaceValueFromKnobPos(int pos)
{
  if (pos < 0) pos = 0;
  if (pos > 1000) pos = 1000;
  double v;
  if (pos >= 500)
  {
    const double span = kSurfaceSpeedMax / kSurfaceSpeedDefault; // 3.0
    v = kSurfaceSpeedDefault * exp(log(span) * ((pos - 500) / 500.0));
  }
  else
  {
    const double span = kSurfaceSpeedDefault / kSurfaceSpeedMin; // 2.0
    v = kSurfaceSpeedDefault / exp(log(span) * ((500 - pos) / 500.0));
  }
  return ClampSurfaceSpeed(v);
}

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
  // latch (see the caller).
  //
  // IT IS A PROPERTY OF THE ACTION, NOT OF HOW WE FOUND IT. Every row below names a real
  // "(MIDI CC relative/mousewheel)" action, so every row states `true` -- and stating it HERE, in
  // the row, is the point: an earlier version left the table rows at the default `false` on the
  // theory that "the canonical ids keep the latch requirement, exactly as before", while the NAME
  // rule set it from the action's own name. The same action therefore got two different answers
  // depending on which rule matched it. The table wins (it is tried first), so the horizontal
  // actions -- which the table lists AND which are the ones a tilt wheel / thumb wheel drives --
  // demanded a wheel latch. That latch is armed by the message hook, and the horizontal wheel
  // message was never watched, so every horizontal notch was refused and REAPER moved natively
  // (forum report, MX Master 3S thumb wheel -> 977/979). Declaring it statically removes the
  // disagreement at its source and costs no lookup on the hot path.
  //
  // A row is `true` when the action is driveable by a relative amount -- i.e. when its REAPER name
  // carries the "mousewheel" marker. That holds for every row below.
  bool relativeAction = false;
};

// Which surface a resolved action belongs to. Stated as a rule over (drive, section, axis, kind),
// not as a list of ids, so a name-matched action lands in the same bucket as the table's own row.
inline Surface SurfaceFor(const ActionSpec &s)
{
  if (s.drive == DRIVE_MCP_WHEEL)
    return Surface::kMixerPan; // the mixer has no direction: REAPER owns that context
  const bool h = (s.axis == Axis::kHorizontal);
  const bool zoom = (s.kind == Kind::kZoom);
  if (s.section == kSectionMidi)
  {
    if (zoom)
      return h ? Surface::kMidiZoomH : Surface::kMidiZoomV;
    // MIDI VERTICAL PAN has no knob (see the note on Surface). It is mapped to the mixer bucket only
    // because the enum has no "none" member; SurfaceSpeedFor() below is what actually keeps it at
    // 1.0x, and the mixer bucket is unreachable from the MIDI editor, so no knob can move it.
    return h ? Surface::kMidiPanH : Surface::kMixerPan;
  }
  // Main (and main-alt, which shares the main table). An action whose NAME says neither Scroll nor
  // Zoom goes with PAN: pan is the motion that has always worked, so an unknown receiver is treated
  // as one rather than given a multiplier it may not share. Its AXIS is still known, so it still
  // splits horizontal from vertical.
  if (zoom)
    return h ? Surface::kMainZoomH : Surface::kMainZoomV;
  return h ? Surface::kMainPanH : Surface::kMainPanV;
}

// ★ THE ONE PLACE that answers "how much does this action get scaled". It exists because one axis
// must be pinned at 1.0x: MIDI vertical PAN (40432/40661), whose receiver quantizes to a whole pitch
// row, so a bigger factor only makes each send COARSER rather than making the view move faster (the
// measurement is in the note on Surface above).
//
// Pinning it HERE rather than by leaving it out of the knob table is deliberate: the table says what
// the user can turn, this says what the delivery actually does, and the two must not be able to drift
// apart. `speed` is the value the caller holds in its array; passing it in keeps this header free of
// the plugin's globals.
inline bool SurfaceIsAdjustable(const ActionSpec &s)
{
  return !(s.section == kSectionMidi && s.axis == Axis::kVertical && s.kind == Kind::kScroll);
}

inline double SurfaceSpeedFor(const ActionSpec &s, const double *speeds)
{
  if (!SurfaceIsAdjustable(s) || !speeds)
    return kSurfaceSpeedDefault;
  const int i = (int)SurfaceFor(s);
  if (i < 0 || i >= (int)Surface::kCount)
    return kSurfaceSpeedDefault;
  return ClampSurfaceSpeed(speeds[i]);
}

// The known-id rows. axis + kind + relativeAction are stated; the delivery is NOT (FilterFor
// derives it). Every row here is a "(MIDI CC relative/mousewheel)" action -- see the note above on
// relativeAction, and _diag/hwheel_probe.cpp, which asserts the table and the name rule agree.
static const ActionSpec kActionsTbl[] = {
    // ---- main: scrolling ----
    {kSectionMain, 988, Axis::kHorizontal, Kind::kScroll, DRIVE_REPLAY, true}, // Scroll horizontally
    {kSectionMain, 977, Axis::kHorizontal, Kind::kScroll, DRIVE_REPLAY, true}, // Scroll horizontally rev
    {kSectionMain, 989, Axis::kVertical, Kind::kScroll, DRIVE_REPLAY, true},   // Scroll vertically
    {kSectionMain, 978, Axis::kVertical, Kind::kScroll, DRIVE_REPLAY, true},   // Scroll vertically rev
    // ---- main: zooming ----
    {kSectionMain, 990, Axis::kHorizontal, Kind::kZoom, DRIVE_REPLAY, true},   // Zoom horizontally
    {kSectionMain, 979, Axis::kHorizontal, Kind::kZoom, DRIVE_REPLAY, true},   // Zoom horizontally rev
    {kSectionMain, 1000, Axis::kVertical, Kind::kZoom, DRIVE_REPLAY, true},    // Zoom vertically
    {kSectionMain, 1001, Axis::kVertical, Kind::kZoom, DRIVE_REPLAY, true},    // Zoom vertically rev
    // ---- MIDI editor: scroll + zoom through the section's own actions ----
    {kSectionMidi, 40430, Axis::kVertical, Kind::kZoom, DRIVE_REPLAY, true},   // Zoom vertically
    {kSectionMidi, 40431, Axis::kHorizontal, Kind::kZoom, DRIVE_REPLAY, true}, // Zoom horizontally
    {kSectionMidi, 40432, Axis::kVertical, Kind::kScroll, DRIVE_REPLAY, true}, // Scroll vertically
    {kSectionMidi, 40433, Axis::kHorizontal, Kind::kScroll, DRIVE_REPLAY, true}, // Scroll horizontally
    {kSectionMidi, 40660, Axis::kHorizontal, Kind::kScroll, DRIVE_REPLAY, true}, // Scroll horiz rev
    {kSectionMidi, 40661, Axis::kVertical, Kind::kScroll, DRIVE_REPLAY, true},   // Scroll vert rev
    {kSectionMidi, 40662, Axis::kHorizontal, Kind::kZoom, DRIVE_REPLAY, true},   // Zoom horiz rev
    {kSectionMidi, 40663, Axis::kVertical, Kind::kZoom, DRIVE_REPLAY, true},     // Zoom vert rev
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
    // ★ 1000/1001 (vertical ZOOM) IS WHOLE UNITS -- tried as a fine stream on 2026-10-13 and
    // REVERTED the same day, so this is now a MEASURED conclusion rather than an inherited one.
    //
    // WHAT WAS TRIED AND WHAT WAS SEEN. The axis was switched to kStream because one notch is 2.28
    // units and the whole-unit grid delivered it as two `val=127` steps, which reads as "it just
    // steps". The first impression was favourable ("很丝滑"), but using it for a while showed the
    // problem the fine stream actually has here: "there is a STALL IN THE MIDDLE, and it is not
    // linear enough". That is this axis refusing to act on the small values -- the fine stream
    // spends much of the gesture below what the receiver can move on, so the motion pauses and then
    // catches up. It is the same effect AGENTS 109 recorded and 110 reverted; the difference this
    // time is that it was re-derived on purpose, by measuring, instead of being taken on trust.
    //
    // WHY THE FIRST IMPRESSION WAS MISLEADING, and the lesson: a fine stream looks smoother in the
    // hand for a short test, because the steps are smaller. What it cannot do is make the axis move
    // when the value is below its own granularity -- and on THIS axis that is most of the gesture.
    // ⚠️ DO NOT SWITCH THIS AXIS TO kStream AGAIN on the strength of a quick trial.
    //
    // The vertical SCROLL below is whole units for a related reason (the row-height gain makes its
    // travel track the track height), and it was NOT part of this experiment.
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
