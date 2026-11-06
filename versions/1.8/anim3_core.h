#ifndef ANIM3_CORE_H
#define ANIM3_CORE_H

#include <cmath> // std::fabs / std::floor / std::ceil (the payout-shape helper below)

// ---------------------------------------------------------------------------
// MODEL 3.0 -- "take how much, give how much, spread over a stated time."
//
// The wheel reports an amount. The model hands that amount over spread across a stated time. That is
// ALL it does: there is no eat, no spit, no speed-dependent shaping -- what goes in is what comes
// out, only spread out.
//
// A received amount becomes ONE WINDOW of exactly `windowMs`, handed over in equal parts across that
// window; overlapping windows simply add up, so while a roll continues the hand-over keeps flowing
// and never stops between notches.
//
//   Feed(amount):  open a window of `windowMs` holding exactly that amount
//   Tick(dt):      hand out every window's share for this frame
//
// WHY THIS IS THE WHOLE MODEL (the change that removed the eat and the spit):
//   Both were SPEED-DEPENDENT shaping -- the eat took a share of a slow amount, the spit multiplied a
//   fast one. Each made the travel handed over depend on how the wheel was turned rather than only on
//   WHAT it reported, and every such rule was a place where "take N, give N" could stop being true.
//   Removing them makes the model EXACTLY conservative: the total handed over equals the total
//   received, always, whatever the speed. The only thing left is the timing.
//
// WHY PER-MESSAGE WINDOWS (the one thing to keep):
//   * "spread it over X ms" is EXACT -- each window ends at exactly `windowMs`, so the setting is a
//     DURATION and not a time constant. A single running total paid out per frame cannot say that:
//     its "XP" would be an exponential time constant and 400 ms would become seconds with a tail.
//   * Overlap is what makes a roll continuous and strong: five notches 60 ms apart at a 300 ms
//     window are five windows in flight at once, and their shares add. That build-up is the feel.
//   * It is frame-rate independent: the share is dt/window, so the total handed over in one window is
//     the same whether frames are 1 ms or 15 ms apart (the tail is coarser, not longer).
//
// Nothing here depends on REAPER or Windows, and nothing here knows which kind of device sent the
// amount -- the classification is a marker for the caller (see src/device.h). This is pure timing.
// ---------------------------------------------------------------------------

namespace anim3 {

struct Params
{
  // X: how long one amount takes to be handed over, in milliseconds. The plugin clamps it to its
  // slider range (100..400 ms), so a value arriving here is already sane.
  double windowMs = 100.0;

  // How much of the smoothstep payout shape THIS amount's window should use (0 = constant rate).
  // The CALLER decides it per message: see the note on PayoutFrac. It is a field and not a constant
  // because it belongs to one window, chosen from the gap that produced it.
  double payoutEase = 0.0;

  double windowSec() const { return (windowMs > 0.0 ? windowMs : 0.0) * 0.001; }
};

// The motion: one window per amount the wheel reported, handed over across `windowMs`.
class Glide
{
public:
  // A free-spinning wheel can report ~1000 amounts/s; at the longest window (400 ms) that is ~400
  // windows in flight. 2048 leaves a wide margin. Anything beyond is handed over at once rather
  // than dropped, so an amount is never lost to the cap.
  static const int kMaxEnv = 2048;

  void Reset()
  {
    n_ = 0;
    due_ = 0.0;
  }

  // One amount the WHEEL reported, in the caller's unit (signed). It is stored EXACTLY as it came --
  // no scaling -- so what is handed over is what arrived.
  void Feed(double amount, const Params &P)
  {
    if (amount == 0.0)
      return;
    if (P.windowSec() <= 0.0)
    {
      due_ += amount; // no window: hand it over on the next tick
      return;
    }
    if (n_ < kMaxEnv)
    {
      amt_[n_] = amount;
      age_[n_] = 0.0;
      ease_[n_] = P.payoutEase; // this window's shape, chosen by the caller for this amount
      ++n_;
    }
    else
    {
      due_ += amount; // the cap is full: do not lose it, just stop spreading it
    }
  }

  // Advance by dt seconds; returns the amount to hand over this frame, in the caller's unit.
  double Tick(double dt, const Params &P)
  {
    if (dt <= 0.0)
      return 0.0;

    double out = due_;
    due_ = 0.0;

    const double w = P.windowSec();
    if (w <= 0.0)
    {
      n_ = 0;
      return out; // every window is "now"
    }

    // ---- HAND OVER ------------------------------------------------------------------------------
    // Each window hands its amount over across exactly its own `windowMs`. HOW it hands it over is
    // the payout SHAPE below (see kPayoutEase and PayoutFrac).
    //
    // Whatever shape is used, this frame's share is taken OFF the window and the window's own
    // progress decides the rest, so over the window the shares telescope to exactly the amount it
    // held when it opened: "spread it over X ms" is exact, and the total handed over equals the
    // total fed, to the last fraction.
    int k = 0;
    for (int i = 0; i < n_; ++i)
    {
      const double u0 = age_[i] / w;          // progress entering this frame
      const double u1 = (age_[i] + dt) / w;   // progress leaving it
      const double s0 = PayoutFrac(u0, ease_[i]); // cumulative share paid by u0
      const double s1 = PayoutFrac(u1, ease_[i]); // ... and by u1
      const double rem = 1.0 - s0;            // of the ORIGINAL amount, what is still to come
      // payout = original * (s1 - s0); and original = amt_[i] / rem, so the division cancels.
      double pay = (rem > 1e-12) ? (amt_[i] * (s1 - s0) / rem) : amt_[i];
      out += pay;
      amt_[i] -= pay; // really taken off the window
      age_[i] += dt;
      if (age_[i] < w) // still running: keep it for the next frame
      {
        amt_[k] = amt_[i];
        age_[k] = age_[i];
        ease_[k] = ease_[i];
        ++k;
      }
    }
    n_ = k;
    return out;
  }

