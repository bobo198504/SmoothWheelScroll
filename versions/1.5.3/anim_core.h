// ---------------------------------------------------------------------------
// anim_core.h -- the wheel-glide animation, standalone.
//
// Same physical model as 1.0.0 (impulse + power-law brake + smooth onset), with
// one structural change: the onset ramp is PER NOTCH instead of shared.
//
//   Shared ramp (1.0.0): every notch adds to injTotal_, and one smoothstep applied
//   to that running total injects it. When a second notch lands, the total jumps by
//   that notch's velocity AND the ramp is already partway up, so the fresh velocity
//   is SLAMMED in -- a same-direction roll shows speed jumps of 1000%+ (measured),
//   which reads as stepping/"jumping". Changing direction resets the glide, so the
//   next notch gets a fresh ramp and feels smooth -- exactly the asymmetry reported.
//
//   Per-notch ramp (here): each notch gets its own ramp, timed from when it
//   arrived. A lone notch is unchanged (it always had its own ramp), so the tuned
//   single-notch arc -- and therefore the approved release -- is untouched. A roll
//   now accumulates smoothly: the new notch eases in on top of what is left.
//
//   Nothing else changes: brake calibration, power-law exponent, relax (brake by
//   rhythm) and the onset window are all as in 1.0.0.
// ---------------------------------------------------------------------------
#ifndef ANIM_CORE_H
#define ANIM_CORE_H

#include <math.h>

namespace anim {

struct Params {
  double startPct  = 15.0;
  double accelPct  = 3.5;
  double releaseMs = 150.0;

  double frictionPow = 0.8;

  double onsetRatio = 1.0;
  double onsetMinMs = 30.0;
  double onsetMaxMs = 260.0;

  double tempoRefMs = 120.0;
  double relaxMax   = 4.0;

  double burstGapMs = 250.0;

  // Speed ceiling. A notch's added velocity is multiplied by
  // 1 - (v/speedCeiling)^ceilingPow, which reaches exactly ZERO at the ceiling: the roll
  // can approach that speed but never pass it. Below the ceiling the factor is close to 1,
  // so ordinary rolling and the single notch are left essentially alone.
  //
  // This replaced a soft "escape velocity" taper (1/(1+(v/knee)^pow)) whose knee only bent
  // the curve. Two things were wrong with that. First, it was not a ceiling at all -- it
  // always allowed a little more. Second, it made the curve DOUBLE-HUMPED on a fast roll:
  // each notch injects more than the last (accel) while the taper injects less as speed
  // rises, and where those two cross the speed dips before rising again -- measured as a
  // visible second peak. A factor that reaches zero at the ceiling cannot do that: the
  // speed rises monotonically to its limit and then falls back.
  //
  // ceilingPow shapes the approach: lower values start easing earlier (a broader, flatter
  // plateau), higher values keep the low range free and stop the speed abruptly near the
  // ceiling. 0, or a ceiling of 0, disables the limit.
  double speedCeiling = 0.0;
  double ceilingPow = 2.5;

  // Release lengthens with speed: the faster a roll is going, the longer it coasts
  // after the last notch. Expressed by dividing the brake coefficient by a factor
  // that grows with speed and saturates, so the growth is early-weighted while the
  // largest effect lands at high speed.
  //
  // relFloor is a dead zone: below that speed the boost is exactly zero, so slow
  // motion and the single notch are bit-for-bit unchanged (their release time does
  // not move at all). Above it the boost rises and saturates; relGain is how far the
  // brake can be divided at the top (0 disables it) and relRef the speed above the
  // floor at which half of that is reached.
  double relGain = 0.0;
  double relRef = 400.0;
  double relFloor = 0.0;

  // While a roll is already moving, a later notch ramps over this shorter window
  // instead of the full onset: it still eases in (no slam) but lands promptly, so
  // the roll keeps its energy. 0 = use the full onset window for every notch.
  double movingOnsetMs = 30.0;

