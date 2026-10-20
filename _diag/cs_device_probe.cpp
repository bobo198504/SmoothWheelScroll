// Parity + behaviour check for the C# port of device.h (D:\Projects\Code\SmoothWheelScroll\WheelDevice.cs).
//
// Two things are being proved, and they are different:
//   [A] PARITY  -- the transcribed C# and the real device.h agree on every input.
//   [B] BEHAVIOUR -- the rules actually do what the comments claim, on the cases that matter:
//       a touchpad that goes quiet must NOT leak into the model, a free-spinner must never be called a
//       touchpad, and a horizontal wheel must not pollute the vertical axis's evidence.
//
// [B] is the part worth having: parity alone would happily confirm two copies of a wrong rule.
#include <cstdio>
#include <cmath>
#include "device.h"

// ---------------------------------------------------------------------------
// The C# DeviceTracker, transcribed line for line.
// ---------------------------------------------------------------------------
namespace cs {

struct WheelDevice
{
  static constexpr long TouchSignature = 0xFF515700L;
  static constexpr long TouchMask = 0xFFFFFF00L;
  static constexpr int WheelDelta = 120;
  static constexpr int DeviceWindow = 8;
  static constexpr int DeviceMinSamples = 3;
  static constexpr int DeviceLevels = 1;
};

class DeviceTracker
{
public:
  void Reset() { n_ = 0; varied_ = false; marked_ = false; }

  Device Feed(int delta, long extraInfo = 0)
  {
    if (delta == 0) return Last();
    if ((extraInfo & WheelDevice::TouchMask) == WheelDevice::TouchSignature)
    {
      marked_ = true;
      return Device::kTouchpad;
    }
    int mag = delta < 0 ? -delta : delta;
    if (mag % WheelDevice::WheelDelta == 0)
    {
      n_ = 0;
      varied_ = false;
      return Device::kNotched;
    }
    for (int i = 0; i + 1 < WheelDevice::DeviceWindow; ++i)
      mags_[i] = mags_[i + 1];
    mags_[WheelDevice::DeviceWindow - 1] = mag;
    if (n_ < WheelDevice::DeviceWindow) ++n_;
    if (n_ >= WheelDevice::DeviceMinSamples && DistinctMagnitudes() >= 2) varied_ = true;
    return SubNotchVerdict();
  }

  Device Last() const
  {
    if (marked_) return Device::kTouchpad;
    if (n_ == 0) return Device::kUnknown;
    return SubNotchVerdict();
  }

  int SubNotchSeen() const { return n_; }

  int DistinctMagnitudes() const
  {
    int levels = 0;
    for (int i = WheelDevice::DeviceWindow - n_; i < WheelDevice::DeviceWindow; ++i)
    {
      bool seen = false;
      for (int j = WheelDevice::DeviceWindow - n_; j < i; ++j)
        if (mags_[j] == mags_[i]) { seen = true; break; }
      if (!seen) ++levels;
    }
    return levels;
  }

private:
  Device SubNotchVerdict() const
  {
    if (n_ < WheelDevice::DeviceMinSamples) return Device::kUnknown;
    if (varied_) return Device::kTouchpad;
    return (DistinctMagnitudes() <= WheelDevice::DeviceLevels) ? Device::kFreeSpin : Device::kTouchpad;
  }

  int mags_[WheelDevice::DeviceWindow] = {0};
  int n_ = 0;
  bool varied_ = false;
  bool marked_ = false;
};

} // namespace cs

static int g_fail = 0;
static void Check(bool ok, const char *what)
{
  std::printf(ok ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!ok) ++g_fail;
}