  bool Active() const { return n_ > 0 || due_ != 0.0; }
  int InFlight() const { return n_; } // windows still being handed over (for the panel/log)

  // How much smoothstep to blend into an OVERLAPPING window's payout. Chosen by measurement over a
  // fine sweep of gaps (window 200 ms, gaps 80..200): 0.5 roughly HALVES the worst ripple (35.4% ->
  // 18.9%) and improves every overlapping gap, while a non-overlapping or integer-ratio one is
  // untouched because the caller gives it 0 (see PayoutEaseFor). Higher (0.6) started to hurt the
  // slowest overlapping gaps again, so 0.5 is the measured optimum.
  static const double kEaseAmount;

private:
  // THE PAYOUT SHAPE.
  //
  // A window pays its amount over its `windowMs`, but not necessarily at a constant rate. With a
  // constant rate (ease = 0, the original behaviour) the OUTPUT RATE of a steady roll ripples, and
  // the reason is structural: notches arrive at a fixed gap while each window lasts a fixed time, so
  // as windows overlap the NUMBER IN FLIGHT steps between n and n+1, and the output rate steps with
  // it. Measured (a steady 150 ms roll, 200 ms window) that ripple is a ~35% standard deviation.
  //
  // An eased shape (smoothstep blended in) has a payout rate of ZERO at both ends of its window, so a
  // window being added or finishing no longer puts a step into the output. Measured, it cuts the
  // ripple of an OVERLAPPING roll by 18-37%.
  //
  // IT ONLY HELPS WHEN WINDOWS OVERLAP. When the gap is at least the window length the windows tile
  // with no overlap, only ONE is ever in flight, and its constant rate is already perfectly flat --
  // easing there can only ADD ripple (measured: a 200 ms roll went from 0.1% to 11%). So the ease is
  // chosen PER MESSAGE by the caller (Params.payoutEase): a message whose gap is SHORTER than the
  // window (i.e. one that will overlap its neighbours) asks for the eased shape, and one that will
  // not overlap asks for 0. That makes it a pure improvement: overlapping rolls get smoother and
  // everything else is bit-for-bit unchanged.
  static double PayoutFrac(double u, double ease)
  {
    if (u <= 0.0)
      return 0.0;
    if (u >= 1.0)
      return 1.0;
    if (ease <= 0.0)
      return u; // constant rate: cumulative share is just the progress
    const double ss = u * u * (3.0 - 2.0 * u); // smoothstep: zero SLOPE at both ends
    return (1.0 - ease) * u + ease * ss;
  }

  double amt_[kMaxEnv] = {0};
  double age_[kMaxEnv] = {0};
  double ease_[kMaxEnv] = {0}; // per-window payout ease (0 = constant rate)
  int n_ = 0;
  double due_ = 0.0; // amounts handed over at once: the cap overflow, or a zero window
};

// How much smoothstep to blend into an OVERLAPPING window's payout (see PayoutFrac).
inline const double Glide::kEaseAmount = 0.5;

// What the caller should put in Params.payoutEase for a message whose gap was `gapMs`, with a window
// of `windowMs`.
//
// Only an OVERLAPPING roll can ripple, and only sometimes: the ripple comes from the NUMBER OF WINDOWS
// IN FLIGHT oscillating, which happens when the window is a NON-INTEGER multiple of the gap. When it
// is an integer multiple (a 200 ms window with a 100 or 200 ms gap) the count is constant, the
// constant-rate payout is already perfectly flat, and easing can only ADD a ripple (measured: 0.1% ->
// 11% in one such case). So the ease is asked for only when the windows overlap AND the ratio is not
// (almost) an integer; everywhere else the caller asks for 0 and the payout is bit-for-bit unchanged.
//
// The exclusion band is TIGHT but must still cover the "barely overlapping" corner, where easing
// backfires. Measured over a fine sweep of the ratio (window 200 ms, unit-grid axis), the only place
// easing makes the output WORSE is the region where just one or two windows are in flight and the
// count very nearly never changes: `|ratio - nearest integer|` between 0.05 and 0.07. At 0.02-0.04
// easing is neutral, and from 0.08 out it is a clear improvement (interval spread 2.4 -> 1.4), so the
// band is 0.08. An earlier 0.15 was too WIDE and quietly skipped the "mid-slow" gaps the user cares
// about most (a 180 ms gap against a 200 ms window has d = 0.111 and was getting no easing at all).
inline double PayoutEaseFor(double gapMs, double windowMs)
{
  if (!(gapMs > 0.0) || !(windowMs > 0.0) || gapMs >= windowMs)
    return 0.0; // no overlap (or the first message): the constant rate is right
  const double ratio = windowMs / gapMs;
  const double nearestInt = (ratio - floor(ratio) < 0.5) ? floor(ratio) : ceil(ratio);
  if (std::fabs(ratio - nearestInt) < 0.08)
    return 0.0; // the window count is (nearly) constant: already flat, do not disturb it
  return Glide::kEaseAmount;
}

} // namespace anim3

#endif // ANIM3_CORE_H
