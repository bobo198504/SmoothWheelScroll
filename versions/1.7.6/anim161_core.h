// ---------------------------------------------------------------------------
// anim_core.h -- the wheel-glide animation, standalone.
//
// CURVE MODEL. The motion no longer comes from a physical simulation. A gesture is two
// phases, and the SPEED through each is a curve:
//
//     phase 1 (rise)  -- as long as notches keep arriving, the speed climbs from 0 up
//                        TOWARD a ceiling. It approaches the ceiling but never passes it,
//                        so a sustained roll keeps getting faster and then settles at the
//                        ceiling instead of running away.
//     phase 2 (fall)  -- once the feeding stops, the speed falls back to 0 over the
//                        Release time.
//
//        ____/----  <- the ceiling: how fast a roll is allowed to get (the Hold slider)
//      /            \
//    /                \___
//   /                     \   <- the tail's length is the Release slider
//   ^Start rises over the Start knob's time
//
// The VELOCITY is this curve and the position is its integral, so whatever the curve looks
// like is what the view does: the picture and the feel cannot disagree, because both come
// from the same functions below. The curve is only the *expression* of the ceiling -- the
// Hold slider decides the ceiling itself, and the curve shapes how the speed gets there.
//
// A BEND bends a segment away from its straight chord: a value in [-0.5, +0.5], where 0 is
// straight, positive bows it up (rises fast then slows), negative bows it down (starts slow
// then rushes). Both ends of a segment stay put, so the joints never move, and a fall is the
// same bend read the other way -- the "opposite before and after the summit" behaviour.
// ---------------------------------------------------------------------------
#ifndef ANIM_CORE_H
#define ANIM_CORE_H

#include <math.h>

namespace anim {

static const int kSegments = 5; // Start, Accel, Hold, Coast, Release

struct Params {
  // ---- the five sliders ----
  double startPct  = 15.0;  // how much speed one notch adds (as a % of a notch)
  double accelPct  = 7.0;   // how much more each further notch of a roll adds
  double releaseMs = 200.0; // the fall duration: how long the tail takes
  double hold     = 1.0;    // THE CEILING: how fast a roll is allowed to get (multiplier)
  double coast    = 1.0;    // how much of the fall the Coast segment owns

  // ---- the five knobs ----
  double onsetMs = 85.0;    // Start knob: the rise's duration, in ms (20..150; default is the middle)
  double bend[kSegments];   // [0] unused (Start is linear); [1..4] Accel/Hold/Coast/Release

  // ---- internals ----
  // A gap longer than this ends the roll: the feeding has stopped, so the fall begins.
  double burstGapMs = 250.0;
  // The ceiling at hold = 1, in the caller units per second: the Hold slider scales it, so
  // this is "how fast a roll may get" at the tuned setting. It was calibrated so a single
  // notch travels the accepted 1.89 units, then raised 20% on request (Hold felt too weak).
  // claimBase was divided by the same 1.2, so a single notch still travels 1.89: only what a
  // LONG roll can reach went up, which is exactly what the Hold slider is for.
  double ceilingUnit = 1605.6;