  // Notches over which the ramp window eases from the full onset down to
  // movingOnsetMs. Without it the window switches in ONE step -- the first notch
  // ramps over the full onset and the second jumps to the short one -- which reads as
  // a sudden lurch between the start of a roll and its middle (the second notch
  // accelerated ~16x harder than the first, measured). Easing it over a few notches
  // removes that step while leaving both ends alone: the single notch (which uses the
  // full onset ramp throughout) and a steady roll (which uses the short window) are
  // both unchanged. 0 = switch immediately, as before.
  double onsetFadeNotches = 0.0;
};

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

inline double SafeP(double p) { return p > 0.99 ? 0.99 : (p < 0.05 ? 0.05 : p); }
inline double QCoeff(double p) { return (2.0 - p) / (1.0 - p); }

inline double NotchFrac(const Params &P, int streak) {
  const double f = P.startPct / 100.0 + (streak - 1) * (P.accelPct / 100.0);
  return f > 0.0 ? f : 0.0;
}

class Glide {
public:
  void Reset() {
    vel_ = 0.0;
    onsetDone_ = 0.0;
    injPrev_ = 0.0;
    onsetPend_ = 0.0;
    powC_ = 0.0;
    total_ = 0.0;
    active_ = false;
    lastNotchSec_ = -1e9;
    lastDir_ = 0;
    streak_ = 0;
    identity_ = 0;
  }

  void Kick(double unitTravel, double sign, int identity, double now, const Params &P) {
    const int dir = (sign < 0.0) ? -1 : 1;
    if (identity != identity_ || dir != lastDir_) {
      vel_ = 0.0;
      onsetDone_ = 0.0;
      injPrev_ = 0.0;
      onsetPend_ = 0.0;
      total_ = 0.0;
      streak_ = 0;
      lastNotchSec_ = -1e9;
    }

    const double gap = now - lastNotchSec_;
    const bool burst = (gap >= 0.0) && (gap * 1000.0 <= P.burstGapMs + 1e-6);
    streak_ = burst ? streak_ + 1 : 1;
    lastNotchSec_ = now;
    lastDir_ = dir;
    identity_ = identity;

    const double T = (P.releaseMs < 1.0 ? 1.0 : P.releaseMs) / 1000.0;
    double relax = 1.0;
    if (burst && gap > 0.0 && P.relaxMax > 1.0) {
      relax = (P.tempoRefMs / 1000.0) / gap;
      if (relax < 1.0) relax = 1.0;
      if (relax > P.relaxMax) relax = P.relaxMax;
    }

    const double p = SafeP(P.frictionPow), oneP = 1.0 - p, Q = QCoeff(p);
    const double dRef = unitTravel * (P.startPct / 100.0);
    if (dRef > 0.0) {
      const double vRef = dRef * Q / T;
      powC_ = pow(vRef, oneP) / (oneP * T) / relax;
    }

    const double vNotch = unitTravel * NotchFrac(P, streak_) * Q / T;
    // Speed ceiling: a notch adds less as the roll approaches the ceiling, and adds
    // nothing at all once it is there, so the speed can never pass it. This is what makes
    // "how fast can this roll get" a well-defined number rather than an asymptote, and it
    // is why the curve has a single hump: the factor falls monotonically to zero, so there
    // is no point where a rising per-notch gain crosses a falling taper and dips.
    double feed = 1.0;
    if (P.speedCeiling > 0.0) {
      const double m = (P.ceilingPow > 0.0) ? P.ceilingPow : 1.0;
      const double r = fabs(vel_) / P.speedCeiling;
      feed = 1.0 - pow(r, m);
      if (feed < 0.0) feed = 0.0;
    }
    // First notch of a glide uses the full (soft) onset; a notch arriving while
    // already moving uses the shorter moving window so the roll keeps its energy.
    // The changeover is eased over the first few notches (see onsetFadeNotches):
    // switching in one step is the lurch between the start of a roll and its middle.
    double W = OnsetSec(P);
    if (active_ && P.movingOnsetMs > 0.0) {
      double wMove = P.movingOnsetMs / 1000.0;
      if (wMove < P.onsetMinMs / 1000.0) wMove = P.onsetMinMs / 1000.0;
      if (P.onsetFadeNotches > 0.0) {
        double f = (streak_ - 1) / P.onsetFadeNotches;
        if (f > 1.0) f = 1.0;
        W += (wMove - W) * SmoothStep(f);
      } else {
        W = wMove;
      }
    }
    AddRamp_(dir * vNotch * feed, W);
    active_ = true;
  }

