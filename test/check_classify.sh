#!/usr/bin/env bash
# Gate for the name-based action rule in src/smooth_wheel_scroll.cpp.
#
# The rule decides which REAPER actions the plugin drives. It cannot be linked outside
# REAPER (it calls kbd_getTextFromCmd), so _diag/classify_probe.cpp mirrors it as it was
# BEFORE and AFTER the "one page" fix and diffs the two over a corpus of action names.
#
# Passing means the fix changed "one page" actions and nothing else -- i.e. no action that
# used to be driven stopped being driven.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
export PATH="/d/Projects/Code/_tools/w64devkit/bin:$PATH"

OUT="$ROOT/build/_classify_probe.exe"
g++ -O2 -o "$OUT" "$ROOT/_diag/classify_probe.cpp"
"$OUT"
rc=$?
rm -f "$OUT"
exit $rc
