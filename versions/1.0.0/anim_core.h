// ---------------------------------------------------------------------------
// anim_core.h -- the wheel-glide animation, standalone.
//
// RESTORED VERBATIM from the recorded "pre-precision" model (the version that was
// tuned and approved), with only two later bug fixes layered on top:
//   - the travel keeps the velocity's SIGN (without it both directions scrolled
//     the same way);
//   - the burst-gap test carries an epsilon (an exact-boundary gap otherwise fails
//     it and silently restarts the roll).
// Nothing else in the model was touched: the power-law brake, the shared smoothstep
// onset and the brake-by-rhythm (relax) are exactly as they were.
//
// MODEL (physical dynamics: an impulse, then friction)
//   A notch does not move the view; it gives it a VELOCITY. Position integrates
//   that velocity while a brake bleeds it off, so the motion grows out of rest,
//   coasts, and settles. Repeated notches add their velocity before the last has
//   decayed, so a sustained roll builds speed.
//
//   RISE (onset): the queued velocity is injected along a smoothstep, so the rate
//                 is zero at the start -- the motion winds up instead of arriving
//                 all at once.
//   FALL (brake): dv/dt = -c * v^p  with p < 1. v reaches exactly 0 in FINITE
//                 time, so the stop is definite and soft, with no exponential
//                 tail and no threshold that clips visible travel. p < 1 also
//                 means the brake is gentle at speed and firmer as it slows,
//                 which reads as easing to a stop rather than clamping on.
//
//   The brake c is calibrated from the FIRST notch's distance and RELEASE, so a
//   lone notch travels exactly its intended distance in exactly RELEASE time.
//   Later notches reuse that c and just add velocity, which is the acceleration.
//
//   BRAKE BY RHYTHM (relax): a roll faster than tempoRefMs loosens the brake, so a
//   quick roll keeps its energy instead of being stopped dead between notches.
//   This is what makes a fast roll build up; it is part of the model, not a tweak.
// ---------------------------------------------------------------------------
#ifndef ANIM_CORE_H
#define ANIM_CORE_H

#include <math.h>

namespace anim {

// ---------------------------------------------------------------------------
// Tuning (compiled-in defaults; the settings window overrides per session).
// ---------------------------------------------------------------------------
struct Params {
  // 1. START: travel of the first notch, as % of one wheel notch.
  double startPct  = 15.0;
  // 2. ACCEL: extra travel added to every notch after the first, same unit.
  double accelPct  = 3.6;
  // 3. RELEASE: time one notch's travel takes to settle, in milliseconds.
  double releaseMs = 100.0;

  // Brake exponent in dv/dt = -c*v^p. 0.8 keeps the settle time close to the
  // proportional case while softening the initial bite -- the "brake builds up
  // instead of being stomped on" feel.
  double frictionPow = 0.8;

  // Onset: the rise window, as a fraction of RELEASE, clamped to [min,max] so it
  // is never shorter than a couple of timer frames.
  double onsetRatio = 0.8;
  double onsetMinMs = 30.0;
  double onsetMaxMs = 260.0;

  // Brake-by-rhythm: a roll faster than tempoRefMs stretches the release toward
  // relaxMax times longer, so a quick flick keeps flying after you stop. Set
  // relaxMax = 1 to disable.
  double tempoRefMs = 120.0;
  double relaxMax   = 4.0;

  // A burst is notches closer together than this.
  double burstGapMs = 250.0;
};

// Rate curve for the onset: S(0)=0, S(1)=1, rate zero at both ends.
inline double SmoothStep(double u) {
  if (u <= 0.0) return 0.0;
  if (u >= 1.0) return 1.0;
  return u * u * (3.0 - 2.0 * u);
}

inline double OnsetSec(const Params &P) {
  double t = (P.releaseMs / 1000.0) * P.onsetRatio;
  const double lo = P.onsetMinMs / 1000.0, hi = P.onsetMaxMs / 1000.0;
  if (t < lo) t = lo;
  if (t > hi) t = hi;
  return t;
}

// Q appears in the closed forms: with u = v^(1-p), du/dt = -c(1-p) is constant,
// so the distance over dt is (u0^Q - u1^Q)/(c(1-p)Q), and a notch of distance D
// settling in T needs v = D*Q/T and c = v^(1-p)/((1-p)T).
// p is clamped away from 1 because the closed form divides by (1-p); p = 1 is the
// exponential case and is not the model in use.
inline double SafeP(double p) { return p > 0.99 ? 0.99 : (p < 0.05 ? 0.05 : p); }
inline double QCoeff(double p) { return (2.0 - p) / (1.0 - p); }

// The travel the streak-th notch of a burst carries, as a multiple of one plain
// notch: START for the first, plus ACCEL for each further notch.
inline double NotchFrac(const Params &P, int streak) {
  const double f = P.startPct / 100.0 + (streak - 1) * (P.accelPct / 100.0);
  return f > 0.0 ? f : 0.0;
}

// ---------------------------------------------------------------------------
// One axis' glide. Notches are added with their travel in the caller's own unit;
// Tick returns the signed travel to apply for the elapsed dt, so the caller never
// deals with velocity, curves or timing.
// ---------------------------------------------------------------------------
class Glide {
public:
  void Reset() {
    vel_ = 0.0;
    injTotal_ = 0.0;
    injDone_ = 0.0;
    onsetSec_ = 0.0;
    powC_ = 0.0;
    total_ = 0.0;
    active_ = false;
    lastNotchSec_ = -1e9;
    lastDir_ = 0;
    streak_ = 0;
    identity_ = 0;
  }

