// MODEL 3.0 -- the "take how much, give how much" model. Pure math, no Windows, no REAPER.
//
// THE WHOLE MODEL
//   Feed(notches):  remember that much to give back
//   Tick(dt):       give back part of it, according to a RULE
//
// Whatever goes in comes out, exactly. Nothing is amplified, nothing is lost. That single
// property is what makes every device agree: the model only ever sees "how much wheel this
// message reports", never "how many messages" or "how fast they came".
//
// THE RULE IS THE ONLY DESIGN CHOICE. Four are provided here so they can be compared by number:
//
//   kLeaky     : give back a fixed FRACTION of what is left each tick. Endless soft tail
//                (needs a snap-to-zero). One parameter: tau.
//   kLinear    : give back the WHOLE amount at a constant rate over `durationMs`. Hard stop.
//   kSmooth    : same fixed duration, but eased with smoothstep -- soft start AND soft stop.
//   kSmoothFast: smoothstep, but with the ramp front-loaded so the start is less lazy.
//
// Two families, and the difference matters:
//   * leaky  = one running total; a new message just adds to the pot. Duration is emergent.
//   * fixed  = each message's share gets its OWN envelope of known length. Duration is exact,
//              which is what "give it back within a specified time" literally means.

#ifndef MODEL3_H
#define MODEL3_H

#include <cmath>

namespace model3 {

enum Rule {
  kLeaky = 0,   // fixed fraction per tick (exponential fade)
  kLinear,      // fixed duration, constant rate
  kSmooth,      // fixed duration, smoothstep (soft both ends)
  kSmoothFast,  // fixed duration, front-loaded smoothstep
  kTrap         // fixed duration, linear ramps + flat middle (soft ends, no 1.5x peak)
};

struct Params {
  Rule rule = kSmooth;
  double durationMs = 150.0;  // fixed-duration rules: the specified time to give one share back
  double tauMs = 150.0;       // kLeaky only: the fade time constant
  double snapNotches = 1.0 / 3840.0 / 2.0; // residual below this is paid off at once
};

// --- the envelope shapes, as cumulative fraction delivered by progress u (0..1) --------------
// S(0) = 0, S(1) = 1 for all of them, so a full envelope always delivers exactly its share.
inline double ShapeFrac(Rule r, double u) {
  if (u <= 0.0) return 0.0;
  if (u >= 1.0) return 1.0;
  switch (r) {
  case kLinear:
    return u;
  case kTrap: {
    // A trapezoid: ramp up over the first quarter, run flat through the middle, ramp down over
    // the last quarter. The RATE (what the eye follows) is a trapezoid, so the peak rate is only
    // 1/(1-r) = 1.33x the average -- lower than smoothstep's 1.5x, i.e. a smaller worst-case
    // step -- while the ends still leave and arrive at zero speed. The two corners are in
    // acceleration, not velocity, which is what makes them far less visible than a velocity jump.
    const double r0 = 0.25;
    const double p = 1.0 / (1.0 - r0);   // peak rate, chosen so the total area is exactly 1
    if (u < r0) return p * u * u / (2.0 * r0);
    if (u <= 1.0 - r0) return p * r0 / 2.0 + p * (u - r0);
    const double v = 1.0 - u;
    return 1.0 - p * v * v / (2.0 * r0);
  }
  case kSmoothFast: {
    // Front-loaded: reaches half its distance at u=0.3 instead of 0.5. Still flat at both ends.
    const double w = u * u * (3.0 - 2.0 * u);        // smoothstep
    const double f = 1.0 - (1.0 - u) * (1.0 - u);    // fast-out ramp
    return 0.65 * f + 0.35 * w;
  }
  case kSmooth:
  default:
    return u * u * (3.0 - 2.0 * u);                  // smoothstep
  }
}

// The model. Everything the plugin needs to remember lives here.
class Glide {
public:
  // How many envelopes can be in flight at once. Only the fixed-duration rules use them, and a
  // fast free-spinning wheel at 1000 msgs/s with a 200 ms duration would need ~200.
  static const int kMaxEnv = 1024;

  void Reset() {
    owed_ = 0.0;
    n_ = 0;
    delivered_ = 0.0;
    for (int i = 0; i < kMaxEnv; ++i) age_[i] = 0.0;
  }

  // One message. `notches` is how much wheel it reports (signed); 1.0 is one standard click.
  // The sign is the direction. Zero-length messages are ignored.
  void Feed(double notches, const Params &P) {
    if (notches == 0.0) return;
    if (P.rule == kLeaky) {
      owed_ += notches;
    } else {
      if (n_ < kMaxEnv) {
        amount_[n_] = notches;
        age_[n_] = 0.0;
        ++n_;
      } else {
        // Over the cap: fold the oldest into the running total rather than drop it. Never
        // silently loses travel.
        owed_ += amount_[0];
        for (int i = 1; i < kMaxEnv; ++i) { amount_[i - 1] = amount_[i]; age_[i - 1] = age_[i]; }
        amount_[kMaxEnv - 1] = notches;
        age_[kMaxEnv - 1] = 0.0;
      }
    }
  }

  // Advance by dt seconds; returns how many notches to give the app this frame (signed).
  double Tick(double dt, const Params &P) {
    if (dt <= 0.0) return 0.0;
    double out = 0.0;

    if (P.rule == kLeaky) {
      if (owed_ != 0.0) {
        const double tau = (P.tauMs > 0.0) ? (P.tauMs / 1000.0) : 1.0;
        if (std::fabs(owed_) < P.snapNotches) {
          out = owed_;
          owed_ = 0.0;
        } else {
          out = owed_ * (1.0 - std::exp(-dt / tau));
          owed_ -= out;
        }
      }
    } else {
      const double D = (P.durationMs > 0.0) ? (P.durationMs / 1000.0) : 1.0;
      int w = 0;
      for (int i = 0; i < n_; ++i) {
        const double u1 = age_[i] / D;
        const double u2 = (age_[i] + dt) / D;
        const double step = amount_[i] * (ShapeFrac(P.rule, u2) - ShapeFrac(P.rule, u1));
        out += step;
        age_[i] += dt;
        if (age_[i] < D) {
          amount_[w] = amount_[i];
          age_[w] = age_[i];
          ++w;
        }
      }
      n_ = w;
      if (owed_ != 0.0) { // the overflow pot, if the envelope cap was ever hit
        out += owed_;
        owed_ = 0.0;
      }
    }

    delivered_ += out;
    return out;
  }

  bool Active() const { return owed_ != 0.0 || n_ > 0; }
  double Delivered() const { return delivered_; }
  int InFlight() const { return n_; }

private:
  double owed_ = 0.0;          // the leaky pot, and the overflow pot for fixed rules
  double amount_[kMaxEnv] = {0};
  double age_[kMaxEnv] = {0};
  int n_ = 0;
  double delivered_ = 0.0;
};

// The finest step the plugin can actually hand to REAPER: one 7-bit unit is 1/15 notch, and the
// relative form carries 1/256 of that, so 1/3840 notch. A step smaller than this cannot be sent,
// so it is carried to the next frame -- this models that carry, which is what a real build does.
inline double Quantize(double want, double *carry) {
  const double step = 1.0 / 3840.0;
  const double total = *carry + want;
  const double n = (total < 0.0) ? -std::floor(-total / step + 0.5) : std::floor(total / step + 0.5);
  const double sent = n * step;
  *carry = total - sent;
  return sent;
}

} // namespace model3

#endif // MODEL3_H