  // How much of the ceiling one notch claims, and how much each FURTHER notch of a roll adds.
  //
  // The build-up is ADDITIVE: each notch stacks its claim on what the roll already has, until
  // the ceiling stops it. That is what makes a sustained roll keep gaining -- n notches reach
  // roughly n times a single notch's speed while there is headroom.
  //
  // The earlier form closed a fraction of the REMAINING gap instead, which converged after a
  // few notches and pinned a roll just above a single notch: measured at 6x a single notch,
  // where the accepted physical model reached 56x. That is what read as sluggish.
  //
  // It is calibrated against the tuned defaults (Start 15%, Accel 7%, Release 200 ms, and the
  // panel's knob defaults) so one notch travels the accepted 1.89 units. Re-derive it whenever
  // those defaults move, because the travel is this times the ceiling -- which is exactly what was
  // done when the knobs' defaults were centred (Start 80 -> 85 ms): the longer rise carried the
  // notch to 1.9195, so this came down from 0.010906 to put it back on 1.89.
  double claimBase = 0.0107382;
  // where the accepted physical model reached 56x. That is what read as sluggish.
};

// A bend applied to a segment's progress. u runs 0..1 along the segment; the result is where
// along the straight chord we are, and both ends are fixed, so the joints do not move:
//   c = +0.5 -> exponent 0.5 -> bows UP   (fast then slow)
//   c =  0   -> exponent 1   -> the straight chord
//   c = -0.5 -> exponent 2   -> bows DOWN (slow then fast)
inline double Bend(double u, double c) {
  if (u <= 0.0) return 0.0;
  if (u >= 1.0) return 1.0;
  return pow(u, pow(2.0, -2.0 * c)); // c=+.5 -> ^0.5, c=0 -> ^1, c=-.5 -> ^2
}

// ---------------------------------------------------------------------------
// Smooth profile through a set of control points.
//
// Each segment used to be its own power curve, which leaves a slope discontinuity where two
// meet -- a visible corner, and where a power is below 1 the slope at that end is even
// vertical. The fix is to treat the control points as ONE curve and fit a MONOTONE CUBIC
// (Fritsch-Carlson) through them: smooth at every join (no corner), unable to overshoot (a
// rise never dips, a fall never bumps back up), and passing exactly through the points, so
// each slider and knob keeps its meaning. The bend of a segment is placed as that segment's
// interior point, so it still visibly bows the curve.
//
// This is the same kind of fit the settings window used to draw its schematic with, which is
// why the drawn line and the motion agree: both come from this one function.
// ---------------------------------------------------------------------------
inline void MonotoneTangents(const double *x, const double *y, int n, double *m) {
  if (n < 2) { if (n == 1) m[0] = 0.0; return; }
  // Room for every control point the gesture can produce (a joint and a midpoint per segment,
  // plus the two extra interior points in the fall), with headroom. This MUST cover n - 1.
  //
  // It used to be 8, which was fine until the fall gained its extra interior points and n went
  // to 12: `m[n - 1] = sec[n - 2]` then read past the end of the array, so the tangents at the
  // tail came from whatever was in memory. That is what made the release read as three joined
  // pieces instead of one curve.
  const int kMaxSec = 40;
  double sec[kMaxSec];
  const int last = (n - 1 < kMaxSec) ? (n - 1) : kMaxSec;
  for (int i = 0; i < last; ++i) {
    const double dx = x[i + 1] - x[i];
    sec[i] = (dx > 1e-12) ? ((y[i + 1] - y[i]) / dx) : 0.0;
  }
  m[0] = sec[0];
  m[n - 1] = sec[n - 2];
  for (int i = 1; i < n - 1; ++i)
    m[i] = (sec[i - 1] * sec[i] <= 0.0) ? 0.0 : (sec[i - 1] + sec[i]) * 0.5;
  // Limit the tangents so the cubic cannot overshoot its interval (Fritsch-Carlson).
  for (int i = 0; i < last; ++i) {
    if (sec[i] == 0.0) { m[i] = 0.0; m[i + 1] = 0.0; continue; }
    const double a = m[i] / sec[i], b = m[i + 1] / sec[i];
    const double s = a * a + b * b;
    if (s > 9.0) {
      const double t = 3.0 / sqrt(s);
      m[i] = t * a * sec[i];
      m[i + 1] = t * b * sec[i];
    }
  }
}
// Evaluate the monotone cubic through (x,y) at xu, clamped to the ends.
inline double SmoothProfile(const double *x, const double *y, const double *m, int n,
                            double xu) {
  if (n < 2) return (n == 1) ? y[0] : 0.0;
  if (xu <= x[0]) return y[0];
  if (xu >= x[n - 1]) return y[n - 1];
  int i = 0;
  while (i + 2 < n && x[i + 1] < xu) ++i;
  const double x0 = x[i], x1 = x[i + 1];
  const double h = (x1 > x0) ? (x1 - x0) : 1e-9;
  double t = (xu - x0) / h;
  if (t < 0.0) t = 0.0;
  if (t > 1.0) t = 1.0;
  const double t2 = t * t, t3 = t2 * t;
  const double h00 = 2 * t3 - 3 * t2 + 1;
  const double h10 = t3 - 2 * t2 + t;
  const double h01 = -2 * t3 + 3 * t2;
  const double h11 = t3 - t2;
  return h00 * y[i] + h10 * h * m[i] + h01 * y[i + 1] + h11 * h * m[i + 1];
}


// ---------------------------------------------------------------------------
// CUBIC SPLINE, C² continuous, with specified end slopes.
//
// This is the standard answer to "join arbitrary segments so the transition looks natural": the
// curve is made continuous in VALUE, in SLOPE and in CURVATURE, so there is no visible corner or
// shoulder anywhere. The previous fit (monotone cubic) is only C¹ -- slope-continuous but not
// curvature-continuous -- and a curvature step is exactly what reads as a stiff joint or a
// shoulder, which is what was reported.
//
// It solves the usual tridiagonal system for the second derivatives M at the knots. The end slopes
// are given so the motion starts and finishes at rest (slope 0 at both ends), which is right for a
// glide and keeps the spline from overshooting off the ends.
//
// Cost: n is at most ~20 knots, so the solve is a few dozen multiply-adds, and it happens only when
// the curve is rebuilt (a parameter change), never per animation step.
// ---------------------------------------------------------------------------
static const int kSplineMax = 40;

inline void CubicSplineSecondDerivs(const double *x, const double *y, int n,
                                    double slopeStart, double slopeEnd, double *M) {
  if (n < 2) { if (n == 1) M[0] = 0.0; return; }
  if (n == 2) { M[0] = M[1] = 0.0; return; } // two points: a straight line, no curvature
  if (n > kSplineMax) n = kSplineMax;
  double h[kSplineMax], a[kSplineMax], b[kSplineMax], c[kSplineMax], d[kSplineMax];
  for (int i = 0; i < n - 1; ++i) {
    h[i] = x[i + 1] - x[i];
    if (h[i] < 1e-12) h[i] = 1e-12;
  }
  b[0] = 2.0 * h[0];
  c[0] = h[0];
  d[0] = 6.0 * ((y[1] - y[0]) / h[0] - slopeStart);
  for (int i = 1; i < n - 1; ++i) {
    a[i] = h[i - 1];
    b[i] = 2.0 * (h[i - 1] + h[i]);
    c[i] = h[i];
    d[i] = 6.0 * ((y[i + 1] - y[i]) / h[i] - (y[i] - y[i - 1]) / h[i - 1]);
  }
  a[n - 1] = h[n - 2];
  b[n - 1] = 2.0 * h[n - 2];
  d[n - 1] = 6.0 * (slopeEnd - (y[n - 1] - y[n - 2]) / h[n - 2]);
  for (int i = 1; i < n; ++i) { // Thomas algorithm
    const double w = a[i] / b[i - 1];
    b[i] -= w * c[i - 1];
    d[i] -= w * d[i - 1];
  }
  M[n - 1] = d[n - 1] / b[n - 1];
  for (int i = n - 2; i >= 0; --i) M[i] = (d[i] - c[i] * M[i + 1]) / b[i];
}

// Evaluate the spline; `M` holds the second derivatives from CubicSplineSecondDerivs.
inline double CubicSplineProfile(const double *x, const double *y, const double *M, int n,
                                 double xu) {
  if (n < 2) return (n == 1) ? y[0] : 0.0;
  if (xu <= x[0]) return y[0];
  if (xu >= x[n - 1]) return y[n - 1];
  int i = 0;
  while (i + 2 < n && x[i + 1] < xu) ++i;
  const double h = (x[i + 1] > x[i]) ? (x[i + 1] - x[i]) : 1e-12;
  const double A = (x[i + 1] - xu) / h, B = (xu - x[i]) / h;
  double v = A * y[i] + B * y[i + 1] +
             ((A * A * A - A) * M[i] + (B * B * B - B) * M[i + 1]) * h * h / 6.0;
  // MONOTONE CLAMP. A C² spline smooths the slope change at every joint by curving through it,
  // and near a sharp change of slope that means it can pass the data and come back -- a dip in a
  // climb, or a bump in a fall (both were measured). Clamping the value into the bracketing knots
  // makes that impossible: the curve can never leave the band between two knots, so a rise only
  // ever rises. Away from the steep joints the clamp does not engage and the fit stays C².
  const double lo = (y[i] < y[i + 1]) ? y[i] : y[i + 1];
  const double hi = (y[i] > y[i + 1]) ? y[i] : y[i + 1];
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  return v;
}
// The ceiling: how fast a roll is allowed to get. The Hold slider sets it, and it is kept
// strictly positive (its minimum is a small positive value, not zero) so the curve always
// has somewhere to climb toward.
// The ceiling: how fast a roll is allowed to get. The Hold slider sets it.
//
// The ramp is GEOMETRIC from a floor up to the tuned value, then straight above it -- the same
// shape the physical model used, and for the same reason: a linear ramp squeezes the useful
// range down. Measured, the linear form put Hold = 2 at 1650 where the accepted model had
// 2676, so the whole upper half of the slider felt weak and a long roll hit the cap early.
// Proportional steps spread Hold's effect across the slider instead.
inline double Ceiling(const Params &P) {
  const double h = (P.hold > 0.0) ? P.hold : 0.0;
  const double kFloor = 250.0;
  const double base = (P.ceilingUnit > kFloor) ? P.ceilingUnit : kFloor * 1.01;
  return (h <= 1.0) ? kFloor * pow(base / kFloor, h) : base * h;
}

// The shape of a gesture: the lengths of the two phases' segments. The rise is segments
// 0..2, the fall is segments 3..4. The heights are not fixed here -- the rise climbs toward
// the ceiling and the fall starts wherever the rise had reached -- so this describes only
// the TIMING and the joins the bends act on.
struct Shape {
  double t[kSegments]; // seconds per segment
  double riseSec;      // t[0] + t[1] + t[2]
  double fallSec;      // t[3] + t[4]
};

inline void BuildShape(const Params &P, Shape *out) {
  Shape &s = *out;
  double riseSec = P.onsetMs / 1000.0;
  if (riseSec < 0.001) riseSec = 0.001;
  double relSec = P.releaseMs / 1000.0;
  if (relSec < 0.001) relSec = 0.001;
  const double coastScale = (P.coast > 0.0) ? P.coast : 0.0;

  // The climb is the Start knob's time; the fall is the Release slider's. Those two are the
  // measured durations. Each phase's SEGMENTS divide their phase's time by weight, so no
  // segment is squeezed to nothing.
  //
  // Hold keeps its share of the rise (it is the stretch that climbs into the ceiling).
  // The rise's three segments divide the rise time by weight; the fall's two divide the fall
  // time. Accel is the widest of the rise because it is the stretch a roll actually spends its
  // time in (the mouse's own repeat rate lives here) and the chart should show that: at 0.54 it
  // is about 20% wider than the 0.45 it was, taken from Start (which the narrow reading suits)
  // so Hold keeps its share.
  //
  // Coast is PROPORTIONAL to its slider and reaches zero at the minimum: at coast = 0 there is
  // no Coast segment at all, so the summit meets Release directly. Its maximum is half what it
  // was (0.285 of the fall, was 0.57), which is the "make it smaller" asked for.
  const double wStart = 0.21;
  const double wAccel = 0.54;
  const double wHold = 0.25;
  const double coastShare = 0.285 * (coastScale / 2.0); // 0 -> 0 (no Coast), 2 -> .285
  s.t[0] = riseSec * wStart;
  s.t[1] = riseSec * wAccel;
  s.t[2] = riseSec * wHold;
  s.t[3] = relSec * coastShare;         // Coast
  s.t[4] = relSec * (1.0 - coastShare); // Release
  s.riseSec = s.t[0] + s.t[1] + s.t[2];
  s.fallSec = s.t[3] + s.t[4];
}

// ---------------------------------------------------------------------------
// ONE profile for the whole gesture.
//
// The climb and the fall used to be fitted as two separate curves, which left their slopes
// mismatched where they meet at the top -- a hard corner at exactly the summit (reported, and
// the reason the Hold knob's segment looked wrong). So the gesture is now a single curve: its
// control points are laid out across the whole width and ONE monotone cubic is fitted through
// them, which makes the summit smooth as well as every interior joint.
//
// The control points, in gesture-normalised x (0..1 across the whole gesture):
//
//   0 -- Start mid -- Start end -- Accel mid -- Accel end -- Hold mid -- TOP
//     -- Coast mid -- Coast end -- Release mid -- 1
//
// Each segment's midpoint is offset from its straight chord by that segment's bend, and
// because a monotone fit cannot overshoot, a rise never dips and a fall never bumps back up.
// Start has no bend knob, so its midpoint is the plain chord.
//
// GestureProfile() returns the height (0..1, where 1 is the top) and, optionally, which
// segment owns that x. BOTH the motion and the settings window's drawing call it, so the line
// that is drawn is the line that is performed.
// ---------------------------------------------------------------------------
// The gesture curve: five segments, each with its own simple SHAPE, joined by FILLETS.
//
// Each segment is defined analytically -- a straight line, a bow, or the Hold approach -- and where
// two of them meet the corner is ROUNDED OFF: a piece is taken from each side and the gap bridged
// by one smooth curve that leaves the first segment along its own slope and arrives at the second
// along its own slope. This is a fillet, what a drawing program or CAD package does to round a
// corner, and it is why the joint disappears: the corner POINT is no longer a point on the curve.
//
// A single spline fitted THROUGH the joint points (the previous approach) can never hide them. It
// passes exactly through the corner, so wherever the two slopes differ it swings hard there and the
// eye sees a shoulder: measured, the step size dipped and then rose again, which is the "second
// peak / step" that was reported.
//
// The piece taken from each side is PROPORTIONAL to that segment's own width, so a long segment
// gives up more of itself than a short one and the join reads evenly. Both cuts are a fraction of
// the segment, so a fillet can never swallow a whole stretch.
// ---------------------------------------------------------------------------
// The gesture curve, as a PARAMETRIC 2-D shape: five segments, each bowed about its own chord,
// joined by fillets. Everything is a function of one parameter phi in [0,1].
//
// WHY PARAMETRIC. Each segment's bulge is applied PERPENDICULAR to that segment's chord, so the
// segment bends like a bow. Offsetting it straight up instead makes a slanted chord's bulge lean
// forward -- the "bulge is not perpendicular to the original line" that was reported for every
// segment. A perpendicular offset moves the point sideways as well as up, so the curve is no
// longer a graph y(x); it is a proper 2-D curve, and the parameter is what the model and the
// drawing both step along.
//
// PERPENDICULAR WHERE? In the space the curve is LOOKED AT, not in its raw numbers -- see ax in
// this struct and the long note in SegPt. The axes of a chart are not equally scaled, so a bulge
// that is square in the arithmetic comes out leaning on the page, which is exactly what was
// reported. `ax` carries the scale; the model passes 1 and keeps the arithmetic it always had.
//
// The fillets are 2-D cubic Hermites between the shoulder points, each leaving its segment along
// that segment's own tangent, so the joins are smooth in direction. The summit joint keeps its
// identity: its shoulders are eased but the joint itself is kept, with a horizontal tangent, so
// the top of the curve is that joint.
struct GestureShape {
  double x[kSegments + 1]; // parameter boundary of each segment
  double y[kSegments + 1]; // chord height at each boundary
  double bend[kSegments];
  double holdPow;          // unused; kept so the struct stays simple
  double ceilY;            // the ceiling's height in shape units
  // How many shape-x units one shape-y unit is worth WHERE THIS SHAPE IS LOOKED AT. The bulges are
  // built perpendicular in that measured space, never in shape space: see SegPt. 1.0 means "no
  // measurement to respect" (the MODEL, whose space is dimensionless time against speed).
  double ax;
  // One fillet per interior joint j (between segment j-1 and segment j): its parameter span, and
  // the 2-D point and tangent at each end.
  double fp[kSegments + 1], fq[kSegments + 1];
  double pX[kSegments + 1], pY[kSegments + 1], pTx[kSegments + 1], pTy[kSegments + 1];
  double qX[kSegments + 1], qY[kSegments + 1], qTx[kSegments + 1], qTy[kSegments + 1];
  bool twoHalf[kSegments + 1]; // the summit: kept as two halves meeting at the joint
  double summitParam, summitX, summitY;
};

// How far a full bend pushes a segment's middle off its chord, as a fraction of that segment's own
// height. How much sharper the Hold stretch's middle becomes at the top of its knob. The fraction
// of each segment a fillet takes -- the SAME at every joint, which is what keeps each segment's
// visible span centred on itself (a cut that varied with the corner's sharpness gave the two ends
// of a segment different cuts, and the bow then looked as if it peaked off to one side).
static const double kHoldGainMax = 1.0;
// The fillet (the rounded join between two segments) takes this fraction of EACH segment it joins.
//
// It has grown in steps on request -- 0.14, then a fifth, now two fifths -- because the joins kept
// reading as corners. Why a bigger cut is the right lever: the segments are STRAIGHT, so a join is a
// straight run rounding into a straight run, and the smaller the round the more it still looks like
// the corner it is meant to hide. Measured on the drawn curve, the tightest turn of the whole shape
// (the summit, where a climb meets a fall) is a 0.81 px radius at a fifth and 1.70 px at two fifths
// (curvature 1230 -> 587). It has to stop short of a half: that is where the two cuts meet and the
// segment has no straight run left. The straight part each segment keeps is 1 - 2*kFilletFrac.
static const double kFilletFrac = 0.40;
// How much of the geometric bulge limit the fullest knob actually uses (see SegPt). The limit keeps
// every tangent on its chord's own side of the horizontal; stopping AT the limit put each knob's
// extreme right onto the horizontal, which read as bowing too hard. Halved on request, so the
// largest bow is half the limit and the horizontal is never approached.
static const double kCurveMaxFrac = 0.5;

// A segment is a CIRCULAR ARC through its two chord endpoints, bulging perpendicular to the chord
// by a sagitta the knob sets. The arc is the natural answer here:
//   - its furthest point is the MIDDLE, pushed straight out perpendicular to the chord;
//   - its x stays monotone as long as the arc is under a semicircle, so the curve can never fold
//     back on itself. The previous perpendicular-offset formulation DID fold (measured up to 390 x
//     reversals, the tangle that was seen), because on a short steep segment the sideways part of
//     the offset outran the segment's own width.
// How big that arc may get is not a constant any more: it is the largest arc whose end tangent still
// stays on the chord's own side of the horizontal, and the knob is the fraction of it. See SegPt.

// PERPENDICULAR MEANS PERPENDICULAR ON THE PAGE, NOT IN THE ARITHMETIC.
//
// The segments are steep: a rising chord runs about 60 degrees, a falling one about -52, because
// a segment is short in TIME and tall in SPEED. Constructing the bulge perpendicular in the raw
// (x, y) numbers therefore produces a bulge that is visibly NOT perpendicular to the drawn line:
// the numbers are isotropic, but the picture is not -- one unit of x is about 290 px and one unit
// of y is about 114, so the geometry gets squashed vertically by a factor of 2.6. Measured on the
// drawn picture, a chord at 35 degrees came out with its bulge at 168 degrees, an error of 43
// degrees from square. That is the "bulges straight up rather than across the line" that was
// reported, and it is why re-deriving the bulge in raw numbers never fixed it.
//
// So the bulge is built in the space it will be DRAWN in: x is measured in y units, using the
// caller's own aspect (GestureShape::ax). A chord at 35 degrees on the page then gets its bulge at
// 125 degrees on the page -- square to the line the eye sees. `ax = 1` reproduces the raw
// behaviour exactly, which is what every caller that has no page (the model) passes.
inline void SegPt(const GestureShape &g, int i, double u, double *px, double *py) {
  const double x0 = g.x[i], y0 = g.y[i];
  const double w0 = g.x[i + 1] - x0, sp = g.y[i + 1] - y0;
  if (u <= 0.0) { *px = x0; *py = y0; return; }
  if (u >= 1.0) { *px = x0 + w0; *py = y0 + sp; return; }
  const double ax = (g.ax > 1e-9) ? g.ax : 1.0;
  // The chord, measured. x is converted into y units so the numbers match the picture.
  const double w = w0 * ax;
  const double L = sqrt(w * w + sp * sp);
  if (L < 1e-12) { *px = x0; *py = y0; return; }

  // The bulge is a CIRCULAR ARC through the chord's two ends. Its middle is pushed out
  // PERPENDICULAR to the chord (in the measured space), so the peak sits at the segment's centre
  // and the tangent there is parallel to the chord. It is written as "walk along the chord, rise by
  // the circle's height", which is exact for an arc.
  //
  // TWO things this version fixes, both of which showed up as wrong directions:
  //   * the rise is applied with |radius|, so a bend the OTHER way is a bulge, not a silent zero
  //     (the old form returned 0 whenever the signed radius came out negative, so a downward
  //     bend drew a straight line -- 'ACC cannot bulge down');
  //   * a DESCENDING segment bulges UP for a positive knob, the same as a rising one. The normal
  //     (-uy, ux) always points upward because ux = w/L > 0, so no per-segment sign juggling is
  //     needed -- negating for descending segments was what made Coast and Release move opposite
  //     to their knobs.
  //
  // The sagitta is capped so the curve keeps to the side of the horizontal its chord is on. TWO
  // caps, and whichever is the tighter wins.
  const double r = fabs(sp) / w; // the chord's steepness, as measured
  //
  //  (1) NO FOLD. Walking the chord while the arc's own half-width shrinks means the walk reverses
  //      once the bulge is big enough -- the curve doubling back in x. This is that limiting
  //      sagitta, solved rather than guessed. It binds on STEEP chords.
  const double noFold = 0.5 / (sqrt(r * r + 1.0) + r);
  //
  //  (2) NEVER PAST HORIZONTAL. An arc's tangent swings by +-a about the chord's own angle, where a
  //      is the arc's half-angle (a = asin((L/2)/R), measured). If a passes the chord's angle the
  //      tangent CROSSES the horizontal at one end, and a stretch comes out wrong at its own end:
  //      a RISING one starts by dipping down (scraping the floor before it climbs) and a FALLING
  //      one finishes by tilting back up (taking off). Capping a at the chord's angle keeps every
  //      stretch on its own side of the horizontal, so NO bulge, however far the knob is turned,
  //      can send a line uphill on a descent or downhill on a climb. Solving sin a <= sin(chord)
  //      for the sagitta gives (sqrt(r^2+1) - 1) / (2r); the form below is that rationalised, which
  //      is also the stable one as r -> 0 (a flat chord admits no bulge at all -- any bulge would
  //      tip it past the horizontal straight away). It binds on SHALLOW chords.
  const double noHoriz = r / (2.0 * (sqrt(r * r + 1.0) + 1.0));
  //
  // At r = 1 (a 45-degree chord) the two are equal; steeper chords are limited by the fold,
  // shallower ones by the horizontal.
  const double kMax = (noFold < noHoriz) ? noFold : noHoriz;
  //
  // THE KNOB IS THE FRACTION OF THE LIMIT, not a fixed sagitta that is then capped. A fixed
  // fraction (the old kArcSag * bend) hit the cap early on the SHALLOW stretches and then did
  // nothing at all for the rest of the turn -- measured, Release reached its cap at 53% of the
  // knob and was dead for the other 47%, which reads as a broken control. Scaling the full knob
  // onto the limit instead makes the whole turn do something on every segment.
  //
  // The limit is then taken at kCurveMaxFrac of itself, so the fullest bow stays short of the
  // horizontal rather than landing exactly on it (see that constant).
  double kk = kMax * kCurveMaxFrac * (2.0 * fabs(g.bend[i]));
  if (kk > kMax) kk = kMax;
  const double sag = (g.bend[i] >= 0.0) ? (kk * L) : (-kk * L);
  if (fabs(sag) < 1e-12) { *px = x0 + w0 * u; *py = y0 + sp * u; return; }

  const double as = fabs(sag);
  const double R = (L * L) / (8.0 * as) + 0.5 * as; // radius, always positive
  const double ux = w / L, uy = sp / L;
  const double nx = -uy, ny = ux;                     // always points upward
  const double mx = x0 + 0.5 * w0, my = y0 + 0.5 * sp; // chord's middle
  double t = (u - 0.5) * L;
  double lift = sqrt(R * R - t * t) - (R - as);       // 0 at the ends, 'as' in the middle
  if (lift < 0.0) lift = 0.0;
  const double sgn = (sag >= 0.0) ? 1.0 : -1.0;
  // Back out of the measured space: the walk and the normal were both in x-in-y-units, so the
  // sideways part is divided by `ax` to return to shape x.
  *px = mx + (t * ux + sgn * lift * nx) / ax;
  *py = my + t * uy + sgn * lift * ny;
  // A downward bulge on the LAST segment would carry the curve a hair below zero (the chord ends
  // there), and a negative speed is meaningless -- it also made the tail of the motion wobble in
  // the gate. Flooring at zero costs nothing visible: it can only bite where the curve is already
  // at a standstill.
  if (*py < 0.0) *py = 0.0;
}
// d(point)/d(global parameter) on segment i at local u.
inline void SegTan(const GestureShape &g, int i, double u, double *tx, double *ty) {
  const double sp = g.x[i + 1] - g.x[i];
  const double e = 1e-5;
  const double a = (u - e < 0.0) ? 0.0 : (u - e);
  const double b = (u + e > 1.0) ? 1.0 : (u + e);
  if (sp <= 1e-12 || b - a < 1e-12) { *tx = 1.0; *ty = 0.0; return; }
  double xa, ya, xb, yb;
  SegPt(g, i, a, &xa, &ya);
  SegPt(g, i, b, &xb, &yb);
  const double k = 1.0 / ((b - a) * sp);
  *tx = (xb - xa) * k;
  *ty = (yb - ya) * k;
}

// The MODEL's rise weights: where the three climb segments meet. The glide samples this very
// curve, so these are load-bearing -- changing one changes the motion, not just the picture. They
// sum to 1.
static const double kModelWStart = 0.21;
static const double kModelWAccel = 0.54;
static const double kModelWHold = 0.25;

// The CHART's rise weights. A roll's rise really lasts as long as the user keeps turning the wheel
// -- its length is the mouse's repeat rate, not a fixed time -- so a static picture cannot show it
// honestly and the chart gives the climb more room than its share of the gesture's clock would.
//
// Hold is drawn TWICE as wide as it used to be (0.27 -> 0.54), which is what makes it about half of
// the whole acceleration phase (Accel + Hold: 0.54 of 1.09). The reason is the JOIN it makes with
// Coast: a fillet is sized from the two stretches it joins, and Hold and Coast were both narrow, so
// their corner -- the summit -- stayed the sharpest turn on the curve however wide the fillet
// fraction was set. Measured, the summit's bridge was 24.8 px before and is 40 px after, against
// roughly 46 px at the ordinary joins, so the summit no longer reads as a corner.
//
// DISPLAY ONLY, like every weight on this side: the model keeps kModelW* and its motion is
// untouched (see kChartOnsetRefMs for the same rule stated for the Start knob).
static const double kChartWStart = 0.18;
static const double kChartWAccel = 0.55;
static const double kChartWHold = 0.54;
static const double kChartRiseGain = 2.0; // how much wider the chart draws the rise
// How much wider the chart draws Coast. DISPLAY ONLY, and only the WIDTH: the model reads the same
// 0.285 share, and this never touches how far Coast descends (that is the coastDrop height).
static const double kChartCoastGain = 1.5;

// The rise time the CHART lays its non-Start stretches out at, in ms. The Start knob is the only
// thing that moves its own stretch's width; Accel, Hold, Coast and Release take their widths from
// THIS reference, so turning Start widens Start alone instead of rescaling the whole picture.
//
// Without it the Start knob fed the rise's duration, which scaled EVERY rise stretch AND shortened
// the fall: measured, turning Start from its minimum to its maximum swung Accel from 5% of the box
// to 33% and Release from 78% down to 34% -- the "everything scales with it" that was reported.
// With the reference, Start's width still tracks the knob across its whole range while the other
// four hold their width to within a few percent.
//
// Kept equal to the Start knob's DEFAULT, so at the default the picture lays out exactly by the
// segment weights. It is a plain number here because anim_core.h does not see the panel's constants;
// move it if that default moves.
//
// The model passes `startMovesWholeRise = true` and does NOT use this: there the Start knob really
// is the climb's duration, and that is what the motion has always done.
static const double kChartOnsetRefMs = 85.0;

inline void BuildGestureWeights(const Params &P, double wS, double wA, double wH, double riseGain,
                                double coastGain, double ax, bool startMovesWholeRise,
                                GestureShape *out) {
  GestureShape &g = *out;
  g.ax = (ax > 1e-9) ? ax : 1.0;

  // --- Timing and boundaries -------------------------------------------------
  double riseSec = P.onsetMs / 1000.0;
  if (riseSec < 0.001) riseSec = 0.001;
  double relSec = P.releaseMs / 1000.0;
  if (relSec < 0.001) relSec = 0.001;
  const double coastScale = (P.coast > 0.0) ? P.coast : 0.0;
  // The chart widens Coast for looks only (`coastGain`, 1.5 there, 1.0 for the model). Only the
  // WIDTH is scaled: the descent's height is coastDrop, set separately, so this cannot make Coast
  // fall further, only take longer to do it.
  double coastShare = 0.285 * (coastScale / 2.0) * coastGain;
  if (coastShare > 0.9) coastShare = 0.9; // keep Release a real stretch whatever the gain is

  const double riseDisp = riseSec * riseGain;
  // The width the OTHER stretches are laid out from. In the model (`startMovesWholeRise`) this is
  // the live rise, so the whole climb widens with the Start knob; on the chart it is the fixed
  // reference, so only the Start stretch answers the knob. See kChartOnsetRefMs.
  const double refDisp = startMovesWholeRise ? riseDisp : (kChartOnsetRefMs / 1000.0 * riseGain);
  const double t0 = riseDisp * wS; // Start: the one stretch the Start knob owns
  const double t1 = refDisp * wA;
  const double t2 = refDisp * wH;
  const double t3 = relSec * coastShare;
  const double t4 = relSec * (1.0 - coastShare);
  const double total = t0 + t1 + t2 + t3 + t4;

  g.x[0] = 0.0;
  if (total > 0.0) {
    const double tt[kSegments] = {t0, t1, t2, t3, t4};
    double acc = 0.0;
    for (int i = 0; i < kSegments; ++i) {
      acc += tt[i];
      g.x[i + 1] = acc / total;
    }
  } else {
    for (int i = 1; i <= kSegments; ++i) g.x[i] = (double)i / kSegments;
  }

  // --- Heights ---------------------------------------------------------------
  // Start and Accel own the first part of the climb and Hold closes the rest. Start is kept low so
  // raising it walks the first stretch further up WITHOUT reaching Accel's end -- it approaches
  // Accel's head, it does not touch it. (At the old 0.75 ceiling the Start joint sat almost level
  // with Accel's, so the curve arrived and then did nothing for a stretch: a flat shoulder that
  // read as a second peak when Start was raised.)
  double f0 = 0.12 + 0.33 * ((P.startPct - 5.0) / 40.0);
  if (f0 < 0.06) f0 = 0.06;
  if (f0 > 0.45) f0 = 0.45;
  double f1 = 0.30 + 0.55 * (P.accelPct / 15.0);
  if (f1 < 0.15) f1 = 0.15;
  if (f1 > 0.90) f1 = 0.90;
  double coastDrop = 0.49 * (coastScale / 2.0); // 0 -> no drop (no Coast at all), 2 -> .49
  if (coastDrop > 0.55) coastDrop = 0.55;

  const double a0 = f0;
  const double a1 = f0 + (1.0 - f0) * f1;
  // The Hold slider runs 0..2 and its whole range must shape the curve. This used to stop at 1.0,
  // which was right when the slider was 0..1 -- but the slider was later widened to 2 and the clamp
  // stayed, so above HALF the slider the curve's shape stopped answering it: measured, the Hold
  // stretch's angle was bit-identical (30.323 degrees) at hold 1.0, 1.5 and 2.0, i.e. the top half
  // of the slider did nothing to the drawn line. The clamp now follows the slider's own maximum.
  double hold = (P.hold > 0.0) ? P.hold : 0.0;
  if (hold > 2.0) hold = 2.0;

  // The summit is capped AT 1.0: without the cap a high Start or Accel pushed the joints up until
  // the curve printed above the top of the chart (measured 1.077 at Start 45%, 1.165 with Accel at
  // 15). The scale below shrinks the whole climb when the sliders together would overflow, so the
  // summit lands exactly on 1.0 and the shape merely gets shallower.
  // Hold always carries the climb a little, even at its minimum: hold = 0 would make the stretch
  // perfectly flat, and a flat stretch sitting after a descending Accel is a plateau followed by a
  // second rise -- the curve came out double-peaked (the gate caught it). A floor keeps Hold
  // RISING whatever the slider says, so the curve has one hump and the top stays the summit.
  const double kHoldRise = 0.21;
  const double holdRise = kHoldRise * (0.15 + 0.85 * hold);
  double y1 = a0;
  double y2 = a1;
  double top = a1 + holdRise;
  if (top > 1.0) {
    const double k = 1.0 / top;
    y1 *= k;
    y2 *= k;
    top = 1.0;
  }

  g.y[0] = 0.0;
  g.y[1] = y1;
  g.y[2] = y2;
  g.y[3] = top;
  g.y[4] = top - coastDrop;
  if (g.y[4] < 0.0) g.y[4] = 0.0;
  g.y[5] = 0.0;

  // --- Per-segment bends ------------------------------------------------------
  // A DESCENDING segment's bend is negated: its span is negative, so the expression that lifts a
  // rising middle would sink a falling one -- the inversion that was reported for Coast.
  for (int i = 0; i < kSegments; ++i) g.bend[i] = 0.0;
  g.bend[1] = P.bend[1];                                  // Accel (rising)
  // No sign juggling here any more: the segment's normal always points upward, so a positive knob
  // bulges up on every segment, rising or falling alike. Negating the descending ones was exactly
  // what made Coast and Release move opposite to their knobs.
  g.bend[3] = P.bend[3]; // Coast
  g.bend[4] = P.bend[4]; // Release
  double kb = P.bend[2];
  if (kb < 1.0) kb = 1.0; // tolerate an older stored 0..0.5
  if (kb > 2.0) kb = 2.0;
  g.holdPow = 2.0;                       // unused
  g.bend[2] = (kb - 1.0) * kHoldGainMax; // knob 1..2 -> 0..kHoldGainMax
  g.ceilY = 1.0;

  // --- Fillets ----------------------------------------------------------------
  for (int j = 0; j <= kSegments; ++j) {
    g.fp[j] = g.fq[j] = 0.0;
    g.pX[j] = g.pY[j] = g.pTx[j] = g.pTy[j] = 0.0;
    g.qX[j] = g.qY[j] = g.qTx[j] = g.qTy[j] = 0.0;
    g.twoHalf[j] = false;
  }
  for (int j = 1; j < kSegments; ++j) {
    const double wL = g.x[j] - g.x[j - 1];
    const double wR = g.x[j + 1] - g.x[j];
    // ONE cut everywhere, INCLUDING the summit. The summit used to take twice this, on the grounds
    // that it is the sharpest turn and needs the room -- but the whole point of a fillet is that it
    // is sized by the segments it joins, and the summit is not exempt from that reading: asked for
    // as one fifth of each segment, so it is one fifth here too. The summit stays the top either
    // way, because its bridge keeps the joint itself (see below); the cut only decides how wide the
    // eased shoulders are.
    const double frac = kFilletFrac;
    const double uL = 1.0 - frac, uR = frac;
    g.fp[j] = g.x[j - 1] + uL * wL;
    g.fq[j] = g.x[j] + uR * wR;
    SegPt(g, j - 1, uL, &g.pX[j], &g.pY[j]);
    SegTan(g, j - 1, uL, &g.pTx[j], &g.pTy[j]);
    SegPt(g, j, uR, &g.qX[j], &g.qY[j]);
    SegTan(g, j, uR, &g.qTx[j], &g.qTy[j]);
    if (j == 3) {
      // THE SUMMIT KEEPS ITS JOINT. The bridge is two halves that meet AT the joint with a
      // HORIZONTAL tangent there, so the top of the curve is exactly that joint (a point with
      // zero slope and falls on both sides is a maximum by construction) and the top edge is
      // never tilted up, which is the ceiling.
      g.twoHalf[j] = true;
      g.summitParam = g.x[j];
      g.summitX = g.x[j];
      g.summitY = g.y[j];
    }
  }
}

// The MODEL's curve: what the glide moves by, with the model's own weights. `ax = 1`: the model
// has no page, so its bulges stay perpendicular in plain (time, speed) arithmetic -- which is the
// geometry the motion has always used and which the acceptance gate has locked down.
// `startMovesWholeRise = true`: here the Start knob is the climb's duration, so the whole climb
// widens with it (the honest behaviour, and what the motion has always done). `coastGain = 1`: the
// chart's cosmetic Coast widening is display-only and the model must not have it.
inline void BuildGesture(const Params &P, GestureShape *out) {
  BuildGestureWeights(P, kModelWStart, kModelWAccel, kModelWHold, 1.0, 1.0, 1.0, true, out);
}

// The CHART's curve: the same curve with the climb spread wider, for drawing only. `ax` is the
// caller's aspect (shape-x units per shape-y unit, i.e. how wide the box is for the height it
// draws), so the bulges come out square to their chords on the PAGE. `startMovesWholeRise = false`:
// on the chart only the Start stretch answers the Start knob. `kChartCoastGain` widens Coast for
// looks. DISPLAY ONLY.
inline void BuildGestureChart(const Params &P, GestureShape *out, double ax) {
  BuildGestureWeights(P, kChartWStart, kChartWAccel, kChartWHold, kChartRiseGain, kChartCoastGain,
                      ax, false, out);
}
inline void BuildGestureChart(const Params &P, GestureShape *out) {
  BuildGestureChart(P, out, 1.0);
}

// A cubic Hermite from p0 to p1 over t in [0,1], with end derivatives m0 and m1 (per unit t).
inline double Herm(double p0, double m0, double p1, double m1, double t) {
  const double t2 = t * t, t3 = t2 * t;
  return (2 * t3 - 3 * t2 + 1) * p0 + (t3 - 2 * t2 + t) * m0 + (-2 * t3 + 3 * t2) * p1 +
         (t3 - t2) * m1;
}

// The point on a built shape at parameter phi, plus which segment owns it.
inline void GesturePointShape(const GestureShape &g, double phi,
                              double *outX, double *outY, int *segOut) {
  if (phi < 0.0) phi = 0.0;
  if (phi > 1.0) phi = 1.0;

  // Inside a fillet? Then the point comes from the bridge.
  for (int j = 1; j < kSegments; ++j) {
    if (phi >= g.fp[j] && phi <= g.fq[j]) {
      if (segOut) *segOut = (phi < g.x[j]) ? (j - 1) : j;
      const double jx = g.x[j], jy = g.y[j];
      if (g.twoHalf[j]) {
        // The summit: two halves meeting at the joint with a HORIZONTAL tangent there, so the
        // joint is the top of the curve and the top edge is never tilted up (the ceiling).
        if (phi <= jx) {
          const double d = jx - g.fp[j];
          double t = (d > 1e-12) ? ((phi - g.fp[j]) / d) : 1.0;
          if (t < 0.0) t = 0.0;
          *outX = Herm(g.pX[j], d * g.pTx[j], jx, d, t);
          *outY = Herm(g.pY[j], d * g.pTy[j], jy, 0.0, t);
        } else {
          const double d = g.fq[j] - jx;
          double t = (d > 1e-12) ? ((phi - jx) / d) : 0.0;
          if (t < 0.0) t = 0.0;
          *outX = Herm(jx, d, g.qX[j], d * g.qTx[j], t);
          *outY = Herm(jy, 0.0, g.qY[j], d * g.qTy[j], t);
        }
        if (*outY > jy) *outY = jy; // nothing may rise above the joint
        return;
      }
      const double d = g.fq[j] - g.fp[j];
      double t = (d > 1e-12) ? ((phi - g.fp[j]) / d) : 0.0;
      if (t < 0.0) t = 0.0;
      if (t > 1.0) t = 1.0;
      // Limit the bridge's x tangents (Fritsch-Carlson) so x stays MONOTONE across the bridge.
      // A bowed segment's tangent has a large x component at its ends, and the bridge is short, so
      // the plain Hermite let x turn back on itself -- the curve folded into a loop (measured: up to
      // 390 x reversals, the tangle reported). FC scaling keeps each bridge's x advancing, so the
      // curve is always a graph going right, while y still gets its rounded corner.
      double mx = d * g.pTx[j], my2 = d * g.qTx[j];
      {
        const double dxs = g.qX[j] - g.pX[j];
        if (fabs(dxs) > 1e-12) {
          const double aa = mx / dxs, bb = my2 / dxs;
          const double ss = aa * aa + bb * bb;
          if (ss > 9.0) { const double tq = 3.0 / sqrt(ss); mx = tq * aa * dxs; my2 = tq * bb * dxs; }
        } else {
          mx = my2 = 0.0;
        }
      }
      *outX = Herm(g.pX[j], mx, g.qX[j], my2, t);
      // Limit the bridge's Y tangents the same way (Fritsch-Carlson). A hard clamp used to keep the
      // bridge inside its two ends, but a clamp FLATTENS a stretch and then releases it, which put a
      // little dip-then-rise in the speed near the tail (the gate caught it as a second rise).
      // Limiting the tangents keeps the bridge monotone between its ends without ever going flat:
      // the rounding corners the joint, and nothing else moves.
      //
      // THIS MONOTONE Y IS LOAD-BEARING, and the reason a C2 (quintic) bridge was tried and REVERTED:
      // matching the second derivative as well gives a smoother-looking corner but drops the
      // monotone guard, so on a bowed segment the bridge carves a dip and then a rise -- measured, a
      // genuine DOUBLE PEAK at bend = +0.5 (a crest before the summit), which fails the single-peak
      // gate. A bigger fillet makes that worse, so the two cannot be traded against each other.
      double my1 = d * g.pTy[j], my3 = d * g.qTy[j];
      {
        const double dys = g.qY[j] - g.pY[j];
        if (fabs(dys) > 1e-12) {
          const double aa = my1 / dys, bb = my3 / dys;
          const double ss = aa * aa + bb * bb;
          if (ss > 9.0) { const double tq = 3.0 / sqrt(ss); my1 = tq * aa * dys; my3 = tq * bb * dys; }
        } else {
          my1 = my3 = 0.0;
        }
      }
      *outY = Herm(g.pY[j], my1, g.qY[j], my3, t);
      return;
    }
  }

  // Otherwise it is inside one segment's own shape.
  int i = kSegments - 1;
  for (int k = 0; k + 1 <= kSegments; ++k) {
    if (phi <= g.x[k + 1]) { i = k; break; }
  }
  const double w = g.x[i + 1] - g.x[i];
  double u = (w > 1e-12) ? ((phi - g.x[i]) / w) : 0.0;
  if (u < 0.0) u = 0.0;
  if (u > 1.0) u = 1.0;
  if (segOut) *segOut = i;
  SegPt(g, i, u, outX, outY);
}

// Evaluate the curve at parameter phi. The MODEL and the CHART use different weights, so each
// caller must evaluate the curve built for ITS OWN weights -- mixing them puts the colours on the
// wrong stretches. `coastGain` and `startMovesWholeRise` are forwarded to BuildGestureWeights: the
// model passes (1, true) and the chart (kChartCoastGain, false).
inline void GesturePointW(const Params &P, double wS, double wA, double wH, double riseGain,
                          double coastGain, double ax, bool startMovesWholeRise, double phi,
                          double *outX, double *outY, int *segOut) {
  GestureShape g;
  BuildGestureWeights(P, wS, wA, wH, riseGain, coastGain, ax, startMovesWholeRise, &g);
  GesturePointShape(g, phi, outX, outY, segOut);
}

inline double GestureProfileW(const Params &P, double wS, double wA, double wH, double riseGain,
                              double phi, int *segOut) {
  double x, y;
  GesturePointW(P, wS, wA, wH, riseGain, 1.0, 1.0, true, phi, &x, &y, segOut);
  return y;
}

inline double GestureProfile(const Params &P, double phi, int *segOut) {
  return GestureProfileW(P, kModelWStart, kModelWAccel, kModelWHold, 1.0, phi, segOut);
}

inline double SummitX(const Params &P) {
  GestureShape g;
  BuildGesture(P, &g);
  return g.summitParam;
}

// The CHART's point at parameter phi, both coordinates: the drawing needs x as well as y now.
// `ax` is the drawing box's aspect, so the bulges are perpendicular ON THE PAGE (see SegPt).
inline void GestureChartPoint(const Params &P, double ax, double phi, double *outX, double *outY,
                              int *segOut) {
  GesturePointW(P, kChartWStart, kChartWAccel, kChartWHold, kChartRiseGain, kChartCoastGain, ax,
                false, phi, outX, outY, segOut);
}

inline double GestureCurve(const Params &P, double phi, int *segOut) {
  return GestureProfileW(P, kChartWStart, kChartWAccel, kChartWHold, kChartRiseGain, phi, segOut);
}
class Glide {
public:
  void Reset() {
    climb_ = 0.0;
    target_ = 0.0;
    fall_ = 0.0;
    vAtFall_ = 0.0;
    vAtFallX_ = 1.0;
    rollN_ = 0.0;
    total_ = 0.0;
    vel_ = 0.0;
    active_ = false;
    falling_ = false;
    clock_ = 0.0;
    feedClock_ = -1e9;
    lastKickNow_ = -1e9;
    lastGap_ = -1.0;
    resid_ = 0.0;
    lastDir_ = 0;
    streak_ = 0;
    identity_ = 0;
  }