  // Add one notch.
  //   unitTravel  one plain notch's travel, in the caller's unit (positive)
  //   sign        +1 / -1
  //   identity    any int naming the operation (section+command); a change, or a
  //               direction change, restarts the streak and the velocity
  //   now         seconds on a monotone clock, used for the burst rhythm
  void Kick(double unitTravel, double sign, int identity, double now, const Params &P) {
    const int dir = (sign < 0.0) ? -1 : 1;
    if (identity != identity_ || dir != lastDir_) {
      vel_ = 0.0;
      injTotal_ = 0.0;
      injDone_ = 0.0;
      onsetSec_ = 0.0;
      total_ = 0.0;
      streak_ = 0;
      lastNotchSec_ = -1e9;
    }

    const double gap = now - lastNotchSec_;
    // FIX (later): the epsilon keeps an exact-boundary gap, measured in seconds,
    // from failing the test and silently restarting the roll.
    const bool burst = (gap >= 0.0) && (gap * 1000.0 <= P.burstGapMs + 1e-6);
    streak_ = burst ? streak_ + 1 : 1;
    lastNotchSec_ = now;
    lastDir_ = dir;
    identity_ = identity;

    const double T = (P.releaseMs < 1.0 ? 1.0 : P.releaseMs) / 1000.0;
    // Brake-by-rhythm: a fast gap stretches this notch's release (a longer coast).
    double relax = 1.0;
    if (burst && gap > 0.0 && P.relaxMax > 1.0) {
      relax = (P.tempoRefMs / 1000.0) / gap;
      if (relax < 1.0) relax = 1.0;
      if (relax > P.relaxMax) relax = P.relaxMax;
    }

    const double p = SafeP(P.frictionPow), oneP = 1.0 - p, Q = QCoeff(p);
    // The brake is calibrated from the FIRST notch's distance, so a lone notch of
    // its nominal size settles in T. Later notches reuse this c (that is the
    // acceleration: same brake, more velocity).
    const double dRef = unitTravel * (P.startPct / 100.0);
    if (dRef > 0.0) {
      const double vRef = dRef * Q / T;
      powC_ = pow(vRef, oneP) / (oneP * T) / relax;
    }
    // This notch's velocity, in the same unit/s.
    const double vNotch = unitTravel * NotchFrac(P, streak_) * Q / T;
    injTotal_ += dir * vNotch;
    active_ = true;
  }

  // Advance by dt seconds; returns the signed travel for this step.
  double Tick(double dt, const Params &P) {
    if (!active_ || dt <= 0.0) return 0.0;
    if (powC_ <= 0.0) { Reset(); return 0.0; }

    // Inject the queued velocity along the onset curve. Evaluating S at the
    // accumulated time and reconciling against what was already injected means a
    // notch added mid-glide contributes its delta immediately -- so a burst keeps
    // building, and the first notch still winds up from rest.
    const double W = OnsetSec(P);
    if (onsetSec_ < W) {
      onsetSec_ += dt;
      if (onsetSec_ > W) onsetSec_ = W;
    }
    const double x = (W > 0.0) ? (onsetSec_ / W) : 1.0;
    const double raw = injTotal_ * SmoothStep(x);
    vel_ += raw - injDone_;
    injDone_ = raw;

    // Friction: dv/dt = -c*v^p, integrated in closed form via u = v^(1-p).
    // The travel keeps the velocity's SIGN; without it both directions would
    // scroll the same way.
    const double p = SafeP(P.frictionPow), oneP = 1.0 - p, Q = QCoeff(p);
    const int sign = (vel_ < 0.0) ? -1 : 1;
    double step = 0.0;
    const double u0 = pow(fabs(vel_), oneP);
    if (u0 > 0.0) {
      double u1 = u0 - powC_ * oneP * dt;
      if (u1 < 0.0) u1 = 0.0;
      step = sign * (pow(u0, Q) - pow(u1, Q)) / (powC_ * oneP * Q);
      vel_ = (u1 > 0.0) ? sign * pow(u1, 1.0 / oneP) : 0.0;
    }

    // v reaches exactly 0 in finite time (p < 1), so "no speed left and the onset
    // finished" is an exact, soft stop.
    if (vel_ == 0.0 && onsetSec_ >= W) active_ = false;

    total_ += step;
    return step;
  }

  bool Active() const { return active_; }
  double Velocity() const { return vel_; }
  double Total() const { return total_; }
  int Streak() const { return streak_; }

private:
  double vel_ = 0.0;         // signed units/s
  double injTotal_ = 0.0;    // signed velocity queued for this glide
  double injDone_ = 0.0;     // signed velocity already injected
  double onsetSec_ = 0.0;    // seconds elapsed in the onset window
  double powC_ = 0.0;        // brake coefficient
  double total_ = 0.0;       // signed travel delivered so far
  bool active_ = false;
  double lastNotchSec_ = -1e9;
  int lastDir_ = 0;
  int streak_ = 0;
  int identity_ = 0;
};

} // namespace anim

#endif // ANIM_CORE_H
