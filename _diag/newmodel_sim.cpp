// A NEW MODEL, studied on its own terms. Nothing here refers to any earlier model.
//
// THE IDEA: take how much wheel a message reports, and give exactly that much back. The only
// thing the plugin does is decide WHEN. So the whole model is one number -- "how much do I still
// owe the app" -- and one rule for paying it down.
//
//   Feed(notches):   owed += notches
//   Tick(dt):        pay back a fixed FRACTION of what is still owed
//
// Paying a fixed fraction per tick is the same as "each message's amount fades out over tau, and
// overlapping fades add" -- the sum of overlapping exponential fades IS a single running total
// times (1 - exp(-dt/tau)). So the simple form and the intuitive description are one object.
//
// The property that matters: whatever goes in comes out. Nothing is amplified, nothing is lost.
//
//   g++ -std=c++17 -O2 -o newmodel_sim.exe newmodel_sim.cpp
//
// Units: everything is in NOTCHES (one standard click = 1.0). The plugin delivers in 7-bit units,
// 15 per notch, so a notch figure is multiplied by 15 at the very end. The finest step the
// relative form can carry is 1/256 unit = 1/3840 notch.

#include <cstdio>
#include <cmath>
#include <initializer_list>

static const double kFineStep = 1.0 / 3840.0;   // finest deliverable step, in notches
static const double kUnitsPerNotch = 15.0;

struct Drain {
  double owed = 0.0;      // notches still owed back (signed)
  double tau;             // seconds: how long the payback takes. THE feel knob.
  double out = 0.0;       // everything paid back so far
  long sends = 0;
  double maxStep = 0.0;   // largest single delivery, in notches
  double goal = 0.0, t_ = 0.0;
  double t95 = -1.0, t99 = -1.0;

  explicit Drain(double tauSec) : tau(tauSec) {}

  void Feed(double notches) { owed += notches; }

  double Tick(double dt) {
    t_ += dt;
    if (owed == 0.0) return 0.0;
    // The residual eventually falls below what can be delivered at all. Pay it out and stop, so
    // the signal can truly end (an exponential never reaches zero by itself).
    if (fabs(owed) < kFineStep * 0.5) {
      const double last = owed;
      owed = 0.0;
      out += last;
      ++sends;
      return last;
    }
    const double s = owed * (1.0 - exp(-dt / tau));
    owed -= s;
    const double a = fabs(s);
    if (a > maxStep) maxStep = a;
    if (a > 0.0) ++sends;
    out += s;
    if (t95 < 0.0 && fabs(out) >= 0.95 * fabs(goal)) t95 = t_;
    if (t99 < 0.0 && fabs(out) >= 0.99 * fabs(goal)) t99 = t_;
    return s;
  }
};

// Feed nMsg messages of (total/nMsg) each, spread evenly over T seconds; tick every dtMs.
// ripple = (smallest per-tick delivery)/(largest) while the device is still feeding.
//   1.00 = the output is perfectly steady; low = it pulses in step with the incoming clicks.
static void run(const char *label, double total, int nMsg, double T, double tauMs, double dtMs)
{
  Drain d(tauMs / 1000.0);
  d.goal = total;
  const double dt = dtMs / 1000.0;
  const double gap = (nMsg > 1) ? T / nMsg : 0.0;
  int sent = 0;
  double nextMsg = 0.0;
  const double end = T + 8.0 * (tauMs / 1000.0) + 0.5;
  double rmin = 1e30, rmax = 0.0;

  for (double t = 0.0; t <= end; t += dt) {
    while (sent < nMsg && t + 1e-12 >= nextMsg) { d.Feed(total / nMsg); ++sent; nextMsg += gap; }
    const double s = d.Tick(dt);
    if (sent > 1 && t > T * 0.25 && t <= T) {
      const double rate = fabs(s) / dt;
      if (rate < rmin) rmin = rate;
      if (rate > rmax) rmax = rate;
    }
  }
  const double ripple = (rmin > 1e29 || rmax <= 0.0) ? 0.0 : rmin / rmax;
  const double errPct = 100.0 * (d.out - total) / total;

  printf("%-18s %2d x %7.4f  T=%4.0fms tau=%3.0fms | out=%9.6f (%+.3f%%) sends=%5ld "
         "maxstep=%7.4f nt=%6.2f u  t95=%4.0f t99=%4.0f  ripple=%.2f\n",
         label, nMsg, total / nMsg, T * 1000, tauMs, d.out, errPct, d.sends, d.maxStep,
         d.maxStep * kUnitsPerNotch, d.t95 * 1000, d.t99 * 1000, ripple);
}

int main()
{
  printf("one notch = %.4f units; finest deliverable step = 1/3840 notch = %.6f notch (%.4f unit)\n\n",
         kUnitsPerNotch, kFineStep, kFineStep * kUnitsPerNotch);

  printf("--- A. THE CORE PROPERTY: does what goes in come out? same 5-notch rotation, tick 4ms ---\n");
  printf("       (three message sizes = three device kinds, all describing the SAME rotation)\n");
  run("1 x 5.0", 5.0, 1, 0.0, 150, 4);
  run("5 x 1.0", 5.0, 5, 0.4, 150, 4);
  run("30 x 0.1667", 5.0, 30, 0.4, 150, 4);
  run("120 x 0.0417", 5.0, 120, 0.4, 150, 4);
  printf("       -> identical totals: the model cannot tell the devices apart.\n");

  printf("\n--- B. does the ROLL SPEED matter? (a slow turn vs a fast flick, same rotation) ---\n");
  for (double T : {0.2, 0.4, 1.0, 2.0}) {
    char b1[64], b2[64];
    snprintf(b1, sizeof(b1), "5x1.0 T=%.0fms", T * 1000);
    snprintf(b2, sizeof(b2), "30x.167 T=%.0fms", T * 1000);
    run(b1, 5.0, 5, T, 150, 4);
    run(b2, 5.0, 30, T, 150, 4);
  }

  printf("\n--- C. tau IS the feel: smoothness vs lag (single notch, tick 4ms) ---\n");
  for (double tau : {40, 60, 80, 100, 150, 200, 300})
    run("1 x 1.0", 1.0, 1, 0.0, tau, 4);

  printf("\n--- D. the notched mouse's steps: does the payback level them out? (5 notches/400ms) --\n");
  for (double tau : {60, 100, 150, 200, 300, 400})
    run("5 x 1.0", 5.0, 5, 0.4, tau, 4);

  printf("\n--- E. tick rate: changes only the granularity, never the total (single notch) ---\n");
  for (double dt : {15.6, 5.0, 4.0, 1.0})
    run("1 x 1.0", 1.0, 1, 0.0, 150, dt);

  return 0;
}