  // A notch RAISES THE TARGET a little and starts (or continues) the climb toward it. It does
  // NOT set the speed: Tick() walks the speed up the curve over the rise's time, which is what
  // makes the Start knob's duration real and what makes the drawn line the motion.
  //
  // The target is capped by the CEILING (the Hold slider's value): notches keep stacking their
  // claim until the ceiling stops them, so a sustained roll approaches the ceiling and never
  // passes it. That is the ceiling doing its job -- however long the roll, "how fast can this
  // get" has an answer, and the answer is the Hold setting.
  void Kick(double unitTravel, double sign, int identity, double now, const Params &P) {
    const int dir = (sign < 0.0) ? -1 : 1;
    if (identity != identity_ || dir != lastDir_)
      Reset();

    // HOW LONG SINCE THE LAST NOTCH, measured on the caller's own clock (`now`), NOT on the
    // motion clock. The motion clock only advances while the animation is running, so it stops
    // during an idle gap and would report every gap as merely the length of the fall -- which
    // is why an earlier attempt at a continuous fade never saw the real idle time. `now` is
    // passed in by the caller for exactly this.
    const double gap = (lastKickNow_ <= -1e8) ? 1e9 : (now - lastKickNow_);
    lastKickNow_ = now;

    // The notch count that drives the Accel growth decays with the same time constant as the
    // build-up itself, so a "later notch of a roll" is judged by how recently the roll was
    // still going -- not by whether the gap crossed a fixed value. `burstGapMs` is now only
    // used for the window during which the climb is held up (see FeedWindowSec), which is what
    // it was really for.
    if (gap < 1e8)
      lastGap_ = gap;
    else
      lastGap_ = -1.0;
    feedClock_ = clock_;
    lastDir_ = dir;
    identity_ = identity;
    (void)unitTravel;
    (void)P;

    // HOW MUCH OF THE PREVIOUS BUILD-UP IS STILL THERE, decided by the TIME since the last
    // notch -- continuously, with no threshold anywhere.
    //
    // A hard "same roll or new roll" switch was the bug: two notches 240 ms apart built up four
    // times as much as two 260 ms apart, so a wheel turned at about that rate produced a slow
    // notch, a fast notch, a slow notch in turn. Here the carry-over simply decays with the
    // idle time: a notch landing while the last one is still working keeps nearly all of it,
    // one arriving long after keeps almost none and starts the build-up afresh.
    //
    // There is no special "first notch ever" branch: a very large gap decays the carry-over to
    // nothing by itself, which is the same result without a second threshold.
    {
      const double relSec = (P.releaseMs > 0.0) ? (P.releaseMs / 1000.0) : 0.15;
      // Three times the Release time as the time constant: long enough that a brisk roll keeps
      // gathering across its own gaps, short enough that a notch a second later starts fresh. It
      // is a multiple of the Release slider so the two follow the same feel.
      const double keep = (gap < 1e8) ? exp(-gap / (relSec * 3.0)) : 0.0;
      target_ *= keep;
      rollN_ *= keep;
    }

    // The climb restarts whenever the previous rise had finished: the fall has taken the speed
    // to zero by then, so this re-rises from rest instead of jumping.
    if (falling_ || climb_ >= 1.0)
      climb_ = 0.0;

    // What this notch claims, as a FRACTION of the ceiling. The Start slider sets the first
    // notch's claim and the Accel slider makes later notches claim more; the notches STACK, so
    // a roll keeps gaining until the ceiling stops it. The claim saturates just under 1 so the
    // target can approach the ceiling but never reach or pass it -- that is the cap.
    //
    // `rollN_` is an EFFECTIVE notch count, decayed above by the same time constant, so a
    // notch after a long pause counts as the first notch again (n = 1) with no threshold: the
    // decay has already brought the old count back to nothing.
    rollN_ += 1.0;
    const double grow = (rollN_ - 1.0) * (P.accelPct / 7.0) * 0.5;
    double claim = P.claimBase * (P.startPct / 15.0) * (1.0 + (grow > 0.0 ? grow : 0.0));
    if (claim < 0.0) claim = 0.0;
    target_ += claim;
    if (target_ > 0.995) target_ = 0.995;

    falling_ = false; // a notch during the fall turns it back into a climb
    active_ = true;
  }

