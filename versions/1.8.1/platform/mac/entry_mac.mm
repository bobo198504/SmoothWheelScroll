// ---------------------------------------------------------------------------
// platform/mac/entry_mac.mm -- the REAPER extension entry point on macOS.
//
// ★ STATUS: SOURCE READY; the latest change still needs the macOS CI build and a real-Mac run.
//
// WHAT THIS FILE IS *NOT*. It is not a second implementation of the plugin's startup. The real
// startup -- loading the API table, reading rec->hwnd_main, registering hookcommand2 and the menu
// hook, reading the saved settings, and the whole unload path -- lives in ONE place, in
// smooth_wheel_scroll.cpp, and is plain C++ with no platform types in it. Duplicating any of that
// here is how two builds drift apart without anyone noticing, which is precisely what this port is
// trying to avoid.
//
// WHAT THIS FILE IS. The minimum a macOS REAPER extension needs before that shared code can run:
//
//   1. Define the entry symbol with C linkage and default visibility. REAPER looks the name up in
//      the .dylib's symbol table, so it must not be C++-mangled, and the build must not strip it.
//      (Windows gets the same effect from __declspec(dllexport).)
//
//   2. Wake the platform layer (a UI thread exists, the clock may be used) and put it back to sleep
//      on unload. On Windows DllMain does this; there is no DllMain on macOS.
//
//   3. Call the shared entry, which is the plugin's ONE implementation of startup.
//
// ★ WHY THE SHARED ENTRY IS CALLED `SwsSharedEntry` AND NOT `ReaperPluginEntry`.
//
// This was very nearly a self-recursion bug. The natural thing is to declare the shared function
// under its own name and call it -- but inside this file that name would denote THIS function (the
// one with C linkage and default visibility), so `return ReaperPluginEntry(hInstance, rec);` would
// call itself until the stack ran out. An optimiser is even allowed to turn that into an infinite
// loop, so it might not appear as a crash at all.
//
// The shared implementation therefore also carries a distinct C name (a thin, separately named
// wrapper in smooth_wheel_scroll.cpp forwards to the real logic), and this file only ever calls the
// distinct one. Anything else depends on overload resolution between a C-linkage external and a C++
// definition, which is exactly the kind of subtlety that only shows up on one platform.
// ---------------------------------------------------------------------------
#include "../../platform.h"

#include "reaper_plugin.h"

// The plugin's single startup implementation. Set by smooth_wheel_scroll.cpp; see the note above on
// why this name is not ReaperPluginEntry.
extern "C" int SwsSharedEntry(void *hInstance, reaper_plugin_info_t *rec);

extern "C" __attribute__((visibility("default"))) int ReaperPluginEntry(
    void *hInstance, reaper_plugin_info_t *rec)
{
  if (!rec)
  {
    // Unload: stop the clock BEFORE the shared code tears anything down, so no tick can arrive on a
    // half-dismantled plugin. The shared entry then deregisters the hooks.
    plat::TimeShutdown();
    const int rc = SwsSharedEntry(hInstance, (reaper_plugin_info_t *)nullptr);
    return rc;
  }

  // A UI thread exists from here on. Done before the shared entry so that anything it starts -- the
  // timer, the panel -- finds the platform layer already awake.
  plat::TimeInit();

  return SwsSharedEntry(hInstance, rec);
}
