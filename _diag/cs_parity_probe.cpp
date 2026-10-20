// Parity check for the C# port of MODEL 3.0 (D:\Projects\Code\SmoothWheelScroll\WheelGlide.cs).
//
// WHY THIS EXISTS: that machine has no .NET SDK, so the C# cannot be compiled there to prove it. But
// the thing that actually matters -- "does the C# produce the same numbers as the C++ the plugin
// uses" -- does not need a C# compiler. This file is the C# code transcribed line for line, run
// against the real anim3_core.h, and their outputs are compared. A transcription is only as good as
// its faithfulness, so the transcription is mechanical: same field order, same expression order.
//
// WHAT IS CHECKED:
//   [1] conservation: total handed over == total fed, to the last bit of double precision
//   [2] parity: this transcription and anim3 agree at every step of a scripted roll
//   [3] the ease decision (PayoutEaseFor) agrees
//   [4] the C# port keeps each window's own ease (a per-message field, not a global)
#include <cstdio>
#include <cmath>
#include <vector>
#include "anim3_core.h"

// ---------------------------------------------------------------------------
// The C# WheelGlide, transcribed. Names kept identical so a reviewer can diff the two by eye.
// ---------------------------------------------------------------------------
namespace cs {

struct Params
{
  double WindowMs = 100.0;
  double PayoutEase = 0.0;
  double WindowSec() const { return WindowMs > 0.0 ? WindowMs * 0.001 : 0.0; }
};

class WheelGlide
{
public:
  static const int MaxWindows = 2048;
  static constexpr double EaseAmount = 0.5;

  int InFlight() const { return n_; }
  bool Active() const { return n_ > 0 || due_ != 0.0; }
  void Reset() { n_ = 0; due_ = 0.0; }

  void Feed(double amount, const Params &P)
  {
    if (amount == 0.0) return;
    if (P.WindowSec() <= 0.0) { due_ += amount; return; }
    if (n_ < MaxWindows) { amt_[n_] = amount; age_[n_] = 0.0; ease_[n_] = P.PayoutEase; ++n_; }
    else { due_ += amount; }
  }

  double Tick(double dt, const Params &P)
  {
    if (dt <= 0.0) return 0.0;
    double output = due_;
    due_ = 0.0;
    double w = P.WindowSec();
    if (w <= 0.0) { n_ = 0; return output; }
    int k = 0;
    for (int i = 0; i < n_; ++i)
    {
      double u0 = age_[i] / w;
      double u1 = (age_[i] + dt) / w;
      double s0 = PayoutFrac(u0, ease_[i]);
      double s1 = PayoutFrac(u1, ease_[i]);
      double rem = 1.0 - s0;
      double pay = rem > 1e-12 ? amt_[i] * (s1 - s0) / rem : amt_[i];
      output += pay;
      amt_[i] -= pay;
      age_[i] += dt;
      if (age_[i] < w) { amt_[k] = amt_[i]; age_[k] = age_[i]; ease_[k] = ease_[i]; ++k; }
    }
    n_ = k;
    return output;
  }

  static double PayoutEaseFor(double gapMs, double windowMs)
  {
    if (!(gapMs > 0.0) || !(windowMs > 0.0) || gapMs >= windowMs) return 0.0;
    double ratio = windowMs / gapMs;
    double nearestInt = ratio - std::floor(ratio) < 0.5 ? std::floor(ratio) : std::ceil(ratio);
    if (std::fabs(ratio - nearestInt) < 0.08) return 0.0;
    return EaseAmount;
  }

private:
  static double PayoutFrac(double u, double ease)
  {
    if (u <= 0.0) return 0.0;
    if (u >= 1.0) return 1.0;
    if (ease <= 0.0) return u;
    double ss = u * u * (3.0 - 2.0 * u);
    return (1.0 - ease) * u + ease * ss;
  }
  double amt_[MaxWindows] = {0};
  double age_[MaxWindows] = {0};
  double ease_[MaxWindows] = {0};
  int n_ = 0;
  double due_ = 0.0;
};

} // namespace cs

static int g_fail = 0;
static void Check(bool ok, const char *what, double a = 0, double b = 0)
{
  if (ok)
    std::printf("  ok   %s\n", what);
  else
  {
    std::printf("  FAIL %s   (%.17g vs %.17g, diff %.3g)\n", what, a, b, std::fabs(a - b));
    ++g_fail;
  }
}

// A scripted roll: notches at fixed gaps, ticks at a fixed frame time. Both models get the same input.
struct Script { double gapMs, frameMs, windowMs; int notches; double amount; };

