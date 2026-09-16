#!/usr/bin/env bash
# Gate for the FILTER rule in src/smooth_wheel_scroll.cpp.
#
# The rule decides HOW a receiver takes the travel (continuous stream / whole units / at once).
# It cannot be linked outside REAPER, so _diag/filter_probe.cpp mirrors it as it was BEFORE and
# AFTER the "one FilterFor(section, axis, kind)" refactor and diffs the two.
#
# Passing means the only actions that changed are the intended class (a MAIN-section vertical
# Scroll/Zoom action matched by name), and that the track panel's vertical scroll and the MIDI
# editor's vertical scroll resolve to the SAME filter. Anything else showing up is a regression.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
export PATH="/d/Projects/Code/_tools/w64devkit/bin:$PATH"

OUT="$ROOT/build/_filter_probe.exe"
g++ -std=c++17 -O2 -I"$ROOT/src" -o "$OUT" "$ROOT/_diag/filter_probe.cpp"
"$OUT"
rc=$?
rm -f "$OUT"
exit $rc
