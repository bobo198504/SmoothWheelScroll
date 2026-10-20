#ifndef SWS_DEVICE_H
#define SWS_DEVICE_H

// ---------------------------------------------------------------------------
// DEVICE CLASSIFICATION -- which kind of wheel is this message from?
//
// Three senders reach the plugin through the same WM_MOUSEWHEEL message and differ only in what
// they put in `delta` and in the message's `extra info` word:
//
//   * NOTCHED MOUSE   -- one notch reports a whole WHEEL_DELTA (120). Discrete clicks.
//   * FREE-SPINNING   -- a wheel with no notches reports a SMALL, REGULAR share of a notch
//                        (e.g. always 15, or always 30), many times per turn.
//   * TOUCHPAD        -- a continuous surface reports small values with NO fixed step (the value
//                        depends on finger speed), and Windows tags the message as touch/pen.
//
// The classification is a MARKER, not a behaviour: it says what arrived, never what to do with it
// (the model and the filter own that). Two signals are used, in this order:
//
//   1. THE DEDICATED MARKER (preferred). Windows sets MI_WP_SIGNATURE (0xFF515700) in the message's
//      extra-info word for messages injected by touch or pen. That is an explicit statement by the
//      OS, so it WINS over any numeric guess -- a touchpad whose values happen to look regular is
//      still a touchpad.
//   2. THE DELTA PATTERN. With no marker: a whole notch is a notched mouse; among the sub-notch
//      values, a REGULAR step is a free-spinning wheel and an IRREGULAR one is a touchpad.
//
// Nothing here touches REAPER or Windows: it is pure arithmetic over (delta, extraInfo), so the
// rule can be compiled and tested outside REAPER (see _diag/device_probe.cpp).
// ---------------------------------------------------------------------------

// The documented touch/pen signature in GetMessageExtraInfo(). The high three bytes carry it; the
// low byte is the contact count and is masked off. See the plugin's IsTouchInjected().
static const long kTouchSignature = 0xFF515700L;
static const long kTouchMask = 0xFFFFFF00L;

enum class Device
{
  kUnknown = 0, // not enough evidence yet (the first messages of a gesture)
  kNotched,     // a plain notched mouse: whole WHEEL_DELTA steps
  kFreeSpin,    // a free-spinning wheel: a small, REGULAR share of a notch
  kTouchpad     // a continuous surface: irregular values, and/or the OS touch/pen marker
};

inline const char *DeviceName(Device d)
{
  switch (d)
  {
  case Device::kNotched: return "notched";
  case Device::kFreeSpin: return "free-spin";
  case Device::kTouchpad: return "touchpad";
  case Device::kUnknown: return "unknown";
  }
  return "?";
}

// One notch, in the wheel's own units. Windows defines the wheel delta so that one notch is this.
static const int kWheelDelta = 120;

// How many recent sub-notch values to weigh, and how the "regular" test works.
//
// A free-spinning wheel reports values from a FIXED, TINY SET -- its sensor counts a flywheel
// through detents, so the share is one constant (e.g. always 15), or at most a couple of constants
// when the wheel has two detents (15 and 30). A touchpad reports what the finger is doing, so its
// values are spread over many DIFFERENT magnitudes.
//
// So the test is "how many DISTINCT magnitudes are in the recent window", not "how close is the
// largest to the smallest": a mix of 15 and 30 is still a free-spinner (two detents), while a train
// of near-identical-but-all-different values is a touchpad.
//
// ⚠️ THE NUMBERS ARE A FIRST CUT AND HAVE NOT BEEN CHECKED AGAINST REAL DEVICES. They are named
// constants so single values move them once real data arrives (the plugin's raw wheel monitor shows
// the actual values). Nothing else depends on their exact value.
static const int kDeviceWindow = 8;   // recent sub-notch values kept
static const int kDeviceMinSamples = 3; // fewer than this: say "unknown" rather than guess
// A free-spinning wheel counts a flywheel through HARDWARE detents, so it reports exactly ONE
// magnitude. Any variation means the value is following a finger, which is a touchpad. (A wheel with
// two detents would report two magnitudes and is therefore treated as a touchpad and passed through
// -- the safe direction, and reversible once there is real device data.)
static const int kDeviceLevels = 1;