int main()
{
  std::printf("=== device.h parity + behaviour ===\n\n");

  // ---------------------------------------------------------------------
  // [A] PARITY. Feed both implementations a long pseudo-random stream and
  //     compare every single verdict. A random stream is the point: it
  //     reaches interleavings a hand-written sequence would not.
  // ---------------------------------------------------------------------
  std::printf("[A] parity with device.h over a random stream\n");
  {
    DeviceTracker cpp;
    cs::DeviceTracker csharp;
    unsigned seed = 12345;
    int mismatches = 0;
    for (int i = 0; i < 200000; ++i)
    {
      seed = seed * 1103515245u + 12345u;
      const int r = (int)((seed >> 16) & 0x7FFF);
      // A mix that exercises every branch: whole notches, a couple of fixed magnitudes,
      // irregular values, zeros, and the touch marker.
      int delta;
      switch (r % 7)
      {
      case 0: delta = 120; break;    // whole notch
      case 1: delta = -120; break;
      case 2: delta = 15; break;     // free-spin's fixed step
      case 3: delta = -15; break;
      case 4: delta = (r % 60) - 30; break; // irregular (touchpad-ish)
      case 5: delta = 0; break;
      default: delta = (r % 5) * 15; break;
      }
      const long extra = (r % 97 == 0) ? 0xFF515700L : 0L;
      const Device a = cpp.Feed(delta, extra);
      const Device b = csharp.Feed(delta, extra);
      if (a != b)
      {
        if (mismatches < 5)
          std::printf("    mismatch at %d: delta=%d extra=%ld cpp=%s cs=%s\n", i, delta, extra,
                      DeviceName(a), DeviceName(b));
        ++mismatches;
      }
    }
    Check(mismatches == 0, "200000 mixed messages, every verdict identical");
  }

  // ---------------------------------------------------------------------
  // [B] BEHAVIOUR. Each case is a claim the comments make; each is run on
  //     the C# port, because that is the copy that ships to the other app.
  // ---------------------------------------------------------------------
  std::printf("\n[B] behaviour of the shipped (C#) rules\n");
  {
    // A free-spinning wheel: many messages, always the same small step. Must read free-spin, never
    // touchpad -- if it were called a touchpad the wheel would be passed through and lose its easing.
    cs::DeviceTracker t;
    Device last = Device::kUnknown;
    for (int i = 0; i < 12; ++i)
      last = t.Feed(15);
    Check(last == Device::kFreeSpin, "a steady 15-step reads free-spin (not touchpad)");
  }
  {
    // The leak the comment warns about: a touchpad that VARIES and then goes quiet. A steady tail of
    // identical small values must not overwrite the earlier evidence.
    cs::DeviceTracker t;
    t.Feed(7);
    t.Feed(23);
    t.Feed(41);       // three different magnitudes -- varied, so locked to touchpad
    Device last = Device::kUnknown;
    for (int i = 0; i < 20; ++i)
      last = t.Feed(9); // a long steady tail
    Check(last == Device::kTouchpad, "a varied gesture stays touchpad through a steady tail");
  }
  {
    // The OS marker wins outright, even over values that look exactly like a free-spinner.
    cs::DeviceTracker t;
    Device v = t.Feed(15, 0xFF515700L);
    Check(v == Device::kTouchpad, "the touch marker beats a free-spin-looking value");

    // ★ A REAL DEFECT FOUND WHILE PORTING, PRESENT IN BOTH COPIES.
    //
    // Feed()'s first branch says the marker wins outright and sets marked_. But a later SUB-NOTCH
    // message falls through to SubNotchVerdict(), which never looks at marked_ -- so it can answer
    // free-spin even though the OS stated this is touch input. The marker is sticky for Last() and
    // not for Feed()'s return value.
    //
    // THE TEST ASSERTS THE CURRENT BEHAVIOUR ON PURPOSE. Fixing it here alone would make the C# and
    // device.h disagree, which is the drift this whole exercise exists to prevent. It is reported as
    // a known bug (AGENTS.md §133) to be fixed on BOTH sides in one change.
    const Device after = t.Feed(15);
    const bool bugPresent = (after != Device::kTouchpad);
    if (bugPresent)
      std::printf("  NOTE the known bug is present (Feed said %s after the marker)\n",
                  DeviceName(after));
    Check(bugPresent == true,
          "KNOWN BUG, asserted as-is: Feed() drops the marker on a sub-notch value");
    Check(t.Last() == Device::kTouchpad,
          "while Last() still honours it -- sticky in one path, not the other");
  }
  {
    // A whole notch resets the gesture: after a notched click, the old sub-notch evidence is gone.
    cs::DeviceTracker t;
    t.Feed(7);
    t.Feed(23);
    Device v = t.Feed(120);
    Check(v == Device::kNotched, "a whole notch reads notched");
    Check(t.SubNotchSeen() == 0, "and it cleared the sub-notch evidence");
  }
  {
    // Too little evidence must say "unknown", never guess. Two values is not enough to decide.
    cs::DeviceTracker t;
    t.Feed(11);
    Device v = t.Feed(11);
    Check(v == Device::kUnknown, "two samples is still unknown (no guessing)");
  }
  {
    // ★ PER-AXIS SEPARATION. The plugin keeps g_device and g_deviceH apart and the comment there says
    // sharing one makes "both wheels classify as touchpad". That claim is TESTED here rather than
    // restated, because the first attempt at this test asserted it and got it wrong twice.
    //
    // The decisive case is the SUB-NOTCH one: if both wheels are sub-notch, their samples land in the
    // same window, the distinct-magnitude count rises, and a free-spinner can be read as a touchpad.
    // A whole notch always wins and clears everything, so a notched vertical wheel masks the problem
    // -- which is exactly the arrangement that made the first two attempts at this test pass wrongly.
    cs::DeviceTracker shared;
    cs::DeviceTracker vertSeparate, horizSeparate;

    // vertical = free-spin at a steady 30 (a second detent); horizontal = free-spin at a steady 15.
    // Both are legitimate free-spinners, and each on its own reads free-spin. Interleaved into ONE
    // tracker, the window sees {30,15,30,15,...} -- two distinct magnitudes -- and calls it a touchpad.
    Device sharedVerdict = Device::kUnknown, sepVerdict = Device::kUnknown, soloVerdict = Device::kUnknown;
    for (int i = 0; i < 6; ++i)
    {
      shared.Feed(30);
      sharedVerdict = shared.Feed(15);

      sepVerdict = vertSeparate.Feed(30);
      horizSeparate.Feed(15);
    }
    {
      cs::DeviceTracker solo;
      for (int i = 0; i < 6; ++i)
        soloVerdict = solo.Feed(30);
    }

    Check(soloVerdict == Device::kFreeSpin, "a free-spinner at 30 reads free-spin on its own");
    Check(sepVerdict == Device::kFreeSpin, "PER AXIS it still reads free-spin while the other axis runs");
    std::printf("       (a single shared tracker reported: %s)\n", DeviceName(sharedVerdict));
    Check(sharedVerdict == Device::kTouchpad,
          "a SHARED tracker calls the same wheel a touchpad -- the reason per-axis is required");
  }

  std::printf("\n%s\n", g_fail ? "*** FAILURES ***" : "ALL CHECKS PASSED");
  return g_fail ? 1 : 0;
}
