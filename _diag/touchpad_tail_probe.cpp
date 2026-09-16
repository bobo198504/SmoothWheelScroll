// THE TAIL: does a touchpad gesture's decaying end get re-classified and animated?
//
//   g++ -std=c++17 -O2 -I../src -o touchpad_tail_probe.exe touchpad_tail_probe.cpp
//
// The user's report: after the hand leaves the touchpad there is a RELEASE (a tail), and while it
// lasts the INPUT and the OUTPUT differ. A passed-through touchpad should show input == output, so
// something is acting on the tail.
//
// Candidate: the classifier weighs the last kDeviceWindow magnitudes and calls a run of ONE repeated
// value a free-spinning wheel. A touchpad's decay tail is small and nearly constant, so the tail can
// flip from "touchpad (passed through)" to "free-spin (ANIMATED)" -- and then the model's window
// produces exactly a release tail.
//
// This probe runs realistic gesture shapes through the REAL tracker and prints, per message, the
// verdict and whether the plugin would animate it.

#include "device.h"
#include <cstdio>

static void Run(const char *what, const int *d, int n)
{
  DeviceTracker t;
  printf("  %s\n", what);
  printf("    #/delta : ");
  for (int i = 0; i < n; ++i)
    printf("%d ", d[i]);
  printf("\n    verdict : ");
  int animated = 0, tailAnimated = 0;
  for (int i = 0; i < n; ++i)
  {
    const Device v = t.Feed(d[i], 0);
    const bool anim = (v == Device::kNotched || v == Device::kFreeSpin);
    if (anim)
      ++animated;
    printf("%c ", anim ? (v == Device::kNotched ? 'N' : 'F') : (v == Device::kTouchpad ? 't' : '?'));
  }
  printf("\n    (N=notched F=free-spin -> ANIMATED by the model;  t=touchpad ?=unknown -> passed to REAPER)\n");
  printf("    animated %d of %d messages\n\n", animated, n);
  (void)tailAnimated;
}

int main()
{
  printf("kDeviceWindow=%d kDeviceMinSamples=%d kDeviceLevels=%d\n\n", kDeviceWindow,
         kDeviceMinSamples, kDeviceLevels);

  // 1. A touchpad gesture: irregular while the finger moves...
  const int swipe[] = {7, 22, 3, 41, 12, 55, 9, 30};
  Run("touchpad main gesture (irregular)", swipe, 8);

  // 2. ...then the SAME gesture, with a decaying tail whose values repeat (the finger has left).
  //    This is the shape the user is describing: the motion is dying out, the values get tiny.
  const int tailConst1[] = {7, 22, 3, 41, 12, 55, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2};
  Run("touchpad + a constant-2 tail  <-- the case to watch", tailConst1, 16);

  const int tailConst2[] = {7, 22, 3, 41, 12, 55, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  Run("touchpad + a constant-1 tail", tailConst2, 16);

  // 3. A tail that still varies a little (the OS smoothing gradually shrinking).
  const int tailVar[] = {7, 22, 3, 41, 12, 55, 4, 3, 3, 2, 2, 1, 1, 1, 1, 1};
  Run("touchpad + a shrinking-but-varied tail", tailVar, 16);

  printf("  READING: if a tail row shows 'F' after a run of 't', the model is animating the tail.\n");
  printf("           That is the release the user feels, and why input != output there.\n");
  return 0;
}