// The running evidence for one wheel. The caller feeds every message.
class DeviceTracker
{
public:
  void Reset()
  {
    n_ = 0;
    varied_ = false;
    marked_ = false;
  }

  // Classify ONE message. `delta` is the signed value out of the message; `extraInfo` is
  // GetMessageExtraInfo() for that message.
  Device Feed(int delta, long extraInfo)
  {
    if (delta == 0)
      return Last();

    // 1. The OS marker wins outright.
    if ((extraInfo & kTouchMask) == kTouchSignature)
    {
      marked_ = true;
      return Device::kTouchpad;
    }

    const int mag = (delta < 0) ? -delta : delta;

    // 2. A whole notch is a notched mouse, whatever came before. It also starts a fresh gesture:
    //    the sub-notch evidence AND the "has varied" lock are cleared.
    if (mag % kWheelDelta == 0)
    {
      n_ = 0;
      varied_ = false;
      return Device::kNotched;
    }

    // 3. Sub-notch: keep the recent magnitudes and read how many DISTINCT ones there are.
    for (int i = 0; i + 1 < kDeviceWindow; ++i)
      mags_[i] = mags_[i + 1];
    mags_[kDeviceWindow - 1] = mag;
    if (n_ < kDeviceWindow)
      ++n_;
    // Once this gesture has shown two different magnitudes it is a surface following a finger, and it
    // stays that way for the rest of the gesture -- see SubNotchVerdict.
    if (n_ >= kDeviceMinSamples && DistinctMagnitudes() >= 2)
      varied_ = true;
    return SubNotchVerdict();
  }

  // What the evidence says right now (without feeding anything).
  Device Last() const
  {
    if (marked_)
      return Device::kTouchpad;
    if (n_ == 0)
      return Device::kUnknown;
    return SubNotchVerdict();
  }

  int SubNotchSeen() const { return n_; }

  // How many distinct magnitudes are in the current window (for logging / diagnosis).
  int DistinctMagnitudes() const
  {
    int levels = 0;
    for (int i = kDeviceWindow - n_; i < kDeviceWindow; ++i)
    {
      bool seen = false;
      for (int j = kDeviceWindow - n_; j < i; ++j)
        if (mags_[j] == mags_[i])
        {
          seen = true;
          break;
        }
      if (!seen)
        ++levels;
    }
    return levels;
  }

private:
  Device SubNotchVerdict() const
  {
    if (n_ < kDeviceMinSamples)
      return Device::kUnknown; // too little to judge -- never guess from one or two values
    // A value that is steady NOW is not proof of a flywheel if this gesture has ALREADY varied: a
    // touchpad creeping slowly (or coasting to a stop) can emit eight identical small values in a row,
    // which fills the whole window and would read as "one fixed step", leaking the touchpad into the
    // model (measured: see _diag/device_swipe_probe.cpp). A real free-spinning wheel reports its fixed
    // step from the FIRST message and never varies, so once a gesture has varied it is locked to
    // touchpad for the rest of the gesture. This only ever moves a case from "animate" to "pass
    // through", which is the safe direction (an over-eager pass-through just loses the easing).
    if (varied_)
      return Device::kTouchpad;
    // ONE fixed magnitude = a flywheel counting detents = free-spinning. Any variation = a surface
    // following a finger = touchpad. See kDeviceLevels for why the line is drawn here.
    return (DistinctMagnitudes() <= kDeviceLevels) ? Device::kFreeSpin : Device::kTouchpad;
  }

  int mags_[kDeviceWindow] = {0};
  int n_ = 0;
  bool varied_ = false; // this gesture has shown more than one magnitude -- locked to touchpad
  bool marked_ = false; // the OS touch/pen marker was seen
};

#endif // SWS_DEVICE_H
