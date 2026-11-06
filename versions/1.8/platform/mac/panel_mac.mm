// ---------------------------------------------------------------------------
// platform/mac/panel_mac.mm -- the settings panel on macOS: NOT PRESENT YET.
//
// ★ THIS IS THE ONE FILE IN THE PORT THAT GENUINELY IS A STUB, and it is a stub on purpose.
//
// The Windows panel is about 3000 lines of hand-drawn Win32: a fader control, a knob control, a
// motion chart with rings and a running ball, theme detection, docking, window placement, keyboard
// forwarding. None of that transposes to AppKit one line at a time -- the drawing primitives are
// different, the layout model is different (Cocoa constraints vs absolute rectangles), and the
// docking is REAPER's own Win32 dock rather than something a Cocoa window can join.
//
// Writing that blind would mean writing 3000 lines nobody can run, and every hour spent on it is an
// hour that cannot go into making the wheel hook work, which is the part users actually feel.
//
// SO THE MAC BUILD SHIPS WITH NO PANEL, WHICH IS A SUPPORTED SHAPE. The plugin reads every setting
// from REAPER's extstate (section "SmoothWheelScroll"), so a build with no UI still honours whatever
// was saved. That gives a tester a working plugin with the DEFAULTS, which is the fastest route to
// the answer we need first: does the wheel animate at all on macOS?
//
// WHAT A TESTER CAN DO IN THE MEANTIME: set the values by hand in the ExtState store. The keys are
// documented in versions/<ver>/MODEL.md ("settings"), and the ones that matter for feel are
// window / startd / budget / speedmul, plus the eight sspd0..sspd7 per-surface factors.
//
// THE HONEST SEQUENCE FROM HERE:
//   1. Get the wheel path working (this build). No panel needed.
//   2. Then the panel, as its own piece of work, WITH a Mac to try it on.
//
// A Cocoa panel is not impossible -- NSView subclasses with drawRect: cover the fader and the knob
// easily, and the motion chart is a path -- but it is a from-scratch UI, not a translation, and it
// should be done where it can be seen.
// ---------------------------------------------------------------------------
#include "../../platform.h"

namespace plat
{
bool PlatPanelPresent() { return false; }

// All five are no-ops because there is no panel to show. They are defined rather than omitted so
// that the plugin's menu entry and its toggle action keep working: pressing "settings" on a Mac
// build does nothing, which is clearer than a menu entry that is missing entirely (and than a crash
// from a null pointer if some path forgot to check PlatPanelPresent).
void PanelShow() {}
void PanelToggle() {}
bool PanelVisible() { return false; }
void PanelRefresh() {}

} // namespace plat
