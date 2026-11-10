#ifndef SWS_MODEL_H
#define SWS_MODEL_H

// ---------------------------------------------------------------------------
// THE MODEL SEAM -- the ONLY file that includes the animation model's header.
//
// Everything the plugin needs from the model is re-exported here under `namespace model`, so:
//   * the plugin never includes the model's own header and never names its namespace directly;
//   * a new model is adopted by editing THIS FILE alone -- the plugin keeps calling the same names.
//
// THE MODEL: "spread each received amount over a window, and let a speed-driven budget decide how far
// a notch goes."
//
//   * A message's amount is handed over across `windowMs` (a PURE TIMING job -- see anim3_core.h).
//     Nothing here scales the timing; if the wheel stops, the windows simply finish and the motion
//     ends. There is no minimum speed and no separate "stop" rule.
//
//   * HOW FAR a notch goes is set by a BUDGET that follows how fast the wheel is being turned: a slow
//     turn moves only `startDeltas` (a hair), and as the turn speeds up the travel rises toward the
//     message's own size. The budget is read in WHEEL DELTAS and decays over time, so it FOLLOWS the
//     current speed -- speed up and it grows, slow down and it falls back. That is what lets the user
//     hold a steady hand speed and get a steady, even glide, at whatever speed they choose.
//
// WHY DELTAS, NOT "NOTCHES" OR "MESSAGES": a notched mouse reports 120 deltas per message; a
// free-spinning wheel reports a small share (e.g. 20) many times more often. Counting MESSAGES would
// make the fast device build up ~8x faster and, on one flick, hand over ~9x the travel of the notched
// mouse (measured). Deltas are the physical quantity the hand actually produced, so a budget counted
// in deltas gives both devices the same behaviour at the same hand speed.
// ---------------------------------------------------------------------------

#include "anim3_core.h"   // the main model: the window/timing model (scroll, and both horizontal axes)
#include "anim161_core.h" // the 1.6.1 curve model, used ONLY by the main view's vertical zoom

#include <cmath> // std::exp, for the budget's decay

