// DEVICE CLASSIFIER PROBE -- does the three-way classification actually separate the senders?
//
//   g++ -std=c++17 -O2 -I../src -o device_probe.exe device_probe.cpp && ./device_probe.exe
//
// It uses the REAL rule (src/device.h) and feeds it the message streams the three devices produce,
// including the cases that could fool it:
//   * a notched mouse, and the same after a free-spinning burst (must not stay "free-spin");
//   * a free-spinning wheel: a FIXED small share, many messages;
//   * a touchpad WITHOUT the OS marker: irregular values (must still come out touchpad);
//   * a touchpad WITH the marker whose values are accidentally REGULAR (the marker must win);
//   * the first messages of a gesture (no evidence yet -> unknown, not a wrong guess).
//
// IMPORTANT: the free-spinning / touchpad value patterns below are REPRESENTATIVE, not measured
// from the user's hardware. The point of this probe is the RULE, not the magnitudes: it shows which
// verdict each pattern gets, so the rule can be judged before real data arrives.

#include "device.h"
#include <cstdio>
#include <cstring>

static int failures = 0;

static void Check(const char *what, Device got, Device want)
{
  const bool ok = (got == want);
  if (!ok) ++failures;
  printf("  %-52s -> %-9s %s\n", what, DeviceName(got), ok ? "ok" : "WRONG");
}

// The same, for a check that is already a yes/no (so the printed value is the verdict itself).
static void Check(const char *what, bool ok, const char *detail)
{
  if (!ok) ++failures;
  printf("  %-52s %-4s %s\n", what, ok ? "ok" : "WRONG", detail);
}

// Feed a whole stream and report the final verdict.
static Device FeedAll(const int *deltas, int n, long extra = 0)
{
  DeviceTracker t;
  Device d = Device::kUnknown;
  for (int i = 0; i < n; ++i)
    d = t.Feed(deltas[i], extra);
  return d;
}