  // How long after the last notch the climb is held at the top before the fall begins.
  //
  // It is the climb's OWN duration, and nothing else. An earlier form also held it for 1.5x the
  // gap to the PREVIOUS notch, to link consecutive notches -- but that used the past gap to
  // guess the next one, so a notch arriving after a long pause (say one second) held the climb
  // for 1.5 seconds and never fell at all: the speed parked at its peak, which made two notches
  // a second apart total many times a single notch instead of twice it.
  //
  // Nothing more is needed: a notch arriving after the fall has begun simply interrupts it
  // (Kick clears `falling_`) and the speed climbs again from where it was. The motion is
  // therefore continuous in the gap without any window having to guess the next one.
  double FeedWindowSec(const Shape &s) const { return s.riseSec; }

  // Advance by dt. Returns this step's travel (signed, in the caller's units).
  //
  // The caller's dt comes from the timer, and timers are not exact (this machine's are
  // clamped to ~1ms at best, ~15.6ms when the multimedia timer is unavailable). Integrating
  // straight on that step made the SAME gesture travel differently depending on the timer --
  // measured, one notch came out anywhere from 1.64 to 1.93 units. So the work is done on a
  // FIXED internal grid (kFineDt) and whatever is left over is carried: the motion is then
  // the same no matter how it is sampled, which is what the model has always promised.
  double Tick(double dt, const Params &P) {
    if (!active_ || dt <= 0.0) return 0.0;
    resid_ += dt;
    double step = 0.0;
    while (active_ && resid_ >= kFineDt) {
      resid_ -= kFineDt;
      step += Advance(kFineDt, P);
    }
    // A gesture that finished mid-way leaves no residual to carry into the next one.
    if (!active_) resid_ = 0.0;
    return step;
  }