namespace model {

// ---- the parameter block ----
// FOUR fields, all filled in from the panel:
//   * `windowMs`     -- how long a received amount takes to be handed over (pure timing);
//   * `startDeltas`  -- travel of the FIRST/slowest notch, in wheel deltas (1..10);
//   * `budgetDeltas` -- how much turning it takes to reach the message's full size (the ramp length);
//   * `speedMul`     -- 1..2: how far past the wheel's own speed the fastest rolls may climb.
using Params = anim3::Params;

// ---- the motion: the timing (windows) ----
using Axis = anim3::Glide;

// Whether THIS message's window should use the eased payout shape: only when it will overlap its
// neighbours (gap shorter than the window), where easing removes a real ripple. See the note on
// anim3::Glide::PayoutFrac. The caller passes its gap and the current window length.
inline double PayoutEaseFor(double gapMs, double windowMs)
{
  return anim3::PayoutEaseFor(gapMs, windowMs);
}

// ---------------------------------------------------------------------------
// THE TRAVEL: how far this message's notch goes, from the speed budget.
//
//   u        = budget / budgetDeltas      (0 at a standstill, 1 once the budget is full)
//   travel   = start + (cap - start) * min(u, speedMul)
//
// where `start` = startDeltas and `cap` = THIS message's own deltas. So:
//   * a slow turn (budget near 0) moves `start` deltas;
//   * once the budget is full the notch moves its whole size, `cap` (the wheel's own speed);
//   * a free-spinning message (cap = 20) tops out at 20, a notched one at 120 -- each device caps at
//     what it actually reports, which is the "balance" the design is after.
//
// `speedMul` (1..2) RAISES THE CEILING of the ramp: past `cap` the travel simply KEEPS CLIMBING on
// the same straight line, up to `speedMul` times it. With speedMul == 1 the ceiling is 1 and this is
// exactly the previous behaviour. Because it is the same line continued, it changes NOTHING below the
// baseline -- the slow region, the mid region and every part of the ramp-up are untouched; it only
// engages once the wheel is already at its own full speed. That is "only at the fastest speed, and
// not during the climb", with no step at the join.
//
// The budget is maintained by SpeedBudget below, which is what makes it FOLLOW the current speed.
// ---------------------------------------------------------------------------
inline double Travel(double messageDeltas, double budget, double budgetDeltas, double startDeltas,
                     double speedMul)
{
  double cap = messageDeltas;
  if (cap < 0.0)
    cap = 0.0;
  double start = startDeltas;
  if (start < 0.0)
    start = 0.0;
  if (start > cap)
    start = cap; // never move more than the notch itself
  if (!(budgetDeltas > 0.0))
    return cap; // no ramp: every notch goes its full size
  const double mul = (speedMul > 1.0) ? speedMul : 1.0; // 1 = stop at the wheel's own speed
  double u = budget / budgetDeltas;
  if (u < 0.0)
    u = 0.0;
  if (u > mul)
    u = mul;
  return start + (cap - start) * u;
}

// ---------------------------------------------------------------------------
// THE SPEED BUDGET: "how much has the wheel been turning lately", in deltas.
//
// Each message adds its own deltas, and the running total DECAYS over time with time constant
// `kDecayMs`. So it is a weighted sum of the recent turn rate: a fast turn keeps it high, a slow turn
// lets it fall back. That decay is what makes the ramp FOLLOW the current speed instead of latching
// once and staying there -- measured, without decay a single fast burst left a slow roll moving at
// full speed for the rest of the gesture.
//
// Chosen over a plain "sum of deltas since the gesture began" for exactly that reason: the user wants
// to be able to hold any speed and glide evenly at it, which a monotone sum cannot do (it only ever
// ratchets up).
//
// `kDecayMs` is the tuning knob for how quickly the travel follows a speed change: smaller = snappier
// but more jittery, larger = steadier but lagging. 300 ms measured a reasonable middle.
//
// It also has to be DEVICE-INDEPENDENT: a notched message of 120 and eight free-spin messages of 15
// both add 120 over the same span of hand movement, so the budget tracks the hand, not the message
// rate. (This falls out of summing DELTAS -- it is the whole point of counting deltas.)
//
// THE DECAY IS AN EULER STEP OF THE CONTINUOUS LAW, `db/dt = rate - b/tau`, not an exponential of the
// gap. The two agree when the gaps are small, but a notched mouse's gaps (125 ms) are NOT small next
// to tau (300 ms), and the exponential form then settles at a DIFFERENT level for a notched mouse than
// for a free-spinner at the same hand speed -- measured, a 14% travel difference on one flick. The
// Euler step has a steady state of exactly `rate * tau` whatever the gap, so the two devices match.
// (`gap >= tau` is clamped to "forget the past" (keep only this message's deltas), which is both the
// limit of the law and what keeps the factor from going negative.)
// ---------------------------------------------------------------------------
class SpeedBudget
{
public:
  // Time constant for the decay, in ms. Defined here rather than at namespace scope because the
  // class must be complete before a `const double` member can be initialised out of line.
  static double DecayMs() { return 300.0; }

  void Reset() { budget_ = 0.0; }

  // Add this message's magnitudes (always positive; the caller sends the sign separately) and let the
  // budget decay for the time since the previous message. Returns the new budget.
  double Add(double messageDeltas, double gapMs)
  {
    const double tau = DecayMs();
    if (gapMs <= 0.0)
      budget_ += messageDeltas; // no elapsed time (first message): just add
    else if (gapMs >= tau)
      budget_ = messageDeltas; // long gap: the past has decayed away
    else
      budget_ = budget_ * (1.0 - gapMs / tau) + messageDeltas; // Euler step of db/dt = rate - b/tau
    return budget_;
  }

  double Value() const { return budget_; }

private:
  double budget_ = 0.0;
};

} // namespace model

// ---------------------------------------------------------------------------
// THE 1.6.1 CURVE MODEL, for the MAIN view's vertical zoom only.
//
// Re-exported under its own namespace so the plugin does not include the header directly and does not
// name `anim::` itself. This file is a VERBATIM copy of versions/1.6.1/anim_core.h (verified by md5),
// so "the zoom uses 1.6.1's behaviour" is a byte-level statement, not an approximation.
//
// WHY A SECOND MODEL AT ALL (AGENTS.md 111): the panel's travel sliders are built for SCROLLING --
// they deliberately make a slow turn move only a little. Measured against 1.6.1 on a sustained roll,
// that made the vertical zoom travel 5-8x too little, so zooming in "never seemed to get there"
// (zooming out felt fine only because its range is small). 1.6.1's zoom did not attenuate at all: every
// notch stacked a claim toward a ceiling, so a roll built up. Matching that exactly needs that model.
// ---------------------------------------------------------------------------
namespace model161 {
using Params = anim::Params;
using Axis = anim::Glide;
static const int kSegments = anim::kSegments;
} // namespace model161

#endif // SWS_MODEL_H