int main()
{
  printf("One notch = %d. Marker tested = 0x%08lX.\n\n", kWheelDelta, kTouchSignature);

  printf("== 1. notched mouse: whole 120 steps ==\n");
  {
    const int d[] = {120, 120, 120, -120, 120};
    Check("five whole notches", FeedAll(d, 5), Device::kNotched);
  }
  {
    const int d[] = {240, -120, 120}; // a double-notch then singles
    Check("a 240 (two notches in one message) still notched", FeedAll(d, 3), Device::kNotched);
  }

  printf("\n== 2. free-spinning wheel: a FIXED small share, many messages ==\n");
  {
    // A flywheel sensor reporting the same step every time. Several plausible step sizes.
    const int d15[] = {15, 15, 15, 15, 15, 15, 15, 15, 15, 15};
    Check("constant 15 (a common free-spin step)", FeedAll(d15, 10), Device::kFreeSpin);
    const int d30[] = {-30, -30, -30, -30, -30, -30};
    Check("constant -30", FeedAll(d30, 6), Device::kFreeSpin);
    const int dmix[] = {15, 15, 30, 15, 15, 15, 30, 15, 15, 15};
    // A wheel with two detents reports two magnitudes, and the line is drawn at ONE (kDeviceLevels),
    // so this is a touchpad by the rule -- the safe direction. It is the same call made in case 7.
    Check("15 with an occasional 30 (two magnitudes) -> touchpad", FeedAll(dmix, 10),
          Device::kTouchpad);
  }

  printf("\n== 3. touchpad WITHOUT the OS marker: irregular values ==\n");
  {
    // A finger drags; the reported share tracks its speed, so magnitudes vary a lot.
    const int d[] = {7, 22, 3, 41, 12, 55, 9, 30, 2, 18};
    Check("scattered small values (no marker)", FeedAll(d, 10), Device::kTouchpad);
  }
  {
    const int d[] = {1, 40, 2, 33, 5, 47, 3, 28, 6, 51};
    Check("another irregular train", FeedAll(d, 10), Device::kTouchpad);
  }

  printf("\n== 4. the OS marker WINS over the numbers ==\n");
  {
    // Values that would look perfectly regular -- but the OS says touch/pen.
    const int d[] = {15, 15, 15, 15, 15, 15, 15, 15};
    Check("regular values, but touch-tagged -> touchpad",
          FeedAll(d, 8, (long)kTouchSignature), Device::kTouchpad);
    Check("a single touch-tagged whole notch -> touchpad",
          FeedAll((const int[]){120}, 1, (long)kTouchSignature), Device::kTouchpad);
  }

  printf("\n== 5. no evidence yet: the first message must NOT guess ==\n");
  {
    DeviceTracker t;
    Check("one sub-notch value on its own", t.Feed(15, 0), Device::kUnknown);
    Check("  ... and still unknown after a second, different one", t.Feed(30, 0),
          Device::kUnknown);
  }

  printf("\n== 6. a notched click clears the sub-notch evidence ==\n");
  {
    DeviceTracker t;
    for (int i = 0; i < 6; ++i) t.Feed(15, 0); // build up a free-spin reading
    const Device before = t.Last();
    t.Feed(120, 0); // one real click
    const Device after = t.Feed(120, 0);
    printf("  free-spin before a click: %-9s | after a notched click: %-9s %s\n",
           DeviceName(before), DeviceName(after), after == Device::kNotched ? "ok" : "WRONG");
    if (after != Device::kNotched) ++failures;
  }

  printf("\n== 7. where the line sits: 'fixed step' vs 'finger-driven' ==\n");
  {
    // The rule is "a free-spinner reports ONE fixed magnitude; anything that varies is a touchpad".
    // The line is drawn strictly, so an UNSURE device goes to REAPER (see case 9).
    const int constant[] = {20, 20, 20, 20, 20, 20, 20, 20};
    const int twoLevels[] = {15, 30, 15, 30, 15, 30, 15, 15}; // two detents
    const int nearMiss[] = {2, 3, 2, 3, 2, 3, 2, 2};          // almost constant
    const int wide[] = {10, 40, 15, 35, 12, 38, 11, 30};
    Check("one constant 20", FeedAll(constant, 8), Device::kFreeSpin);
    // Two magnitudes is NOT one fixed step: a flywheel counts detents, so its step is a single
    // value. Anything with variation is treated as a finger -- the safe direction.
    Check("two levels 15/30 -> touchpad (not a single step)", FeedAll(twoLevels, 8),
          Device::kTouchpad);
    Check("a nearly-constant 2/3 (a finger, not a detent)", FeedAll(nearMiss, 8),
          Device::kTouchpad);
    Check("10..40, clearly scattered", FeedAll(wide, 8), Device::kTouchpad);
    printf("  NOTE: this boundary is set to the SAFE side -- only a single, exactly-repeated step\n");
    printf("        counts as a wheel. A two-detent wheel would be passed through; that is\n");
    printf("        reversible once real-device data exists (kDeviceLevels).\n");
  }

  printf("\n== 8/9. the leaks that let a touchpad into the model ==\n");
  {
    // 8. The first messages of a gesture: fewer than kDeviceMinSamples -> UNKNOWN, and the plugin
    //    must NOT animate an unknown (it used to, which leaked every gesture's opening).
    {
      DeviceTracker t;
      const Device a = t.Feed(3, 0);  // 1st
      const Device b = t.Feed(7, 0);  // 2nd -- still < kDeviceMinSamples
      Check("1st and 2nd sub-notch values are UNKNOWN (must not be animated)",
            a == Device::kUnknown && b == Device::kUnknown, "");
      const Device c = t.Feed(11, 0); // 3rd -- enough to judge, and they vary -> touchpad
      Check("3rd completes the evidence -> touchpad", c == Device::kTouchpad, "");
    }
    // 9. A whole notch is identified on its very FIRST message, so the strict rule costs a plain
    //    mouse nothing.
    {
      DeviceTracker t;
      Check("a whole 120 is notched on the first message", t.Feed(120, 0) == Device::kNotched, "");
    }
    // 10. THE MID-GESTURE VARIATION LOCK. A touchpad that creeps slowly (or coasts to a stop) can
    //     emit eight identical small values in a row, which would fill the window and read as "one
    //     fixed step" -- leaking the touchpad into the model (measured: _diag/device_swipe_probe.cpp).
    //     Once a gesture has varied by 2+, it must be locked to touchpad for the rest of the gesture.
    {
      DeviceTracker t;
      const int varied[] = {30, 28, 26, 24, 22, 20, 18, 16};
      for (int v : varied)
        t.Feed(v, 0);
      const Device afterVarying = t.Last();
      // Now a long steady run at ONE value -- the pattern that used to flip back to free-spin.
      Device last = afterVarying;
      for (int i = 0; i < 12; ++i)
        last = t.Feed(12, 0);
      Check("a touchpad that starts varying stays a touchpad through a steady run",
            last == Device::kTouchpad, "");
    }
    // 11. A REAL free-spinner is unaffected: constant from the very first message, never varies.
    {
      DeviceTracker t;
      Device last = Device::kUnknown;
      for (int i = 0; i < 12; ++i)
        last = t.Feed(15, 0);
      Check("a genuinely constant step is still a free-spin", last == Device::kFreeSpin, "");
    }
  }

  printf("\n%s\n", failures ? "FAIL: the classifier does not separate the senders as intended"
                           : "OK: the three senders separate as intended, and the OS marker wins");
  printf("\nNOTE: the free-spin/touchpad VALUE patterns above are representative, not measured.\n");
  printf("      The rule is verified here; the threshold needs the raw wheel monitor on real\n");
  printf("      hardware (AGENTS.md 32) before it is called settled.\n");
  return failures ? 1 : 0;
}