  bool Active() const { return active_; }
  double Velocity() const { return vel_; }
  double Total() const { return total_; }
  int Streak() const { return streak_; }

  // How much of a notch a given streak is worth; used by callers and for labelling.
  static double StreakFrac(const Params &P, int streak) {
    const double f = P.startPct / 100.0 +
                     ((streak - 1) > 0 ? (streak - 1) * (P.accelPct / 100.0) : 0.0);
    return f > 0.0 ? f : 0.0;
  }

private:
  // The fixed integration grid. 0.5 ms: fine enough that the curve is followed closely, and
  // cheap (a 4 ms frame is eight of these).
  static constexpr double kFineDt = 0.0005; // 0.5 ms, the fixed integration step (seconds)

  // One step of the fixed grid. This is the whole of the motion; Tick() only decides how many
  // of these to run for the time it was given.
  double Advance(double dt, const Params &P) {
    clock_ += dt;

    Shape s;
    BuildShape(P, &s);
    if (Ceiling(P) * target_ <= 0.0) { Reset(); return 0.0; }

    const bool fed = (clock_ - feedClock_) <= FeedWindowSec(s);
    // How much of the width the climb owns. The climb phase walks x from 0 to summitX; the
    // fall phase walks it from summitX to 1. One profile covers both, so the summit is smooth.
    const double summitX = SummitX(P);

    if (!falling_ && !fed) {
      // The feeding has stopped: the fall begins from wherever the climb had got to, so the
      // speed does not jump at the hand-over.
      falling_ = true;
      fall_ = 0.0;
      vAtFall_ = vel_;
      vAtFallX_ = GestureProfile(P, climb_ * summitX, 0);
      if (vAtFallX_ <= 1e-9) vAtFallX_ = 1e-9;
    }

    if (!falling_) {
      // CLIMB, over the rise's own time: climb_ walks 0..1 and x = climb_ * summitX, so the
      // speed follows the drawn rise and the summit is reached exactly when the Start knob's
      // time says.
      climb_ += (s.riseSec > 0.0) ? (dt / s.riseSec) : 1.0;
      if (climb_ > 1.0) climb_ = 1.0;
      vel_ = Ceiling(P) * target_ * GestureProfile(P, climb_ * summitX, 0);
    } else {
      // FALL, over the Release slider's time. The speed is the drawn fall scaled so it starts
      // at the speed the climb had reached: the shape is the profile's, the scale keeps the
      // hand-over continuous. The last step is clipped to where the fall actually ends, so a
      // coarse sample cannot lose the tail.
      const double dfx = (s.fallSec > 0.0) ? (dt / s.fallSec) : 1.0;
      double f = fall_ + dfx;
      double useDt = dt;
      if (f > 1.0) {
        useDt = (dfx > 0.0) ? (dt * (1.0 - fall_) / dfx) : 0.0;
        f = 1.0;
      }
      if (useDt < 0.0) useDt = 0.0;

      // Midpoint evaluation, so the travel does not depend on the sampling rate.
      const double fMid = 0.5 * (fall_ + f);
      const double x = summitX + fMid * (1.0 - summitX);
      vel_ = vAtFall_ * (GestureProfile(P, x, 0) / vAtFallX_);
      fall_ = f;
      if (fall_ >= 1.0) {
        vel_ = 0.0;
        active_ = false;
        // The fall has run out: this gesture is over, so nothing is left to continue from.
        target_ = 0.0;
        rollN_ = 0.0;
      }
      const double step = lastDir_ * vel_ * useDt;
      total_ += step;
      return step;
    }

    // Climb: midpoint treatment too, for the same reason (no sampling-rate bias).
    const double cPrev = climb_ - ((s.riseSec > 0.0) ? (dt / s.riseSec) : 1.0);
    const double cMid = 0.5 * (cPrev + climb_);
    vel_ = Ceiling(P) * target_ * GestureProfile(P, cMid * summitX, 0);
    const double step = lastDir_ * vel_ * dt;
    total_ += step;
    return step;
  }

  double climb_ = 0.0;    // how far up the climb, 0..1 (the rise's progress)
  double target_ = 0.0;   // what this roll has built up, as a fraction of the ceiling (0..1)
  double fall_ = 0.0;     // fall progress, 0..1
  double vAtFall_ = 0.0;  // speed at the moment the fall began
  double vAtFallX_ = 1.0; // profile height where the fall began, so the fall scales from it
  double rollN_ = 0.0;     // notches in this roll, faded by the fall (so no gap threshold)
  double total_ = 0.0;
  double vel_ = 0.0;
  bool active_ = false;
  bool falling_ = false;
  double clock_ = 0.0;
  double feedClock_ = -1e9;
  double lastKickNow_ = -1e9; // the caller's wall clock at the last Kick (real idle gap)
  double lastGap_ = -1.0; // interval between the last two notches; -1 = only one so far
  double resid_ = 0.0;    // time not yet consumed by the fixed integration grid
  int lastDir_ = 0;
  int streak_ = 0;
  int identity_ = 0;
};

// The old name, kept so existing callers compile.
inline double NotchFrac(const Params &P, int streak) {
  return Glide::StreakFrac(P, streak);
}

} // namespace anim

#endif // ANIM_CORE_H
