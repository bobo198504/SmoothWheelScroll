// Does the touchpad reversal apply to the WHOLE gesture, or only part of it?
//
//   g++ -std=c++17 -O2 -I../src -o touchpad_reverse_probe.exe touchpad_reverse_probe.cpp
//
// The reverse fires only when the last wheel was classified kTouchpad (src/device.h). The classifier
// needs kDeviceMinSamples messages before it will name a device, so the FIRST messages of every
// touchpad gesture are kUnknown -- and an unknown is passed through UNREVERSED. This probe runs a
// touchpad stream through the REAL tracker and prints, message by message, whether the reversal
// would have been applied. It is the direct test of "only the middle of the gesture is flipped".

#include "device.h"
#include <cstdio>

int main()
{
  // A touchpad during a sideways swipe: small, IRREGULAR magnitudes (it follows the finger).
  const int stream[] = {7, 22, 3, 41, 12, 55, 9, 30, 2, 18, 33, 5, 47, 11, 28, 6, 51, 14, 39, 8};
  const int n = (int)(sizeof(stream) / sizeof(stream[0]));

  DeviceTracker t;
  printf("kDeviceMinSamples = %d, kDeviceLevels = %d, window = %d\n\n", kDeviceMinSamples,
         kDeviceLevels, kDeviceWindow);
  printf("  #   delta  verdict    reversed?\n");
  printf("  --  -----  ---------  ---------\n");
  int unknownAtHead = 0, reversed = 0;
  for (int i = 0; i < n; ++i)
  {
    const Device d = t.Feed(stream[i], 0); // no touch marker: the DELTA must decide
    const bool rev = (d == Device::kTouchpad); // exactly what TouchpadZoomReverse's test sees
    if (d == Device::kUnknown)
      ++unknownAtHead;
    if (rev)
      ++reversed;
    printf("  %2d  %5d  %-9s  %s\n", i + 1, stream[i], DeviceName(d),
           d == Device::kUnknown ? "NO (unknown)" : (rev ? "yes" : "no (not a touchpad)"));
  }
  printf("\n  reversed %d of %d messages; the first %d were unknown and NOT reversed.\n", reversed,
         n, unknownAtHead);
  printf("  => the HEAD of the gesture goes the other way round, the rest is flipped.\n");
  return 0;
}
