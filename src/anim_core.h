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

  // While a roll is already moving, a later notch ramps over this shorter window
  // instead of the full onset: it still eases in (no slam) but lands promptly, so
  // the roll keeps its energy. 0 = use the full onset window for every notch.
  double movingOnsetMs = 30.0;
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
    // First notch of a glide uses the full (soft) onset; a notch arriving while
    // already moving uses the shorter moving window so the roll keeps its energy.
    double W = OnsetSec(P);
    if (active_ && P.movingOnsetMs > 0.0) {
      W = P.movingOnsetMs / 1000.0;
      if (W < P.onsetMinMs / 1000.0) W = P.onsetMinMs / 1000.0;
    }
    AddRamp_(dir * vNotch, W);
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
    const double u0 = pow(fabs(vel_), oneP);
    if (u0 > 0.0) {
      double u1 = u0 - powC_ * oneP * dt;
      if (u1 < 0.0) u1 = 0.0;
      step = sign * (pow(u0, Q) - pow(u1, Q)) / (powC_ * oneP * Q);
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
