#!/usr/bin/env bash
# Gate: the macOS host seam keeps the properties that cannot be checked by the pure model probes:
#   * the observer is process-local AppKit, not a session-wide CGEventTap;
#   * the shared action path actually consumes the platform wheel snapshot;
#   * unload removes the local monitor unconditionally;
#   * the macOS build enables blocks and no longer asks for Accessibility-only frameworks.
#
# This is a source-contract gate, not a claim that REAPER-on-macOS has been exercised.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
WHEEL="$ROOT/src/platform/mac/wheel_mac.mm"
MAIN="$ROOT/src/smooth_wheel_scroll.cpp"
BUILD="$ROOT/build.sh"
PANEL="$ROOT/src/platform/mac/panel_mac.mm"

fail()
{
  echo "FAIL: $*" >&2
  exit 1
}

grep -q 'addLocalMonitorForEventsMatchingMask' "$WHEEL" ||
  fail "macOS wheel observer is not the process-local NSEvent monitor"
if grep -q 'CGEventTapCreate' "$WHEEL" || grep -q 'kCGSessionEventTap' "$WHEEL"; then
  fail "session-wide CGEventTap is back; it needs Accessibility and sees other applications"
fi
grep -q '\[NSEvent removeMonitor:g_monitor\]' "$WHEEL" ||
  fail "macOS local monitor is not removed"
grep -q 'plat::LastWheel()' "$MAIN" ||
  fail "shared action path does not consume the platform wheel snapshot"
grep -q 'SyncPlatformWheel()' "$MAIN" ||
  fail "platform wheel snapshot is not synchronised into the action path"
grep -q 'PanelBindSettings' "$PANEL" "$MAIN" ||
  fail "Cocoa panel and shared settings are not connected through the platform callback"
grep -q 'NSPanel' "$PANEL" ||
  fail "macOS settings panel is not a native Cocoa window"
grep -q 'PanelShutdown' "$PANEL" "$MAIN" ||
  fail "Cocoa panel is not destroyed during plugin unload"

REMOVE_ALL="$(sed -n '/static void RemoveAll()/,/^}/p' "$MAIN")"
printf '%s\n' "$REMOVE_ALL" | grep -q 'plat::WheelHookRemove();' ||
  fail "RemoveAll no longer removes the macOS monitor"

grep -q -e '-fblocks' "$BUILD" ||
  fail "macOS build does not enable blocks for NSEvent's monitor handler"
grep -q -e '-fobjc-arc' "$BUILD" ||
  fail "macOS build does not retain the local monitor token with ARC"
if grep -q -e '-framework ApplicationServices' "$BUILD"; then
  fail "macOS build still links ApplicationServices after removing the global event tap"
fi

echo "OK: macOS uses a process-local monitor, bridges it once, and tears it down on unload"