  double Tick(double dt, const Params &P) {
    if (!active_ || dt <= 0.0) return 0.0;
    if (powC_ <= 0.0) { Reset(); return 0.0; }

    // Sum every notch's own ramp: a still-ramping notch contributes v*S(x), a
    // finished one contributes its full v (folded into onsetDone_).
    double sum = onsetDone_;
    for (int i = 0; i < onsetPend_; ++i) {
      Pent *q = &ramp_[i];
      q->e += dt;
      const double x = (q->w > 0.0) ? (q->e / q->w) : 1.0;
      if (x >= 1.0) {
        onsetDone_ += q->v;
        sum += q->v;
        *q = ramp_[--onsetPend_];
        --i;
        continue;
      }
      sum += q->v * SmoothStep(x);
    }
    vel_ += sum - injPrev_;
    injPrev_ = sum;

    const double p = SafeP(P.frictionPow), oneP = 1.0 - p, Q = QCoeff(p);
    const int sign = (vel_ < 0.0) ? -1 : 1;
    double step = 0.0;

    // Release lengthens with speed (see relGain): divide the brake by a factor that
    // grows with speed and saturates. relFloor is a dead zone -- below it the boost is
    // exactly zero, so a single notch and slow rolls keep their release untouched.
    double cUse = powC_;
    if (P.relGain > 0.0 && P.relRef > 0.0) {
      double over = fabs(vel_) - P.relFloor;
      if (over < 0.0) over = 0.0;
      const double rr = over / P.relRef;
      cUse = powC_ / (1.0 + P.relGain * rr / (rr + 1.0));
    }

    const double u0 = pow(fabs(vel_), oneP);
    if (u0 > 0.0) {
      double u1 = u0 - cUse * oneP * dt;
      if (u1 < 0.0) u1 = 0.0;
      step = sign * (pow(u0, Q) - pow(u1, Q)) / (cUse * oneP * Q);
      vel_ = (u1 > 0.0) ? sign * pow(u1, 1.0 / oneP) : 0.0;
    }

    if (vel_ == 0.0 && onsetPend_ == 0) active_ = false;

    total_ += step;
    return step;
  }

  bool Active() const { return active_; }
  double Velocity() const { return vel_; }
  double Total() const { return total_; }
  int Streak() const { return streak_; }

private:
  struct Pent { double v = 0.0, w = 0.0, e = 0.0; };
  static const int kMaxPend = 32;

  void AddRamp_(double v, double w) {
    if (onsetPend_ >= kMaxPend) { onsetDone_ += v; return; }
    ramp_[onsetPend_].v = v;
    ramp_[onsetPend_].w = w;
    ramp_[onsetPend_].e = 0.0;
    ++onsetPend_;
  }

  double vel_ = 0.0;
  double onsetDone_ = 0.0;
  double injPrev_ = 0.0;
  double powC_ = 0.0;
  double total_ = 0.0;
  bool active_ = false;
  double lastNotchSec_ = -1e9;
  int lastDir_ = 0;
  int streak_ = 0;
  int identity_ = 0;
  Pent ramp_[kMaxPend];
  int onsetPend_ = 0;
};

} // namespace anim

#endif // ANIM_CORE_H
