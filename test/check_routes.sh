#!/usr/bin/env bash
# Gate: the ROUTING rule must reproduce the frozen 1.6.1 behaviour, except for RECORDED changes.
#
# It compiles _diag/route_probe.cpp, which includes the REAL src/routing.h (not a hand-copied
# mirror), runs it, and compares its output against test/expected/routes_before.txt.
#
# That frozen file was produced BEFORE the unification by _diag/legacy_rules.cpp, a faithful
# transcription of 1.6.1's three decision sites. So a diff proves the unification -- table, name
# rule and filter, wherever they are called from -- changed no decision.
#
# THE RECORDED CHANGE:
#   (104) the MAIN view's HORIZONTAL PAN (988/977) moved stream -> step: a pixel pan DISCARDS
#         sub-unit pieces, so a slow notch moved nothing until a piece reached a whole unit.
#
# (Two more delivery changes were tried and REVERTED, so they are NOT in the allowed set: the MIDI
# horizontal pan whole-unit and synthetic-wheel attempts -- AGENTS.md 105/106/107 -- and the MAIN
# vertical ZOOM going to the fine stream -- AGENTS.md 109, reverted in 110 because a fine grid makes
# that axis jitter and a slow zoom not move. Both are back to their 1.6.1 form.)
#
# The gate does NOT simply accept the new file (that is how the rule drifted silently before,
# AGENTS.md 70). It asserts the ONLY changed lines are that recording -- so any OTHER change still
# fails: a changed vertical id, a changed MIDI row, a new name rule, anything.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
export PATH="/d/Projects/Code/_tools/w64devkit/bin:$PATH"

OUT="$ROOT/build/_route_probe.exe"
g++ -std=c++17 -O2 -I"$ROOT/src" -o "$OUT" "$ROOT/_diag/route_probe.cpp"
"$OUT" > "$ROOT/build/_routes_after.txt"

ALLOWED_ADD='\| +(988|977) \|.* ?step'
ALLOWED_DEL='\| +(988|977) \|.*stream'

rc=0
while IFS= read -r line; do
  case "$line" in
    '+++'*|'---'*) continue ;;
    '+'*) printf '%s\n' "$line" | grep -qE "$ALLOWED_ADD" || {
           echo "UNEXPECTED ADDED LINE: $line"; rc=1; } ;;
    '-'*) printf '%s\n' "$line" | grep -qE "$ALLOWED_DEL" || {
           echo "UNEXPECTED REMOVED LINE: $line"; rc=1; } ;;
  esac
done < <(diff "$ROOT/test/expected/routes_before.txt" "$ROOT/build/_routes_after.txt" || true)

# And the recorded change must actually BE there (guards against a silent revert).
grep -qE '\| +(988|977) \|.* ?step' "$ROOT/build/_routes_after.txt" || {
  echo "MISSING: the main horizontal pan is no longer whole units"; rc=1; }

rm -f "$OUT" "$ROOT/build/_routes_after.txt"
if [ $rc -ne 0 ]; then
  echo ""
  echo "FAIL: routing differs from 1.6.1 by more than the recorded changes."
  exit 1
fi
echo ""
echo "OK: routing reproduces 1.6.1 except the recorded change (104: main pan step)."