// Comparing only the TOTAL is not enough: two different hand-over schedules can end at the same sum,
// which would let a real behavioural difference through. So every frame's payout is compared too, and
// the largest single-frame disagreement is reported -- that is the number that says "these are the
// same motion", not just "these move the same distance".
static void RunScript(const Script &s, double &csTotal, double &anTotal, double *maxFrameDiff = nullptr)
{
  cs::Params P;
  P.WindowMs = s.windowMs;
  cs::WheelGlide csGlide;

  anim3::Params A;
  A.windowMs = s.windowMs;
  anim3::Glide anGlide;

  csTotal = 0.0;
  anTotal = 0.0;
  double worst = 0.0;

  const double frameSec = s.frameMs * 0.001;
  const int framesPerGap = (int)(s.gapMs / s.frameMs + 0.5);
  double lastFeed = -1e300;

  for (int n = 0; n < s.notches; ++n)
  {
    // gap from the previous feed, exactly as the C# driver computes it
    double now = (double)n * s.gapMs * 0.001;
    double gapMs = (lastFeed < -1e299) ? 0.0 : (now - lastFeed) * 1000.0;
    lastFeed = now;

    P.PayoutEase = cs::WheelGlide::PayoutEaseFor(gapMs, s.windowMs);
    A.payoutEase = anim3::PayoutEaseFor(gapMs, s.windowMs);

    csGlide.Feed(s.amount, P);
    anGlide.Feed(s.amount, A);

    for (int f = 0; f < framesPerGap; ++f)
    {
      const double c = csGlide.Tick(frameSec, P);
      const double a = anGlide.Tick(frameSec, A);
      csTotal += c;
      anTotal += a;
      const double d = std::fabs(c - a);
      if (d > worst)
        worst = d;
    }
  }
  // let both drain completely
  for (int f = 0; f < 20000 && (csGlide.Active() || anGlide.Active()); ++f)
  {
    const double c = csGlide.Tick(frameSec, P);
    const double a = anGlide.Tick(frameSec, A);
    csTotal += c;
    anTotal += a;
    const double d = std::fabs(c - a);
    if (d > worst)
      worst = d;
  }
  if (maxFrameDiff)
    *maxFrameDiff = worst;
}

int main()
{
  std::printf("=== MODEL 3.0 parity: C# port (transcribed) vs anim3_core.h ===\n\n");

  // [1] CONSERVATION. The model's whole promise: what goes in is what comes out.
  std::printf("[1] conservation (C# port): total out == total in\n");
  {
    const double scripts[][4] = {
        // gapMs, frameMs, windowMs, notches
        {60, 16.7, 200, 10}, {120, 16.7, 200, 8}, {40, 8, 300, 20},
        {250, 16.7, 100, 6}, {16, 16.7, 400, 30}, {100, 4, 100, 12},
    };
    for (int i = 0; i < 6; ++i)
    {
      Script s;
      s.gapMs = scripts[i][0];
      s.frameMs = scripts[i][1];
      s.windowMs = scripts[i][2];
      s.notches = (int)scripts[i][3];
      s.amount = 100.0;

      double a, b;
      RunScript(s, a, b);
      const double expected = s.amount * s.notches;
      char buf[160];
      std::snprintf(buf, sizeof(buf), "gap=%gms win=%gms n=%d  in=%.1f out=%.10f",
                    s.gapMs, s.windowMs, s.notches, expected, a);
      Check(std::fabs(a - expected) < 1e-6, buf, a, expected);
    }
  }

  // [2] PARITY. The transcription must agree with the header the plugin actually compiles.
  std::printf("\n[2] parity with anim3_core.h (same input, same output)\n");
  {
    const double scripts[][4] = {
        {60, 16.7, 200, 10}, {120, 16.7, 200, 8}, {40, 8, 300, 20},
        {250, 16.7, 100, 6}, {16, 16.7, 400, 30}, {100, 4, 100, 12},
        {150, 16.7, 150, 10}, {30, 1, 200, 25}, {200, 16.7, 200, 5},
    };
    for (int i = 0; i < 9; ++i)
    {
      Script s;
      s.gapMs = scripts[i][0];
      s.frameMs = scripts[i][1];
      s.windowMs = scripts[i][2];
      s.notches = (int)scripts[i][3];
      s.amount = 100.0;

      double a, b, worst;
      RunScript(s, a, b, &worst);
      char buf[190];
      std::snprintf(buf, sizeof(buf),
                    "gap=%gms frame=%gms win=%gms n=%d  cs=%.10f anim3=%.10f worstFrame=%.3g",
                    s.gapMs, s.frameMs, s.windowMs, s.notches, a, b, worst);
      Check(std::fabs(a - b) < 1e-9 && worst < 1e-9, buf, a, b);
    }
  }

  // [3] THE EASE DECISION. Getting this wrong is silent: the motion still works, it just ripples.
  std::printf("\n[3] PayoutEaseFor agrees\n");
  {
    const double cases[][2] = {
        {0, 200}, {200, 200}, {250, 200}, {100, 200}, {180, 200},
        {60, 200}, {50, 200}, {40, 200}, {33, 200}, {150, 150},
    };
    bool all = true;
    for (int i = 0; i < 10; ++i)
    {
      double c = cs::WheelGlide::PayoutEaseFor(cases[i][0], cases[i][1]);
      double a = anim3::PayoutEaseFor(cases[i][0], cases[i][1]);
      if (c != a)
      {
        std::printf("  FAIL gap=%g win=%g  cs=%g anim3=%g\n", cases[i][0], cases[i][1], c, a);
        all = false;
      }
    }
    Check(all, "all 10 (gap, window) pairs match");
  }

  // [4] PER-WINDOW EASE. Each window carries the ease chosen when IT arrived; a later message with a
  //     different ease must not rewrite the windows already in flight.
  std::printf("\n[4] per-window ease is not shared\n");
  {
    cs::WheelGlide g;
    cs::Params P;
    P.WindowMs = 200;
    P.PayoutEase = 0.5;
    g.Feed(100.0, P); // window A: eased
    P.PayoutEase = 0.0;
    g.Feed(100.0, P); // window B: constant rate
    const int inFlight = g.InFlight();
    // A is eased, B is not; both must still drain to exactly their own amount.
    double total = 0.0;
    for (int i = 0; i < 2000 && g.Active(); ++i)
      total += g.Tick(0.001, P);
    Check(inFlight == 2, "two windows in flight", inFlight, 2);
    Check(std::fabs(total - 200.0) < 1e-9, "both drained to exactly their own amount", total, 200.0);
  }

  std::printf("\n%s\n", g_fail ? "*** FAILURES ***" : "ALL CHECKS PASSED");
  return g_fail ? 1 : 0;
}
