#!/usr/bin/env bash
# Gate: the "Custom:" macro path, END TO END.
#
#   1. _diag/macro_parse_probe.cpp -- the ACT line parser over lines copied from reaper-kb.ini.
#   2. _diag/macro_gate_probe.cpp  -- what each child of a macro BECOMES (AGENTS.md 125): a view wheel
#      action is DRIVABLE and gets animated; anything else (an SWS mode toggle, a script, a nested
#      macro, "one page", "select next track") is PLAIN and is invoked exactly ONCE; a child that takes
#      a whole notch at once still REFUSES the whole macro, and so does a macro with nothing drivable.
#      The "plain runs once" class is the point: once is what REAPER itself does, so no child can be
#      amplified -- a "select next track" must never be fired dozens of times by the glide.
#   3. _diag/macro_chain_probe.cpp -- the whole chain: ACT line -> children -> one notch split across
#      them, asserting every DRIVABLE child is reached with the full travel, that no PLAIN child is ever
#      delivered travel, and that the plain ones are placed around the gesture (before the first
#      drivable child -> start; after -> end). Includes the forum report's SWS mode-toggle macro.
#
# All three include the REAL src/macro.h and src/routing.h, not copies.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
export PATH="/d/Projects/Code/_tools/w64devkit/bin:$PATH"

rc=0
for p in macro_parse_probe macro_gate_probe macro_chain_probe; do
  OUT="$ROOT/build/_${p}.exe"
  g++ -std=c++17 -O2 -I"$ROOT/src" -o "$OUT" "$ROOT/_diag/${p}.cpp"
  "$OUT" || rc=1
  rm -f "$OUT"
done
exit $rc
